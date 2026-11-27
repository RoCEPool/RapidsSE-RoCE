/*============================================================================*/
/* Steps_Optimiser.cpp -- Optimiser step group                                */
/*============================================================================*/

#include <time.h>
#include "BASE.h"
#include "Step.hpp"
#include "EnterExit.h"
#include "StepAccum.hpp"
#include "Steps.hpp"

#include "Op.hpp"
#include "Job.hpp"
#include "CardIter.hpp"
#include "Grid.hpp"
#include "Node.hpp"

#include "LogicUnit.hpp"
#include "Error.hpp"
#include "Card.hpp"
#include "ExtendedText.hpp"
#include "StatementCache.hpp"
#include <mutex>
#include <unordered_map>

// Optimiser cache (see the statement cache in Steps_SqlCompiler.cpp): the
// optimiser turns the text of a pipeline statement into the text of the
// optimised statement and of its C code, reading the job tags, the catalogue
// and the statistics. The same text with the same tags, while no data or
// catalogue object has changed, gets the same two texts back: dissect notes
// the hit, transform does nothing, weave hands the texts to the job. Not
// used with the join order tags (they return rows), on a grid of more than
// one node, or with the job tag /optimiser_off_sql_cache.
namespace {
  struct OptPending { std::string sKey; i8 iEpoch = 0; bool bHit = false; STORE::OptCacheItem xItem; };
  std::mutex g_oOptPendingLock;
  std::unordered_map<void*, OptPending> g_oOptPending;     // by Optimiser

  void vTextBlocks(ExtendedText* pText, std::vector<std::string>& rvs)
  {
    for (i8 i = 0; i < pText->nTextBlocks(); i++)
      rvs.push_back(std::string((const char*) pText->szTextBlock(i)->azChar()));
  }

  ExtendedText* pTextOfBlocks(const std::vector<std::string>& rvs)
  {
    ExtendedText* pText = new ExtendedText();
    for (const std::string& s : rvs)
      pText->vConcat((Stz*) s.c_str());
    return pText;
  }

  // the note of dissect for this optimiser, taken out
  bool bTakePending(void* pOptimiser, OptPending& rxPending)
  {
    std::lock_guard<std::mutex> oGuard(g_oOptPendingLock);
    auto it = g_oOptPending.find(pOptimiser);
    if (it == g_oOptPending.end())
      return false;
    rxPending = it->second;
    g_oOptPending.erase(it);
    return true;
  }

  bool bPendingHit(void* pOptimiser)
  {
    std::lock_guard<std::mutex> oGuard(g_oOptPendingLock);
    auto it = g_oOptPending.find(pOptimiser);
    return it != g_oOptPending.end() && it->second.bHit;
  }
}

/*----------------------------------------------------------------------------*/
/* add primitives                                                             */
/*----------------------------------------------------------------------------*/
void Step::vAddSteps_Optimiser(void)
{
  vAddStep("Optimiser[]"_Stz,              &Step::vOptimiserGet);
  vAddStep("Optimiser[].delete"_Stz ,      &Step::vOptimiserDelete);
  vAddStep("Optimiser[].dissect"_Stz,      &Step::vOptimiserDissect);
  vAddStep("Optimiser[].get"_Stz,          &Step::vOptimiserGet);
  vAddStep("Optimiser[].new"_Stz,          &Step::vOptimiserNew);
  vAddStep("Optimiser[].transform"_Stz,    &Step::vOptimiserTransform);
  vAddStep("Optimiser[].weave"_Stz,        &Step::vOptimiserWeave);
}


/*----------------------------------------------------------------------------*/
/* Optimiser[].delete -- delete optimiser                                     */
/*----------------------------------------------------------------------------*/
void Step::vOptimiserDelete(void)
{
  ENTRY();
    
  m_pLogicUnit->m_xpOptimiser.vDeleteComponent();

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* Optimiser[].dissect -- dissed unoptimised pipeline spec                    */
/*----------------------------------------------------------------------------*/
void Step::vOptimiserDissect(void)
{
  ENTRY();
    
  VertexSpec* pUnOptPlineSpec =
  _pVertexSpec(m_oDataStack.pPopPtr(__func__));

  // optimiser cache: the key is the statement of the job and its tags
  {
    Job* pJob = MY::pJob();
    TagSet* pJobTags = pJob ? pJob->pJobTags() : nullptr;
    ExtendedText* pCmdText = m_pJob.bIsNotNull() ? m_pJob->pCmdText() : nullptr;
    if (pJobTags && pCmdText && AM::GRID::nKnownNodes() <= 1 &&
        ! pJobTags->bHasName("<optimiser_off_sql_cache>"_Stz) &&
        ! pJobTags->bHasName("<joinOrder>"_Stz) && ! pJobTags->bHasName("<joinPermutations>"_Stz) &&
        ! pJobTags->bHasName("<optimiser_trace>"_Stz)) {
      OptPending xPending;
      xPending.iEpoch = STORE::iColCacheDataEpoch();
      pJobTags->vAppendKey(xPending.sKey, "<sqlStatement>"_Stz);
      std::vector<std::string> vsCmd;
      vTextBlocks(pCmdText, vsCmd);
      for (const std::string& sBlock : vsCmd) {
        xPending.sKey += '\n';
        xPending.sKey += sBlock;
      }
      xPending.bHit = STORE::bOptCacheGet(xPending.sKey, xPending.iEpoch, xPending.xItem);
      bool bHit = xPending.bHit;
      {
        std::lock_guard<std::mutex> oGuard(g_oOptPendingLock);
        g_oOptPending[(void*) *m_pLogicUnit->m_xpOptimiser] = xPending;
      }
      if (bHit) {
        EXIT();
        return;
      }
    }
  }

  m_pLogicUnit->m_xpOptimiser->vDissect(pUnOptPlineSpec);

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* Optimiser[].get -- get optimiser                                           */
/*----------------------------------------------------------------------------*/
void Step::vOptimiserGet(void)
{
  ENTRY();

  if (m_pLogicUnit->m_xpOptimiser.bIsNotNull())
    m_oDataStack.vPushPtr(* m_pLogicUnit->m_xpOptimiser);
  else
    m_oDataStack.vPushPtr(nullptr);
  
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* phase mark -- with the job tag <phaseTimes>, one trace line per point:      */
/*   [phase] <node> J<origin>.<job> <point> <monotonic ms>                    */
/* tests/tpch_sf1_correctness/scripts/phase_times.py turns these into the time */
/* of each phase of a query (SQL compile, optimise, C compile, execute).      */
/*----------------------------------------------------------------------------*/
void Step::vPhaseMark(const char* szPoint)
{
  Job* pJob = nullptr;
  if (m_pJob.bIsNotNull())
    pJob = *m_pJob;
  else if (m_pOp.bIsNotNull())
    pJob = m_pOp->pOuterJobOfOp();
  if (! pJob || ! pJob->bHasTag("<phaseTimes>"_Stz))
    return;

  struct timespec xNow;
  clock_gettime(CLOCK_MONOTONIC, &xNow);
  double dMs = (double) xNow.tv_sec * 1000.0 + (double) xNow.tv_nsec / 1.0e6;

  JobId tId = pJob->tJobId();
  vTrace("[phase] %s J%04lld.%04lld %s %.3f\n"_Stz,
         AM::pGrid()->pThisNode()->szNodeNm(), tId.uNodeId(), tId.uJobNo(), (Stz*) szPoint, dMs);
}

/*----------------------------------------------------------------------------*/
/* Optimiser[].new -- new optimiser                                           */
/*----------------------------------------------------------------------------*/
void Step::vOptimiserNew(void)
{
  ENTRY();
    
  Job* pJob = nullptr;

  if (m_pJob.bIsNotNull()) {
    //RASSERT(! *m_pOp);
    pJob = *m_pJob;
  }
  else
  if (m_pOp.bIsNotNull()) {
    RASSERT(m_pJob.bIsNull());
    pJob = m_pOp->pOuterJobOfOp();
  }
  else {
    NEVER_REACH;
  }

  vPhaseMark("optimise_start");
  m_pLogicUnit->m_xpOptimiser = new Optimiser(pJob->pJobTags());

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* Optimiser[].transform -- transform a pipeline                              */
/*----------------------------------------------------------------------------*/
void Step::vOptimiserTransform(void)
{
  ENTRY();

  b8 bResult;
  Pds *pPdsRowBuffer = nullptr;

  Job* pJob = MY::pJob();
  TagSet* pJobTags = pJob->pJobTags();
  bool bJoinOrder = pJobTags->bHasName("<joinOrder>"_Stz);
  bool bJoinPermutations = pJobTags->bHasName("<joinPermutations>"_Stz);

  if (bJoinOrder || bJoinPermutations) {
    // get the PdsRowBuffer for joinOrder
		pPdsRowBuffer = _pPds(*(m_pLogicUnit)->m_xpPds);
  }

  // optimiser cache: the result is known
  if (bPendingHit((void*) *m_pLogicUnit->m_xpOptimiser)) {
    m_oDataStack.vPushBol(true);
    vPhaseMark("optimise_end");
    EXIT();
    return;
  }

  Error xError;
  CardIter::vXformTimesReset();
  bResult = m_pLogicUnit->m_xpOptimiser->bTransform(xError, pPdsRowBuffer);
  if (! bResult) {                        // nothing to keep
    OptPending xDropped;
    bTakePending((void*) *m_pLogicUnit->m_xpOptimiser, xDropped);
  }
  if (pJobTags->bHasName("<phaseTimes>"_Stz)) {
    s256 sJob;
    sJob.vSprint("J%04lld.%04lld"_Stz, pJob->tJobId().uNodeId(), pJob->tJobId().uJobNo());
    CardIter::vXformTimesTrace(sJob.szStz());
  }

  // push error code
  if (! bResult) {
    m_oDataStack.vPushStz(xError.aErrorMsg());
    m_oDataStack.vPushInt((i8) xError.eErrorCode());
  }

  m_oDataStack.vPushBol(bResult);

  vPhaseMark("optimise_end");

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* Optimiser[].weave -- weave optimised pipeline statement                    */
/*----------------------------------------------------------------------------*/
void Step::vOptimiserWeave(void)
{
  ENTRY();
    
  // optimiser cache: hand the kept texts to the job, or keep the new ones
  OptPending xPending;
  bool bPending = bTakePending((void*) *m_pLogicUnit->m_xpOptimiser, xPending);
  if (bPending && xPending.bHit) {
    ExtendedText* pOldCmd = m_pJob->pCmdText();
    m_pJob->vCmdText((ExtendedText*)nullptr);
    delete pOldCmd;
    m_pJob->vCmdText(pTextOfBlocks(xPending.xItem.vsStmt));
    m_pJob->vCodeBlock(xPending.xItem.bHasCode ? pTextOfBlocks(xPending.xItem.vsCode) : (ExtendedText*)nullptr);
    vPhaseMark("weave_end");
    EXIT();
    return;
  }

  Stz* szOptStmt = m_pLogicUnit->m_xpOptimiser->szWeave();
  UNUSED(szOptStmt);

  // With <optimiser_trace>, show the pipeline that the optimiser produced.
  if (szOptStmt && AM::JOB::bHasTag(C_OPTIMISER_TRACE)) {
    const char* azStmt = (const char*) szOptStmt->azChar();
    i8 nLen = (i8) strlen(azStmt);
    vTrace("Optimised pipeline:\n"_Stz);
    for (i8 i_Pos = 0; i_Pos < nLen; i_Pos += 1000) {
      s2K sChunk;
      sChunk.vCopy((Stz*) (azStmt + i_Pos), (nLen - i_Pos < 1000) ? nLen - i_Pos : 1000);
      vTrace("%s"_Stz, sChunk.szStz());
    }
    vTrace("\nEnd of optimised pipeline\n"_Stz);
  }

  //m_pJob->vCmdText(szOptStmt);

  // CAREFUL RAY
  ExtendedText* pCmdText = m_pJob->pCmdText();
  m_pJob->vCmdText((ExtendedText*)nullptr);
  delete pCmdText;

  m_pJob->vCmdText(m_pLogicUnit->m_xpOptimiser->pOptStmt());
  m_pLogicUnit->m_xpOptimiser->vOptStmt((ExtendedText*)nullptr);

  ExtendedText* pCodeBlock = m_pLogicUnit->m_xpOptimiser->pCodeBlock();
  if (bPending) {
    xPending.xItem.iEpoch = xPending.iEpoch;
    vTextBlocks(m_pJob->pCmdText(), xPending.xItem.vsStmt);
    xPending.xItem.bHasCode = pCodeBlock != nullptr;
    if (pCodeBlock)
      vTextBlocks(pCodeBlock, xPending.xItem.vsCode);
    STORE::vOptCachePut(xPending.sKey, xPending.xItem);
  }
  if (pCodeBlock) {
    m_pLogicUnit->m_xpOptimiser->vCodeBlock(nullptr);
  }

  m_pJob->vCodeBlock(pCodeBlock);

  vPhaseMark("weave_end");

  EXIT();
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
