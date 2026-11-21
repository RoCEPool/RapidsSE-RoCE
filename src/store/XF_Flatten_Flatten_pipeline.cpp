/*============================================================================*/
/* XF_Flatten_Flatten_pipeline.cpp -- Flatten Pipeline of SubQueries          */
/*============================================================================*/

#include "XF_Flatten_Flatten_pipeline.hpp"

#include "Board.hpp"
#include "CardIter.hpp"
#include "Optimiser.hpp"

#include "eXF.h"
#include "XF_Flatten_Reorder_join.hpp"

#include "XF_Flatten_Fold_subquery.hpp"

#include "XF_Flatten_Delete_fold.hpp"

#include "XF_Flatten_X_expCol_To_tblCol.hpp"
#include "XF_Utility.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction(STORE, XF_Flatten_Flatten_pipeline, CN_XF_FLATTEN_FLATTEN_PIPELINE)
__aNewFunction(XF_Flatten_Flatten_pipeline, CN_XF_FLATTEN_FLATTEN_PIPELINE)
__vDeleteFunction(XF_Flatten_Flatten_pipeline, CN_XF_FLATTEN_FLATTEN_PIPELINE)
__aFooterFunction(XF_Flatten_Flatten_pipeline)

/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
XF_Flatten_Flatten_pipeline::XF_Flatten_Flatten_pipeline(CardIter* pCI)
: XF_FLATTEN(CN_XF_FLATTEN_FLATTEN_PIPELINE, pCI)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_FLATTEN_FLATTEN_PIPELINE, _FOOTER_);

  m_pCI = pCI;

  _checkInitialised_(CN_XF_FLATTEN_FLATTEN_PIPELINE);

  m_pCI->vUseBoard(nullptr);


  _postcondition_(_ADDRESS_, _MAGIC_, CN_XF_FLATTEN_FLATTEN_PIPELINE, _FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
XF_Flatten_Flatten_pipeline::~XF_Flatten_Flatten_pipeline(void) noexcept(false)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_FLATTEN_FLATTEN_PIPELINE, _FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_Flatten_Flatten_pipeline::vFlatten(CardIter* pCI)
{
  _static_

  XF_Flatten_Flatten_pipeline* pMakeFlat = new XF_Flatten_Flatten_pipeline(pCI);

  pMakeFlat->vFlatten();

  delete pMakeFlat;
}


/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_Flatten_Flatten_pipeline::vFlatten(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_FLATTEN_FLATTEN_PIPELINE, _FOOTER_);

  m_pCI->vUseBoard(nullptr);

  vFlatten_recurse(nullptr,0);

  XF_Flatten_Delete_fold::vXF_Delete_fold(m_pCI);
}

/*----------------------------------------------------------------------------*/
/* stop if folding a sub-query did not remove the reference to it             */
/*----------------------------------------------------------------------------*/
// Without this the flattening loops forever, holding the job's resources.
// It happens when the query cannot be flattened into a single pipeline, e.g.
// when a sub-query that aggregates (a view or WITH query) is both joined in
// the FROM clause and read by a scalar sub-query, as in TPC-H Q15 written
// with a view.
static void vCheck_progress(Card_Term* p_term, Card_Term*& p_term_folded)
{
  if (p_term == p_term_folded) {
    Error xError(EC_NOT_IMPLEMENTED,
                 "the optimiser cannot flatten this combination of sub-queries"_Stz,
                 (Stz*) __FILE__, __LINE__);
    throw xError;
  }

  p_term_folded = p_term;
}

/*----------------------------------------------------------------------------*/
/* push a sub-query filter on a derived table into the derived table          */
/*----------------------------------------------------------------------------*/
// (refer: Q15)
//   select ... from supplier, (select l_suppkey as no, sum(...) as total
//                                from lineitem group by l_suppkey) r
//    where s_suppkey = no
//      and total = (select max(total) from ...)
//
// The comparison with the scalar sub-query is on a column that the derived
// table aggregates. Once the derived table is folded into this query, the
// <where> lies behind the aggregation of the derived table:
//
//   <from> <gbyAgg>.. <fold From> [<from>..] <where> ..
//
// Folding the sub-query into that <where> cannot serve it: the result of the
// sub-query is not carried past the aggregation (the fold then never removed
// the reference to the sub-query and flattening did not end).
//
// The same comparison as a <having> on the rows of the derived table is
// equivalent, is a shape the folding does handle (H_A_NC_ro, Q11), and
// filters before the derived table is joined to anything:
//
//   <from> <gbyAgg>.. <fold From> <having> [<from>..] [<where>] ..
//
// Done only for the plain case: one folded derived table that ends in its
// <gbyAgg>; the comparison has one column of the derived table on the left
// and the sub-query on the right; the sub-query is a non-correlated aggregate
// compared with a relational operator. The sub-query is folded here.
void XF_Flatten_Flatten_pipeline::vPush_sqyFilter_Into_derived(i8 iDepth)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_FLATTEN_FLATTEN_PIPELINE, _FOOTER_);

  Bench* pBench = m_pCI->m_pBench;

  for (;;) {

    // -- <gbyAgg> <fold From> ... <where>, with no other <fold>

    Card*       p_Op_prev = nullptr;
    Card*       p_gbyAgg  = nullptr;
    Card*       p_fold    = nullptr;
    Card*       p_after   = nullptr;
    Card_Where* p_where   = nullptr;
    i8          nFolds    = 0;

    for (Kid xDeck(m_pCI->E.p_deck); xDeck.bEach(); /**/) {
      Card* p_Op = xDeck.pChild();
      if (p_Op->bIsCard(CK_o_FOLD)) {
        nFolds++;
        p_fold   = p_Op;
        p_gbyAgg = p_Op_prev;
        p_where  = nullptr;
        p_after  = p_Op;
      }
      else if (p_Op->bIsCard(CK_o_WHERE)) {
        p_where = _pCard_Where(p_Op);
      }
      p_Op_prev = p_Op;
    }

    if (nFolds != 1 || ! p_where || ! p_gbyAgg || ! p_gbyAgg->bIsCard(CK_o_GBYAGG))
      return;
    if (! p_fold->pSymbol()->bHasFacet(SF_FLD_NM, "From"_Stz))
      return;

    Card_Emit* p_emit_fold   = p_fold->pSub_emit();
    Card_Emit* p_emit_gbyAgg = p_gbyAgg->pSub_emit();
    if (! p_emit_fold || ! p_emit_gbyAgg)
      return;
    // the columns of the derived table are $1..$n of the stream, as its <gbyAgg> numbers them
    i8 nDerived = p_emit_fold->nChildCards(CK_EXPR);
    if (nDerived != p_emit_gbyAgg->nChildCards(CK_EXPR))
      return;

    if (! p_where->pSub_filter() || ! p_where->pSub_filter()->pSub_predicate())
      return;
    Card_Predicate* p_predicate = p_where->pSub_filter()->pSub_predicate();

    // -- a comparison "<column of the derived table> <op> (scalar sub-query)"

    Card_RlnConj* p_rlnConj_found = nullptr;

    auto bfFind = [&](Card* p_card)
    {
      if (! p_card->bIsCard(CK_RLNCONJ) || p_card->nChildCards() != 2)
        return C_CONTINUE_RECURSION;
      Card_RlnConj* p_rlnConj = _pCard_RlnConj(p_card);
      if (! p_rlnConj->bIsRlnOp())
        return C_CONTINUE_RECURSION;

      Card_Term* p_term_0 = p_rlnConj->pGetTerm_0();
      Card_Term* p_term_1 = p_rlnConj->pGetTerm_1();
      if (! p_term_0 || ! p_term_1)
        return C_CONTINUE_RECURSION;

      // the sub-query on the right: that is where the <having> expects it
      // (Card_Having::vCorrect_After_Inject)
      if (! p_term_1->pSymbol()->bHasFacet(SF_SQY_NO) || p_term_0->pSymbol()->bHasFacet(SF_SQY_NO))
        return C_CONTINUE_RECURSION;
      Card_Term* p_term_sqy = p_term_1;
      Card_Term* p_term_col = p_term_0;

      Symbol* pSym_col = p_term_col->pSymbol();
      if (! pSym_col->bHasFacet(SF_EXP_COL) || pSym_col->bHasFacet(SF_TBL_COL) || pSym_col->bHasFacet(SF_SQY_NO))
        return C_CONTINUE_RECURSION;
      i8 iCol = pSym_col->iTo_ordinal(SF_EXP_COL);
      if (iCol < 1 || iCol > nDerived)
        return C_CONTINUE_RECURSION;

      SubQuery* pSqy = pBench->pFindSubQuery(p_term_sqy->pSymbol());
      if (! pSqy || ! pSqy->bViaWhere() || ! pSqy->bIsAggregation() || pSqy->bIsCorrelated() ||
          pSqy->bByIn() || pSqy->bByNotIn() || pSqy->bByExists() || pSqy->bByNotExists())
        return C_CONTINUE_RECURSION;

      p_rlnConj_found = p_rlnConj;
      return C_TERMINATE_RECURSION;
    };

    // only a comparison that every row has to pass: the predicate itself or a member of its AND
    Card* p_top = p_predicate->nChildCards() ? p_predicate->pPeekChild() : nullptr;
    if (! p_top)
      return;
    if (p_top->bIsCard(CK_ANDCONJ)) {
      for (Kid xAnd(p_top); xAnd.bEach(); /**/)
        if (bfFind(xAnd.pChild()) == C_TERMINATE_RECURSION)
          break;
    }
    else {
      (void) bfFind(p_top);
    }

    if (! p_rlnConj_found)
      return;

    m_pCI->vTransform(__FILE__, __func__, XF_FLATTEN_PUSH_SQYFILTER);

    // -- the comparison as <having> behind the <gbyAgg> of the derived table

    Card_RlnConj* p_rlnConj_having = p_rlnConj_found->pClone();
    Card_Term* p_term_sqy_having = p_rlnConj_having->pGetTerm_1();

    Card_Having* p_having = new Card_Having;
    p_having->vAppendChild(new Card_Level(AL_ORIGIN));
    Card_Filter* p_filter = new Card_Filter;
    p_having->vAppendChild(p_filter);
    Card_Predicate* p_predicate_having = new Card_Predicate;
    p_predicate_having->xTypeId() = TypeId::xIdOf(CT_BOOLEAN);
    p_filter->vAppendChild(p_predicate_having);
    p_predicate_having->vAppendChild(p_rlnConj_having);

    // Directly behind the <fold>: the folding looks for the <having> from the end
    // of the deck up to the last <fold>, and walks from the <gbyAgg> over the
    // <fold> to it.
    m_pCI->E.p_deck->vInsertChildAfter(p_having, p_after);

    SubQuery* pSqy_scalar = pBench->pFindSubQuery(p_term_sqy_having->pSymbol());
    pSqy_scalar->vClause(QC_HAVING_CLAUSE);

    // -- and no longer in the <where>

    p_rlnConj_found->vMark1(MARK_AT);
    (void) p_predicate->nCullMarked_rlnConj(MARK_AT);

    m_pCI->vClearRegisters();
    m_pCI->vBeginEach();
    m_pCI->vDeleteWhenEmptySub(CK_o_WHERE, CK_PREDICATE);

    m_pCI->E.p_deck->vMarksClearRecursive();
    m_pCI->vEndTransform(__LINE__);

    // -- fold the sub-query into the <having>

    Symbol* pSym_sqyNo_nested = p_term_sqy_having->pSymbol();
    vFlatten_recurse(pSym_sqyNo_nested, iDepth + 1);
    XF_Flatten_Fold_subquery::vDecisionTree(m_pCI, pSym_sqyNo_nested);

    // Another comparison of this kind would now find two <fold>s: one is handled.
    return;
  }
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_Flatten_Flatten_pipeline::vFlatten_recurse(Symbol* pSym_sqyNo_inner, i8 iDepth)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_FLATTEN_FLATTEN_PIPELINE, _FOOTER_);

  Board *pBoard_outer = nullptr;
  if (iDepth) {
    pBoard_outer = m_pCI->E.pBoard;
    SubQuery *pSqy_inner = m_pCI->m_pBench->pFindSubQuery(pSym_sqyNo_inner);
    m_pCI->vPushEach();
    m_pCI->vUseBoard(pSqy_inner->pBoard());
  }

  // v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v
  // v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v
  // v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v v

  // things before sub-queries

  XF_Flatten_X_expCol_To_tblCol::vFlatten(m_pCI);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // flatten the sub-queries of this outer query

  for (;;) {
    m_pCI->vBeginEach();
    if (! m_pCI->bEachOp(CK_o_CALL))
      break;

    Symbol* pSym_sqyNo_nested = m_pCI->E.p_call->pSymbol();
    vFlatten_recurse(pSym_sqyNo_nested, iDepth + 1);
    XF_Flatten_Fold_subquery::vDecisionTree(m_pCI, pSym_sqyNo_nested);
  }

  // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
  // a sub-query filter on a derived table that the derived table can do itself

  vPush_sqyFilter_Into_derived(iDepth);

  // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

  bool bFound;

  // The sub-query reference that the last fold was for. Folding a sub-query
  // replaces its reference, so finding it again means the fold did not
  // apply to this query (see vCheck_progress).
  Card_Term* p_term_folded = nullptr;

  do {
    m_pCI->vBeginEach_tail();
    if (!m_pCI->bEachOp_reverse_foldBreak(CK_o_WHERE, CK_PREDICATE))
      break;

    auto bLambdaFn = [&bFound,&p_term_folded,this, iDepth](Card_Term* p_term)
    {
       if (! p_term->pSymbol()->bHasFacet(SF_SQY_NO))
         return C_CONTINUE_RECURSION;

      vCheck_progress(p_term, p_term_folded);

      Symbol* pSym_sqyNo_nested = p_term->pSymbol();
      vFlatten_recurse(pSym_sqyNo_nested, iDepth + 1);
      XF_Flatten_Fold_subquery::vDecisionTree(m_pCI, pSym_sqyNo_nested);

      bFound = true;
      return C_TERMINATE_RECURSION;
    };


    bFound = false;
    bool b = m_pCI->E.p_predicate->bLambdaWalk_term(bLambdaFn);
    UNUSED(b);
    vDebugPoint();
  } while (bFound);


  // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

  do {
    m_pCI->vBeginEach_tail();

    if (!m_pCI->bEachOp_reverse_foldBreak(CK_o_HAVING, CK_PREDICATE))
      break;

    auto bLambdaFn = [&bFound,&p_term_folded,this,iDepth](Card_Term* p_term)
    {
      if (! p_term->pSymbol()->bHasFacet(SF_SQY_NO))
        return C_CONTINUE_RECURSION;

      vCheck_progress(p_term, p_term_folded);

      Symbol* pSym_sqyNo_nested = p_term->pSymbol();
      vFlatten_recurse(pSym_sqyNo_nested, iDepth + 1);
      XF_Flatten_Fold_subquery::vDecisionTree(m_pCI, pSym_sqyNo_nested);

      bFound = true;
      return C_TERMINATE_RECURSION;
    };


    bFound = false;
    bool b = m_pCI->E.p_predicate->bLambdaWalk_term(bLambdaFn);
    UNUSED(b);
    vDebugPoint();
  } while (bFound);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // things to do to this outer query after flattening it's sub-queries

  XF_Utility::vRemove_LeftMost(m_pCI);
  XF_Flatten_Reorder_join::vXF_Reorder_join(m_pCI);
  vRotate_Next();

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // sub-query has been flattened

  //Card_Fold::vMorph_to_flat(m_pCI);

  // m_pCI->PRINTBOARD_dbg();

  // ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^
  // ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^
  // ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^ ^

  if (iDepth) {
    m_pCI->vPopEach();
    m_pCI->vUseBoard(pBoard_outer);
  }

  vDebugPoint();
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/