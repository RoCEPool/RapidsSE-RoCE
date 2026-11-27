/*============================================================================*/
/* Steps_SqlCompiler.cpp - sql compiler step group                            */
/*============================================================================*/

#include "BASE.h"
#include <regex>
#include <functional>
#include <string>
#include <unordered_map>
#include <mutex>
#include "Step.hpp"
#include "EnterExit.h"
#include "StepAccum.hpp"
#include "Steps.hpp"
#include "Grid.hpp"
#include "Node.hpp"

#include "SkipRoots.hpp"
#include "EntBuffer.hpp"
#include "IndexTree.hpp"
#include "TablePtn.hpp"
#include "Table.hpp"

#include "OpSpec.hpp"
#include "StageSpec.hpp"
#include "VertexSpec.hpp"
#include "Pipeline.hpp"
#include "Backend.hpp"
#include "store/sqlCompiler/shadowNodes/planObjects/PlanObject.hpp"
#include "Job.hpp"
#include "Node_I.hpp"
#include "LogicUnit.hpp"

#include "PlanNode.hpp"
#include "RootNode.hpp"
#include "PlanDefinitionParser.hpp"
#include "SqlPlanner.hpp"
#include "ShadowRoot.hpp"
#include "MetadataNode.hpp"
#include "ShadowPlan.hpp"
#include "PlanOperand.hpp"

#include "AggregateSplit.hpp"
#include "OrFactor.hpp"
#include "EqClosure.hpp"
#include "ConstFold.hpp"
#include "CorrSemiJoin.hpp"
#include "NameValidator.hpp"
#include "ExprValidator.hpp"
#include "SetOpValidator.hpp"
#include "PostProject.hpp"
#include "ExprPushdown.hpp"
#include "ProjValidation.hpp"
#include "CorrBySquash.hpp"
#include "JoinOptimisation.hpp"
#include "PartitionPlan.hpp"
#include "StageMarkers.hpp"
#include "MainSubquery.hpp"

#include "TagConstants.h"

#include "store/sqlCompiler/shadowNodes/planObjects/planMarkers/statementMarkers/dml/ExplainMarker.hpp"
#include "ExplainOp.hpp"

#include "GraphValidator.hpp"

#include "SqlException.hpp"

/*----------------------------------------------------------------------------*/
/* add primitives                                                             */
/*----------------------------------------------------------------------------*/
void Step::vAddSteps_SqlCompiler(void)
{
  // Ancilliary steps used in the compilation pipeline
  vAddStep("sqlCompiler::deletePlan"_Stz,         &Step::vSqlCompilerDeletePlan);
  vAddStep("sqlCompiler::doExplain"_Stz,          &Step::vSqlCompilerDoExplain);
  vAddStep("sqlCompiler::doExplainTliterate"_Stz, &Step::vSqlCompilerDoExplainTliterate);
  vAddStep("sqlCompiler::explainFormat"_Stz,      &Step::vSqlCompilerExplainFormat);
  vAddStep("sqlCompiler::hasExplain"_Stz,         &Step::vSqlCompilerHasExplain);

  // Stages in the compilation pipeline (in alphabetical order)
  vAddStep("sqlCompiler::aggregateSplit"_Stz,     &Step::vSqlCompilerAggregateSplit);
  vAddStep("sqlCompiler::corrBySquash"_Stz,       &Step::vSqlCompilerCorrBySquash);
  vAddStep("sqlCompiler::explainPlan"_Stz,        &Step::vSqlCompilerExplainPlan);
  vAddStep("sqlCompiler::exprValidate"_Stz,       &Step::vSqlCompilerExprValidate);
  vAddStep("sqlCompiler::exprPushdown"_Stz,       &Step::vSqlCompilerExprPushdown);
  vAddStep("sqlCompiler::joinOptimisation"_Stz,   &Step::vSqlCompilerJoinOptimisation);
  vAddStep("sqlCompiler::nameValidate"_Stz,       &Step::vSqlCompilerNameValidate);
  vAddStep("sqlCompiler::parser"_Stz,             &Step::vSqlCompilerParser);
  vAddStep("sqlCompiler::partitionPlan"_Stz,      &Step::vSqlCompilerPartitionPlan);
  vAddStep("sqlCompiler::partitionExplain"_Stz,   &Step::vSqlCompilerPartitionExplain);
  vAddStep("sqlCompiler::postProject"_Stz,        &Step::vSqlCompilerPostProject);
  vAddStep("sqlCompiler::projValidation"_Stz,     &Step::vSqlCompilerProjValidation);
  vAddStep("sqlCompiler::setOpValidate"_Stz,      &Step::vSqlCompilerSetOpValidate);
  vAddStep("sqlCompiler::stageMarkers"_Stz,       &Step::vSqlCompilerStageMarkers);
  vAddStep("sqlCompiler::transliterate"_Stz,      &Step::vSqlCompilerTransliterate);
}


// Job tag /sqlStepTimes traces the time of each SQL compiler step.
namespace {
// The parsed plan graph is checked against its definitions only when the job
// asks for checks (/optimiser_validate or /validate_plan).
bool bValidatePlanGraph(void)
{
  return AM::JOB::bHasTag("<optimiser_validate>"_Stz) || AM::JOB::bHasTag("<validate_plan>"_Stz);
}

struct SqlStepTimer {
  const char* m_szStep;
  u8 m_uStart;
  explicit SqlStepTimer(const char* szStep)
  : m_szStep(szStep), m_uStart(AM::JOB::bHasTag("<sqlStepTimes>"_Stz) ? Hardware::uReadClock() : 0) {}
  ~SqlStepTimer() {
    if (m_uStart)
      vTrace("sqlStepTime %s %.3f ms\n"_Stz, m_szStep, (Hardware::uReadClock() - m_uStart) / 1e6);
  }
};
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::aggregateSplit -- Split ordinal expressions that have multiple*/
/* aggregate terms into child ProjectOps                                      */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerAggregateSplit(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   *  The purpose of this stage is to handle a limitation of the Relational Logic
   *  Unit and generally make the transliteration easier. The RLU currently cannot
   *  handle expressions that involve the calculation and combination of two
   *  aggregations. e.g. the ordinal expression "SUM($1) / COUNT($1)" involves
   *  calculating the two aggregates and then dividing one into the other.
   *
   *  This multi-stage approach needs to be simplified in a series of single stages.
   *  Since we know this will only occur in ProjectOps and AggregateOps (since other
   *  operators need to use the projected columns) we can do this by checking each
   *  ProjectOp & AggregateOp and adding a ProjectOp underneath them that contain
   *  the individual expression calculations.
   *
   *  e.g. for the example above we add a ProjectOp with two additional columns of
   *  SUM($1) and COUNT($1). The above ordinal expression is then implemented as
   *  $1 / $2, which is supported by the RLU and transliteration.
   */


  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));



  try {
    AggregateSplit::vAggregateSplit(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
    Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::corrBySquash -- Squash ProjectOps for CORRESPONDING BY clauses*/
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerCorrBySquash(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   *  This stage is relevant if the query has a SetOp (UNION, INTERSECT, EXCEPT)
   *  and the SetOp has a CORRESPONDING BY clause. In that case the children of
   *  the SetOp will be ProjectOps. This stage will push these ProjectOps down
   *  as far as possible into the children. This will simplify the transliteration
   *  and also any possible double projections from occurring.
   *
   *  e.g. given the query:
   *    SELECT * FROM nation UNION ALL CORRESPONDING SELECT n_nationkey FROM nation;
   *  Then the first child query (SELECT * FROM nation) is projecting all 4 columns
   *  when the union only needs the n_nationkey column. So the CORRESPONDING clause
   *  will cause a ProjectOp to be placed as a child of the union, above the first
   *  child query. This first child query will have a query plan looking like this:
   *
   *           UnionOp
   *           /      \
   *   ProjectOp[*]   ProjectOp
   *   ProjectOp      TableScanOp
   *   TableScanOp
   *
   *   The ProjectOp marked with [*] is the effect of the CORRESPONDING clause.
   *   This stage will look for this condition and eliminate any redundant ProjectOps
   *   immediately below the CORRESPONDING BY ProjectOp.
   *
   *   Also refer to this document for implementation details, challenges and constraints
   *   via some use cases:
   *     Design/Design Discussions/Optimiser-SquashingCorrBy.key
   */


  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));



  try {
    CorrBySquash::vSearchAndSquash(pPlanRoot);

    // DISTINCT aggregates become two aggregations here, before the plan is
    // partitioned, so that the lower one is partitioned like any GROUP BY.
    AggregateSplit::vDistinctSplit(pPlanRoot);

    // Conditions that every branch of an OR in a filter implies, added to the
    // filter so that they can be applied to a table before the joins.
    OrFactor::vOrFactor(pPlanRoot);

    // Join predicates implied by equal columns (a = b and b = c: a = c), so
    // that a join order can join the tables of the chain in any order.
    EqClosure::vEqClosure(pPlanRoot);

    // DATE literal +/- INTERVAL literal computed once, not for every row.
    ConstFold::vConstFold(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::deletePlan -- Simply delete the plan in the operand           */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerDeletePlan(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * Simply delete the plan in the operand.
   * Used to help cleanup after transliterate.
   * Necessary because of the ability to explain the transliterate step.
   */

  // Get the plan operand
  PlanOperand *pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  if (pPlanOperand->azCachedPipeline()) {     // from the statement cache: no plan
    EXIT();
    return;
  }
  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  ASSERT(ShadowRoot::bIsExactType(pRootNode));
  delete pRootNode;

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::doExplain -- (potentially) create the explain of the plan     */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerDoExplain(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * This is not a strict stage in the sql compiler pipeline. Instead this
   * function is called after every stage is invoked if vSqlCompilerHasExplain()
   * returns true.
   *
   * vSqlCompilerHasExplain() will return true if the plan is topped by an ExplainOp.
   * But not every stage in the plan needs to be explained. So this function first
   * determines if the current stage needs to be explained and if so it creates
   * the rows that describe the plan at this time. Multiple stages can be explained.
   *
   * Finally, if we are explaining the plan then we also need to stop the plan from
   * being executed, so this function also determines when that is. We can't
   * terminate the pipeline early if we haven't explained a stage that the user
   * has requested.
   *
   * This function takes two parameters off the data stack - the name of the
   * current stage that was just processed, and the plan itself.
   */

  // get the streamSet table
  Table* pTableDef = _pTable(m_oDataStack.pPopPtr(__func__));

  // Get the compiler stage
  s256 sCompilerStage;
  m_oDataStack.vPopStr(sCompilerStage, __func__);
  ASSERT(sCompilerStage.nLength() > 0);

  // Get the plan operand
  PlanOperand *pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pRootNode);
  ASSERT(ShadowRoot::bIsExactType(pRootNode));

  // get the Pds for explain
  Pds *pPdsRowBuffer = _pPds(*(m_pLogicUnit)->m_xpPds);

  // Find the explainOp. First get the /__root__/plan node
  QueryNode* pPlanRoot = ShadowRoot::pPlanRoot(pRootNode);
  ASSERT(pPlanRoot);
  ASSERT(ShadowPlan::bIsExactType(pPlanRoot));

  // Get the main subquery
  QueryNode* pMainSubqueryMarker = ShadowPlan::pMainSubquery(pPlanRoot);
  ASSERT(pMainSubqueryMarker);
  ASSERT(MainSubquery::bIsOfType(pMainSubqueryMarker));
  ASSERT(pMainSubqueryMarker->nChildren() == 1);

  QueryNode* pExplainMarker = _pQueryNode(pMainSubqueryMarker->pFirstChild());
  ASSERT(pExplainMarker);
  ASSERT(ExplainMarker::bIsExactType(pExplainMarker));

  QueryNode* pExplainOp = _pQueryNode(pExplainMarker->pFirstChild());
  ASSERT(pExplainOp);
  ASSERT(ExplainOp::bIsExactType(pExplainOp));

  // Determine if this stage needs to be explained. The stage range list
  // may indicate otherwise.
  if (ExplainOp::bExplainStage(pExplainOp)) {
    // This stage does need explaining!

    i8 iStageNum = ExplainOp::iStageCount(pExplainOp);

    // If with_types was specified then we don't want to do this for the parser stage
    // because it doesn't have resolved columns and types and stuff, so temporarily
    // turn this off.
    b1 bResetWithTypes = false;
    if (ExplainOp::bWithTypes(pExplainOp) && iStageNum == 1){
      bResetWithTypes = true;
      ExplainOp::vWithTypes(pExplainOp, false);
    }

    // Don't explain the explain node, but do explain each child under it.
    s48K sExplainBuf;

    PlanNodeIter* pIter = pExplainOp->pNewChildrenIter();
    while (pIter->bHasNext()) {
      PlanNode* pExplainable = pIter->pNext();
      Parser::PlanObject::vExplain(pExplainable, pRootNode, pExplainOp, sExplainBuf, 0);
    }
    delete pIter;

    if (bResetWithTypes) {
      ExplainOp::vWithTypes(pExplainOp, true);
    }


    Row *pRow = pPdsRowBuffer->pAllotRow(pTableDef->pStreamDef(),
                                         C_VARGS_BEGIN,
                                         iStageNum,
                                         sCompilerStage.szStz(),
                                         sExplainBuf.szStz(),
                                         C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
  }

  // Do we need to exit after this stage?
  if (ExplainOp::bExitEarly(pExplainOp)) {
    m_pLogicUnit->m_bCompilerToExit = true;
    delete pRootNode;
  }

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::doExplainTliterate -- (potentially) create the explain of plan */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerDoExplainTliterate(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * This function is called if we have transliterated a plan and that plan
   * has an explain operator. We simply want to put the transliterated pipeline
   * command in the row set.
   */

  // get the streamSet table
  Table* pTableDef = _pTable(m_oDataStack.pPopPtr(__func__));

  // Get the compiler stage
  s256 sCompilerStage;
  m_oDataStack.vPopStr(sCompilerStage, __func__);
  ASSERT(sCompilerStage.nLength() > 0);

  // Get the pipeline command
  s48K sPipelineCmd;
  m_oDataStack.vPopStr(sPipelineCmd, __func__);
  ASSERT(sPipelineCmd.nLength() > 0);

  // Get the plan operand
  PlanOperand *pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pRootNode);
  ASSERT(ShadowRoot::bIsExactType(pRootNode));

  // get the Pds for explain
  Pds *pPdsRowBuffer = _pPds(*(m_pLogicUnit)->m_xpPds);

  // Find the explainOp
  QueryNode* pPlanRoot = ShadowRoot::pPlanRoot(pRootNode);
  ASSERT(pPlanRoot);
  ASSERT(ShadowPlan::bIsExactType(pPlanRoot));

  // Get the main subquery
  QueryNode* pMainSubqueryMarker = ShadowPlan::pMainSubquery(pPlanRoot);
  ASSERT(pMainSubqueryMarker);
  ASSERT(MainSubquery::bIsOfType(pMainSubqueryMarker));
  ASSERT(pMainSubqueryMarker->nChildren() == 1);

  QueryNode* pExplainMarker = _pQueryNode(pMainSubqueryMarker->pFirstChild());
  ASSERT(pExplainMarker);
  ASSERT(ExplainMarker::bIsExactType(pExplainMarker));

  QueryNode* pExplainOp = _pQueryNode(pExplainMarker->pFirstChild());
  ASSERT(pExplainOp);
  ASSERT(ExplainOp::bIsExactType(pExplainOp));

  // Determine if this stage needs to be explained. The stage range list
  // may indicate otherwise.
  if (ExplainOp::bExplainStage(pExplainOp)) {
    // This stage does need explaining!

    i8 iStageNum = ExplainOp::iStageCount(pExplainOp);

    Row *pRow = pPdsRowBuffer->pAllotRow(pTableDef->pStreamDef(),
                                         C_VARGS_BEGIN,
                                         iStageNum,
                                         sCompilerStage.szStz(),
                                         sPipelineCmd.szStz(),
                                         C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
  }

  // We always delete the plan's root node at this point
  delete pRootNode;

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::explainFormat -- Return the tabledef for the explain format   */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerExplainFormat(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * Given that vSqlCompilerHasExplain() has returned true, this function returns
   * the string constant that is looked up to get the table definition for the
   * rows that are returned from an EXPLAIN query.
   *
   * This function looks up the format property of the ExplainOp and returns
   * the appropriate tabledef type. All returned string constants must come
   * from C_TDEF_*.
   */

  // Get the plan operand
  PlanOperand *pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pRootNode);
  ASSERT(ShadowRoot::bIsExactType(pRootNode));

  // Find the explainOp
  QueryNode* pPlanRoot = ShadowRoot::pPlanRoot(pRootNode);
  ASSERT(pPlanRoot);
  ASSERT(ShadowPlan::bIsExactType(pPlanRoot));

  // Get the main subquery
  QueryNode* pMainSubqueryMarker = ShadowPlan::pMainSubquery(pPlanRoot);
  ASSERT(pMainSubqueryMarker);
  ASSERT(MainSubquery::bIsOfType(pMainSubqueryMarker));
  ASSERT(pMainSubqueryMarker->nChildren() == 1);

  QueryNode* pExplainMarker = _pQueryNode(pMainSubqueryMarker->pFirstChild());
  ASSERT(pExplainMarker);
  ASSERT(ExplainMarker::bIsExactType(pExplainMarker));

  QueryNode* pExplainOp = _pQueryNode(pExplainMarker->pFirstChild());
  ASSERT(pExplainOp);
  ASSERT(ExplainOp::bIsExactType(pExplainOp));

  // Get the tabledef string for the current explain format
  Stz* szTableDef = ExplainOp::szTableDef(pExplainOp);
  ASSERT(szTableDef);

  m_oDataStack.vPushStz(szTableDef);

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::explainPlan -- explain plan stage                             */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerExplainPlan(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * The purpose of this stage in the pipeline is to be a marker for the last
   * point in the SQL compilation pipeline before the plan graph is
   * transliterated into a pipeline statement. If an query is being explained
   * then this is the point that it happens.
   *
   * However the explain does not actually occur here. Because we can ask for
   * an explain of many or all stages from the parser to this stage the
   * SQL compilation pipeline calls the above vSqlCompilerDoExplain() step after
   * running the step for each stage of the pipeline. So we don't need to do
   * anything specific here. Instead this empty stage is basically a name
   * marker for the last step prior to transliteration.
   *
   * The plan is not modified in this stage.
   */

  EXIT();
}


/*----------------------------------------------------------------------------*/
/* sqlCompiler::exprPushdown -- Push expressions down as far as possible      */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerExprPushdown(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * The purpose of this stage is to ensure that all expressions in sql operators
   * above a ProjectOp or AggregateOp (so a HAVING clause's FilterOp or a
   * ORDER BY's OrderOp) are pushed down into a ProjectOp or AggregateOp to be
   * calculated, if required.
   *
   * e.g. consider this statement:
   *   SELECT col1, SUM(col2) FROM t GROUP BY col1 HAVING AVG(col3) > 0;
   * In this statement neither AVG(col3) or col3 is available or can be calculated
   * because the AggregateOp below it in the plan only projects [col1, SUM(col2)].
   * So we need it to also project [AVG(col3) > 0] as well so the FilterOp that implements
   * the HAVING clause can test it.
   *
   * After pushing down expressions we will then walk the graph and validate that
   * for all Sql operators that contain expressions those expressions can be entirely
   * represented in ordinal notation relative to the set of projected columns that
   * operator will receive.
   *
   * Note that in this stage there may be multiple ProjectOps in a given StatementMarker
   * section due to the PostProject stage running beforehand.
   *
   * The plan may be modified in this stage.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));


  try {
    ExprPushdown::vPushdownExprs(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}


/*----------------------------------------------------------------------------*/
/* sqlCompiler::joinOptimisation -- temporary join optimisation via hinting   */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerJoinOptimisation(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * The purpose of this stage is to look for any join hints and apply them.
   *
   * The plan may be modified in this stage.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));


  try {
    JoinOptimisation::vOptimiseJoins(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::exprValidate -- expression validation                         */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerExprValidate(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * At this point of the pipeline we have a basic plan with names that have
   * been successfully resolved and fully qualified. The plan is not yet executable
   * and follows a basic structure - there is either one ProjectOp or AggregateOp
   * (depending on whether there is a GROUP BY list), a FilterOp below this
   * Project/AggregateOp represents a WHERE clause and a FilterOp above this
   * Project/AggregateOp represents a HAVING clause.
   *
   * This stage checks the expressions of (almost) all operators in the plan
   * to ensure that they are semantically correct. They tend to focus on
   * aggregations and group by expressions, as well as the expressions that can
   * be found in the operators that sit above the Project/AggregateOp.
   *
   * Some expression validation was done at parse time because it was just easier
   * to do them then. e.g. validating that expressions in a WHERE clause cannot
   * contain aggregations.
   *
   * The plan is not modified in this stage.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));


  try {
    ExprValidator::vValidateExprs(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::hasExplain -- does sql have to do an explain?                 */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerHasExplain(void) {
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  bool bExplain = false;

  // Get the compiler stage
  s256 sCompilerStage;
  m_oDataStack.vPopStr(sCompilerStage, __func__);
  ASSERT(sCompilerStage.nLength() > 0);

  // Get the plan operand
  PlanOperand *pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));

  // a statement from the cache (never an EXPLAIN) has no plan
  if (pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushInt(false);
    EXIT();
    return;
  }

  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pRootNode);
  ASSERT(ShadowRoot::bIsExactType(pRootNode));

  // Determine if there is an ExplainOp in this plan.
  QueryNode* pPlanRoot = ShadowRoot::pPlanRoot(pRootNode);

  // pPlanRoot should have a child which is a MainSubquery marker.
  // This marker may have one child being an ExplainMarker.
  // The ExplainMarker should have one child being the ExplainOp

  // Get the main subquery
  QueryNode* pMainSubqueryMarker = ShadowPlan::pMainSubquery(pPlanRoot);
  ASSERT(pMainSubqueryMarker);
  ASSERT(MainSubquery::bIsOfType(pMainSubqueryMarker));
  ASSERT(pMainSubqueryMarker->nChildren() == 1);

  QueryNode* pMarker = _pQueryNode(pMainSubqueryMarker->pFirstChild());
  if (pMarker && ExplainMarker::bIsExactType(pMarker)) {

    QueryNode* pExplainOp = _pQueryNode(pMarker->pFirstChild());
    if (pExplainOp && ExplainOp::bIsExactType(pExplainOp)) {
      bExplain = true;

      // Increment the number of compiler stages encountered
      ExplainOp::vIncStageCount(pExplainOp);

      // Record the names of the compiler stages encountered
      ExplainOp::vAddCompilerStageName(pExplainOp, sCompilerStage.szStz());
    }
  }

  m_oDataStack.vPushInt(bExplain);

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::nameValidate -- name validation                               */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerNameValidate(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * At this stage of the pipeline we have a basic plan with a basic shape
   * that is not yet executable. The expressions may contain ColumnRefs that
   * are not fully qualified (i.e. use a catalog.schema.table.columnName
   * syntax) and these ColumnRefs may not be correct.
   *
   * This stage will try to resolve the ColumnRefs using the DataSource objects
   * (of which a TableScanOp is an example) that sit below them. Errors will
   * be returned for columns that are unresolvable or ambiguous. If a ColumnRef
   * can be resolved then it will also then be fully qualified too (i.e. the
   * catalog/schema/table names will be added to the ColumnRef).
   *
   * Therefore the plan is modified in this stage.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));


  // Walk the graph and validate that each name can be resolved uniquely.
  // i.e. each table is known and unambiguous, each column refers to tables
  // that are being used, etc.
  // Fill in the catalog/schema/table fields of the columnRef and tableRefs
  // where applicable.
  try {
    NameValidator::vValidateNames(pPlanRoot);

    // The restriction of the outer query on the key of a correlated sub-query,
    // copied into the sub-query (CorrSemiJoin.cpp), found on this plan now
    // that its names are resolved. A changed statement is planned again; if
    // that fails, this plan stays.
    if (pPlanOperand->azSqlText() &&
        !AM::JOB::bHasTag("<optimiser_off_corr_semijoin>"_Stz)) {
      std::string sRewritten;
      if (Parser::CorrSemiJoin::bRewrite(pPlanRoot, std::string(pPlanOperand->azSqlText()), sRewritten)) {
        Parser::SqlPlanner* pPlanner2 = new Parser::SqlPlanner();
        pPlanner2->m_bValidatePlan = bValidatePlanGraph();
        Parser::PlanNode* pPlanRoot2 = nullptr;
        try {
          pPlanRoot2 = pPlanner2->plan((Stz*)sRewritten.c_str(), Parser::PlanDefinitionParser::pGraphTemplate());
          if (pPlanRoot2)
            NameValidator::vValidateNames(Parser::_pRootNode(pPlanRoot2));
        } catch (...) {
          delete pPlanRoot2;
          pPlanRoot2 = nullptr;
          if (AM::JOB::bHasTag("<optimiser_trace>"_Stz))
            vTrace("CorrSemiJoin: planning the rewritten statement failed\n"_Stz);
        }
        delete pPlanner2;
        if (pPlanRoot2) {
          delete pPlanRoot;
          pPlanRoot = Parser::_pRootNode(pPlanRoot2);
          pPlanOperand->vPlanNode(pPlanRoot2);
          if (AM::JOB::bHasTag("<optimiser_trace>"_Stz))
            vTrace("CorrSemiJoin: %s\n"_Stz, sRewritten.c_str());
        }
      }
    }

    NameValidator::vCheckDisabledFunctionality(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }

  // At this point all names in the graph should be fully resolved
  // i.e. they have catalog/schema/table names.

  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* Statement cache: SELECT text -> pipeline statement.                        */
/*                                                                            */
/* The SQL compiler (the operators parser ... transliterate of the pipeline   */
/* sqlCompiler) turns the text of a statement into the text of a pipeline     */
/* statement; for the 22 TPC-H queries that is 19-35 ms each, more than       */
/* DuckDB needs for the whole of Q7 or Q8. A SELECT whose text and job tags   */
/* were compiled before gets the same pipeline statement again: the parser    */
/* step returns a plan operand without a plan that carries it, the steps in   */
/* between pass that operand on, and transliterate returns its text.          */
/*                                                                            */
/* What a compile reads besides the text: the job tags (part of the key), the */
/* catalogue and the statistics of the tables. So the cache is emptied by     */
/* every statement that is not a SELECT (DDL, load, insert, analyse ... all   */
/* come through the parser step), and an entry is used only while no table    */
/* data has changed since it was compiled (iColCacheDataEpoch, counted in the */
/* hooks of CrwBuffer.cpp). Not cached: statements with parameters or with    */
/* functions of the moment (current_date, now, random ...), EXPLAIN, and any  */
/* statement on a grid of more than one node (another node's DDL is not seen  */
/* here). Job tag /optimiser_off_sql_cache: compile every time.               */
/*----------------------------------------------------------------------------*/
namespace STORE { i8 iColCacheDataEpoch(void); }
#include "StatementCache.hpp"

namespace {
  struct SqlCacheItem { std::string sPipeline; i8 iEpoch; };
  std::mutex g_oSqlCacheLock;
  std::unordered_map<std::string, SqlCacheItem> g_oSqlCache;

  // the run-time optimiser's result for a pipeline statement (see
  // Steps_Optimiser.cpp): the optimised statement and the C code, as text
  std::unordered_map<std::string, STORE::OptCacheItem> g_oOptCache;

  // the first word of the statement, lower case (comments and blanks skipped)
  std::string sSqlFirstWord(const char* az)
  {
    for (;;) {
      while (*az == ' ' || *az == '\t' || *az == '\n' || *az == '\r')
        az++;
      if (az[0] == '-' && az[1] == '-') {
        while (*az && *az != '\n')
          az++;
        continue;
      }
      if (az[0] == '/' && az[1] == '*') {
        const char* azEnd = strstr(az + 2, "*/");
        if (! azEnd)
          return "";
        az = azEnd + 2;
        continue;
      }
      break;
    }
    std::string sWord;
    while ((*az >= 'a' && *az <= 'z') || (*az >= 'A' && *az <= 'Z'))
      sWord += (char) (*az++ | 0x20);
    return sWord;
  }

  bool bSqlCacheable(const char* azSql)
  {
    std::string sLower(azSql);
    for (char& c : sLower)
      if (c >= 'A' && c <= 'Z')
        c |= 0x20;
    static const char* aszNot[] = {"?", "current", "now", "today", "rand", "sysdate", "localtime", "uuid",
                                   "explain", "user", "session", "into"};
    for (const char* sz : aszNot)
      if (sLower.find(sz) != std::string::npos)
        return false;
    return true;
  }
}

namespace STORE {
  bool bOptCacheGet(const std::string& rsKey, i8 iEpoch, OptCacheItem& rxItem)
  {
    std::lock_guard<std::mutex> oGuard(g_oSqlCacheLock);
    auto it = g_oOptCache.find(rsKey);
    if (it == g_oOptCache.end())
      return false;
    if (it->second.iEpoch != iEpoch) {
      g_oOptCache.erase(it);
      return false;
    }
    rxItem = it->second;
    return true;
  }

  void vOptCachePut(const std::string& rsKey, const OptCacheItem& rxItem)
  {
    std::lock_guard<std::mutex> oGuard(g_oSqlCacheLock);
    if (g_oOptCache.size() >= 256)
      g_oOptCache.clear();
    g_oOptCache[rsKey] = rxItem;
  }
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::parser -- sql parser                                          */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerParser(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  vPhaseMark("sqlcompile_start");

  /**
   * This stage of the plan is all about producing an initial basic plan graph.
   * The graph is not yet executable and it may not be correct (e.g. incorrect
   * names, bad semantics, etc).
   *
   * The parser will generate one ProjectOp for a basic SELECT statement. If the
   * statement contains a GROUP BY clause then it will produce an AggregateOp
   * instead. So the select list can be found by looking for either the ProjectOp
   * or AggregateOp.
   *
   * The basic structure of the plan is as follows (for a SELECT statement):
   *
   *    StatementMarker            (denotes the type of statement that follows)
   *    SetOp                      (represents UNION/INTERSECT/EXCEPT)
   *    LimitOp
   *    OrderOp
   *    FilterOp                   (represents the HAVING clause)
   *    ProjectOp or AggregateOp   (represents the SELECT list and GROUP BY)
   *    FilterOp                   (represents WHERE clause)
   *    JoinOp
   *    DataSource                 (e.g. TableScanOp etc)
   *
   * A more complicated query that contains multiple distinct sub parts are
   * delineated by further StatementMarkers. Examples of this could include
   * an INSERT..SELECT statement, CREATE TABLE AS SELECT, a UNION of two
   * SELECT statements, a SELECT statement with subqueries, etc.
   *
   * Here's an example of a UNIONed SELECT statement:
   *
   *                   StatementMarker           (indicates it is a SELECT statement)
   *                   UnionOp
   *    StatementMarker       StatementMarker
   *    LimitOp               LimitOp
   *    OrderOp               OrderOp
   *    FilterOp              FilterOp
   *    ProjectOp             ProjectOp
   *    FilterOp              FilterOp
   *    JoinOp                JoinOp
   *    DataSource            DataSource
   *
   * Note that there is a StatementMarker for the top level unioned statement
   * as well as for each of the children. This allows us to walk the graph
   * between StatementMarkers easily, that way the same algorithms can focus on
   * a single SELECT statement and not be concerned about the shape of the overall
   * query.
   *
   */

  PlanOperand* pPlanOperand;

  // Get the original SQL statement entered
  s4K sSqlStatement;
  m_oDataStack.vPopStr(sSqlStatement, __func__);
  
  //vTrace("vSqlCompilerParser got : %s\n"_Stz,sSqlStatement.szStz());

  // statement cache (see above)
  std::string sCacheKey;
  i8 iCacheEpoch = STORE::iColCacheDataEpoch();
  {
    std::string sWord = sSqlFirstWord(sSqlStatement.szStz()->azChar());
    if (sWord != "select" && sWord != "with") {
      std::lock_guard<std::mutex> oGuard(g_oSqlCacheLock);
      g_oSqlCache.clear();
      g_oOptCache.clear();
    }
    else if (AM::GRID::nKnownNodes() <= 1 && ! AM::JOB::bHasTag("<optimiser_off_sql_cache>"_Stz) &&
             bSqlCacheable(sSqlStatement.szStz()->azChar())) {
      Job* pJob = MY::pJob();
      TagSet* pJobTags = pJob ? pJob->pJobTags() : nullptr;
      if (pJobTags)
        pJobTags->vAppendKey(sCacheKey, "<sqlStatement>"_Stz);
      sCacheKey += '\n';
      sCacheKey += sSqlStatement.szStz()->azChar();
      std::string sHit;
      {
        std::lock_guard<std::mutex> oGuard(g_oSqlCacheLock);
        auto it = g_oSqlCache.find(sCacheKey);
        if (it != g_oSqlCache.end()) {
          if (it->second.iEpoch == iCacheEpoch)
            sHit = it->second.sPipeline;
          else
            g_oSqlCache.erase(it);
        }
      }
      if (! sHit.empty()) {
        PlanOperand* pCached = new PlanOperand(nullptr);
        pCached->vSqlText(sSqlStatement.szStz()->azChar());
        pCached->vCachedPipeline(sHit.c_str());
        m_oDataStack.vPushPtr(pCached);
        EXIT();
        return;
      }
    }
  }

  // Read in and parse the plan graph definitions
  const Parser::PlanNode* pGraphTemplate = Parser::PlanDefinitionParser::pGraphTemplate();
  RASSERT(pGraphTemplate);

  // Parse the SQL command.
  Parser::SqlPlanner* pPlanner = new Parser::SqlPlanner();
  pPlanner->m_bValidatePlan = bValidatePlanGraph();
  Parser::PlanNode* pPlanRoot;

  try {
    pPlanRoot = pPlanner->plan(sSqlStatement.szStz(), pGraphTemplate);

  } catch (Parser::SyntaxException& e) {
    //vTrace("SyntaxException at %lld:%lld : %s\n"_Stz, e.i_Line(), e.iColumn(), e.szError());
    Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Grid* pGrid = AM::pGrid();
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SYNTAX_ERROR,
                                e.iLine(),
                                e.iColumn(),
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;

  } catch (Parser::SemanticException& e) {
    //vTrace("SemanticException: %s\n"_Stz, e.szError());
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }

  ASSERT(pPlanRoot);

  pPlanOperand = new PlanOperand(pPlanRoot);
  pPlanOperand->vSqlText(sSqlStatement.szStz()->azChar());
  if (! sCacheKey.empty())
    pPlanOperand->vCacheKey(sCacheKey.c_str(), iCacheEpoch);
  m_oDataStack.vPushPtr(pPlanOperand);

  //vTrace("  antlr(%s) -> RootNode %p\n"_Stz,
  //       sSqlStatement.szStz(),pPlanNode_root);

L_Exit:
  delete pPlanner;
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::partitionExplain -- explain the partitioned plan stage        */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerPartitionExplain(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * The purpose of this stage in the pipeline is to be a marker for the last
   * point in the SQL compilation pipeline before the plan graph is
   * transliterated into a pipeline statement. If a partitioned query is being
   * explained then this is the point that it happens.
   *
   * However the explain does not actually occur here. Because we can ask for
   * an explain of many or all stages from the parser to this stage the
   * SQL compilation pipeline calls the above vSqlCompilerDoExplain() step after
   * running the step for each stage of the pipeline. So we don't need to do
   * anything specific here. Instead this empty stage is basically a name
   * marker for the last step prior to transliteration.
   *
   * The plan is not modified in this stage.
   */

  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::partitionPlan -- parallelise the plan based on partitions     */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerPartitionPlan(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * The purpose of this stage in the pipeline is to...
   *
   * The plan is heavily modified in this stage.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  ASSERT(ShadowRoot::bIsExactType(pRootNode));


  try {
    // Partition the plan
    PartitionPlan::vPartitionPlan(pRootNode);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pRootNode;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }

#ifdef WITH_ASSERT_MACRO
  // Ensure the graph syntax is still valid after this major change
  GraphValidator::vValidatePlan(pRootNode);
#endif

  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::PostProject -- Ensure only selected cols are being returned   */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerPostProject(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * This stage solves the issue of there being an expression in an ORDER BY
   * or HAVING clause that is not in the select list.
   *   e.g. SELECT col1 FROM t ORDER BY col2;
   *
   * In the above example we need to ensure that col2 is being generated
   * (refer to the exprPushdown stage for that) but that only col1 is being
   * returned. We do that by adding another ProjectOp on top of the plan,
   * if required.
   *
   * The plan may be modified in this stage.
   * At the end of this stage we may have multiple ProjectOps in the same
   * statement section of the plan. Prior to this stage there was only ever
   * one ProjectOp or AggregateOp (which is derived from ProjectOp anyway).
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pRootNode = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pRootNode);
  ASSERT(ShadowRoot::bIsExactType(pRootNode));


  try {
    // Add the post projection, if required
    PostProject::vCheckPostProject(pRootNode);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pRootNode;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::projValidation -- Check column projections are valid          */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerProjValidation(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  // the step reads the plan and does not change it: its column lists are
  // built once per node and shared (PlanObject::ColumnExprsScope)
  Parser::PlanObject::ColumnExprsScope oColumnExprsScope;
  ENTRY();

  /**
   * The purpose of this stage is to ensure that all expressions in SQL operators
   * are supplied the right column definitions by the SQL operator below them.
   *
   * i.e., an expression in an arbitrary SQL operator can be converted into an
   * ordinal representation and that ordinal expression is "fully ordinal" - it
   * does not have any ColumnRefs at all. This means that the expression only
   * uses the projected columns from operators below it.
   *
   * The plan is not modified in this stage.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));


  try {
    ProjValidation::vValidateExprVisibility(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }


  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::setOpValidate -- SetOp simplication & validation              */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerSetOpValidate(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * This stage of the plan is about looking for set operators (UNION,
   * INTERSECT, etc) and handling their CORRESPONDING BY list if applicable.
   * Then we can validate that the semantics of the set operation are valid.
   * Each set operator should exist in a separate statement section, bounded
   * by StatementMarkers.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));



  try {
    if (SetOpValidator::bHasSetOps(pPlanRoot)) {
      SetOpValidator::vSimplifySetOps(pPlanRoot);
      SetOpValidator::vValidateSetOps(pPlanRoot);
    }

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }

  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::stageMarkers -- Convert StatementMarkers to StageMarkers      */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerStageMarkers(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  ENTRY();

  /**
   * This stage of the plan is about removing StatementMarkers (used by the compiler)
   * and inserting StageMarkers (used by the transliteration). StageMarkers tell
   * the transliterator when to create a new stage in the pipeline statement.
   */

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));
  // a statement from the cache has no plan: pass it on (see the parser step)
  if (pPlanOperand && pPlanOperand->azCachedPipeline()) {
    m_oDataStack.vPushPtr(pPlanOperand);
    EXIT();
    return;
  }
  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  UNUSED(pPlanRoot);
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));

  try {
    StageMarkers::vStageMarkers(pPlanRoot);

  } catch (Parser::SqlException& e) {
    Grid* pGrid = AM::pGrid();
		Pds* pPds = _pPds(*m_pLogicUnit->m_xpPdsErr);
    Table* pTableErr = _pTable(*m_pLogicUnit->m_xpTableDefErr);
    Row* pRow = pPds->pAllotRow(pTableErr->pStreamDef(),
                                C_VARGS_BEGIN,
                                pGrid->pThisNode()->szNodeNm(),
                                EC_SEMANTIC_ERROR,
                                0,
                                0,
                                e.szError(),
                                C_VARGS_END);
    ASSERT(pRow);
    UNUSED(pRow);
    delete pPlanRoot;
    delete pPlanOperand;
    m_oDataStack.vPushPtr(nullptr);
    goto L_Exit;
  }

  // TODO: eventually we want the received plan graph to be immutable,
  //       so we would send a different version. But for now we will just
  //       manipulate and send the same plan graph
  m_oDataStack.vPushPtr(pPlanOperand);

  L_Exit:
  EXIT();
}



/*----------------------------------------------------------------------------*/
/* HAVING over the groups, from the groups kept in memory                     */
/*----------------------------------------------------------------------------*/
// TPC-H Q11 compares each group's sum with a fraction of the same sum over
// all the rows the groups were made from:
//
//   select k, sum(e) as value from T where W group by k
//   having sum(e) > (select sum(e) * c from T where W)
//
// The sub-query is a second pass over T (its scans and joins). The sum over
// all the rows is the sum of the group sums, and the groups are all in memory
// at the origin once the last GROUP BY level has them: the GROUP BY there
// adds them up in a first pass over its groups and emits, in a second pass,
// the groups that pass the comparison (CombinedSrc, job tag <havingGroups>),
// as DuckDB would reuse the materialised groups instead of a second pass over
// T. This rewrites the pipeline statement for that: the sub-query pipeline
// and the filter on it go; the job gets the tags <havingGroups> (what to
// compare) and <optimiser_off_local_gbyagg> (the groups must reach the
// origin). Job tag /optimiser_off_having_groups keeps the statement as is.
namespace {
  std::string sNormaliseStages(const std::string& s)
  {
    static const std::regex xMarker("stageMarker[0-9]+");
    return std::regex_replace(s, xMarker, "stageMarker");
  }

  bool bRewriteHavingGroups(std::string& sStmt, std::string& sTags)
  {
    // the filter on the sub-query, right behind the group by in its stage
    static const std::regex xFilter(
        "(gbyAgg \" groupBy  ([^\"]*?) aggregates  ([^\"]*?) \\)  ([^\"]*?) sum  \")\n"
        "( *)\\| filter \" type BOOLEAN  \\$([0-9]+) (__subquery[0-9]+)  (>|>=|<|<=)  \"\n");
    std::smatch m;
    if (! std::regex_search(sStmt, m, xFilter))
      return false;
    std::string sAggrs = m[3].str();      // e.g. "type INTEGER ( 19 ) $K1 , type DECIMAL ( 19 2 )"
    std::string sCol   = m[6].str();
    std::string sSqy   = m[7].str();
    std::string sCmp   = m[8].str();

    // the compared column is the sum: the last aggregate, the only one not a key copy
    i8 nAggrs = 1;
    for (char c : sAggrs) if (c == ',') nAggrs++;
    i8 nKeys = 0;
    for (size_t p = sAggrs.find("$K"); p != std::string::npos; p = sAggrs.find("$K", p + 2)) nKeys++;
    if (nAggrs - nKeys != 1 || std::to_string(nAggrs) != sCol)
      return false;

    // the sub-query pipeline: the same stages as the main pipeline up to the
    // group by, with aggreg sum of the same expression, then times a constant
    std::string sBegin = "  pipeline " + sSqy + "\n  begin\n";
    size_t iSqy = sStmt.find(sBegin);
    if (iSqy == std::string::npos)
      return false;
    size_t iSqyEnd = sStmt.find("\n  end\n", iSqy);
    if (iSqyEnd == std::string::npos)
      return false;
    iSqyEnd += 7;
    std::string sSub = sStmt.substr(iSqy + sBegin.size(), iSqyEnd - 7 - iSqy - sBegin.size());

    static const std::regex xAggreg("\\| aggreg \" ([^\"]*?) \\)  ([^\"]*?) sum  \"\n");
    std::smatch ma;
    if (! std::regex_search(sSub, ma, xAggreg))
      return false;
    std::string sSubPrefix = sNormaliseStages(sSub.substr(0, ma.position(0)));
    static const std::regex xFactor("project \" type [^\"]*? \\$1 ([0-9]+)\\.([0-9]+)  \\*  \"");
    std::smatch mf;
    if (! std::regex_search(sSub, mf, xFactor))
      return false;
    std::string sNum = mf[1].str() + mf[2].str();
    std::string sDen = "1" + std::string(mf[2].str().size(), '0');

    // the main pipeline from its first stage to its first group by (on the
    // partitions), which must sum the same expression
    size_t iMain = sStmt.find("  stage stageStart", iSqyEnd);
    if (iMain == std::string::npos)
      return false;
    static const std::regex xGby1("\\| gbyAgg \" groupBy  ([^\"]*?) aggregates  ([^\"]*?) \\)  ([^\"]*?) sum  \"\n");
    std::smatch mg;
    std::string sMain = sStmt.substr(iMain);
    if (! std::regex_search(sMain, mg, xGby1) || mg[3].str() != ma[2].str())
      return false;
    std::string sMainPrefix = sNormaliseStages(sMain.substr(0, (size_t) mg.position(0)));
    // the sub-query's prefix is indented two more (it is a nested pipeline)
    std::string sSubOut;
    for (size_t p = 0; p < sSubPrefix.size(); ) {
      size_t e = sSubPrefix.find('\n', p);
      std::string sLine = sSubPrefix.substr(p, e == std::string::npos ? std::string::npos : e - p + 1);
      if (sLine.compare(0, 2, "  ") == 0)
        sLine = sLine.substr(2);
      sSubOut += sLine;
      if (e == std::string::npos) break;
      p = e + 1;
    }
    if (sSubOut != sMainPrefix)
      return false;

    // rewrite: drop the filter and the sub-query pipeline
    std::string sNew = sStmt.substr(0, (size_t) m.position(0)) + m[1].str() + "\n" +
                       sStmt.substr((size_t) m.position(0) + m.length(0));
    sNew.erase(iSqy, iSqyEnd - iSqy);
    // keep a group when its sum (payload 0) * den <cmp> (sum of the sums) * num
    std::string sCmpNm = sCmp == ">" ? "gt" : sCmp == ">=" ? "ge" : sCmp == "<" ? "lt" : "le";
    sTags = "sum." + std::string("0.") + sCmpNm + "." + sNum + "." + sDen;
    sStmt = sNew;
    return true;
  }

  // TPC-H Q15 (a WITH query read twice, once by a scalar sub-query):
  //
  //   with r (k, v) as (select k, sum(e) from T where W group by k)
  //   select ... from S, r where S.x = r.k and r.v = (select max(v) from r)
  //
  // The groups of r are in memory at the origin: the GROUP BY there finds
  // the max (or min, or sum) of v in a first pass and emits the groups that
  // equal it, instead of a second pass over T for the sub-query. Done when
  // the WITH query is called by the sub-query and once by a cross join behind
  // the scan of one table (the column numbers of r are then known).
  bool bRewriteHavingCte(std::string& sStmt, std::string& sTags,
                         const std::function<i8(const std::string&)>& nColumns)
  {
    static const std::regex xSqy(
        "  pipeline (__subquery[0-9]+)\n  begin\n"
        "    stage stageStart on origin :\n      start\n      \\| pulse once\n      \\| stageMarker[0-9]+\n"
        "    stage stageMarker[0-9]+ on origin :\n"
        "      call \"([A-Za-z0-9_]+) \"\n"
        "      \\| aggreg \" type DECIMAL \\( [0-9]+ [0-9]+ \\) \\)  \\$([0-9]+) (max|min|sum)  \"\n"
        "      \\| combined 0\n      \\| stageLast\n"
        "    stage stageLast on origin :\n      output\n      \\| sink\n      \\| stageStart\n"
        "  end\n");
    std::smatch m;
    if (! std::regex_search(sStmt, m, xSqy))
      return false;
    std::string sSqy = m[1].str(), sCte = m[2].str(), sFn = m[4].str();
    i8 iCol = std::stoll(m[3].str());
    size_t iSqy = (size_t) m.position(0), nSqy = (size_t) m.length(0);

    // the WITH query ends in a group by whose output column iCol is a sum
    std::string sBegin = "  pipeline " + sCte + "\n  begin\n";
    size_t iCte = sStmt.find(sBegin);
    if (iCte == std::string::npos)
      return false;
    size_t iCteEnd = sStmt.find("\n  end\n", iCte);
    if (iCteEnd == std::string::npos)
      return false;
    std::string sCteBody = sStmt.substr(iCte, iCteEnd - iCte);
    static const std::regex xGby("gbyAgg \" groupBy  ([^\"]*?) aggregates  ([^\"]*?) \\)  ([^\"]*?) sum  \"");
    std::smatch mg;
    std::string sLastGby;
    for (auto it = std::sregex_iterator(sCteBody.begin(), sCteBody.end(), xGby); it != std::sregex_iterator(); ++it)
      sLastGby = (*it)[2].str();
    if (sLastGby.empty())
      return false;
    i8 nCols = 1, nKeys = 0;
    for (char c : sLastGby) if (c == ',') nCols++;
    for (size_t p = sLastGby.find("$K"); p != std::string::npos; p = sLastGby.find("$K", p + 2)) nKeys++;
    if (nCols - nKeys != 1 || iCol != nCols || sLastGby.find("DECIMAL") == std::string::npos)
      return false;

    // the main query: scan of one table, cross join with the WITH query,
    // then the filter that compares column iCol of the WITH query
    static const std::regex xMain(
        "      scan rapids public ([A-Za-z0-9_]+)\n    \\| stageMarker[0-9]+ broadcast\n"
        "  stage stageMarker[0-9]+ on all partitions :\n"
        "    call \"([A-Za-z0-9_]+) \"\n    \\| abstractJoin \"CROSS true  \"\n");
    std::smatch mm;
    std::string sMain = sStmt.substr(iCteEnd);
    std::string sMainNorm = std::regex_replace(sMain, std::regex("\n    scan rapids"), "\n      scan rapids");
    if (! std::regex_search(sMainNorm, mm, xMain) || mm[2].str() != sCte)
      return false;
    i8 nLeft = nColumns(mm[1].str());
    if (nLeft <= 0)
      return false;
    std::string sRef = "$" + std::to_string(nLeft + iCol) + " " + sSqy + "  ==  and  ";
    size_t iRef = sStmt.find(sRef);
    if (iRef == std::string::npos || sStmt.find(sSqy, iRef + sRef.size()) != std::string::npos)
      return false;
    // the WITH query is called by the sub-query and the cross join only
    i8 nCalls = 0;
    for (size_t p = sStmt.find("call \"" + sCte + " \""); p != std::string::npos; p = sStmt.find("call \"" + sCte + " \"", p + 1))
      nCalls++;
    if (nCalls != 2)
      return false;

    sStmt.erase(iRef, sRef.size());
    sStmt.erase(iSqy, nSqy);
    sTags = sFn + ".0.eq.1.1";
    return true;
  }

}

/*----------------------------------------------------------------------------*/
/* sqlCompiler::transliterate -- plan to pipeline transliterate               */
/*----------------------------------------------------------------------------*/
void Step::vSqlCompilerTransliterate(void)
{
  SqlStepTimer oSqlStepTimer(__func__);
  // the step reads the plan and does not change it: its column lists are
  // built once per node and shared (PlanObject::ColumnExprsScope)
  Parser::PlanObject::ColumnExprsScope oColumnExprsScope;
  ENTRY();

  s256 sPipelineName;

  //vTrace("\nStep::vSqlCompilerTransliterate\n"_Stz);

  PlanOperand* pPlanOperand = _pPlanOperand(m_oDataStack.pPopPtr(__func__));

  // get the pipeline name
  m_oDataStack.vPopStr(sPipelineName, __func__);

  // a statement from the cache: its pipeline statement as it was made
  if (pPlanOperand->azCachedPipeline()) {
    s48K sCachedStmt;
    sCachedStmt.vSprint("%s"_Stz, pPlanOperand->azCachedPipeline());
    m_oDataStack.vPushStr(sCachedStmt);
    vPhaseMark("sqlcompile_end");
    EXIT();
    return;
  }

  Parser::RootNode* pPlanRoot = Parser::_pRootNode(pPlanOperand->pPlanNode());
  ASSERT(ShadowRoot::bIsExactType(pPlanRoot));

  // Walk the graph and produce a pipeline statement.
  //bool bTransliterate = Dynamo::bPlanToPipeline(sPipelineStmt,pPlanRoot);

  s48K sPipelineStmt;

  Backend* pBackend = new Backend(pPlanRoot);
  bool bTransliterate = pBackend->bTransliterate(sPipelineName, sPipelineStmt);
  ASSERT(bTransliterate);
  UNUSED(bTransliterate);

  delete pBackend;

  if (! AM::JOB::bHasTag("<optimiser_off_having_groups>"_Stz) &&
      ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz)) {
    std::string sStmt(sPipelineStmt.azChar());
    std::string sHaving;
    auto nColumns = [pPlanRoot](const std::string& sTable) -> i8 {
      s256 sTbl;
      sTbl.vSprint("%s"_Stz, sTable.c_str());
      Parser::QueryNode* pMetadataNode = Parser::ShadowRoot::pMetadataNode(pPlanRoot);
      if (! pMetadataNode)
        return 0;
      Parser::PlanNodeIter* pIter = Parser::MetadataNode::pColumnDefIter(pMetadataNode,
          "rapids"_Stz, "public"_Stz, (Stz*) sTbl.azChar());
      if (! pIter)
        return 0;
      i8 n = 0;
      while (pIter->bHasNext()) {
        pIter->pNext();
        n++;
      }
      delete pIter;
      return n;
    };
    if (bRewriteHavingGroups(sStmt, sHaving) || bRewriteHavingCte(sStmt, sHaving, nColumns)) {
      size_t iBegin = sStmt.find("\nbegin\n");
      if (iBegin != std::string::npos) {
        sStmt.insert(iBegin + 1, "  tag <havingGroups> = " + sHaving + " endTag\n"
                                 "  tag <optimiser_off_local_gbyagg> = 1 endTag\n");
        sPipelineStmt.vSprint("%s"_Stz, sStmt.c_str());
      }
    }
  }

  // job tag /show_pipeline: trace the pipeline statement the SQL became
  if (AM::JOB::bHasTag("<show_pipeline>"_Stz))
    vTrace("Transliterated pipeline:\n%s\nEnd of transliterated pipeline\n"_Stz, sPipelineStmt.azChar());

  // statement cache: keep the pipeline statement of a cacheable SELECT
  if (pPlanOperand->azCacheKey()) {
    std::lock_guard<std::mutex> oGuard(g_oSqlCacheLock);
    if (g_oSqlCache.size() >= 256)
      g_oSqlCache.clear();
    g_oSqlCache[pPlanOperand->azCacheKey()] = SqlCacheItem{std::string(sPipelineStmt.azChar()), pPlanOperand->iCacheEpoch()};
  }

  // return the pipeline
  m_oDataStack.vPushStr(sPipelineStmt);

  vPhaseMark("sqlcompile_end");

  EXIT();
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
