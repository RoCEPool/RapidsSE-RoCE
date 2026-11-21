/*============================================================================*/
/* Card_KeyExists.cpp -- Key Exists                                           */
/*============================================================================*/

#include "Card_KeyExists.hpp"

#include "CardIter.hpp"
#include "XF_Optimise_Early_projection.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE, Card_KeyExists, CK_o_KEYEXISTS)
__aNewFunction(Card_KeyExists,CN_CARD)
__vDeleteFunction(Card_KeyExists,CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_KeyExists::Card_KeyExists(void)
: Card_CONDITION(CK_o_KEYEXISTS, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);
  
  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_KeyExists::~Card_KeyExists(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* from morphs to keyExists                                                   */
/*----------------------------------------------------------------------------*/
void Card_KeyExists::vMorph_from_To_keyExists(CardIter *pCI)
{
  _static_

  pCI->E.p_keyExists = pCI->E.p_from->pSub_keyExists();

  pCI->E.p_from->vDeleteChild(pCI->E.p_keyExists);

  pCI->E.p_keyExists = _pCard_KeyExists(pCI->E.p_from->pCard(CK_o_KEYEXISTS));

  // todo: this should be in index list
  Card_IndexList* p_indexList = pCI->E.p_keyExists->pSub_indexList();
  if (! (p_indexList && p_indexList->bChoose_index_For_keyExists())) {
    pCI->E.p_index = pCI->E.p_keyExists->pSub_index();
    pCI->E.p_index->vSet_Choose_index();
    pCI->E.p_index->pSub_bound()->pSymbol()->Y_vSet(P_ChosenBound);
  }

  pCI->E.p_filter = pCI->E.p_keyExists->pSub_filter();
  pCI->E.p_filter->vCard(CK_p_CONSTRAINT);
}


/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* the arriving columns that the constraint of a key lookup reads             */
/*----------------------------------------------------------------------------*/
// A correlated sub-query can compare a column of the looked-up table with an
// outer column in another way than "==" (TPC-H Q21: l2.l_suppkey <>
// l1.l_suppkey). That comparison is a constraint of the lookup, and the outer
// column has to arrive: without this the early project dropped it unless the
// query selected it, and the lookup compared with another column.
void Card_KeyExists::vAdd_transients_Of_constraint(CardIter* pCI, Card* p_Op)
{
  _static_

  Card* p_constraint = p_Op->pSub_constraint();
  if (! p_constraint)
    p_constraint = p_Op->pSub_filter();
  if (! p_constraint)
    return;
  Card_Predicate* p_predicate = p_constraint->pSub_predicate();
  if (! p_predicate)
    return;

  char cTblSyn_lookup = p_Op->pSub_table()->pSymbol()->cTblSyn();

  auto bfNeed = [pCI,cTblSyn_lookup](Card_Term* p_term)
  {
    Symbol* pSymbol = p_term->pSymbol();
    if (! pSymbol->bHasFacet(SF_TBL_COL) || pSymbol->cTblSyn() == cTblSyn_lookup)
      return C_CONTINUE_RECURSION;

    Symbol xSym_tblCol;
    xSym_tblCol.vApplyFacet(SF_TBL_COL, pSymbol->szFacet(SF_TBL_COL));
    if (pCI->Early.m_pNeeded_later->pFind_expr(&xSym_tblCol))
      return C_CONTINUE_RECURSION;

    if (Card_Expr* p_expr_defn = pCI->pPrior_expr(&xSym_tblCol)) {
      Card_Expr* p_expr_clone = p_expr_defn->pClone();
      p_expr_clone->pGet_Symbol(0)->vDeleteFacet(SF_EXP_COL);
      pCI->Early.m_pNeeded_later->vAppendChild(p_expr_clone);
      pCI->Early.m_pNeeded_later->vRenumberAll_expr();
    }

    return C_CONTINUE_RECURSION;
  };

  (void) p_predicate->bLambdaWalk_term(bfNeed);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_KeyExists::vAdd_transients(CardIter* pCI)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);


  pCI->E.p_indexList = pSub_indexList();
  pCI->E.p_index     = pCI->E.p_indexList->pFindChosenIndex();
  pCI->E.p_bounds    = pCI->E.p_index->pSub_bounds();
  pCI->E.p_bound     = pCI->E.p_bounds->pFindChosenBound();
  //pCI->G.p_bound->vDump();
  pCI->E.p_term      = pCI->E.p_bound->pGet_tblCol();

  RASSERT(pCI->E.p_term);
  //pCI->G.p_term->vDump();

  /*
   * todo:
   *
   * i) is the bound $t.c in the needed projection?
   *    n: find defn expr for $t.c
   *       add expr (defn expr for $t.c) into needed projection
   */

  if (!pCI->Early.m_pNeeded_later->pFind_expr(pCI->E.p_term->pSymbol())) {

    //pCI->Early.m_pNeeded_later->vDump();

    if (Card_Expr* p_expr_defn = pCI->pPrior_expr(pCI->E.p_term->pSymbol())) {
      //p_expr_defn->vDump();
      Card_Expr* p_expr_clone = p_expr_defn->pClone();
      // The clone carries the position ($n) that the column has where it is
      // defined. It may arrive here at another position, e.g. behind an
      // index join, and needed columns are matched on any facet: drop the
      // position, as Card_IndexJoin::vAdd_transients does.
      p_expr_clone->pGet_Symbol(0)->vDeleteFacet(SF_EXP_COL);
      pCI->Early.m_pNeeded_later->vAppendChild(p_expr_clone);
      pCI->Early.m_pNeeded_later->vRenumberAll_expr();
    }

    //pCI->Early.m_pNeeded_later->vDump();

    vDebugPoint();
  }

  vAdd_transients_Of_constraint(pCI, this);

  vDebugPoint();

 }

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_KeyExists::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);
  UNUSED(pParent);

  if (1 == iDepth) {
    vValidate_child_mustBe(CK_p_LEVEL, CK_p_TABLE, CK_p_CONSTRAINT, CK_NONE);
    RASSERT(pFindPpyRecursive(P_ChosenIndex, CK_INDEX));
    RASSERT(pSub_bound());
  }

  Card::vValidate(pCI,iDepth,pParent);
  if (1 == iDepth)
    Card_OP::vValidate(pCI,iDepth,pParent);
  pCI->vValidateChildren(this,iDepth+1);
  vValidationPassed();
}


/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
