/*============================================================================*/
/* Index.cpp - index portion of table partition                               */
/*============================================================================*/

#include "BASE.h"

#include "SkipRoots.hpp"
#include "EntBuffer.hpp"
#include "IndexTree.hpp"
#include "CrvBuffer.hpp"
#include "TablePtn.hpp"
#include "Table.hpp"
#include "Op.hpp"
#include "Stage.hpp"
#include "Stream.hpp"
#include "Ent.hpp"
#include "Key.hpp"

Index*  gpReadApiIndex;

/*----------------------------------------------------------------------------*/
/*-- Externs -----------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* find the index                                                             */
/*----------------------------------------------------------------------------*/
extern "C" void* aExtern_GetIndexDef(void* aIndex) {
  Index* pIndex = _pIndex(aIndex);
  return pIndex->pIndexDef();
}

/*----------------------------------------------------------------------------*/
/* find the index                                                             */
/*----------------------------------------------------------------------------*/
extern "C" void* aExtern_GetIndexTree(void* aTable, void* aIndex) {

  LglPtn tLglPtn;

  Table* pTable = _pTable(aTable);
  Index* pIndex = _pIndex(aIndex);

  StreamDef* pStreamDef = pTable->pStreamDef();
  Op* pOp = MY::pOp();

  if (pStreamDef->bReplicated()) {

    Stage*  pStage   = pOp->pStage();
    Stream* pStream0 = pStage->pGet(0);
    Op*     pOp0_0   = pStream0->pGet(0);

    tLglPtn = pOp0_0->tLglPtn();
  }
  else {
    tLglPtn = pOp->tLglPtn();
  }

  i8 i_TablePtn = pTable->iPtnNoToTablePtn(tLglPtn);
  TablePtn* pTablePtn = pTable->pTablePtn(i_TablePtn);

  i8 iIndexNo = pIndex->iIndexNo();
  IndexTree* pIndexTree = pTablePtn->pIndexTree(iIndexNo);

  return pIndexTree;
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* safe cast                                                                  */
/*----------------------------------------------------------------------------*/
Index* STORE::_pIndex(void* pAddr)
{
  Index* pIndex = reinterpret_cast<Index*>(pAddr);
  
  RASSERT_TYPE(pIndex,CN_INDEX);
  
  return pIndex;
}


/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* operator new                                                               */
/*----------------------------------------------------------------------------*/
void* Index::operator new(size_t)
{
  Index* pIndex = (Index*)Heaps::pAllocate(CN_INDEX);
  
  return pIndex;
}

/*----------------------------------------------------------------------------*/
/* operator new                                                               */
/*----------------------------------------------------------------------------*/
void* Index::operator new(size_t, Index* pIndex)
{
  return pIndex;
}

/*----------------------------------------------------------------------------*/
/* operator delete                                                            */
/*----------------------------------------------------------------------------*/
void Index::operator delete(void* aAddrIndex)
{
  Index* pIndex = reinterpret_cast<Index*>(aAddrIndex);

  Heaps::vFree(pIndex);
}

/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Index::Index(Stz* szIndexNm)
: Operand(CN_INDEX)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);

#ifdef WITH_STATISTICS_COUNTERS
  gpAttachMap->m_oStatistics.Index.m_nCreates++;
#endif
    
  m_pIndexDef = nullptr;
  
  m_iIndexNo  = -1;
  m_sIndexNm  = szIndexNm;
  
  m_pTable    = nullptr;
  
  _checkInitialised_(CN_INDEX);
  
  _postcondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Index::~Index(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
#ifdef WITH_STATISTICS_COUNTERS
  gpAttachMap->m_oStatistics.Index.m_nDrops++;
#endif
  
  vTidyUp();
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* dump data                                                                  */
/*----------------------------------------------------------------------------*/
void Index::vDump(eCN eClass, Object* pObject, Stz* aTab)
{
  UNUSED(eClass);
  UNUSED(pObject);
  UNUSED(aTab);
}

/*----------------------------------------------------------------------------*/
/* post recv function                                                         */
/*----------------------------------------------------------------------------*/
void Index::vPostRecv(void* aIndex)
{
  _static_
  
  Index* pIndex = _pIndex(aIndex);
  
  pIndex->vResetPointers();
}


/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* analyze the index                                                          */
/*----------------------------------------------------------------------------*/
void Index::vAnalyze(i8& nBuffers, i8& nEntries)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  i8 nPtnsPerNode = AM::SYSARGS::nPtnsPerNode();
  
  nBuffers = 0;
  nEntries = 0;

  for (i8 i_TablePtn = 0; i_TablePtn < nPtnsPerNode; i_TablePtn++)
    vAnalyze(i_TablePtn,nBuffers,nEntries);
}

/*----------------------------------------------------------------------------*/
/* analyze the index                                                          */
/*----------------------------------------------------------------------------*/
void Index::vAnalyze(i8 i_TablePtn, i8& nBuffers, i8& nEntries)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
    
  IndexTree* pIndexTree = m_pTable->pTablePtn(i_TablePtn)->pIndexTree(m_iIndexNo);
  EntBuffer *pIndexBuffer = pIndexTree->pRootBuffer();
    
  if (pIndexBuffer)
    pIndexBuffer->vAnalyze(nBuffers, nEntries);
}

/*----------------------------------------------------------------------------*/
/* estimate how many rows share one key of this index (at least 1)            */
/*----------------------------------------------------------------------------*/
// Join ordering needs to know whether probing this index returns one row or
// many. Entries are kept in key order, so a prefix of the index in the first
// local table partition gives the average number of entries per key. The cost
// is bounded by the sample size, whatever the size of the table.
i8 Index::nEstKeyFanOut(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);

  static const i8 C_FANOUT_SAMPLE = 20000;

  if (bIsUnique())
    return 1;

  TablePtn* pTablePtn = m_pTable->pTablePtn(m_pTable->iPtnNoToTablePtn(0));
  if (! pTablePtn)
    return 1;

  IndexTree* pIndexTree = pTablePtn->pIndexTree(m_iIndexNo);
  if (! pIndexTree)
    return 1;

  eKE eKind = pIndexTree->eKeyKind();
  if (eKind != KE_INT && eKind != KE_GENERAL)
    return 1;

  EntBuffer* pEntBuffer = nullptr;
  Ent* aEnt = IndexTree::aIndexBegin(pIndexTree, &pEntBuffer);
  Ent* aEnt_prev = nullptr;

  i8 nEntries = 0;
  i8 nKeys    = 0;

  while (aEnt && nEntries < C_FANOUT_SAMPLE) {
    bool bNewKey;
    if (! aEnt_prev)
      bNewKey = true;
    else if (KE_INT == eKind)
      bNewKey = aEnt->iIntElm0() != aEnt_prev->iIntElm0();
    else
      bNewKey = ! Key::bEqualTo(m_pIndexDef, aEnt->aKey(), aEnt_prev->aKey());

    if (bNewKey)
      nKeys++;
    nEntries++;

    aEnt_prev = aEnt;
    aEnt = EntBuffer::aGetNextEntry(&pEntBuffer, aEnt);
  }

  if (! nKeys)
    return 1;

  return (nEntries + nKeys - 1) / nKeys;
}

/*----------------------------------------------------------------------------*/
/* get index column                                                           */
/*----------------------------------------------------------------------------*/
Column* Index::pGetColumn(i8 iColumn)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_pIndexDef->pGetColumn(iColumn);
}

/*----------------------------------------------------------------------------*/
/* get no of column vector                                                    */
/*----------------------------------------------------------------------------*/
i8 Index::nColumns()
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_pIndexDef->nColumns();
}

/*----------------------------------------------------------------------------*/
/* get the index definition                                                   */
/*----------------------------------------------------------------------------*/
IndexDef* Index::pIndexDef(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_pIndexDef;
}

/*----------------------------------------------------------------------------*/
/* set the index definition                                                   */
/*----------------------------------------------------------------------------*/
void Index::vIndexDef(IndexDef* pIndexDef)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  m_pIndexDef = pIndexDef;

  if (m_pIndexDef->pLocation())
    m_pIndexDef->pLocation()->vSet();
  
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  
  if (m_pIndexDef->szIndexNm()->bSame(S_READAPI_I))
    AM::vReadApiIndex(this);
}

/*----------------------------------------------------------------------------*/
/* get the index name                                                         */
/*----------------------------------------------------------------------------*/
Stz* Index::szIndexNm(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_sIndexNm;
}

/*----------------------------------------------------------------------------*/
/* set the indexNo                                                            */
/*----------------------------------------------------------------------------*/
void Index::vIndexNo(i8 iIndexNo)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  m_iIndexNo = iIndexNo;
}

/*----------------------------------------------------------------------------*/
/* get the indexNo                                                            */
/*----------------------------------------------------------------------------*/
i8 Index::iIndexNo(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_iIndexNo;
}

/*----------------------------------------------------------------------------*/
/* is this a primary key  index                                               */
/*----------------------------------------------------------------------------*/
bool Index::bIsPrimaryKey(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_pIndexDef->m_bIsPrimaryKey;
}

/*----------------------------------------------------------------------------*/
/* set index as a primary key index                                           */
/*----------------------------------------------------------------------------*/
void Index::vIsPrimaryKey(bool bPrimaryKey)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  m_pIndexDef->m_bIsPrimaryKey = bPrimaryKey;
}

/*----------------------------------------------------------------------------*/
/* is this index unique?                                                      */
/*----------------------------------------------------------------------------*/
bool Index::bIsUnique(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_pIndexDef->m_bIsUnique;
}

/*----------------------------------------------------------------------------*/
/* set index unique                                                           */
/*----------------------------------------------------------------------------*/
void Index::vIsUnique(bool bUnique)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  m_pIndexDef->m_bIsUnique = bUnique;
}

/*----------------------------------------------------------------------------*/
/* set index as a primary key index                                           */
/*----------------------------------------------------------------------------*/
void Index::vListContents(i8 i_Depth)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  vTrace("%sIndex %lld %s\n"_Stz,
         Stz::szTab(i_Depth),
         m_iIndexNo,
         m_sIndexNm.szStz());
  
  m_pIndexDef->vListContents(i_Depth+1);
}

/*----------------------------------------------------------------------------*/
/* get location                                                               */
/*----------------------------------------------------------------------------*/
Location* Index::pLocation(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return & m_oLocation;
}

/*----------------------------------------------------------------------------*/
/* reset members which are pointers (after being received)                    */
/*----------------------------------------------------------------------------*/
void Index::vResetPointers(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  m_pIndexDef = nullptr;
  m_pTable    = nullptr;
}

/*----------------------------------------------------------------------------*/
/* compute the size of sort key datum                                         */
/*----------------------------------------------------------------------------*/
i8 Index::iSortKeyDatum(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  i8 nKeyDatumSize = 0;
  i8 nIdxColumns = m_pIndexDef->nColumns();
  for (i8 i_IdxCol = 0; i_IdxCol < nIdxColumns; i_IdxCol++) {
    nKeyDatumSize += sizeof(i8);
  }
  
  return nKeyDatumSize;
}

/*----------------------------------------------------------------------------*/
/* get the table                                                              */
/*----------------------------------------------------------------------------*/
Table* Index::pTable(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  return m_pTable;
}

/*----------------------------------------------------------------------------*/
/* set the table                                                              */
/*----------------------------------------------------------------------------*/
void Index::vTable(Table* pTable)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  m_pTable = pTable;
}

/*----------------------------------------------------------------------------*/
/* tidyup                                                                     */
/*----------------------------------------------------------------------------*/
void Index::vTidyUp(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_INDEX,_FOOTER_);
  
  delete m_pIndexDef;
}


/*----------------------------------------------------------------------------*/
/*-- Footer ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
__aFooterFunction(Index)

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
