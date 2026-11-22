/*============================================================================*/
/* CorrSemiJoin.hpp - outer IN restrictions copied into correlated sub-queries */
/*============================================================================*/
#pragma once

#include "PlanNode.hpp"
#include "QueryNodeList.hpp"
#include <string>

namespace Parser
{
  class QueryNode;

  class CorrSemiJoin
  {
  private:
    CorrSemiJoin();

  public:
    // When the statement has a sub-query correlated by "inner = outer" in a
    // comparison of the AND of a filter, and the same AND has "outer IN
    // (sub-query S)" with S not correlated, returns in sOut the statement
    // with "inner IN (S)" added to the WHERE of the correlated sub-query.
    // Returns false when there is nothing to add (sOut is then undefined).
    static b1 bRewrite(const std::string& sSql, const PlanNode* pGraphTemplate, std::string& sOut);

  private:
    static b1 bRewriteOnce(const std::string& sSql, const PlanNode* pGraphTemplate, std::string& sOut);
    static b1 bFindInPlan(PlanNode* pRootNode, const std::string& sSql, std::string& sOut);
    static b1 bTryFilter(QueryNode* pFilterOp, PlanNode* pRootNode, const std::string& sSql, std::string& sOut);
    static QueryNode* pWhereOf(QueryNode* pExprSubquery);
    static void vFlattenAnd(QueryNode* pExpr, QueryNodeList* pList);
    static b1 bSplice(const std::string& sSql, const std::string& sSubquery,
                      const std::string& sCondition, std::string& sOut);
  };
}
