/*============================================================================*/
/* CorrSemiJoin.cpp - outer key restrictions copied into correlated sub-queries */
/*============================================================================*/
//
//   select ps_suppkey from partsupp
//    where ps_partkey in (select p_partkey from part where p_name like 'forest%')
//      and ps_availqty > (select 0.5 * sum(l_quantity) from lineitem
//                          where l_partkey = ps_partkey and l_suppkey = ps_suppkey
//                            and l_shipdate >= ... )                   (TPC-H Q20)
//
// The run-time optimiser computes a correlated sub-query once for every value
// of its correlation key (a GROUP BY over the whole inner table) and joins the
// result with the outer query. DuckDB first takes the distinct keys of the
// outer rows (its "delim" join) and computes the sub-query for those keys only.
// The same is done here in SQL: when the sub-query is an operand of a
// comparison in the AND of a filter and is correlated by "inner = outer" in the
// AND of its WHERE, "inner IN (K)" is added to that WHERE, with K a set of keys
// that contains the outer keys the filter can keep:
//
//   1. the AND has "outer IN (S)", S not correlated: K is S (Q20);
//   2. otherwise the AND has conditions on the outer column's table alone,
//      compared with constants: K is the keys of that table that pass them
//      (Q17: p_brand, p_container; Q2: p_size, p_type).
//
// K is written as "(select k.c from <table> k where ... group by k.c)" when S
// (or the table) is one table with such conditions: the optimiser folds an IN
// with GROUP BY as a stream of distinct keys that drives an index join into the
// inner table (as Q18's IN), so the sub-query reads only the rows of those
// keys. The table gets an alias of its own (csj_kN), apart from other scans of
// it in the statement.
//
//      ... where (l_partkey = p_partkey) and lineitem.l_partkey in
//          (select csj_k1.p_partkey from rapids.public.part csj_k1
//            where csj_k1.p_brand = 'Brand#23' and csj_k1.p_container = 'MED BOX'
//            group by csj_k1.p_partkey)                                 (Q17)
//
// For an outer row that the filter keeps, outer is in K, and the inner rows
// that the sub-query uses have inner = outer, so inner IN (K) is TRUE for them:
// the sub-query has the same value. Any other outer row is rejected by the
// filter whatever the sub-query returns. The rows of the statement do not
// change.
//
// The change is made to the text of the statement, which is then planned
// again: the plan of a sub-query is made from its text, and the text of every
// sub-query is a piece of the statement (SelectMarker queryText). Names are
// resolved on a trial plan to find the columns.
//
// Job tag /optimiser_off_corr_semijoin leaves the statement as it was.
//
#include "BASE.h"
#include "CorrSemiJoin.hpp"
#include "PlanNode.hpp"
#include "QueryNode.hpp"
#include "GraphNav.hpp"
#include "SqlPlanner.hpp"
#include "NameValidator.hpp"
#include "SqlException.hpp"
#include "FilterOp.hpp"
#include "ProjectOp.hpp"
#include "AggregateOp.hpp"
#include "SelectMarker.hpp"
#include "MainSubquery.hpp"
#include "ExprSubquery.hpp"
#include "SubqueryMarker.hpp"
#include "SubqueryRefExpr.hpp"
#include "Expr.hpp"
#include "ExprList.hpp"
#include "AndExpr.hpp"
#include "EqExpr.hpp"
#include "NeExpr.hpp"
#include "LtExpr.hpp"
#include "LeExpr.hpp"
#include "GtExpr.hpp"
#include "GeExpr.hpp"
#include "InExpr.hpp"
#include "LikeExpr.hpp"
#include "BetweenExpr.hpp"
#include "LiteralExpr.hpp"
#include "ProjectedColumn.hpp"
#include "ColumnRef.hpp"
#include "TableScanOp.hpp"
#include "TableRef.hpp"
#include "DataSource.hpp"
#include <set>
#include <strings.h>
#include <cctype>

using namespace Parser;

static const i8 C_MAX_REWRITES = 4;

/*----------------------------------------------------------------------------*/
/* entry                                                                      */
/*----------------------------------------------------------------------------*/
b1 CorrSemiJoin::bRewrite(const std::string& sSql, const PlanNode* pGraphTemplate, std::string& sOut)
{
  // a statement and a correlated sub-query: at least two SELECTs
  std::string sLower(sSql);
  for (auto& c : sLower)
    c = (char)tolower((unsigned char)c);
  i8 nSelect = 0;
  for (size_t i = sLower.find("select"); i != std::string::npos; i = sLower.find("select", i + 1))
    nSelect++;
  if (nSelect < 2)
    return false;

  std::string sCurrent(sSql);
  b1 bChanged = false;
  for (i8 i = 0; i < C_MAX_REWRITES; i++) {
    std::string sNext;
    if (!bRewriteOnce(sCurrent, pGraphTemplate, sNext))
      break;
    sCurrent = sNext;
    bChanged = true;
  }
  if (bChanged)
    sOut = sCurrent;
  return bChanged;
}

b1 CorrSemiJoin::bRewriteOnce(const std::string& sSql, const PlanNode* pGraphTemplate, std::string& sOut)
{
  SqlPlanner* pPlanner = new SqlPlanner();
  PlanNode* pPlanRoot = nullptr;
  b1 bFound = false;
  try {
    pPlanRoot = pPlanner->plan((Stz*)sSql.c_str(), pGraphTemplate);
    if (pPlanRoot) {
      NameValidator::vValidateNames(pPlanRoot);
      bFound = bFindInPlan(pPlanRoot, sSql, sOut);
    }
  } catch (...) {
    // the original statement is planned again and reports its own errors
    bFound = false;
  }
  delete pPlanRoot;
  delete pPlanner;
  return bFound;
}

/*----------------------------------------------------------------------------*/
/* the first filter with something to add                                     */
/*----------------------------------------------------------------------------*/
b1 CorrSemiJoin::bFindInPlan(PlanNode* pRootNode, const std::string& sSql, std::string& sOut)
{
  b1 bFound = false;
  auto fVisitor = [&](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (!pNode->szType())
      return GraphNav::WALK_CONTINUE;
    if (FilterOp::bIsOfType(pNode) && bTryFilter(_pQueryNode(pNode), pRootNode, sSql, sOut)) {
      bFound = true;
      return GraphNav::WALK_ABORT;
    }
    return GraphNav::WALK_CONTINUE;
  };
  GraphNav::bGlobalWalk(pRootNode, false, true, fVisitor, nullptr);
  return bFound;
}

void CorrSemiJoin::vFlattenAnd(QueryNode* pExpr, QueryNodeList* pList)
{
  if (AndExpr::bIsOfType(pExpr)) {
    PlanNodeIter* pIter = pExpr->pNewChildrenIter();
    while (pIter->bHasNext())
      vFlattenAnd(_pQueryNode(pIter->pNext()), pList);
    delete pIter;
    return;
  }
  pList->vAppend(pExpr);
}

static b1 bIsComparison(QueryNode* pExpr)
{
  return EqExpr::bIsOfType(pExpr) || NeExpr::bIsOfType(pExpr) ||
         LtExpr::bIsOfType(pExpr) || LeExpr::bIsOfType(pExpr) ||
         GtExpr::bIsOfType(pExpr) || GeExpr::bIsOfType(pExpr);
}

// the child of the node of the given type
static QueryNode* pChildOfType(QueryNode* pNode, b1 (*fbIsOfType)(PlanNode*))
{
  QueryNode* pFound = nullptr;
  PlanNodeIter* pIter = pNode->pNewChildrenIter();
  while (pIter->bHasNext() && !pFound) {
    QueryNode* pChild = _pQueryNode(pIter->pNext());
    if (fbIsOfType(pChild))
      pFound = pChild;
  }
  delete pIter;
  return pFound;
}

static std::string sLowered(Stz* sz)
{
  std::string s(sz ? sz->azChar() : "");
  for (auto& c : s)
    c = (char)tolower((unsigned char)c);
  return s;
}

// the name by which a column's table is known in the statement: its alias,
// or the name of the table
static Stz* szTableOf(QueryNode* pColumnRef)
{
  Stz* sz = ColumnRef::szTableAlias(pColumnRef);
  if (sz && sz->nLength() > 0)
    return sz;
  return ColumnRef::szTable(pColumnRef);
}

// the aliases of the tables that a SELECT scans itself (not its sub-queries)
static void vInnerAliases(QueryNode* pNode, std::set<std::string>& xAliases)
{
  PlanNodeIter* pIter = pNode->pNewChildrenIter();
  while (pIter->bHasNext()) {
    QueryNode* pChild = _pQueryNode(pIter->pNext());
    if (!pChild->szType() || SubqueryMarker::bIsOfType(pChild))
      continue;
    if (TableScanOp::bIsOfType(pChild)) {
      Stz* szAlias = DataSource::szAliasName(pChild);
      if (szAlias && szAlias->nLength() > 0)
        xAliases.insert(sLowered(szAlias));
      else
        xAliases.insert(sLowered(TableRef::szTable(TableScanOp::pTableRef(pChild))));
      continue;
    }
    vInnerAliases(pChild, xAliases);
  }
  delete pIter;
}

// true when the term is a condition on columns of one table only, compared
// with constants: =, <>, <, <=, >, >=, LIKE, IN (list) or BETWEEN
static b1 bOnlyColumnsOf(QueryNode* pNode, QueryNode* pColumnOfTable, b1& bHasColumn)
{
  if (SubqueryRefExpr::bIsOfType(pNode))
    return false;
  if (ColumnRef::bIsOfType(pNode)) {
    bHasColumn = true;
    return sLowered(szTableOf(pNode)) == sLowered(szTableOf(pColumnOfTable)) &&
           sLowered(ColumnRef::szTable(pNode)) == sLowered(ColumnRef::szTable(pColumnOfTable));
  }
  PlanNodeIter* pIter = pNode->pNewChildrenIter();
  b1 bOk = true;
  while (pIter->bHasNext() && bOk) {
    QueryNode* pChild = _pQueryNode(pIter->pNext());
    if (!pChild->szType())
      continue;
    if (LiteralExpr::bIsOfType(pChild))
      continue;
    if (ColumnRef::bIsOfType(pChild))
      bOk = bOnlyColumnsOf(pChild, pColumnOfTable, bHasColumn);
    else
      bOk = false;
  }
  delete pIter;
  return bOk;
}

static b1 bTableCondition(QueryNode* pTerm, QueryNode* pColumnOfTable)
{
  if (!(EqExpr::bIsOfType(pTerm) || NeExpr::bIsOfType(pTerm) || LtExpr::bIsOfType(pTerm) ||
        LeExpr::bIsOfType(pTerm) || GtExpr::bIsOfType(pTerm) || GeExpr::bIsOfType(pTerm) ||
        LikeExpr::bIsOfType(pTerm) || InExpr::bIsOfType(pTerm) || BetweenExpr::bIsOfType(pTerm)))
    return false;
  b1 bHasColumn = false;
  return bOnlyColumnsOf(pTerm, pColumnOfTable, bHasColumn) && bHasColumn;
}


// "(select K.c from <catalog.schema.table> K where <conditions> group by K.c)":
// the distinct keys of a table that pass its conditions. The table gets an
// alias of its own, so that it is not the same table as another scan of it in
// the statement. pKeyCol is a column of the table; the conditions are columns
// of the table compared with constants (bTableCondition).
static i8 giKeySetAlias = 0;

static b1 bKeySet(QueryNode* pKeyCol, QueryNodeList* pConditions, PlanNode* pRootNode, std::string& sOut)
{
  Stz* szCatalog = ColumnRef::szCatalog(pKeyCol);
  Stz* szSchema = ColumnRef::szSchema(pKeyCol);
  Stz* szTable = ColumnRef::szTable(pKeyCol);
  Stz* szColumn = ColumnRef::szColumn(pKeyCol);
  if (!szCatalog || !szSchema || !szTable || !szColumn)
    return false;
  std::string sTable = std::string(szCatalog->azChar()) + "." + szSchema->azChar() + "." + szTable->azChar();
  std::string sAlias = "csj_k" + std::to_string(++giKeySetAlias);
  std::string sPrefix = sTable + ".";

  std::string sConditions;
  for (i8 k = 0; k < pConditions->nItems(); k++) {
    s2K sDescr;
    Expr::vDescribe(pConditions->pGet(k), sDescr, pRootNode);
    std::string sText(sDescr.szStz()->azChar());
    if (sText.find("''") != std::string::npos)
      return false;
    for (size_t i = sText.find(sPrefix); i != std::string::npos; i = sText.find(sPrefix, i + sAlias.size() + 1))
      sText.replace(i, sPrefix.size(), sAlias + ".");
    if (sText.find(sTable) != std::string::npos)
      return false;
    sConditions += (sConditions.empty() ? "" : " and ") + sText;
  }

  std::string sKey = sAlias + "." + szColumn->azChar();
  sOut = "(select " + sKey + " from " + sTable + " " + sAlias +
         (sConditions.empty() ? std::string() : " where " + sConditions) + " group by " + sKey + ")";
  return true;
}

// the one projected column of a sub-query that is one SELECT, or nullptr
static QueryNode* pProjectedColumnOf(QueryNode* pExprSubquery);

// the WHERE of a sub-query that is one SELECT: exprSubquery/mainSubquery/
// selectMarker/(projectOp|aggregateOp)/filterOp
QueryNode* CorrSemiJoin::pWhereOf(QueryNode* pExprSubquery)
{
  QueryNode* pMain = pChildOfType(pExprSubquery, &MainSubquery::bIsOfType);
  if (!pMain)
    return nullptr;
  QueryNode* pSelect = pChildOfType(pMain, &SelectMarker::bIsOfType);
  if (!pSelect)
    return nullptr;
  QueryNode* pOp = pChildOfType(pSelect, &ProjectOp::bIsOfType);
  if (!pOp)
    pOp = pChildOfType(pSelect, &AggregateOp::bIsOfType);
  if (!pOp)
    return nullptr;
  return pChildOfType(pOp, &FilterOp::bIsOfType);
}

/*----------------------------------------------------------------------------*/
/* one filter                                                                 */
/*----------------------------------------------------------------------------*/
b1 CorrSemiJoin::bTryFilter(QueryNode* pFilterOp, PlanNode* pRootNode, const std::string& sSql, std::string& sOut)
{
  QueryNodeList* pTerms = new QueryNodeList();
  vFlattenAnd(FilterOp::pFilterExpr(pFilterOp), pTerms);
  b1 bDone = false;

  for (i8 iTerm = 0; iTerm < pTerms->nItems() && !bDone; iTerm++) {
    QueryNode* pTerm = pTerms->pGet(iTerm);
    if (!bIsComparison(pTerm))
      continue;

    // a correlated sub-query that is an operand of the comparison
    QueryNode* pRef = pChildOfType(pTerm, &SubqueryRefExpr::bIsOfType);
    if (!pRef)
      continue;
    QueryNode* pSubquery = SubqueryRefExpr::pSubquery(pRef, pRootNode);
    if (!pSubquery || !ExprSubquery::bIsOfType(pSubquery))
      continue;
    QueryNode* pWhere = pWhereOf(pSubquery);
    if (!pWhere)
      continue;
    Stz* szSubqueryText = SubqueryMarker::szSqlText(pSubquery);
    if (!szSubqueryText)
      continue;
    std::set<std::string> xInnerAliases;
    {
      QueryNode* pMain = pChildOfType(pSubquery, &MainSubquery::bIsOfType);
      QueryNode* pSelect = pMain ? pChildOfType(pMain, &SelectMarker::bIsOfType) : nullptr;
      if (pSelect)
        vInnerAliases(pSelect, xInnerAliases);
    }
    if (xInnerAliases.empty())
      continue;

    QueryNodeList* pInner = new QueryNodeList();
    vFlattenAnd(FilterOp::pFilterExpr(pWhere), pInner);

    for (i8 iConj = 0; iConj < pInner->nItems() && !bDone; iConj++) {
      QueryNode* pEq = pInner->pGet(iConj);
      if (!EqExpr::bIsOfType(pEq))
        continue;
      QueryNode* p1 = EqExpr::pChild1(pEq);
      QueryNode* p2 = EqExpr::pChild2(pEq);
      if (!ColumnRef::bIsOfType(p1) || !ColumnRef::bIsOfType(p2))
        continue;

      // which side is the outer column: the one whose table the sub-query
      // does not scan
      QueryNode* pOuter = nullptr;
      QueryNode* pInnerCol = nullptr;
      b1 bInner1 = xInnerAliases.count(sLowered(szTableOf(p1))) > 0;
      b1 bInner2 = xInnerAliases.count(sLowered(szTableOf(p2))) > 0;
      if (bInner1 && !bInner2) {
        pOuter = p2;
        pInnerCol = p1;
      } else if (bInner2 && !bInner1) {
        pOuter = p1;
        pInnerCol = p2;
      }
      if (!pOuter)
        continue;

      // nothing to do when the WHERE already restricts the inner column with IN
      b1 bHasIn = false;
      for (i8 k = 0; k < pInner->nItems() && !bHasIn; k++)
        bHasIn = InExpr::bIsOfType(pInner->pGet(k)) &&
                 Expr::bIsSymbolicallyEqual(InExpr::pLeftExpr(pInner->pGet(k)), pInnerCol);
      if (bHasIn)
        continue;

      Stz* szAlias = szTableOf(pInnerCol);
      Stz* szColumn = ColumnRef::szColumn(pInnerCol);
      if (!szAlias || !szColumn || szAlias->nLength() == 0 || szColumn->nLength() == 0)
        continue;
      std::string sInner = std::string(szAlias->azChar()) + "." + szColumn->azChar();

      // 1. "outer IN (S)", S not correlated, in the outer AND: the keys are S
      for (i8 k = 0; k < pTerms->nItems() && !bDone; k++) {
        QueryNode* pIn = pTerms->pGet(k);
        if (!InExpr::bIsOfType(pIn) || !Expr::bIsSymbolicallyEqual(InExpr::pLeftExpr(pIn), pOuter))
          continue;
        QueryNode* pRefS = pChildOfType(pIn, &SubqueryRefExpr::bIsOfType);
        if (!pRefS)
          continue;
        QueryNode* pS = SubqueryRefExpr::pSubquery(pRefS, pRootNode);
        if (!pS || SubqueryMarker::bIsCorrelated(pS) || !SubqueryMarker::szSqlText(pS))
          continue;

        // S as a key set of its own (grouped, its table under an alias of its
        // own) when it is one table with conditions on constants; else S as
        // it is written
        std::string sKeys;
        QueryNode* pKeyCol = pProjectedColumnOf(pS);
        QueryNode* pWhereS = pWhereOf(pS);
        std::set<std::string> xTablesS;
        {
          QueryNode* pMainS = pChildOfType(pS, &MainSubquery::bIsOfType);
          QueryNode* pSelectS = pMainS ? pChildOfType(pMainS, &SelectMarker::bIsOfType) : nullptr;
          if (pSelectS)
            vInnerAliases(pSelectS, xTablesS);
        }
        if (pKeyCol && ColumnRef::bIsOfType(pKeyCol) && xTablesS.size() == 1) {
          QueryNodeList* pCondS = new QueryNodeList();
          b1 bAll = true;
          if (pWhereS) {
            vFlattenAnd(FilterOp::pFilterExpr(pWhereS), pCondS);
            for (i8 c = 0; c < pCondS->nItems() && bAll; c++)
              bAll = bTableCondition(pCondS->pGet(c), pKeyCol);
          }
          if (!bAll || !bKeySet(pKeyCol, pCondS, pRootNode, sKeys))
            sKeys.clear();
          pCondS->vRemoveAll();
          delete pCondS;
        }
        std::string sS(SubqueryMarker::szSqlText(pS)->azChar());
        std::string sCondition = sInner + " in " + (sKeys.empty() ? "(" + sS + ")" : sKeys);
        bDone = bSplice(sSql, szSubqueryText->azChar(), sCondition, sOut);
      }
      if (bDone)
        break;

      // 2. otherwise the keys of the outer table that pass its own conditions
      //    in the outer AND (DuckDB takes the keys of the whole outer query)
      QueryNodeList* pCondO = new QueryNodeList();
      for (i8 k = 0; k < pTerms->nItems(); k++) {
        QueryNode* pCond = pTerms->pGet(k);
        if (pCond != pTerm && bTableCondition(pCond, pOuter))
          pCondO->vAppend(pCond);
      }
      std::string sKeys;
      b1 bKeys = pCondO->nItems() > 0 && bKeySet(pOuter, pCondO, pRootNode, sKeys);
      pCondO->vRemoveAll();
      delete pCondO;
      if (!bKeys)
        continue;
      std::string sCondition = sInner + " in " + sKeys;
      bDone = bSplice(sSql, szSubqueryText->azChar(), sCondition, sOut);
    }
    pInner->vRemoveAll();
    delete pInner;
  }

  pTerms->vRemoveAll();
  delete pTerms;
  return bDone;
}

/*----------------------------------------------------------------------------*/
/* text                                                                       */
/*----------------------------------------------------------------------------*/

// position of keyword kw at parenthesis depth iTarget of s, from i0, outside strings
// and quoted names; npos when absent
static size_t nFindKeyword(const std::string& s, const char* kw, size_t i0, i8 iTarget)
{
  size_t n = strlen(kw);
  i8 iDepth = 0;
  for (size_t i = i0; i < s.size(); i++) {
    char c = s[i];
    if (c == '\'' || c == '"') {
      size_t j = s.find(c, i + 1);
      if (j == std::string::npos)
        return std::string::npos;
      i = j;
      continue;
    }
    if (c == '(') { iDepth++; continue; }
    if (c == ')') { iDepth--; continue; }
    if (iDepth != iTarget || i + n > s.size())
      continue;
    if (strncasecmp(s.c_str() + i, kw, n) != 0)
      continue;
    b1 bStart = (i == 0) || !(isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_');
    b1 bEnd = (i + n == s.size()) || !(isalnum((unsigned char)s[i + n]) || s[i + n] == '_');
    if (bStart && bEnd)
      return i;
  }
  return std::string::npos;
}

// sOut is sSql with "where C" of the sub-query text replaced by
// "where (C) and <condition>"
b1 CorrSemiJoin::bSplice(const std::string& sSql, const std::string& sSubquery,
                         const std::string& sCondition, std::string& sOut)
{
  if (sSubquery.find("--") != std::string::npos || sSubquery.find("/*") != std::string::npos)
    return false;
  size_t iAt = sSql.find(sSubquery);
  if (iAt == std::string::npos || sSql.find(sSubquery, iAt + 1) != std::string::npos)
    return false;

  // the text of a sub-query may include its parentheses
  size_t iFirst = sSubquery.find_first_not_of(" \t\r\n");
  size_t iLast = sSubquery.find_last_not_of(" \t\r\n");
  if (iFirst == std::string::npos)
    return false;
  i8 iTarget = 0;
  size_t iEnd = sSubquery.size();
  if (sSubquery[iFirst] == '(' && sSubquery[iLast] == ')') {
    iTarget = 1;
    iEnd = iLast;
  }

  size_t iWhere = nFindKeyword(sSubquery, "where", 0, iTarget);
  if (iWhere == std::string::npos || iWhere > iEnd)
    return false;
  size_t iFrom = iWhere + 5;
  for (const char* kw : {"group", "having", "order", "limit", "union", "intersect", "except",
                         "window", "fetch", "offset"}) {
    size_t i = nFindKeyword(sSubquery, kw, iFrom, iTarget);
    if (i < iEnd)
      iEnd = i;
  }

  std::string sNew = sSubquery.substr(0, iWhere) + "where (" + sSubquery.substr(iFrom, iEnd - iFrom) +
                     ") and " + sCondition + " " + sSubquery.substr(iEnd);
  sOut = sSql.substr(0, iAt) + sNew + sSql.substr(iAt + sSubquery.size());
  return true;
}


static QueryNode* pProjectedColumnOf(QueryNode* pExprSubquery)
{
  QueryNode* pMain = pChildOfType(pExprSubquery, &MainSubquery::bIsOfType);
  QueryNode* pSelect = pMain ? pChildOfType(pMain, &SelectMarker::bIsOfType) : nullptr;
  QueryNode* pProject = pSelect ? pChildOfType(pSelect, &ProjectOp::bIsOfType) : nullptr;
  if (!pProject)
    return nullptr;
  QueryNode* pFound = nullptr;
  i8 nFound = 0;
  auto fVisitor = [&](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (pNode->szType() && ProjectedColumn::bIsOfType(pNode)) {
      nFound++;
      pFound = ProjectedColumn::pExprNode(_pQueryNode(pNode));
    }
    return GraphNav::WALK_CONTINUE;
  };
  GraphNav::bGlobalWalk(pProject, true, true, fVisitor, nullptr);
  return nFound == 1 ? pFound : nullptr;
}

