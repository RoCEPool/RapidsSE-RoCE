/*============================================================================*/
/* ColCache.cpp -- column cache of CRW buffers (columnar scan, phase 1)       */
/*============================================================================*/
// Next to each CRW buffer a scan has read, keep the columns it asked for as
// arrays of 8-byte cells, one per row, holding the value the CRW inflater
// puts in the cell (for a string column: the address of the string, which
// the entry keeps). A table scan whose columns are all cached
// reads the cells from the arrays instead of decoding each row (CombinedSrc
// vColCacheInflate). See doc/design/columnar-scan.md.
//
// Validity: an entry belongs to one buffer (its address and its first head),
// holds the number of heads it was built from, and is used only while that
// buffer has no deleted heads and the global write epoch has not moved. Any
// statement that changes table data bumps the epoch (vColCacheBumpEpoch),
// which drops the whole cache. A missing or invalid entry makes the buffer
// use the CRW path, so results never depend on the cache.
#include "BASE.h"
#include "AM.hpp"
#include "IndexDef.hpp"
#include "RowCoder.hpp"
#include "HeadLine.hpp"
#include "TailLine.hpp"
#include "Crw.hpp"
#include "CrwBuffer.hpp"
#include "Row.hpp"
#include "RowSingle.hpp"
#include "RowInflator.hpp"
#include "StreamDef.hpp"
#include "Column.hpp"
#include "ClangDynamic.h"

#include <mutex>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <unistd.h>

using namespace BASE;
using namespace STORE;

namespace {

  // the cells of one column; a string column also holds its strings, and its
  // cells point into them, as the CRW inflater's cells point into the string
  // area of the row (SI_STRAREA_ADR)
  struct ColData {
    std::vector<i8>   vCells;
    std::vector<char> vStrings;
    i8 nBytes(void) { return (i8)(vCells.size() * sizeof(i8) + vStrings.size()); }
  };

  struct ColCacheEntry {
    void* aHead0;                 // first head of the buffer when built
    i8    nHeads;                 // heads of the buffer when built
    i8    iEpoch;                 // write epoch when built
    std::vector<std::shared_ptr<ColData>> vCols;   // one per column (empty: not built)
  };

  std::mutex g_oColCacheLock;
  std::unordered_map<void*, ColCacheEntry> g_oColCache;
  std::atomic<i8> g_iColCacheEpoch(0);
  i8 g_nColCacheBytes = 0;

  // byte budget of the cache: environment variable SE_COLCACHE_MB, by default
  // a quarter of the physical memory; a column that does not fit is not
  // cached (its scans keep the CRW path)
  i8 nColCacheBudget(void)
  {
    static i8 nBudget = -1;
    if (nBudget < 0) {
      const char* az = getenv("SE_COLCACHE_MB");
      if (az) {
        i8 nMB = atoll(az);
        nBudget = (nMB < 0 ? 0 : nMB) * 1024 * 1024;
      }
      else {
        long nPages = sysconf(_SC_PHYS_PAGES);
        long nPageSize = sysconf(_SC_PAGESIZE);
        nBudget = (nPages > 0 && nPageSize > 0) ? (i8)nPages * nPageSize / 4
                                                : (i8)1024 * 1024 * 1024;
      }
    }
    return nBudget;
  }

  // The arrays a running handler reads stay alive while it reads them: each
  // call pins the arrays it hands out to the Dynamic_T that asked, until that
  // Dynamic_T asks again, so dropping an entry never frees them under a scan.
  std::unordered_map<void*, std::vector<std::shared_ptr<ColData>>> g_oColCachePins;

  void vFreeEntry(ColCacheEntry& rEntry)
  {
    rEntry.vCols.clear();
  }

  bool bFixedWidth(eCT eType)
  {
    switch (eType) {
      case CT_INTEGER:
      case CT_DECIMAL:
      case CT_DATE:
      case CT_TIME:
      case CT_TIMESTAMP:
      case CT_FLOAT:
      case CT_BOOLEAN:
      case CT_CHAR:
      case CT_VARCHAR:
        return true;
      default:
        return false;
    }
  }
}

namespace STORE {

  // A CRW buffer was made, changed, paged in or freed: drop its entry.
  void vColCacheDrop(void* pCrwBuffer)
  {
    std::lock_guard<std::mutex> oGuard(g_oColCacheLock);
    auto it = g_oColCache.find(pCrwBuffer);
    if (it == g_oColCache.end())
      return;
    for (auto& a : it->second.vCols)
      if (a)
        g_nColCacheBytes -= a->nBytes();
    vFreeEntry(it->second);
    g_oColCache.erase(it);
  }

  // Drops the whole cache (truncate).
  void vColCacheBumpEpoch(void)
  {
    std::lock_guard<std::mutex> oGuard(g_oColCacheLock);
    g_iColCacheEpoch++;
    for (auto& rItem : g_oColCache)
      vFreeEntry(rItem.second);
    g_oColCache.clear();
    g_nColCacheBytes = 0;
  }
}

/*----------------------------------------------------------------------------*/
/* point m_aaColCache[c] at the cached cells of each column c in uColMask of  */
/* the CRW buffer being scanned; building the missing ones in one pass over   */
/* the rows. Returns false (CRW path) if a column cannot be cached.          */
/*----------------------------------------------------------------------------*/
extern "C" i8 iExtern_ColCache(void* aDynamic, u8 uColMask)
{
  Dynamic_T* aDynamicT = (Dynamic_T*)aDynamic;
  CrwBuffer* pCrwBuffer = (CrwBuffer*)aDynamicT->m_pCrwBuffer;
  if (! pCrwBuffer || ! uColMask)
    return 0;

  RowNub* pNub = pCrwBuffer->pRowTray()->pNub();
  if (pNub->nHeadsDeleted() != 0)
    return 0;

  StreamDef* pStreamDef = pNub->pDesc();
  if (! pStreamDef)
    return 0;
  i8 nColumns = pStreamDef->nColumns();
  for (i8 i_Col = 0; i_Col < 64; i_Col++) {
    if (! (uColMask & ((u8)1 << i_Col)))
      continue;
    if (i_Col >= nColumns || ! bFixedWidth(pStreamDef->pGetColumn(i_Col)->eColumnType()))
      return 0;
  }

  i8 nHeads = pNub->nHeads();
  if (nHeads <= 0)
    return 0;
  void* aHead0 = pCrwBuffer->aGetHead(0);

  std::lock_guard<std::mutex> oGuard(g_oColCacheLock);

  ColCacheEntry& rEntry = g_oColCache[(void*)pCrwBuffer];
  if (rEntry.aHead0 != aHead0 || rEntry.nHeads != nHeads ||
      rEntry.iEpoch != g_iColCacheEpoch.load()) {
    for (auto& a : rEntry.vCols)
      if (a)
        g_nColCacheBytes -= a->nBytes();
    vFreeEntry(rEntry);
    rEntry.aHead0 = aHead0;
    rEntry.nHeads = nHeads;
    rEntry.iEpoch = g_iColCacheEpoch.load();
  }
  if ((i8)rEntry.vCols.size() < nColumns)
    rEntry.vCols.resize(nColumns);

  u8 uMissing = 0;
  for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
    if ((uColMask & ((u8)1 << i_Col)) && ! rEntry.vCols[i_Col])
      uMissing |= (u8)1 << i_Col;

  if (uMissing) {
    if (g_nColCacheBytes + nHeads * 8 * __builtin_popcountll(uMissing) > nColCacheBudget())
      return 0;
    std::vector<std::shared_ptr<ColData>> vNew(nColumns);
    std::vector<std::vector<i8>> vStrOfs(nColumns);
    for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
      if (uMissing & ((u8)1 << i_Col)) {
        vNew[i_Col] = std::make_shared<ColData>();
        vNew[i_Col]->vCells.resize(nHeads);
      }
    HV_RowSingle hvRowSingle(pStreamDef);
    for (i8 i_Crw = 0; i_Crw < nHeads; i_Crw++) {
      Crw* aCrw = (Crw*)pCrwBuffer->aGetHead(i_Crw);
      RowInflator::vInflate_AllColumns_into_RowSingle(hvRowSingle, aCrw);
      Row* pRow = hvRowSingle->pRowCurrent();
      for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++) {
        if (! (uMissing & ((u8)1 << i_Col)))
          continue;
        eCT eType = pStreamDef->pGetColumn(i_Col)->eColumnType();
        if (eType == CT_VARCHAR || eType == CT_CHAR) {
          // the offset in the strings for now, the address once they are all in
          if (pRow->bIsColNull(i_Col)) {
            vNew[i_Col]->vCells[i_Crw] = C_DATUM_NULL;
            vStrOfs[i_Col].push_back(-1);
          }
          else {
            const char* az = (const char*)pRow->szColVal_ofs(i_Col)->azChar();
            size_t n = strlen(az) + 1;
            std::vector<char>& rStrings = vNew[i_Col]->vStrings;
            vStrOfs[i_Col].push_back((i8)rStrings.size());
            rStrings.insert(rStrings.end(), az, az + n);
          }
        }
        else
          vNew[i_Col]->vCells[i_Crw] = ((i8*)hvRowSingle->ayCellArray())[i_Col];
      }
    }
    for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++) {
      if (! (uMissing & ((u8)1 << i_Col)))
        continue;
      if (! vStrOfs[i_Col].empty()) {
        char* azBase = vNew[i_Col]->vStrings.data();
        for (i8 i_Crw = 0; i_Crw < nHeads; i_Crw++)
          if (vStrOfs[i_Col][i_Crw] >= 0)
            vNew[i_Col]->vCells[i_Crw] = (i8)(azBase + vStrOfs[i_Col][i_Crw]);
      }
      g_nColCacheBytes += vNew[i_Col]->nBytes();
      rEntry.vCols[i_Col] = vNew[i_Col];
    }
  }

  std::vector<std::shared_ptr<ColData>>& rPins = g_oColCachePins[aDynamic];
  rPins.clear();
  for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
    if (uColMask & ((u8)1 << i_Col)) {
      aDynamicT->m_aaColCache[i_Col] = rEntry.vCols[i_Col]->vCells.data();
      rPins.push_back(rEntry.vCols[i_Col]);
    }

  return 1;
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
