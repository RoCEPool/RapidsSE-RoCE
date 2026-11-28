/*============================================================================*/
/* XF_FLATTEN.cpp -- Flatten                                              */
/*============================================================================*/

#include "XF_FLATTEN.hpp"


/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
XF_FLATTEN::XF_FLATTEN(eCN eClass, CardIter* pCI)
: XF_TRANSFORM(eClass, pCI)
{
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
XF_FLATTEN::~XF_FLATTEN(void) noexcept(false)
{
}


/*----------------------------------------------------------------------------*/
/*-- public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* XF_Fold a from                                                             */
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/* renumber the stream columns of an op after a <from> sub-query is folded    */
/*----------------------------------------------------------------------------*/
// See vFold_from(). Every <term> $n in the subtree of p_card that refers to
// the joined stream is renumbered: $n for n <= nOuter becomes $(n + nCalled),
// and $n for nOuter < n <= nOuter + nCalled becomes $(n - nOuter).
// The <injectable> of a table describes the table's own columns, not the
// stream, so it is left alone.
static void vRenumber_expCols(Card* p_card, i8 nOuter, i8 nCalled)
{
  if (p_card->bIsCard(CK_p_INJECTABLE)) {
    return;
  }

  if (p_card->bIsCard(CK_TERM)) {
    Symbol* pSymbol = p_card->pSymbol();
    if (pSymbol->bHasFacet(SF_EXP_COL) && !pSymbol->bHasFacet(SF_TBL_COL)) {
      Stz* szExpCol = pSymbol->szFacet(SF_EXP_COL);
      if (szExpCol && szExpCol->azChar()[0] == '$') {
        i8 iExpCol = atoll((const char*) szExpCol->azChar() + 1);
        if (iExpCol >= 1 && iExpCol <= nOuter) {
          pSymbol->vApplyFacet(SF_EXP_COL, iExpCol + nCalled);
        } else if (iExpCol > nOuter && iExpCol <= nOuter + nCalled) {
          pSymbol->vApplyFacet(SF_EXP_COL, iExpCol - nOuter);
        }
      }
    }
  }

  for (i8 i_Child = 0; i_Child < p_card->nChildCards(); i_Child++) {
    vRenumber_expCols(p_card->pGetChild(i_Child), nOuter, nCalled);
  }
}

void XF_FLATTEN::vFold_from(Stz* szFoldNm, CardIter* pCI)
{

  // 1) find W_A_C_ro subquery call in mq:<where>/<boolPred>

  RASSERT(pCI->bEachOp(CK_o_CALL));

  // The outer query reads a stream that is the columns of the ops before the
  // <call> (nOuter of them) followed by the columns that the called sub-query
  // emits (nCalled of them). The fold puts the sub-query first, so the stream
  // becomes the sub-query's columns followed by the outer columns. Columns of
  // tables are referred to by table ($$a.n) and are not affected, but columns
  // that come from the sub-query are referred to by position ($n), so those
  // references are renumbered below.
  i8 nOuter = 0;
  i8 nCalled = 0;
  {
    Card* p_deck = pCI->E.p_deck;
    for (i8 i_Card = 0; i_Card < p_deck->nChildCards(); i_Card++) {
      Card* p_card = p_deck->pGetChild(i_Card);
      if (p_card == pCI->E.p_call) {
        break;
      }
      if (Card_Emit* p_emit = p_card->pSub_emit()) {
        nOuter = p_emit->nChildCards(CK_EXPR);
      }
    }
    if (Card_Emit* p_emit_called = pCI->pCalled_emit(pCI->E.p_call)) {
      nCalled = p_emit_called->nChildCards(CK_EXPR);
    }
  }

  Board* pBoard = pCI->m_pBench->pFindBoard(pCI->E.p_call->pSymbol());
  if (! pBoard) {
    vTrace("DID NOT FIND Board %s\n"_Stz, pCI->E.p_call->szFacet(SF_SQY_NO));
    NEVER_REACH
  }

  pCI->E.p_fold = Card_Fold::pCraft_fold(szFoldNm, pBoard->i_Board());
  pCI->Flat.m_p_fold_current = pCI->E.p_fold;
  pCI->E.p_deck->vPushChild(pCI->E.p_fold);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // get copy of sq and push before mq

  // x) push to sq

  pCI->vPushEach();
  pCI->vUseBoard(pBoard);

  pCI->Flat.m_pCopy_inner = pCI->E.p_deck->pDupSubCardsRecursive(0);

  // pCI->PRINTBOARD_dbg();

  // ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^  ^

  // x) pop back to mq
  pCI->vPopEach();
  pCI->E.p_deck->vPushChild(pCI->Flat.m_pCopy_inner);
  pCI->E.p_deck->vRemoveChildLeavingSubs(pCI->Flat.m_pCopy_inner);
  DELETE_CARD(pCI->Flat.m_pCopy_inner);

  pCI->E.p_deck->vRemoveChild(pCI->E.p_call);
  DELETE_CARD(pCI->E.p_call);

  // 7) derive the fold (SQ result) expr
  pCI->vClearRegisters();
  pCI->vBeginEach();
  if (pCI->bEachOp_fold_current()) {
    pCI->Flat.m_i_fold = pCI->E.i_Op;
    Board::vDerive_foldProjection_F(pCI);
  }

  // 8) renumber the stream columns of the outer ops that read the joined
  //    stream: those after the <fold>, up to and including the first op
  //    that emits a new row.
  if (nOuter > 0 && nCalled > 0) {
    Card* p_deck = pCI->E.p_deck;
    bool bAfterFold = false;

    for (i8 i_Card = 0; i_Card < p_deck->nChildCards(); i_Card++) {
      Card* p_card = p_deck->pGetChild(i_Card);

      if (p_card == pCI->Flat.m_p_fold_current) {
        bAfterFold = true;
        continue;
      }
      if (!bAfterFold || !Card_OP::bIsOp(p_card->eCard())) {
        continue;
      }

      vRenumber_expCols(p_card, nOuter, nCalled);

      if (p_card->bIsCard(CK_o_PROJECT) || p_card->bIsCard(CK_o_GBYAGG) ||
          p_card->bIsCard(CK_o_AGGREG)  || p_card->bIsCard(CK_o_ORDAGG)) {
        break;
      }
    }
  }
}


/*----------------------------------------------------------------------------*/
/* XF_Fold a sub-query (before outer-query)                                   */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vFold_subQuery(CardIter* pCI, Stz* szFoldNm, eCK eCardWithSQ)
{
  // 1) find H_A_NC_ro subquery call in mq:<having>/<boolPred>/<roPred>
  pCI->vBeginEach_tail();
  if (pCI->bEachOp_reverse_foldBreak(eCardWithSQ, CK_PREDICATE)) {
    if (!Board::bMark_sqyCall(pCI)) {
      vTrace("DID NOT FIND %s SubQuery call\n"_Stz, szFoldNm);
      NEVER_REACH
    }
  }
  // pCI->PRINTBOARD_dbg();

  // 2) push to inner query
  Board* pBoard_inner = pCI->m_pBench->pFindBoard(pCI->Flat.m_p_term_sqyCall->pSymbol());
  if (! pBoard_inner) {
    vTrace("DID NOT FIND Board %s\n"_Stz, pCI->Flat.m_p_term_sqyCall->szFacet(SF_SQY_NO));
    NEVER_REACH
  }

  pCI->vPushEach();
  Board* pBoard_outer = pCI->pBoard();
  pCI->vUseBoard(pBoard_inner);

  // pCI->PRINTBOARD_dbg();

  // A sub-query of this inner query that is already folded into it (an IN
  // sub-query in its WHERE) may have left a <fold> in front of every <from>:
  // folded as a join, join ordering has moved the sub-query's table after the
  // <fold>, so it ends nothing; drop it. The <correlation> must be the first
  // card, as only the cards after it are copied. (A folded IN with GROUP BY
  // keeps its <fold>, after its own cards: that <fold> is inside the inner
  // query, which ends at the <fold> made here, Flat.m_p_fold_current.)
  {
    Kid xDeck(pCI->E.p_deck);
    Card* p_correlation = nullptr;
    while (xDeck.bEach()) {
      if (CK_o_CORRELATION == xDeck.eChild()) {
        p_correlation = xDeck.pChild();
        break;
      }
    }
    if (p_correlation) {
      Kid xFold(pCI->E.p_deck);
      while (xFold.bEach()) {
        if (CK_o_FROM == xFold.eChild())
          break;
        if (CK_o_FOLD == xFold.eChild())
          xFold.vDelete();
      }
      if (pCI->E.p_deck->pGetChild(0) != p_correlation)
        pCI->E.p_deck->vRotateFirst(p_correlation);
    }
  }

  i8 i_dup_from = 0;
  pCI->vBeginEach();
  if (pCI->bEachOp(CK_o_CORRELATION)) {
    pCI->Flat.m_pCorrelation = pCI->E.p_deck->pSub_correlation();
    i_dup_from = 1;
  }
  else {
    vDebugPoint();
  }


  // 3) get copy of this inner query
  pCI->Flat.m_pCopy_inner = pCI->E.p_deck->pDupSubCardsRecursive(i_dup_from);

  // 4) pop back to outer query
  pCI->vUseBoard(pBoard_outer);
  pCI->vPopEach();

  // 5) add <fold> (before MQ)
  pCI->E.p_fold = Card_Fold::pCraft_fold(szFoldNm, pBoard_inner->i_Board());
  pCI->Flat.m_p_fold_current = pCI->E.p_fold;
  // When this outer query is itself a correlated sub-query, its <correlation>
  // stays in front (after the <subquery> cards that come before it): the inner
  // query and the <fold> go after it, so that the <fold> is followed by the
  // outer query's own cards.
  Card* p_correlation_outer = nullptr;
  for (i8 i_Main = 0; i_Main < pCI->E.p_deck->nChildCards(); i_Main++) {
    Card* p_card = pCI->E.p_deck->pGetChild(i_Main);
    if (p_card->bIsCard(CK_o_CORRELATION)) {
      p_correlation_outer = p_card;
      break;
    }
    if (p_card->bIsCard(CK_o_FROM) || p_card->bIsCard(CK_o_FOLD))
      break;
  }

  if (p_correlation_outer)
    pCI->E.p_deck->vInsertChildAfter(pCI->E.p_fold, p_correlation_outer);
  else
    pCI->E.p_deck->vPushChild(pCI->E.p_fold);

  // 6) copy up inner (add before fold)
  if (p_correlation_outer)
    pCI->E.p_deck->vInsertChildAfter(pCI->Flat.m_pCopy_inner, p_correlation_outer);
  else
    pCI->E.p_deck->vPushChild(pCI->Flat.m_pCopy_inner);
  pCI->E.p_deck->vRemoveChildLeavingSubs(pCI->Flat.m_pCopy_inner);
  DELETE_CARD(pCI->Flat.m_pCopy_inner);


  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  pCI->vClearRegisters();
  pCI->vBeginEach();
  while (pCI->bEachOp()) {

    pCI->vSnapIf_arriving();
    if (pCI->pPeekNextIf(CK_o_FOLD) == pCI->Flat.m_p_fold_current)
      break;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // 7) derive the fold (SQ result) expr
  pCI->Flat.m_pExpr_fold = nullptr;
  pCI->vBeginEach();
  if (pCI->bEachOp_fold_current()) {
    pCI->Flat.m_i_fold = pCI->E.i_Op;
    pCI->E.p_fold->vDerive_emit_fold(pCI, pBoard_inner->pSubQuery());
    pCI->Flat.m_pExpr_fold = pCI->E.p_expr;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  m_pCI->E.pBoard->vRenumberCards();

  // pCI->PRINTBOARD_dbg();
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vInto_KEYnotEXISTS(CardIter* pCI)
{
  pCI->vClearRegisters();
  pCI->vBeginEach();
  RASSERT(pCI->bEachOp_break(CK_o_FROM));


  //RASSERT(pCI->bEachOp(CK_o_WHERE));
  RASSERT(pCI->bEachOp_break(CK_o_PROJECT));

//  pCI->G.p_constraint = pCI->G.p_where->pSub_constraint();
//  pCI->G.p_where->vRemoveSub(pCI->G.p_constraint);
//
//  //pCI->G.p_filter = Card_Filter::pNew(p_constraint);
//  pCI->G.p_table->vAppendChild(pCI->G.p_constraint);

  Card_From* p_from_exists = pCI->E.p_from;
  pCI->E.p_deck->vRemoveChild(p_from_exists);

  pCI->E.p_term = pCI->E.p_project->pSub_term();
  pCI->E.p_project->vRemoveSub(pCI->E.p_term);


  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//  pCI->G.p_deck->vDeleteChild(pCI->G.p_where);
  pCI->E.p_deck->vDeleteChild(pCI->E.p_project);

  // pCI->PRINTBOARD_dbg();

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  pCI->vBeginEach_tail();
  RASSERT(pCI->bEachOp_reverse_foldBreak(CK_o_WHERE));

  RASSERT(pCI->bEachOp_reverse_foldBreak(CK_o_FROM));
  Card_Emit* p_emitted_table = pCI->E.p_from->pSub_emit();

  pCI->vBeginEach_tail();
  RASSERT(pCI->bEachOp_reverse_foldBreak(CK_o_WHERE));


  pCI->E.p_deck->vInsertChildBefore(p_from_exists, pCI->E.p_where);

  pCI->E.p_predicate = pCI->E.p_where->pSub_predicate();
  pCI->E.p_rlnConj  = _pCard_RlnConj(pCI->E.p_predicate->pFindCard_WithSubFacet(CK_RLNCONJ, SF_SQY_NO));

  if (pCI->E.p_rlnConj->pSymbol()->bHasFacet("in"_Stz) ||
      pCI->E.p_rlnConj->pSymbol()->bHasFacet("exists"_Stz))
    p_from_exists->vAppendChild(new Card_KeyExists);
  else
  if (pCI->E.p_rlnConj->pSymbol()->bHasFacet("notIn"_Stz) ||
      pCI->E.p_rlnConj->pSymbol()->bHasFacet("notExists"_Stz)) {
    Card_KeyNotExists* p_keyNotExists = new Card_KeyNotExists;
    // NOT EXISTS keeps an outer row whose key is NULL (the sub-query finds
    // nothing); NOT IN does not (NULL NOT IN (...) is not true)
    if (pCI->E.p_rlnConj->pSymbol()->bHasFacet("notExists"_Stz)) {
      p_keyNotExists->pSymbol()->Y_vSet(P_NullPasses);
      if (Card_Table* p_table = p_from_exists->pSub_table())
        Card_OP::st_sNullPasses.push_back(p_table->pSymbol()->cTblSyn());
    }
    p_from_exists->vAppendChild(p_keyNotExists);
  }

  pCI->vSnapIf_arriving(p_emitted_table,nullptr);
  pCI->E.p_rlnConj->vMorph_sqlOp_To_EQ(pCI, pCI->E.p_term);

  DELETE_CARD(pCI->E.p_term);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  if (! pCI->Flat.m_pCorrelation) {
    return;
  }

  // Each parameter %n of the sub-query stands for an outer column: the n-th
  // argument of the EXISTS / NOT EXISTS call (IN has the one). Put the outer
  // column in its place. The parameter that is compared with "==" becomes
  // the bound of the lookup; any other use (TPC-H Q21: l2.l_suppkey <>
  // l1.l_suppkey) stays in the filter of the looked-up table and becomes a
  // constraint of the lookup that reads the arriving row.
  i8 nArgs = pCI->Flat.m_n_sqyArg > 0 ? pCI->Flat.m_n_sqyArg : 1;
  for (i8 i_arg = 0; i_arg < nArgs; i_arg++) {

    Card* p_parent;
    Symbol xPrmOrd(SF_SQY_PRM, i_arg + 1);
    Card_Term* p_term_prm = _pCard_Term(p_from_exists->pFindSymbolRecursive(CK_TERM,&xPrmOrd,p_parent));
    if (! p_term_prm)
      continue;

    Card_Term* p_term_outer = pCI->Flat.m_n_sqyArg > 0 ? pCI->Flat.m_ap_term_sqyArg[i_arg]
                                                       : pCI->Flat.m_p_term_crlKey;
    Card_Term* p_term_sqyArg = p_term_outer->pClone();

    // the cast of the outer column, where the table that was snapped has it
    if (Card_Expr* p_expr_found = p_emitted_table->pFind_expr(p_term_outer->pSymbol()))
      p_term_sqyArg->vApplyCast_p(p_expr_found->pGet_term(0));

    Symbol xSymbol_prmCol(SF_SQY_PRM, i_arg + 1);
    p_from_exists->bUpdateSymbolRecursive(CK_TERM, &xSymbol_prmCol, p_term_sqyArg->pSymbol());
    delete p_term_sqyArg;
  }

  // pCI->PRINTBOARD_dbg();

  pCI->vClearRegisters();
  pCI->vBeginEach();
  if (pCI->bEachOp(CK_o_WHERE, CK_PREDICATE)) {
    (void)pCI->E.p_predicate->nCullMarked_rlnConj(MARK_AT);
  }

  pCI->vClearRegisters();
  pCI->vBeginEach();
  pCI->vDeleteWhenEmptySub(CK_o_WHERE, CK_PREDICATE);

  vDebugPoint();

}

/*----------------------------------------------------------------------------*/
/* set smooth                                                                 */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vSet_smooth(CardIter* pCI, eCK eCardWithSQ, eQO eWhatOp)
{
  pCI->vBeginEach();

  RASSERT(pCI->bEachOp_fold_current());
  RASSERT(pCI->E.p_Op->bIsCard(CK_o_FOLD));

  // <subQuery>
  if (pCI->pPeekNextIf(CK_SUBQUERY))
    RASSERT(pCI->bEachOp());

  // <from>*
  while (pCI->pPeekNextIf(CK_o_FROM) && pCI->bEachOp(CK_o_FROM))
    {;}

  // [<where>]
  if (pCI->pPeekNextIf(CK_o_WHERE) && pCI->bEachOp(CK_o_WHERE))
    {;}

  // an aggregation between the <where> and the <sort> restates the columns: what the
  // sub-query adds to the rows of the <where> does not reach the <sort> then
  bool bAggregated = false;

  // <aggreg>*
  while (pCI->pPeekNextIf(CK_o_AGGREG) && pCI->bEachOp(CK_o_AGGREG))
    bAggregated = true;

  // <gbyAgg>*
  while (pCI->pPeekNextIf(CK_o_GBYAGG) && pCI->bEachOp(CK_o_GBYAGG))
    bAggregated = true;

  // [<project>]
  if (pCI->pPeekNextIf(CK_o_PROJECT) && pCI->bEachOp(CK_o_PROJECT)) {
    pCI->Smooth.Project_postAgg.m_bSeen = true;
  }

  // [<fold>]
  // the end of a derived table that was folded in earlier, when a <having> on
  // its columns follows it (XF_Flatten_Flatten_pipeline::vPush_sqyFilter_Into_derived)
  if (CK_o_HAVING == eCardWithSQ && pCI->pPeekNextIf(CK_o_FOLD) && pCI->bEachOp(CK_o_FOLD))
  {;}

  // <having>
  if (pCI->pPeekNextIf(CK_o_HAVING) && pCI->bEachOp(CK_o_HAVING))
  {;}

  // <sort>
  // the sort keys move up by the injected column only when that column reaches the <sort>:
  // a sub-query of the <having> is injected into the aggregation itself, a sub-query of the
  // <where> reaches the <sort> when no aggregation lies between them (refer: Q18)
  if (pCI->pPeekNextIf(CK_o_SORT) && pCI->bEachOp(CK_o_SORT)) {
    pCI->Smooth.Sort.m_bCorrect_After_Inject = (CK_o_HAVING == eCardWithSQ) || ! bAggregated;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  if (CK_o_HAVING == eCardWithSQ) {
    pCI->Smooth.GbyAgg.m_bInject_foldExpr = true;
    pCI->Smooth.Having.m_bCorrect_After_Inject = true;
    pCI->Smooth.Unproject.m_bUnproject_foldExpr = true;
  }
  else {
    if (QO_IN_OP == eWhatOp || QO_NOT_IN_OP == eWhatOp)
      pCI->Smooth.Where_preAgg.m_bMorph_sqlOp_To_EQ = true;
    else
    if (QO_RELN_OP == eWhatOp)
      pCI->Smooth.Where_preAgg.bReplace_sqCall_With_result = true;
  }
}

/*----------------------------------------------------------------------------*/
/* smooth after fold                                                          */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vSmooth(CardIter* pCI)
{
  //pCI->PRINTBOARD_dbg();

  pCI->vBeginEach();
  RASSERT(pCI->bEachOp_fold_current());
  RASSERT(pCI->E.p_Op->bIsCard(CK_o_FOLD));

  // The rows that arrive after the <fold> are the rows of the folded
  // sub-query, whose emit is the emit of the <fold>.
  pCI->vSnap_arriving(pCI->E.p_Op->pSub_emit(), nullptr);

  if (pCI->pPeekNextIf(CK_SUBQUERY))
    RASSERT(pCI->bEachOp());

  // <from>*
  while (pCI->pPeekNextIf(CK_o_FROM) && pCI->bEachOp(CK_o_FROM)) {
      pCI->E.p_table = pCI->E.p_from->pSub_table();
      pCI->E.p_Op->vRestate_emit(pCI);
    pCI->vSnapIf_arriving();
  }

  // [<where>]
  if (pCI->pPeekNextIf(CK_o_WHERE) && pCI->bEachOp(CK_o_WHERE)) {
      pCI->Dic.m_p_expr = pCI->Arriving.m_pEmitted_prior;
      pCI->E.p_where->vSmooth(pCI);
      pCI->E.p_Op->vRestate_emit(pCI);
    pCI->vSnapIf_arriving();
  }

  // [<gbyAgg partition|node|origin>]
  while (pCI->pPeekNextIf(CK_o_GBYAGG) && pCI->bEachOp(CK_o_GBYAGG)) {
      pCI->E.p_gbyAgg->vSmooth(pCI);
      pCI->E.p_Op->vRestate_emit(pCI);
    pCI->vSnapIf_arriving();
  }

  // [<project>]
  if (pCI->pPeekNextIf(CK_o_PROJECT) && pCI->bEachOp(CK_o_PROJECT)) {
      pCI->E.p_project->vSmooth_postAgg(pCI);
      pCI->E.p_Op->vRestate_emit(pCI);
    pCI->vSnapIf_arriving();
  }

  // [<fold>]
  // the end of a derived table that was folded in earlier, when a <having> on
  // its columns follows it. The rows that arrive at the <having> are those of
  // the <gbyAgg>, with the injected column: the <fold> is not snapped.
  if (pCI->Smooth.Having.m_bCorrect_After_Inject &&
      pCI->pPeekNextIf(CK_o_FOLD) && pCI->bEachOp(CK_o_FOLD))
  {;}

  // [<having>]
  if (pCI->pPeekNextIf(CK_o_HAVING) && pCI->bEachOp(CK_o_HAVING)) {
      pCI->E.p_having->vSmooth(pCI);
      pCI->E.p_Op->vRestate_emit(pCI);
    pCI->vSnapIf_arriving();
  }

  // [<sort>]
  if (pCI->pPeekNextIf(CK_o_SORT) && pCI->bEachOp(CK_o_SORT)) {
    if (pCI->Arriving.m_pEmitted_prior) {
      pCI->E.p_sort->vSmooth(pCI);
      pCI->E.p_Op->vRestate_emit(pCI);
    }
    pCI->vSnapIf_arriving();
  }

  //  --  --  --  --  --  --  --  --  --  --  --  --  --  --

  if (pCI->Smooth.Unproject.m_bUnproject_foldExpr) {
    pCI->E.p_emit = pCI->E.p_gbyAgg->pSub_emit();
    Card_Project::vUnproject_foldExpr(pCI);
    //pCI->E.p_Op->vRestate_emit(pCI);
    //pCI->vSnapIf_arriving();
  }

  //pCI->PRINTBOARD_dbg();
}

/*----------------------------------------------------------------------------*/
/* count number of tables in sub-query                                        */
/*----------------------------------------------------------------------------*/
i8 XF_FLATTEN::nTables_In_SubQuery(CardIter* pCI, Stz* szFold, eCK eCardWithSQ)
{
  _static_

  // 1) find H_A_NC_ro subquery call in mq:<having>/<boolPred>/<roPred>
  pCI->vClearRegisters();
  pCI->vBeginEach();
  if (pCI->bEachOp(eCardWithSQ, CK_PREDICATE)) {
    if (!Board::bMark_sqyCall(pCI)) {
      vTrace("DID NOT FIND %s SubQuery call\n"_Stz,szFold);
      NEVER_REACH
    }
  }
  // pCI->PRINTBOARD_dbg();

  // 2) push to sq
  Board* pBoard = pCI->m_pBench->pFindBoard(pCI->Flat.m_p_term_sqyCall->pSymbol());
  if (! pBoard) {
    vTrace("DID NOT FIND Board %s\n"_Stz, pCI->Flat.m_p_term_sqyCall->szFacet(SF_SQY_NO));
    NEVER_REACH
  }

  pCI->vPushEach(pBoard);
  i8 nTables = Card_Table::nTables(pCI);
  pCI->vPopEach();

  return nTables;
}

/*----------------------------------------------------------------------------*/
/* remove left-most subquery and rotate next sub-query to be first card       */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vRotate_Next(void)
{
  //m_pCI->vTransform(__FILE__, __func__, XF_FLATTEN_DELETE_LEFT_SUBQUERY);

  Kid xDeck(m_pCI->E.p_deck);
  if (xDeck.bEach(CK_SUBQUERY)) {
    m_pCI->E.p_deck->vRotateFirst(xDeck.pChild());
  }

  //m_pCI->vEndTransform(__LINE__);
}


/*----------------------------------------------------------------------------*/
/*-- private -----------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* convert call of sub-query to correlated join to left sub-stream            */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vPromote_aggreg_To_gbyAgg(CardIter* pCI)
{

  // We have a flattened query.  It has a left-size IQ, and a right-side OQ

  //        IQ... ...OQ           //
  // F F A A A P + F F W A A A    //
  //                              //

  // Start with processing left-side IQ (inner sub-query)

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  // innerQuery innerQuery innerQuery innerQuery innerQuery innerQuery innerQuery
  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // snap correlation key in IQ:<from>/<filter>/<boolPred>//<rlnConj>
  //

  //   v                          //
  // F F A A A P + F F W A A A    // ]FROM
  //   f         +                //  .]filter
  //   b         +                //  ..]boolPred
  //   r         +                //  ...]rlnConj == ((pCI->Flat.m_p_conj_crlKey))
  //             +                //            $d.2 ((pCI->Flat.m_p_term_crlKey))
  //             +                //              %1 // correlation parameter

  bool bSnapped = bSnap_IQ_crlKey(pCI);
  RASSERT(bSnapped);
  //pCI->PRINTBOARD_dbg();
//  pCI->Flat.m_p_expr_crlKey->vDump(__func__);
//  pCI->Flat.m_p_conj_crlKey->vDump(__func__);
//  pCI->Flat.m_p_term_crlKey->vDump(__func__);
  i8 nKeys = pCI->Flat.m_n_crlKey;
  for (i8 i_key = 0; i_key < nKeys; i_key++)
    RASSERT(pCI->Flat.m_ap_expr_crlKey[i_key]->pSymbol()->bMatches(pCI->Flat.m_ap_term_crlKey[i_key]->pSymbol()));


  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // promote IQ:<aggreg> to be IQ:<gbyAgg>/<key=crlKey>
  //   thereby producing ordered subQuery stream

  vPromote_IQ_aggreg_To_gbyAgg_crlKey(pCI);
  //pCI->PRINTBOARD_dbg();

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // add correlation key to IQ:[<project>/<emit>/<crlKey>]

  vEnhance_IQ_emit_project_Add_crlKey(pCI);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // snap final IQ:<op>/<emit>

  if (CK_o_PROJECT == pCI->E.e_Op || CK_o_GBYAGG == pCI->E.e_Op)
    pCI->E.p_emit = pCI->E.p_Op->pSub_emit();
  else
    NEVER_REACH

  // + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
  // fold fold fold fold fold fold fold fold fold fold fold fold fold fold fold
  // + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +

  RASSERT(pCI->bEachOp_fold_current());

  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
  // outerQuery outerQuery outerQuery outerQuery outerQuery outerQuery outerQuery
  // = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =

  // restate the emits of the leading OQ:<from>* sequence
  pCI->vSnap_arriving(nullptr,nullptr);
  while (pCI->pPeekNextIf(CK_o_FROM) && pCI->bEachOp(CK_o_FROM)) {
    pCI->E.p_Op->vRestate_emit(pCI);
    pCI->vSnapIf_arriving();
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Convert the OQ to join to the IQ gby correlation column ordered streamed from SQ

  // v                             //  ]filter
  // F G G G P + F F F W A A A     //  .]boolPred
  // s         +                   //  ..]cmpPred ==
  // f         +                   //           $d.2  ((pCI->m_pTerm_corrArg))
  //           +                   //             %1


  for (i8 i_key = 0; i_key < nKeys; i_key++) {
    Card_CONJ* p_conj_crlKey = pCI->Flat.m_ap_conj_crlKey[i_key];

    pCI->vClearRegisters();
    pCI->vBeginEach();
    bool bRemoved = false;
    while (pCI->bEachOp(CK_o_FROM, CK_o_FILTER, CK_PREDICATE)) {
      if (pCI->E.p_predicate->bHasSub(p_conj_crlKey)) {
        // takes the comparison out and tidies up an AND that it leaves
        // with one child or none
        pCI->E.p_predicate->vRemove_rlnConj(_pCard_RlnConj(p_conj_crlKey));
        bRemoved = true;
        break;
      }
    }
    RASSERT(bRemoved);
    RASSERT(p_conj_crlKey->pSymbol()->Y_bHas(P_CrlKeyConj));

    // tidy up empty SQ:<table>/<filter>
    pCI->E.p_from->vCull_filter_IfEmpty(pCI->E.p_filter);
  }

  //                   v           //  ]filter
  // F A A A P + F F F W A A A     //  .]boolPred
  // s         +       f           //  ..]cmpPred ==
  // f         +       bc          //           $d.2  ((pCI->m_pTerm_corrArg))
  //           +                   //             %1

  // + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +
  // fold fold fold fold fold fold fold fold fold fold fold fold fold fold fold
  // + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + + +

  RASSERT(pCI->bEachOp_fold_current());

  // todo: this is messy and unclear

  while (pCI->bEachOp_reverse()) {
    if (pCI->E.p_Op->pSub_emit())
      break;
  }

  pCI->vSnapIf_arriving();
  RASSERT(pCI->bEachOp_fold_current());

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // ???

  RASSERT(pCI->bEachOp(CK_o_WHERE, CK_PREDICATE));
  // the inner query arrives as its keys followed by its result
  pCI->E.p_predicate->vBump_term_ordCol(SF_EXP_COL, nKeys + 1);

  // append correlation key conjunctions onto the OQ:<where>/<filter>/<boolPred>
  for (i8 i_key = 0; i_key < nKeys; i_key++) {
    Card_CONJ* p_conj_crlKey = pCI->Flat.m_ap_conj_crlKey[i_key];
    p_conj_crlKey->pSymbol()->Y_vCut(P_CrlKeyConj);
    p_conj_crlKey->pSymbol()->Y_vSet(P_CrlKeyTest);

    pCI->E.p_predicate->vAppend_and_rlnConj(_pCard_RlnConj(p_conj_crlKey));
  }

  //                   v           //  ]filter
  // F A A A P + F F F W A A A     //  .]boolPred
  // s         +       f           //  ..]roPred <
  // f         +       bc          //           $a.5
  //           +                   //           $b.1
  //           +                   //           $Q1

  //pCI->PRINTBOARD_dbg();
  //pCI->G.p_predicate->vDump(__func__);
  vConvert_OQ_sqyPrm_Into_expCol(pCI, pCI->E.p_predicate);

  //pCI->G.p_predicate->vDump(__func__);
  vConvert_OQ_sqyNo_Into_expCol(pCI, pCI->E.p_predicate);

  //pCI->PRINTBOARD_dbg();
  //pCI->G.p_predicate->vDump(__func__);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // move the correlation predicate to table as a bounds

  // TODO: this should be done using standard predicate moving

  pCI->vClearRegisters();
  pCI->vBeginEach();

  //                   v           //  ]filter
  // F A A A P + F F F W A A A     //  .]boolPred
  // s         +       f           //  ..]cmpPred ==
  // f         +       bc          //           $d.2  ((pCI->m_pTerm_corrArg))
  //           + b <-  cp          //             %1

  RASSERT(pCI->bEachOp_fold_current());
  RASSERT(pCI->bEachOp(CK_o_WHERE, CK_PREDICATE));
  vMove_OQ_rlnConj_rslTest_To_from_filter(pCI, pCI->E.p_predicate);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//  pCI->vClearRegisters();
  pCI->vBeginEach();
//  if (pCI->bEachOp_fold_current()) {
//    if (pCI->bEachOp(CK_o_WHERE)) {
//      // TODO:
//      // R_vUTE_Unproject_foldExpr_After_where();
//      R_vUTE_Bump_where_BaseOn_foldExpr();
//    }
//  }

}

/*----------------------------------------------------------------------------*/
/* the <outerColNm> cards of a <correlation>: one per parameter, in order     */
/*----------------------------------------------------------------------------*/
static Card_OuterColNm* pOuterColNm(Card_Correlation* p_correlation, i8 iOrdinal)
{
  if (! p_correlation)
    return nullptr;

  i8 n = 0;
  for (Kid xCorrelation(p_correlation); xCorrelation.bEach(); /**/) {
    if (! xCorrelation.pChild()->bIsCard(CK_OUTERCOLNM))
      continue;
    if (++n == iOrdinal)
      return _pCard_OuterColNm(xCorrelation.pChild());
  }

  return nullptr;
}

static i8 nOuterColNms(Card_Correlation* p_correlation)
{
  i8 n = 0;
  while (pOuterColNm(p_correlation, n + 1))
    n++;

  return n ? n : 1;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
bool XF_FLATTEN::bSnap_IQ_crlKey(CardIter* pCI)
{

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // pre-fold identify the <table>/<filter>/<boolPred>/<cmpPred> which is correlated by parameter
  // The inner query may be correlated on several columns: one "column ==
  // %n" comparison per parameter. Each is a correlation key, kept in
  // parameter order so that key n belongs to %n.
  i8 nKeys = nOuterColNms(pCI->Flat.m_pCorrelation);
  RASSERT(nKeys >= 1 && nKeys <= pCI->Flat.C_MAX_CRLKEYS);

  pCI->Flat.m_n_crlKey = 0;
  for (i8 i_key = 0; i_key < pCI->Flat.C_MAX_CRLKEYS; i_key++) {
    pCI->Flat.m_ap_conj_crlKey[i_key] = nullptr;
    pCI->Flat.m_ap_term_crlKey[i_key] = nullptr;
    pCI->Flat.m_ap_expr_crlKey[i_key] = nullptr;
  }

  auto fbSnap_sqyArg = [pCI,nKeys](Card_CONJ* p_conj, bool bUpwards)
  {
    if (bUpwards)
      return C_CONTINUE_RECURSION;

    if (! p_conj->bIsCorrConj())
      return C_CONTINUE_RECURSION;

    Card_Term* p_term_prm = p_conj->pGetTerm(0)->pSymbol()->bHasFacet(SF_SQY_PRM) ?
                              p_conj->pGetTerm(0) : p_conj->pGetTerm(1);
    i8 i_key = p_term_prm->pSymbol()->iTo_ordinal(SF_SQY_PRM) - 1;
    RASSERT(i_key >= 0 && i_key < nKeys);
    RASSERT(! pCI->Flat.m_ap_conj_crlKey[i_key]); // one comparison per parameter

    p_conj->vMark1(MARK_AT);
    p_conj->pSymbol()->Y_vSet(P_CrlKeyConj);

    Card_Term* p_term_crlKey = static_cast<Card_RlnConj *>(p_conj)->pGet_term_crlKey();
    p_term_crlKey->pSymbol()->Y_vSet(P_CrlKeyTerm);

    pCI->Flat.m_ap_conj_crlKey[i_key] = p_conj;
    pCI->Flat.m_ap_term_crlKey[i_key] = p_term_crlKey;
    pCI->Flat.m_n_crlKey++;

    return C_CONTINUE_RECURSION;
  };

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  // the inner query is the part of the deck before the <fold>
  // (the <fold> being made: an inner query can hold the <fold> of one of its
  // own sub-queries)
  i8 i_Op_fold = C_UNKNOWN;
  for (i8 i_Main = 0; i_Main < pCI->E.p_deck->nChildCards(); i_Main++) {
    Card* p_card = pCI->E.p_deck->pGetChild(i_Main);
    if (p_card->bIsCard(CK_o_FOLD) &&
        (!pCI->Flat.m_p_fold_current || p_card == pCI->Flat.m_p_fold_current)) {
      i_Op_fold = i_Main;
      break;
    }
  }

  pCI->vClearRegisters();
  pCI->vBeginEach();
  while (pCI->bEachOp(CK_o_FROM, CK_p_EMIT, CK_o_FILTER)) {

    if (C_UNKNOWN != i_Op_fold && pCI->E.i_Op > i_Op_fold)
      break;

    pCI->E.p_predicate = pCI->E.p_filter->pSub_predicate();
    Card_CONJ *p_conj_root = pCI->E.p_predicate->pRoot_conj();

    i8 nBefore = pCI->Flat.m_n_crlKey;
    bool bIgnore = p_conj_root->bLambdaWalk_conj(fbSnap_sqyArg);
    UNUSED(bIgnore);

    // the keys found on this <from>: snap the columns they are defined by
    if (pCI->Flat.m_n_crlKey == nBefore)
      continue;
    for (i8 i_key = 0; i_key < nKeys; i_key++) {
      if (! pCI->Flat.m_ap_term_crlKey[i_key] || pCI->Flat.m_ap_expr_crlKey[i_key])
        continue;
      Card_Expr* p_expr = pCI->pBackSearch_expr_For_col(pCI->Flat.m_ap_term_crlKey[i_key]->pSymbol(),
                                                        pCI->E.i_Op + 1);
      p_expr->vMark1(MARK_AT);
      p_expr->pSymbol()->Y_vSet(P_CrlKeyDefn);
      pCI->Flat.m_ap_expr_crlKey[i_key] = p_expr;
    }

    if (pCI->Flat.m_n_crlKey == nKeys)
      break;
  }

  if (! pCI->Flat.m_n_crlKey)
    return false;

  // every parameter needs its "==" comparison: a parameter that is used in
  // another way is not supported here
  RASSERT(pCI->Flat.m_n_crlKey == nKeys);

  pCI->Flat.m_p_conj_crlKey = pCI->Flat.m_ap_conj_crlKey[0];
  pCI->Flat.m_p_term_crlKey = pCI->Flat.m_ap_term_crlKey[0];
  pCI->Flat.m_p_expr_crlKey = pCI->Flat.m_ap_expr_crlKey[0];

  return true;
}
/*----------------------------------------------------------------------------*/
/* promote aggreg of inner query to be a gbyAgg keyed by crlKey               */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vPromote_IQ_aggreg_To_gbyAgg_crlKey(CardIter* pCI)
{
  // promote IQ aggreg... to gbyAgg...

  pCI->vClearRegisters();

  pCI->vBeginEach();
  RASSERT(pCI->bEachOp_break_fold_current(CK_o_AGGREG));
  for (i8 i_agg = 0; i_agg < 3; i_agg++) {

    pCI->E.p_Op->vCard(CK_o_GBYAGG);

    pCI->E.p_gbyAgg = _pCard_GbyAgg(pCI->E.p_Op);
    pCI->E.p_level  = pCI->E.p_gbyAgg->pSub_level();
    pCI->E.p_emit   = pCI->E.p_gbyAgg->pSub_emit();

    // -- -- -- -- -- -- -- -- -- -- --
    // key

    Card_Key::vAssemble_injCrlKey(pCI);
    pCI->E.p_gbyAgg->vInsertChildBefore(pCI->E.p_key, pCI->E.p_emit);
    // -- -- -- -- -- -- -- -- -- -- --
    // emit

    // the keys go in front of the aggregates, in order: push the last first
    for (i8 i_key = pCI->Flat.m_n_crlKey - 1; i_key >= 0; i_key--) {
      pCI->E.p_expr = pCI->E.p_key->pGet_expr(i_key);
      Card_Expr::vAssemble_injCrlRsl(pCI, i_key);
      pCI->E.p_emit->vPushChild(pCI->E.p_expr);
    }

    pCI->E.p_emit->vRenumberAll_expr();

    // -- -- -- -- -- -- -- -- -- -- --

    if (pCI->bEachOp()) {
      if (! pCI->E.p_Op->bIsCard(CK_o_AGGREG)) {
        bool b = pCI->bRewindOp();
        RASSERT(b);
        break;
      }
    }
  }
}

/*----------------------------------------------------------------------------*/
/* enhance possible last IQ project adding crlKey to emit                     */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vEnhance_IQ_emit_project_Add_crlKey(CardIter* pCI)
{

  if (!pCI->pPeekNextIf(CK_o_PROJECT))
    return;
  RASSERT(pCI->bEachOp(CK_o_PROJECT));

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // pre-fold sq <project>

  RASSERT (pCI->E.p_Op->bIsCard(CK_o_PROJECT));
  RASSERT(CT_NONE != pCI->E.p_key->eCTVal());

  pCI->E.p_emit = pCI->E.p_project->pSub_emit();
  i8 i_Board = pCI->Flat.m_pSqy_inner->pBoard()->i_Board();
  i8 nKeys = pCI->Flat.m_n_crlKey;

  // push <project>/_SQCRLKEY_ for each key, the last key first: each push
  // puts a "$1" in front and moves every column that is already there, the
  // keys pushed before included, one place up.
  for (i8 i_key = nKeys - 1; i_key >= 0; i_key--) {
    Cast xCast_key = pCI->Flat.m_ap_expr_crlKey[i_key]->xCast();

    pCI->E.p_emit->vBump_expCol(pCI);

    pCI->vPushChild(pCI->E.p_emit, CK_EXPR);
    if (0 == i_key)
      pCI->E.p_expr->pSymbol()->vApplyFacet_varg(SF_NMD_COL, C_CRLKEY, i_Board);
    else
      pCI->E.p_expr->pSymbol()->vApplyFacet_varg(SF_NMD_COL, C_CRLKEY_N, i_Board, i_key + 1);
    pCI->E.p_expr->pSymbol()->Y_vSet(P_InjCrlKey);

    pCI->vAppendNewChild(pCI->E.p_expr, CK_TERM, Symbol(SF_EXP_COL, "$1"_Stz));
    pCI->E.p_term->vApplyCast_x(xCast_key);
    pCI->E.p_expr->vApplyCast_x(xCast_key);
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // supplemental: the result follows the keys
  pCI->E.p_expr = pCI->E.p_emit->pGet_expr(nKeys);
  pCI->E.p_expr->pSymbol()->vApplyFacet_varg(SF_NMD_COL, C_SQYRSL, i_Board);



  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  pCI->E.p_emit->vRenumberAll_expr();
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vConvert_OQ_sqyPrm_Into_expCol(CardIter* pCI,
                                                            Card_Predicate *p_predicate)
{

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  //

  auto vResolve = [pCI](Card_CONJ* p_conj_arg,
                        auto&& vResolve /*oddity permits recursive lambda*/)
  {
    if (! Card_CONJ::bIsConj(p_conj_arg))
      return; /* this effectively declares resolve as void */

    if (p_conj_arg->bIsCard(CK_RLNCONJ)) {

      if (!p_conj_arg->pSymbol()->Y_bHas(P_CrlKeyTest))
        return;

      RASSERT(p_conj_arg->pSymbol()->bHasFacet("=="_Stz));

      // which parameter is this the comparison of? key n arrives as $n
      i8 iOrdinal = C_UNKNOWN;
      for (Kid xPrm(p_conj_arg); xPrm.bEach(); /**/) {
        if (xPrm.pChild()->pSymbol()->bHasFacet(SF_SQY_PRM))
          iOrdinal = xPrm.pChild()->pSymbol()->iTo_ordinal(SF_SQY_PRM);
      }
      RASSERT(iOrdinal >= 1 && iOrdinal <= nOuterColNms(pCI->Flat.m_pCorrelation));

      Kid xConj(p_conj_arg);
      while (xConj.bEach()) {
        // TODO: test for: == $d.2 %1 => == $1 [TRANSLATE PARAM %1]
        if (xConj.pChild()->pSymbol()->bHasFacet(SF_SQY_PRM)) {
          Symbol xSym_expCol(SF_EXP_COL, iOrdinal);
          xConj.pChild()->pSymbol()->vApplyFacets(&xSym_expCol);

          Card_Expr* p_expr_found =
              pCI->Arriving.m_pEmitted_prior->pGet_expr(iOrdinal - 1);
          xConj.pChild()->vApplyCast_x(p_expr_found->xCast());
        }
        else
        if (xConj.pChild()->bIsCard(CK_TERM) && xConj.pChild()->pSymbol()->bHasFacet(SF_TBL_COL)) {
          Card_OuterColNm* p_outerColNm = pOuterColNm(pCI->Flat.m_pCorrelation, iOrdinal);

          // todo: hack
          Card* p_card = p_outerColNm->pDupCardRecursive();
          p_card->vCard(CK_TERM);
          Card_Term* p_term = _pCard_Term(p_card);
          Symbol* pSym_term = p_term->pSymbol();

          Card_Expr* p_expr_otr = pCI->pBackSearch_expr_For_colNm(pSym_term, pCI->i_Child());
          Stz* szProjectedCol = p_expr_otr->pGet_term(0)->szFacet(SF_TBL_COL);
          xConj.pChild()->pSymbol()->vApplyFacet(SF_TBL_COL, szProjectedCol);  // TODO: hack!!
          DELETE_CARD(p_term);
        }
        else {
          NEVER_REACH
        }
      }
      return;
    }
    else
    if (p_conj_arg->bIsCard(CK_ANDCONJ) || p_conj_arg->bIsCard(CK_ORCONJ)) {
      Kid xConj(p_conj_arg);
      while (xConj.bEach()) {
        RASSERT(Card_CONJ::bIsConj(xConj.pChild()));
        vResolve((Card_CONJ*)xConj.pChild(), vResolve);
      }
    }
    else {
      NEVER_REACH;
    }

  };

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  Card_CONJ* p_conj_root = static_cast<Card_CONJ*>(p_predicate->pRoot_conj());

  vResolve(p_conj_root, vResolve);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vConvert_OQ_sqyNo_Into_expCol(CardIter* pCI,
                                                           Card_Predicate *p_predicate)
{

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  auto vResolve = [pCI](Card_CONJ* p_conj_arg,
                        auto&& vResolve /*oddity permits recursive lambda*/)
  {
    if (! Card_CONJ::bIsConj(p_conj_arg))
      return; /* this effectively declares resolve as void */

    //  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
    // process AND or OR

    if (p_conj_arg->bIsCard(CK_ANDCONJ) || p_conj_arg->bIsCard(CK_ORCONJ)) {
      for (Kid xConj(p_conj_arg); xConj.bEach(); /**/) {
        RASSERT(Card_CONJ::bIsConj(xConj.pChild()));
        vResolve(static_cast<Card_CONJ*>(xConj.pChild()), vResolve);
      }
      return;
    }

    //  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
    // process <RLNCONJ>

    RASSERT(p_conj_arg->bIsCard(CK_RLNCONJ));

    if (! p_conj_arg->bHasSubFacet(SF_SQY_NO))
      return;

    RASSERT(! p_conj_arg->pSymbol()->Y_bHas(P_CrlKeyConj));
    RASSERT(p_conj_arg->bIsRelnOp());

    for (Kid xConj(p_conj_arg); xConj.bEach(); /**/) {
      Card* p_conj_child = xConj.pChild();
      if (Card_CONJ::bIsConj(p_conj_child)) {
        vResolve(static_cast<Card_CONJ*>(p_conj_child), vResolve);
        continue;
      }

      if (! p_conj_child->pSymbol()->bHasFacet(SF_SQY_NO))
        continue;

      RASSERT(pCI->m_pBench->bKnown_sqy(p_conj_child->pSymbol()));

      // The call of the sub-query is preceded by its arguments, one per
      // correlation key: take them out.
      i8 nKeys = pCI->Flat.m_n_crlKey;
      RASSERT(nKeys >= 1);

      i8 i_call = 0;
      while (p_conj_arg->pGetChild(i_call) != p_conj_child)
        i_call++;
      RASSERT(i_call >= nKeys);
      for (i8 i_key = 0; i_key < nKeys; i_key++) {
        Card* p_arg = p_conj_arg->pGetChild(--i_call);
        p_conj_arg->vRemoveChild(p_arg);
        DELETE_CARD(p_arg);
      }

      // the result follows the correlation keys
      p_conj_child->pSymbol()->vApplyFacet(SF_EXP_COL, nKeys + 1);
      Card_Expr* p_expr_found = pCI->Arriving.m_pEmitted_prior->pGet_expr(nKeys);
      p_conj_child->vApplyCast_x(p_expr_found->xCast());
      p_conj_arg->pSymbol()->Y_vSet(P_SqyRslTest);

      // children were removed under the iterator: a comparison has one call
      break;
    }

    return;
  };

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  Card_CONJ* p_conj_root = static_cast<Card_CONJ*>(p_predicate->pRoot_conj());

  vResolve(p_conj_root, vResolve);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vMove_OQ_rlnConj_rslTest_To_from_filter(CardIter* pCI, Card_Predicate *p_predicate)
{
  p_predicate->vRemove_rlnConj(pCI->Flat.m_p_conj_rslTest);

  //p_rlnConj_sqyRslTest->vDump();

  Card_Term* p_term_tblCol = pCI->Flat.m_p_conj_rslTest->pGetFirstTerm_tblCol();
  Symbol* pSym_term_tblCol = p_term_tblCol->pSymbol();
  Card_OP* p_OP = pCI->pFind_from_By_tblSyn(pSym_term_tblCol, pCI->E.i_Op);

  pCI->E.p_filter = p_OP->pEnsure_filter();

  pCI->E.p_filter->pSub_predicate()->vAppend_and_rlnConj(pCI->Flat.m_p_conj_rslTest);
}

/*----------------------------------------------------------------------------*/
/* Derive correlated fold projection                                          */
/*----------------------------------------------------------------------------*/
void XF_FLATTEN::vRestate_emit_fold(CardIter* pCI)
{
  pCI->vSnapIf_arriving_till(CK_o_FOLD);
  RASSERT(CK_o_FOLD == pCI->E.e_Op);

  pCI->E.p_fold->vAppendChild(pCI->Arriving.m_pEmitted_prior->pDupCardRecursive());
}


/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
