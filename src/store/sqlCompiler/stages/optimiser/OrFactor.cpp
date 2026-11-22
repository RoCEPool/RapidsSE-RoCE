/*============================================================================*/
/* OrFactor.cpp - conditions implied by an OR, added to the filter            */
/*============================================================================*/
//
//   where (p_partkey = l_partkey and p_brand = 'Brand#12' and l_shipmode in ('AIR', 'AIR REG') ...)
//      or (p_partkey = l_partkey and p_brand = 'Brand#23' and l_shipmode in ('AIR', 'AIR REG') ...)
//      or ...                                                       (TPC-H Q19)
//
//   where (n1.n_name = 'FRANCE' and n2.n_name = 'GERMANY')
//      or (n1.n_name = 'GERMANY' and n2.n_name = 'FRANCE')          (TPC-H Q7)
//
// The run-time optimiser moves a condition to the table it is about only when it
// is a member of the AND of the filter; an OR stays where it is, after all the
// joins. Every branch of these ORs implies conditions on one table:
//
//   1. a condition that is a member of every branch:
//        p_partkey = l_partkey, l_shipmode in ('AIR', 'AIR REG'), ...
//   2. for a column that every branch compares with constants (= or IN):
//        n1.n_name in ('FRANCE', 'GERMANY'), p_brand in ('Brand#12', 'Brand#23', 'Brand#34')
//
// They are added to the AND of the filter; the OR itself stays. A row passes a
// filter only when it evaluates to TRUE, and then the OR is TRUE, so one of its
// branches is TRUE, so every condition that all branches contain is TRUE, and the
// column is equal to one of the constants: the added conditions remove no row that
// the filter would keep. Members that contain a sub-query are left alone.
//
// Job tag /optimiser_off_or_factor leaves the plan as it was.
//
#include "BASE.h"
#include "OrFactor.hpp"
#include "PlanNode.hpp"
#include "QueryNode.hpp"
#include "GraphNav.hpp"
#include "ShadowPlan.hpp"
#include "ShadowRoot.hpp"
#include "FilterOp.hpp"
#include "Expr.hpp"
#include "AndExpr.hpp"
#include "OrExpr.hpp"
#include "EqExpr.hpp"
#include "InExpr.hpp"
#include "ColumnRef.hpp"
#include "LiteralExpr.hpp"
#include "SubqueryRefExpr.hpp"
#include "AM.hpp"

using namespace Parser;

/*----------------------------------------------------------------------------*/
/* all filters of the plan                                                    */
/*----------------------------------------------------------------------------*/
void OrFactor::vOrFactor(PlanNode* pRootNode)
{
  if (AM::JOB::bHasTag("<optimiser_off_or_factor>"_Stz))
    return;

  QueryNode* pPlanNode = _pQueryNode(ShadowRoot::pPlanRoot(pRootNode));
  ASSERT(pPlanNode);

  auto fVisitor = [pRootNode](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (FilterOp::bIsOfType(pNode))
      vFactorFilter(_pQueryNode(pNode), pRootNode);
    return GraphNav::WALK_CONTINUE;
  };

  GraphNav::bGlobalWalkBottomUp(pPlanNode, false, true, fVisitor, nullptr);
}

/*----------------------------------------------------------------------------*/
/* the members of an AND (bAnd) or of an OR, nested ones included             */
/*----------------------------------------------------------------------------*/
void OrFactor::vFlatten(QueryNode* pExpr, b1 bAnd, QueryNodeList* pList)
{
  if (bAnd ? AndExpr::bIsOfType(pExpr) : OrExpr::bIsOfType(pExpr)) {
    PlanNodeIter* pIter = pExpr->pNewChildrenIter();
    while (pIter->bHasNext())
      vFlatten(_pQueryNode(pIter->pNext()), bAnd, pList);
    delete pIter;
    return;
  }
  pList->vAppend(pExpr);
}

// no sub-query anywhere in the expression
b1 OrFactor::bUsable(QueryNode* pExpr)
{
  b1 bSubquery = false;
  auto fFind = [&bSubquery](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (SubqueryRefExpr::bIsOfType(pNode)) {
      bSubquery = true;
      return GraphNav::WALK_ABORT;
    }
    return GraphNav::WALK_CONTINUE;
  };
  GraphNav::bGlobalWalk(pExpr, true, true, fFind, nullptr);
  return !bSubquery;
}

b1 OrFactor::bContains(QueryNodeList* pList, QueryNode* pExpr)
{
  for (i8 i = 0; i < pList->nItems(); i++)
    if (Expr::bIsSymbolicallyEqual(pList->pGet(i), pExpr))
      return true;
  return false;
}

// "column = constant", "constant = column" or "column IN (constants)": the column
// and the constants
b1 OrFactor::bValueSet(QueryNode* pConj, QueryNode** ppColumn, QueryNodeList* pValues)
{
  if (EqExpr::bIsOfType(pConj)) {
    QueryNode* p1 = EqExpr::pChild1(pConj);
    QueryNode* p2 = EqExpr::pChild2(pConj);
    if (ColumnRef::bIsOfType(p1) && LiteralExpr::bIsOfType(p2) && !LiteralExpr::bIsNull(p2)) {
      *ppColumn = p1;
      pValues->vAppend(p2);
      return true;
    }
    if (ColumnRef::bIsOfType(p2) && LiteralExpr::bIsOfType(p1) && !LiteralExpr::bIsNull(p1)) {
      *ppColumn = p2;
      pValues->vAppend(p1);
      return true;
    }
    return false;
  }
  if (InExpr::bIsOfType(pConj)) {
    QueryNode* pLeft = InExpr::pLeftExpr(pConj);
    if (!ColumnRef::bIsOfType(pLeft))
      return false;
    QueryNodeList* pItems = new QueryNodeList();
    PlanNodeIter* pIter = InExpr::pRightExprIter(pConj);
    b1 bAllLiterals = true;
    while (pIter->bHasNext()) {
      QueryNode* pItem = _pQueryNode(pIter->pNext());
      if (!LiteralExpr::bIsOfType(pItem) || LiteralExpr::bIsNull(pItem)) {
        bAllLiterals = false;
        break;
      }
      pItems->vAppend(pItem);
    }
    delete pIter;
    if (bAllLiterals)
      for (i8 i = 0; i < pItems->nItems(); i++)
        pValues->vAppend(pItems->pGet(i));
    pItems->vRemoveAll();
    delete pItems;
    if (!bAllLiterals)
      return false;
    *ppColumn = pLeft;
    return true;
  }
  return false;
}

/*----------------------------------------------------------------------------*/
/* one filter                                                                 */
/*----------------------------------------------------------------------------*/
void OrFactor::vFactorFilter(QueryNode* pFilterOp, PlanNode* pRootNode)
{
  UNUSED(pRootNode);
  QueryNode* pExpr = FilterOp::pFilterExpr(pFilterOp);

  QueryNodeList* pTerms = new QueryNodeList();      // members of the AND (not owned)
  vFlatten(pExpr, true, pTerms);

  QueryNodeList* pAdded = new QueryNodeList();      // new members (owned)

  for (i8 iTerm = 0; iTerm < pTerms->nItems(); iTerm++) {
    QueryNode* pTerm = pTerms->pGet(iTerm);
    if (!OrExpr::bIsOfType(pTerm) || !bUsable(pTerm))
      continue;

    QueryNodeList* pBranches = new QueryNodeList();
    vFlatten(pTerm, false, pBranches);
    i8 nBranches = pBranches->nItems();

    // the members of each branch
    QueryNodeList** apConj = new QueryNodeList*[nBranches];
    for (i8 b = 0; b < nBranches; b++) {
      apConj[b] = new QueryNodeList();
      vFlatten(pBranches->pGet(b), true, apConj[b]);
    }

    // 1. members of every branch
    for (i8 c = 0; c < apConj[0]->nItems(); c++) {
      QueryNode* pConj = apConj[0]->pGet(c);
      b1 bEverywhere = true;
      for (i8 b = 1; b < nBranches && bEverywhere; b++)
        bEverywhere = bContains(apConj[b], pConj);
      if (bEverywhere && !bContains(pTerms, pConj) && !bContains(pAdded, pConj))
        pAdded->vAppend(pConj->pDeepCopy(true));
    }

    // 2. a column that every branch compares with constants
    for (i8 c = 0; c < apConj[0]->nItems(); c++) {
      QueryNode* pColumn = nullptr;
      QueryNodeList* pValues = new QueryNodeList();
      if (bValueSet(apConj[0]->pGet(c), &pColumn, pValues)) {
        b1 bEverywhere = true;
        for (i8 b = 1; b < nBranches && bEverywhere; b++) {
          b1 bFound = false;
          for (i8 k = 0; k < apConj[b]->nItems() && !bFound; k++) {
            QueryNode* pOtherColumn = nullptr;
            QueryNodeList* pOther = new QueryNodeList();
            if (bValueSet(apConj[b]->pGet(k), &pOtherColumn, pOther) &&
                Expr::bIsSymbolicallyEqual(pColumn, pOtherColumn)) {
              bFound = true;
              for (i8 v = 0; v < pOther->nItems(); v++)
                if (!bContains(pValues, pOther->pGet(v)))
                  pValues->vAppend(pOther->pGet(v));
            }
            pOther->vRemoveAll();
            delete pOther;
          }
          bEverywhere = bFound;
        }
        if (bEverywhere) {
          QueryNode* pIn = InExpr::pCreate(pColumn->pDeepCopy(true));
          for (i8 v = 0; v < pValues->nItems(); v++)
            pIn->vAppendChild(pValues->pGet(v)->pDeepCopy(true));
          if (!bContains(pTerms, pIn) && !bContains(pAdded, pIn))
            pAdded->vAppend(pIn);
          else
            delete pIn;
        }
      }
      pValues->vRemoveAll();
      delete pValues;
    }

    for (i8 b = 0; b < nBranches; b++) {
      apConj[b]->vRemoveAll();
      delete apConj[b];
    }
    delete[] apConj;
    pBranches->vRemoveAll();
    delete pBranches;
  }

  if (pAdded->nItems() > 0) {
    // the filter becomes: <filter> AND <added 1> AND <added 2> ...
    QueryNode* pNew = pExpr->pDeepCopy(true);
    while (pAdded->nItems() > 0)
      pNew = AndExpr::pCreate(pNew, pAdded->pPop());
    pExpr->vTransmogrifyTo(pNew, true);
    delete pNew;
  }

  pTerms->vRemoveAll();
  delete pTerms;
  delete pAdded;
}
