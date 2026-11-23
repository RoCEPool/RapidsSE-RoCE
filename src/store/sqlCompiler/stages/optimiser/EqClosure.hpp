/*============================================================================*/
/* EqClosure.hpp - join predicates implied by equal columns                   */
/*============================================================================*/
#pragma once

#include "PlanNode.hpp"
#include "QueryNodeList.hpp"

namespace Parser
{
  class QueryNode;

  class EqClosure
  {
  private:
    EqClosure();

  public:
    // For every filter of the plan: when its AND makes columns of three or
    // more tables equal (a = b and b = c), the equalities between the other
    // pairs of those tables (a = c) are added to the AND, so that the join
    // order search can join the tables in any order of the chain.
    static void vEqClosure(PlanNode* pRootNode);

  private:
    static void vCloseFilter(QueryNode* pFilterOp);
  };
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
