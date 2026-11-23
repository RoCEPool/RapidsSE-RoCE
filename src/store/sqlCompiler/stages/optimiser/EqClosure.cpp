/*============================================================================*/
/* EqClosure.cpp - join predicates implied by equal columns                   */
/*============================================================================*/
//
//   where c_nationkey = s_nationkey
//     and s_nationkey = n_nationkey
//     and n_regionkey = r_regionkey and r_name = 'ASIA' ...      (TPC-H Q5)
//
// The run-time optimiser joins a table to the stream only through a join
// predicate that names it. Here customer is joined to nation only through
// supplier, so no join order can start from the selective region and nation
// and reach customer and orders before lineitem: every order reads lineitem
// for the orders of all regions. The two equalities imply c_nationkey =
// n_nationkey. DuckDB derives these from equivalence sets of columns (its
// filter combiner and the equivalence sets of the join order optimiser) and so
// may join nation to customer directly.
//
// For each filter, the members of its AND that make two columns of two tables
// of this query equal are grouped into sets of equal columns. For every pair
// of tables of a set that no member joins yet, the equality of their first
// columns in the set is added to the AND. A row passes the filter only when
// all members are TRUE, and then the added equalities are TRUE too: they
// remove no row. Columns of an enclosing query (correlation) are left out.
//
// Job tag /optimiser_off_eq_closure leaves the plan as it was.
//
#include "BASE.h"
#include "EqClosure.hpp"
#include "PlanNode.hpp"
#include "QueryNode.hpp"
#include "GraphNav.hpp"
#include "ShadowRoot.hpp"
#include "FilterOp.hpp"
#include "Expr.hpp"
#include "AndExpr.hpp"
#include "EqExpr.hpp"
#include "ColumnRef.hpp"
#include "DataSource.hpp"
#include "TableScanOp.hpp"
#include "TableRef.hpp"
#include "AM.hpp"

#include <string>
#include <vector>
#include <strings.h>

using namespace Parser;

namespace
{
  std::string sLower(Stz* sz)
  {
    std::string s(sz ? sz->azChar() : "");
    for (char& c : s)
      c = (char) tolower((unsigned char) c);
    return s;
  }

  // the members of the AND, nested ANDs included
  void vFlattenAnd(QueryNode* pExpr, std::vector<QueryNode*>& rList)
  {
    if (AndExpr::bIsOfType(pExpr)) {
      PlanNodeIter* pIter = pExpr->pNewChildrenIter();
      while (pIter->bHasNext())
        vFlattenAnd(_pQueryNode(pIter->pNext()), rList);
      delete pIter;
      return;
    }
    rList.push_back(pExpr);
  }

  struct EqCol
  {
    std::string sTable;   // alias, or table name
    std::string sKey;     // alias.column
    QueryNode*  pColumn;  // a ColumnRef of the filter (not owned)
    int         iSet;
  };

  int iFind(std::vector<int>& rParent, int i)
  {
    while (rParent[i] != i)
      i = rParent[i] = rParent[rParent[i]];
    return i;
  }
}

/*----------------------------------------------------------------------------*/
/* all filters of the plan                                                    */
/*----------------------------------------------------------------------------*/
void EqClosure::vEqClosure(PlanNode* pRootNode)
{
  if (AM::JOB::bHasTag("<optimiser_off_eq_closure>"_Stz) ||
      AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return;

  QueryNode* pPlanNode = _pQueryNode(ShadowRoot::pPlanRoot(pRootNode));
  ASSERT(pPlanNode);

  auto fVisitor = [](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (FilterOp::bIsOfType(pNode))
      vCloseFilter(_pQueryNode(pNode));
    return GraphNav::WALK_CONTINUE;
  };

  GraphNav::bGlobalWalkBottomUp(pPlanNode, false, true, fVisitor, nullptr);
}

/*----------------------------------------------------------------------------*/
/* one filter                                                                 */
/*----------------------------------------------------------------------------*/
void EqClosure::vCloseFilter(QueryNode* pFilterOp)
{
  // the tables this filter reads (not those of a sub-query, nor of an
  // enclosing query)
  std::vector<std::string> xTables;
  auto fTables = [&xTables](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (TableScanOp::bIsOfType(pNode)) {
      QueryNode* pScan = _pQueryNode(pNode);
      Stz* szAlias = DataSource::szAliasName(pScan);
      if (szAlias)
        xTables.push_back(sLower(szAlias));
      QueryNode* pTableRef = TableScanOp::pTableRef(pScan);
      if (pTableRef && TableRef::szTable(pTableRef))
        xTables.push_back(sLower(TableRef::szTable(pTableRef)));
    }
    return GraphNav::WALK_CONTINUE;
  };
  GraphNav::bBoundedWalk(pFilterOp, false, true, fTables, nullptr);
  if (xTables.size() < 3)
    return;
  auto bOwnTable = [&xTables](const std::string& s) {
    for (const std::string& t : xTables)
      if (t == s)
        return true;
    return false;
  };

  QueryNode* pExpr = FilterOp::pFilterExpr(pFilterOp);
  std::vector<QueryNode*> xTerms;
  vFlattenAnd(pExpr, xTerms);

  // the columns made equal by "column = column" of two tables of this query
  std::vector<EqCol> xCols;
  std::vector<std::pair<int, int>> xEdges;
  auto iCol = [&xCols](QueryNode* pColumn, const std::string& sTable) {
    std::string sKey = sTable + "." + sLower(ColumnRef::szColumn(pColumn));
    for (int i = 0; i < (int) xCols.size(); i++)
      if (xCols[i].sKey == sKey)
        return i;
    xCols.push_back({sTable, sKey, pColumn, 0});
    return (int) xCols.size() - 1;
  };
  for (QueryNode* pTerm : xTerms) {
    if (!EqExpr::bIsExactType(pTerm))
      continue;
    QueryNode* p1 = EqExpr::pChild1(pTerm);
    QueryNode* p2 = EqExpr::pChild2(pTerm);
    if (!ColumnRef::bIsExactType(p1) || !ColumnRef::bIsExactType(p2))
      continue;
    // the alias of the table, or its name when it has none
    Stz* szT1 = ColumnRef::szTableAlias(p1) ? ColumnRef::szTableAlias(p1) : ColumnRef::szTable(p1);
    Stz* szT2 = ColumnRef::szTableAlias(p2) ? ColumnRef::szTableAlias(p2) : ColumnRef::szTable(p2);
    if (!szT1 || !szT2 || !ColumnRef::szColumn(p1) || !ColumnRef::szColumn(p2))
      continue;
    std::string s1 = sLower(szT1), s2 = sLower(szT2);
    if (s1 == s2 || !bOwnTable(s1) || !bOwnTable(s2))
      continue;
    xEdges.push_back({iCol(p1, s1), iCol(p2, s2)});
  }
  if (xEdges.size() < 2)
    return;

  // sets of equal columns
  std::vector<int> xParent(xCols.size());
  for (int i = 0; i < (int) xCols.size(); i++)
    xParent[i] = i;
  for (const auto& e : xEdges)
    xParent[iFind(xParent, e.first)] = iFind(xParent, e.second);
  for (int i = 0; i < (int) xCols.size(); i++)
    xCols[i].iSet = iFind(xParent, i);

  // the pairs of tables that a member already joins, per set
  auto bJoined = [&](int iSet, const std::string& a, const std::string& b) {
    for (const auto& e : xEdges) {
      const EqCol& c1 = xCols[e.first];
      const EqCol& c2 = xCols[e.second];
      if (c1.iSet != iSet)
        continue;
      if ((c1.sTable == a && c2.sTable == b) || (c1.sTable == b && c2.sTable == a))
        return true;
    }
    return false;
  };

  std::vector<QueryNode*> xAdded;
  std::vector<std::pair<std::string, std::string>> xAddedPairs;
  for (int i = 0; i < (int) xCols.size(); i++) {
    for (int j = i + 1; j < (int) xCols.size(); j++) {
      const EqCol& a = xCols[i];
      const EqCol& b = xCols[j];
      if (a.iSet != b.iSet || a.sTable == b.sTable || bJoined(a.iSet, a.sTable, b.sTable))
        continue;
      bool bDone = false;
      for (const auto& p : xAddedPairs)
        if ((p.first == a.sTable && p.second == b.sTable) || (p.first == b.sTable && p.second == a.sTable))
          bDone = true;
      if (bDone)
        continue;
      xAdded.push_back(EqExpr::pCreate(a.pColumn->pDeepCopy(true), b.pColumn->pDeepCopy(true)));
      xAddedPairs.push_back({a.sTable, b.sTable});
    }
  }

  if (!xAdded.empty()) {
    // the filter becomes: <filter> AND <added 1> AND <added 2> ...
    QueryNode* pNew = pExpr->pDeepCopy(true);
    for (QueryNode* pAdd : xAdded)
      pNew = AndExpr::pCreate(pNew, pAdd);
    pExpr->vTransmogrifyTo(pNew, true);
    delete pNew;
  }
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
