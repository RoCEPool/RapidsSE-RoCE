/*============================================================================*/
/* ColCache.cpp -- column cache of CRW buffers (columnar scan, phase 1)       */
/*============================================================================*/
// Next to each CRW buffer a scan has read, keep the fixed-width columns it
// asked for as arrays of 8-byte cells, one per row, holding the value the
// CRW inflater puts in the cell. A table scan whose columns are all cached
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

using namespace BASE;
using namespace STORE;

namespace {

  struct ColCacheEntry {
    void* aHead0;                 // first head of the buffer when built
    i8    nHeads;                 // heads of the buffer when built
    i8    iEpoch;                 // write epoch when built
    std::vector<std::shared_ptr<i8>> vCols;   // one array per column (empty: not built)
  };

  std::mutex g_oColCacheLock;
  std::unordered_map<void*, ColCacheEntry> g_oColCache;
  std::atomic<i8> g_iColCacheEpoch(0);
  i8 g_nColCacheBytes = 0;

  // byte budget of the cache: environment variable SE_COLCACHE_MB (default
  // 1024); a column that does not fit is not cached (its scans keep the CRW path)
  i8 nColCacheBudget(void)
  {
    static i8 nBudget = -1;
    if (nBudget < 0) {
      const char* az = getenv("SE_COLCACHE_MB");
      i8 nMB = az ? atoll(az) : 1024;
      nBudget = (nMB < 0 ? 0 : nMB) * 1024 * 1024;
    }
    return nBudget;
  }

  // The arrays a running handler reads stay alive while it reads them: each
  // call pins the arrays it hands out to the Dynamic_T that asked, until that
  // Dynamic_T asks again, so dropping an entry never frees them under a scan.
  std::unordered_map<void*, std::vector<std::shared_ptr<i8>>> g_oColCachePins;

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
        g_nColCacheBytes -= it->second.nHeads * (i8)sizeof(i8);
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
        g_nColCacheBytes -= rEntry.nHeads * (i8)sizeof(i8);
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
    i8 nNeed = 0;
    for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
      if (uMissing & ((u8)1 << i_Col))
        nNeed += nHeads * (i8)sizeof(i8);
    if (g_nColCacheBytes + nNeed > nColCacheBudget())
      return 0;
    for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
      if (uMissing & ((u8)1 << i_Col)) {
        rEntry.vCols[i_Col] = std::shared_ptr<i8>(new i8[nHeads], std::default_delete<i8[]>());
        g_nColCacheBytes += nHeads * (i8)sizeof(i8);
      }
    HV_RowSingle hvRowSingle(pStreamDef);
    for (i8 i_Crw = 0; i_Crw < nHeads; i_Crw++) {
      Crw* aCrw = (Crw*)pCrwBuffer->aGetHead(i_Crw);
      RowInflator::vInflate_AllColumns_into_RowSingle(hvRowSingle, aCrw);
      for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
        if (uMissing & ((u8)1 << i_Col))
          rEntry.vCols[i_Col].get()[i_Crw] = ((i8*)hvRowSingle->ayCellArray())[i_Col];
    }
  }

  std::vector<std::shared_ptr<i8>>& rPins = g_oColCachePins[aDynamic];
  rPins.clear();
  for (i8 i_Col = 0; i_Col < nColumns && i_Col < 64; i_Col++)
    if (uColMask & ((u8)1 << i_Col)) {
      aDynamicT->m_aaColCache[i_Col] = rEntry.vCols[i_Col].get();
      rPins.push_back(rEntry.vCols[i_Col]);
    }

  return 1;
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
