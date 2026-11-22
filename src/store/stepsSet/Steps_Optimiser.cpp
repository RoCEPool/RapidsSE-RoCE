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

  Error xError;
  CardIter::vXformTimesReset();
  bResult = m_pLogicUnit->m_xpOptimiser->bTransform(xError, pPdsRowBuffer);
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
