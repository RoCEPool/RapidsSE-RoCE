/*============================================================================*/
/* OrFactor.hpp - conditions implied by an OR, added to the filter            */
/*============================================================================*/
#pragma once

#include "PlanNode.hpp"
#include "QueryNodeList.hpp"

namespace Parser
{
  class QueryNode;

  class OrFactor
  {
  private:
    OrFactor();

  public:
    // For every filter (WHERE, HAVING, ON) of the plan: when a member of its AND
    // is an OR, the conditions that every branch of the OR implies are added to
    // the AND, so that they can be applied to a table before the joins.
    static void vOrFactor(PlanNode* pRootNode);

  private:
    static void vFactorFilter(QueryNode* pFilterOp, PlanNode* pRootNode);
    static void vFlatten(QueryNode* pExpr, b1 bAnd, QueryNodeList* pList);
    static b1   bUsable(QueryNode* pExpr);
    static b1   bContains(QueryNodeList* pList, QueryNode* pExpr);
    static b1   bValueSet(QueryNode* pConj, QueryNode** ppColumn, QueryNodeList* pValues);
  };
}
