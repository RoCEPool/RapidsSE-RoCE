/*============================================================================*/
/* ViewCatalog.cpp -- View definitions (CREATE VIEW / DROP VIEW)              */
/*============================================================================*/

#include "BASE.h"

#include <strings.h>

#include "SkipRoots.hpp"
#include "EntBuffer.hpp"
#include "IndexTree.hpp"
#include "TablePtn.hpp"
#include "Table.hpp"
#include "MetaDataTables.hpp"
#include "ViewCatalog.hpp"

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Do two object names refer to the same object?                              */
/*----------------------------------------------------------------------------*/
bool ViewCatalog::bSameName(Stz* szA, Stz* szB)
{
  if (!szA || !szB) {
    return false;
  }

  return strcasecmp((const char*) szA->azChar(), (const char*) szB->azChar()) == 0;
}

/*----------------------------------------------------------------------------*/
/* Encode free text so that it can be a procedure parameter                   */
/*----------------------------------------------------------------------------*/
std::string ViewCatalog::sEncodeText(const std::string& text)
{
  static const char azHex[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(1 + text.length() * 2);
  encoded.push_back('V');

  for (unsigned char c : text) {
    encoded.push_back(azHex[c >> 4]);
    encoded.push_back(azHex[c & 0x0f]);
  }

  return encoded;
}

/*----------------------------------------------------------------------------*/
/* Decode text encoded by sEncodeText                                         */
/*----------------------------------------------------------------------------*/
std::string ViewCatalog::sDecodeText(const std::string& encoded)
{
  auto fDigit = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };

  std::string text;

  if (encoded.empty() || encoded[0] != 'V') {
    return text;
  }

  text.reserve(encoded.length() / 2);

  for (size_t i = 1; i + 1 < encoded.length(); i += 2) {
    text.push_back(static_cast<char>((fDigit(encoded[i]) << 4) | fDigit(encoded[i + 1])));
  }

  return text;
}

/*----------------------------------------------------------------------------*/
/* Find a view on this node                                                   */
/*----------------------------------------------------------------------------*/
bool ViewCatalog::bFind(Stz* szViewNm, s256& sStoredNm, s4K& sColumnList, s4K& sDefinition)
{
  Table* pTable = AM::pMetaDataTableViews();
  if (!pTable) {
    return false;
  }

  HashMap*  pHashMap  = AM::pHashMap();
  TablePtn* pTablePtn = pTable->pTablePtn(0);

  BufferId oRootBufferId = pTablePtn->oRootBufferId();
  Pds* pRootPds = pTablePtn->pRootPds();

  bool bFound = false;

  // Iterate the root buffer of this table partition. It holds the buffer ids
  // of the row buffers.
  PdsCursor* pCursor = new PdsCursor(pRootPds);

  while (!bFound && !pCursor->bIsCurrNull()) {
    SkipListBlock* pSLBlock = pCursor->m_pCurrentBlock;

    BufferId oBufferId = pTablePtn->oExtractBufferId((Object*) pSLBlock->aItem());
    Pds* pPdsRowBuffer = _pPds(pHashMap->pValueAt(oBufferId.m_pvAsAddress, true));

    PdsCursor* pRowCursor = new PdsCursor(pPdsRowBuffer);

    while (!pRowCursor->bIsCurrNull()) {
      Row* pRow = pRowCursor->pCurrRow();

      if (bSameName(pRow->szColVal_ofs(C_COL_NAME), szViewNm)) {
        sStoredNm.vCopy(pRow->szColVal_ofs(C_COL_NAME));
        sColumnList.vCopy(pRow->szColVal_ofs(C_COL_COLUMNLIST));
        sDefinition.vCopy(pRow->szColVal_ofs(C_COL_DEFINITION));
        bFound = true;
        break;
      }

      pRowCursor->vNext();
    }

    delete pRowCursor;
    pHashMap->vDropKeyLock(pPdsRowBuffer->oGetBufferId().m_pvAsAddress);

    pCursor->vNext();
  }

  delete pCursor;
  pHashMap->vDropKeyLock(oRootBufferId.m_pvAsAddress);

  return bFound;
}

/*----------------------------------------------------------------------------*/
/* Is there a view of this name on this node?                                 */
/*----------------------------------------------------------------------------*/
bool ViewCatalog::bExists(Stz* szViewNm)
{
  s256 sStoredNm;
  s4K  sColumnList;
  s4K  sDefinition;

  return bFind(szViewNm, sStoredNm, sColumnList, sDefinition);
}

/*----------------------------------------------------------------------------*/
/* Add a view to this node's "views" metadata table                           */
/*----------------------------------------------------------------------------*/
void ViewCatalog::vInsert(Stz* szViewNm, Stz* szColumnList, Stz* szDefinition)
{
  typedef struct {
    i8 iKey;
    BufferId oBufferId;
  } PdsBufferIdItem;

  Table* pTable = AM::pMetaDataTableViews();
  if (!pTable) {
    return;
  }

  StreamDef* pStreamDef = pTable->pStreamDef();
  TablePtn*  pTablePtn  = pTable->pTablePtn(0);
  Pds* pPdsRootBuffer = _pPds(
      AM::pHashMap()->pValueAt(
          pTablePtn->oRootBufferId().m_pvAsAddress, true
      ));

  // Append to the last row buffer of the table, or start a new one.
  Pds* pPdsRowBuffer = nullptr;
  bool bAppend = false;

  if (pPdsRootBuffer->nItems() == 0) {
    pPdsRowBuffer = new Pds(pStreamDef, BT_DATA);
    bAppend = true;

  } else {
    i8 iSearchKey = C_INT_MAX;
    SkipListBlock* pHead = pPdsRootBuffer->pFindLessThan((void*) &iSearchKey);

    if (pHead != nullptr) {
      PdsBufferIdItem* pBufferIdStruct = (PdsBufferIdItem*) pHead->aItem();
      HashMapEntry* pEntry = AM::pHashMap()->pEntryAt(pBufferIdStruct->oBufferId.m_pvAsAddress, true);
      pPdsRowBuffer = _pPds(pEntry->m_pValObject);

    } else {
      pPdsRowBuffer = new Pds(pStreamDef, BT_DATA);
      bAppend = true;
    }
  }

  Row* pRow = pPdsRowBuffer->pAllotRow(pStreamDef,
                                       C_VARGS_BEGIN,
                                       "rapids"_Stz,
                                       "public"_Stz,
                                       szViewNm,
                                       szColumnList,
                                       szDefinition,
                                       C_VARGS_END);

  if (!pRow && !bAppend) {
    // The last row buffer is full. Put the row in a new one.
    AM::pHashMap()->vDropKeyLock(pPdsRowBuffer->oGetBufferId().m_pvAsAddress);

    pPdsRowBuffer = new Pds(pStreamDef, BT_DATA);
    bAppend = true;

    pRow = pPdsRowBuffer->pAllotRow(pStreamDef,
                                    C_VARGS_BEGIN,
                                    "rapids"_Stz,
                                    "public"_Stz,
                                    szViewNm,
                                    szColumnList,
                                    szDefinition,
                                    C_VARGS_END);
  }

  RASSERT(pRow);

  if (bAppend) {
    i8 nRows = 0;
    pTablePtn->vAppendBuffer(pPdsRowBuffer, nRows);
  } else {
    AM::pHashMap()->vDropKeyLock(pPdsRowBuffer->oGetBufferId().m_pvAsAddress);
  }

  AM::pHashMap()->vDropKeyLock(pPdsRootBuffer->oGetBufferId().m_pvAsAddress);
}

/*----------------------------------------------------------------------------*/
/* Remove a view from this node's "views" metadata table                      */
/*----------------------------------------------------------------------------*/
void ViewCatalog::vDelete(Stz* szViewNm)
{
  Table* pTable = AM::pMetaDataTableViews();
  if (!pTable) {
    return;
  }

  MetaDataTables::vDeleteRowFromTable(pTable, C_COL_NAME, szViewNm);
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
