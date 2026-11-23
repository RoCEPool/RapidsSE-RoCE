/*============================================================================*/
/* ConstFold.cpp - date and interval constants computed once                  */
/*============================================================================*/
//
//   where l_shipdate <= date '1998-12-01' - interval '90' day      (TPC-H Q1)
//     and o_orderdate < date '1994-01-01' + interval '1' year      (TPC-H Q5)
//
// The generated code computed the constant on the right for every row: a day
// interval through timegm() (with the time zone work of the C library), a
// year-month interval through the month arithmetic. DuckDB folds constant
// expressions when it plans the query (its expression rewriter); here the SQL
// compiler does it for DATE +/- INTERVAL and INTERVAL + DATE literals, with
// the same arithmetic as the run-time macros (MathMacros.h: _ADD_DTEIVD,
// _SUB_DTEIVD, _ADD_DTEIVY, _SUB_DTEIVY), and into the type the expression
// had: a TIMESTAMP for a day-time interval, a DATE for a year-month one. A
// value the macros would treat in a special way (fractions of a second, a
// result out of the date range) is left to them.
//
// Job tag /optimiser_off_const_fold leaves the plan as it was.
//
#include "BASE.h"
#include "ConstFold.hpp"
#include "PlanNode.hpp"
#include "QueryNode.hpp"
#include "GraphNav.hpp"
#include "ShadowRoot.hpp"
#include "Expr.hpp"
#include "AdditionExpr.hpp"
#include "SubtractionExpr.hpp"
#include "DateLiteral.hpp"
#include "IntervalLiteral.hpp"
#include "TimestampLiteral.hpp"
#include "CodeSynthCommon.h"
#include "AM.hpp"

#include <time.h>
#include <vector>

using namespace Parser;

namespace
{
  const i8 C_MICROS = 1000000;
  const i8 aiDaysPerMonth[13]     = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const i8 aiDaysPerMonthLeap[13] = {0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  // date + months (bSub: date - months); false when left to the run time
  bool bAddMonths(Dte_T xDte, Ivl_T xIvl, bool bSub, Dte_T& rxOut)
  {
    i8 iArg2 = xIvl.m_xIvl.u.m_xIvlYM.m_uMonths;
    i8 iYear_arg2 = iArg2 / 12;
    i8 iMonth_arg2 = iArg2 % 12;
    if (xIvl.m_xIvl.m_bIsNeg) {
      iYear_arg2 = -iYear_arg2;
      iMonth_arg2 = -iMonth_arg2;
    }
    if (bSub) {
      iYear_arg2 = -iYear_arg2;
      iMonth_arg2 = -iMonth_arg2;
    }
    i8 iYearDelta = (i8) xDte.m_uYearDelta + iYear_arg2;
    i8 iMonth = (i8) xDte.m_uMonth + iMonth_arg2;
    i8 iDay = xDte.m_uDay;
    // _CONVERT_TO_VALID_DATE
    if (iMonth <= 0) {
      iMonth += 12;
      iYearDelta--;
    }
    if (iMonth > 12) {
      iYearDelta = iYearDelta + iMonth / 12;
      iMonth = iMonth % 12;
    }
    if (iMonth < 1 || iMonth > 12 || iYearDelta < 0 || iYearDelta > (i8) C_YEAR_RANGE)
      return false;
    i8 iYear = iYearDelta + C_YEAR_BASE;
    bool bLeap = ((iYear % 4 == 0) && (iYear % 100 != 0)) || (iYear % 400 == 0);
    i8 nDays = bLeap ? aiDaysPerMonthLeap[iMonth] : aiDaysPerMonth[iMonth];
    if (iDay > nDays)
      iDay = nDays;
    rxOut.m_iAsI8 = 0;
    rxOut.m_uYearDelta = iYearDelta;
    rxOut.m_uMonth = iMonth;
    rxOut.m_uDay = iDay;
    return true;
  }

  // date + day-time interval, as a timestamp (bSub: date - interval)
  bool bAddMicros(Dte_T xDte, Ivl_T xIvl, bool bSub, Tsp_T& rxOut)
  {
    i8 iArg2 = xIvl.m_xIvl.u.m_xIvlDT.m_uMicros;
    bool bNeg = xIvl.m_xIvl.m_bIsNeg;
    if (bSub)
      bNeg = ! bNeg;
    if (bNeg)
      iArg2 = -iArg2;
    if (iArg2 % C_MICROS != 0)
      return false;
    struct tm xDateTime = {};
    xDateTime.tm_year = xDte.m_uYearDelta;
    xDateTime.tm_mon  = xDte.m_uMonth - 1;
    xDateTime.tm_mday = xDte.m_uDay;
    xDateTime.tm_sec  = iArg2 / C_MICROS;
    timegm(&xDateTime);
    if (xDateTime.tm_year < 0 || xDateTime.tm_year > (int) C_YEAR_RANGE)
      return false;
    rxOut.m_iAsI8 = 0;
    rxOut.m_uYearDelta = xDateTime.tm_year;
    rxOut.m_uMonth     = xDateTime.tm_mon + 1;
    rxOut.m_uDay       = xDateTime.tm_mday;
    rxOut.m_uHour      = xDateTime.tm_hour;
    rxOut.m_uMinute    = xDateTime.tm_min;
    rxOut.m_uSecond    = xDateTime.tm_sec;
    rxOut.m_uMicro     = 0;
    return true;
  }

  // the value of pExpr, or nullptr
  QueryNode* pFold(QueryNode* pExpr, PlanNode* pRootNode)
  {
    bool bSub = SubtractionExpr::bIsExactType(pExpr);
    if (! bSub && ! AdditionExpr::bIsExactType(pExpr))
      return nullptr;
    QueryNode* p1 = bSub ? SubtractionExpr::pChild1(pExpr) : AdditionExpr::pChild1(pExpr);
    QueryNode* p2 = bSub ? SubtractionExpr::pChild2(pExpr) : AdditionExpr::pChild2(pExpr);
    QueryNode* pDate = nullptr;
    QueryNode* pIvl = nullptr;
    if (DateLiteral::bIsExactType(p1) && IntervalLiteral::bIsExactType(p2)) {
      pDate = p1;
      pIvl = p2;
    }
    else if (! bSub && IntervalLiteral::bIsExactType(p1) && DateLiteral::bIsExactType(p2)) {
      pDate = p2;
      pIvl = p1;
    }
    if (! pDate || pDate->bIsNullValue() || pIvl->bIsNullValue())
      return nullptr;

    Dte xDte = DateLiteral::xValue(pDate);
    Ivl xIvl = IntervalLiteral::xValue(pIvl);
    if (xDte.bIsNull() || xIvl.bIsNull())
      return nullptr;

    eCT eResult = Expr::xTypeId(pExpr, pRootNode).eCTVal();
    eCT eIvl = IntervalLiteral::eIvlCategory(pIvl);

    if (CT_INTERVAL_YM == eIvl && CT_DATE == eResult) {
      Dte_T xOut;
      if (! bAddMonths(xDte.T, xIvl.T, bSub, xOut))
        return nullptr;
      Dte xVal = xDte;
      xVal.T = xOut;
      return DateLiteral::pCreate(xVal);
    }
    if (CT_INTERVAL_DT == eIvl && CT_TIMESTAMP == eResult) {
      Tsp_T xOut;
      if (! bAddMicros(xDte.T, xIvl.T, bSub, xOut))
        return nullptr;
      Tsp xVal;
      xVal.T = xOut;
      return TimestampLiteral::pCreate(xVal);
    }
    return nullptr;
  }
}

/*----------------------------------------------------------------------------*/
/* every expression of the plan                                               */
/*----------------------------------------------------------------------------*/
void ConstFold::vConstFold(PlanNode* pRootNode)
{
  if (AM::JOB::bHasTag("<optimiser_off_const_fold>"_Stz) ||
      AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return;

  QueryNode* pPlanNode = _pQueryNode(ShadowRoot::pPlanRoot(pRootNode));
  ASSERT(pPlanNode);

  // collect first, replace after the walk
  std::vector<QueryNode*> xFound;
  auto fVisitor = [&xFound](PlanNode* pNode, s2K& sPath, i8 iDepth, void* pvArg) -> GraphNav::eWalkCtrl {
    UNUSED(sPath);
    UNUSED(iDepth);
    UNUSED(pvArg);
    if (AdditionExpr::bIsExactType(pNode) || SubtractionExpr::bIsExactType(pNode))
      xFound.push_back(_pQueryNode(pNode));
    return GraphNav::WALK_CONTINUE;
  };
  GraphNav::bGlobalWalk(pPlanNode, true, true, fVisitor, nullptr);

  for (QueryNode* pExpr : xFound) {
    QueryNode* pValue = pFold(pExpr, pRootNode);
    if (! pValue)
      continue;
    pExpr->vTransmogrifyTo(pValue, true);
    delete pValue;
  }
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
