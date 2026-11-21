/*============================================================================*/
/* XF_Optimise_Cull_bound.cpp -- Cut Sort                                                */
/*============================================================================*/

#include "XF_Optimise_Cull_bound.hpp"
#include "Card_IndexList.hpp"
#include "Card_Index.hpp"
#include "Card_IdxColList.hpp"

#include "eOT.h"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction(STORE, XF_Optimise_Cull_bound, CN_XF_OPTIMISE_CULL_BOUND)
__aNewFunction(XF_Optimise_Cull_bound, CN_XF_OPTIMISE_CULL_BOUND)
__vDeleteFunction(XF_Optimise_Cull_bound, CN_XF_OPTIMISE_CULL_BOUND)
__aFooterFunction(XF_Optimise_Cull_bound)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
XF_Optimise_Cull_bound::XF_Optimise_Cull_bound(CardIter* pCI)
: XF_OPTIMISE(CN_XF_OPTIMISE_CULL_BOUND, OT_CULL_BOUND, pCI)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_OPTIMISE_CULL_BOUND, _FOOTER_);

  _checkInitialised_(CN_XF_OPTIMISE_CULL_BOUND);
  
  _postcondition_(_ADDRESS_, _MAGIC_, CN_XF_OPTIMISE_CULL_BOUND, _FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
XF_Optimise_Cull_bound::~XF_Optimise_Cull_bound(void) noexcept(false)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_OPTIMISE_CULL_BOUND, _FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void XF_Optimise_Cull_bound::vOptimise(CardIter* pCI)
{
  _static_

  XF_Optimise_Cull_bound* pXF_Cull_NonUsed_bound = new XF_Optimise_Cull_bound(pCI);

  bool b1 = pXF_Cull_NonUsed_bound->bXF_Cull_NonUsed_bound();
  bool b2 = pXF_Cull_NonUsed_bound->bXF_Cull_Used_bound();

  pXF_Cull_NonUsed_bound->vOptimised(b1 | b2);

  delete pXF_Cull_NonUsed_bound;
}


/*----------------------------------------------------------------------------*/
/*-- public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* cull non-used bounds                                                       */
/*----------------------------------------------------------------------------*/
bool XF_Optimise_Cull_bound::bXF_Cull_NonUsed_bound(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_OPTIMISE_CULL_BOUND, _FOOTER_);

  bool bOptRun = false;

  m_pCI->vTransform(__FILE__, __func__, XF_OPTIMISE_CULL_BOUND);

  m_pCI->vClearRegisters();
  m_pCI->vBeginEach();

  while (m_pCI->bEachOp()) {

    if (!(m_pCI->E.p_Op->bIsCard(CK_o_INDEXJOIN) ||
          m_pCI->E.p_Op->bIsCard(CK_o_INDEXSCAN)))
      continue;
    if (!(m_pCI->E.p_indexList = m_pCI->E.p_Op->pSub_indexList()))
      continue;

    // have <bounds> & <filter> under <main>
    if (Card_Index::nCullNonusedBounds(m_pCI))
      bOptRun = true;

  }

  m_pCI->vEndTransform(__LINE__);

  return bOptRun;
}

/*----------------------------------------------------------------------------*/
/* cull non-used bounds                                                       */
/*----------------------------------------------------------------------------*/
bool XF_Optimise_Cull_bound::bXF_Cull_Used_bound(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_XF_OPTIMISE_CULL_BOUND, _FOOTER_);

  bool bOptRun = false;

  m_pCI->vTransform(__FILE__, __func__, XF_OPTIMISE_CULL_USED_BOUND);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // mark any cmpPred that are a bound in bounds

  m_pCI->vClearRegisters();
  m_pCI->vBeginEach();
  while (m_pCI->bEachOp()) {

    if (! (m_pCI->E.p_Op->bIsCard(CK_o_INDEXJOIN) ||
           m_pCI->E.p_Op->bIsCard(CK_o_INDEXSCAN) ||
           m_pCI->E.p_Op->bIsCard(CK_o_KEYEXISTS) ||
           m_pCI->E.p_Op->bIsCard(CK_o_KEYNOTEXISTS)
    ))
      continue;
    // only the bounds of the chosen index are applied by the operator; the
    // first <bounds> found under it may belong to another index
    Card_IndexList* p_indexList = m_pCI->E.p_Op->pSub_indexList();
    Card_Index* p_index = p_indexList ? p_indexList->pFindChosenIndex() : nullptr;
    if (! p_index)
      continue;
    if (! (m_pCI->E.p_bounds = p_index->pSub_bounds()))
      continue;
    Card_IdxColList* p_idxColList = p_index->pSub_idxColList();

    if ((m_pCI->E.p_filter = m_pCI->E.p_Op->pSub_filter())) {

      // have <bounds> & <filter> under <main>

      CardIter* pCI = m_pCI;
      auto bfDerive = [pCI, p_idxColList](Card* p_card) {
        Card_RlnConj* p_rlnConj = _pCard_RlnConj(p_card);
        if (p_rlnConj->bSubsetOf_bounds(pCI->E.p_bounds, p_idxColList) &&
            ! p_rlnConj->pSymbol()->Y_bHas(P_CrlKeyTest)) {
          //RASSERT(m_pCI->G.p_rlnConj->bIsJoinConj());
          p_rlnConj->vMark1(MARK_AT);
        }

        return C_CONTINUE_RECURSION;
      };

      m_pCI->E.p_filter->bLambdaWalk(CK_RLNCONJ, bfDerive, pCI);

      continue;
    }

    if ((m_pCI->E.p_constraint = m_pCI->E.p_Op->pSub_constraint())) {

      // have <bounds> & <filter> under <main>

      CardIter* pCI = m_pCI;
      auto bfDerive = [pCI, p_idxColList](Card* p_card) {
        Card_RlnConj* p_rlnConj = _pCard_RlnConj(p_card);
        if (p_rlnConj->bSubsetOf_bounds(pCI->E.p_bounds, p_idxColList))
          p_rlnConj->vMark1(MARK_AT);

        return C_CONTINUE_RECURSION;
      };
      m_pCI->E.p_constraint->bLambdaWalk(CK_RLNCONJ, bfDerive, pCI);

      continue;
    }

  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // cull marked cmpPred from filter

  m_pCI->vClearRegisters();
  m_pCI->vBeginEach();

  i8 nCulled = 0;
  while (m_pCI->bEachOp()) {
    if ((m_pCI->E.p_filter = m_pCI->E.p_Op->pSub_filter()))
      nCulled += m_pCI->E.p_filter->nCullMarked_rlnConj(MARK_AT);
    if ((m_pCI->E.p_constraint = m_pCI->E.p_Op->pSub_constraint())) {

      // A key lookup keeps a comparison in its constraint. When every
      // comparison is also a bound (a sub-query whose only predicate is its
      // correlation: "exists (select * from orders where o_custkey =
      // c_custkey)", or IN without a predicate), culling them all left an
      // empty predicate, which crashed XF_Epilog_Type_Cast. Keep one, and
      // restate it from the chosen bound: <index column> == <bound value>.
      // The lookup then checks again what the bound already selected. The
      // comparison itself cannot be kept as it is: one that was morphed
      // from IN or NOT IN names its outer side by a position ($1) that is
      // not that of the outer column.
      if (m_pCI->E.p_Op->bIsCard(CK_o_KEYEXISTS) || m_pCI->E.p_Op->bIsCard(CK_o_KEYNOTEXISTS)) {
        i8 nRlnConj = 0;
        i8 nMarked  = 0;
        Card* p_conj_keep = nullptr;
        auto bfCount = [&nRlnConj, &nMarked, &p_conj_keep](Card* p_card) {
          nRlnConj++;
          if (p_card->bMark1(MARK_AT)) {
            nMarked++;
            if (! p_conj_keep)
              p_conj_keep = p_card;
          }
          return C_CONTINUE_RECURSION;
        };
        m_pCI->E.p_constraint->bLambdaWalk(CK_RLNCONJ, bfCount, m_pCI);

        Card_IndexList* p_indexList = m_pCI->E.p_Op->pSub_indexList();
        Card_Index*     p_index     = p_indexList ? p_indexList->pFindChosenIndex() : nullptr;
        Card_Bound*     p_bound     = p_index && p_index->pSub_bounds()
                                        ? p_index->pSub_bounds()->pFindChosenBound() : nullptr;

        if (nRlnConj && nRlnConj == nMarked && p_bound && p_bound->bIsEBound() &&
            p_bound->pGet_idxCol() && p_bound->pGet_IfNot_idxCol()) {

          Card_IdxCol* p_idxCol = p_index->pSub_idxColList()->pGetIdxCol_byIdxCol(
                                    p_bound->pGet_idxCol()->szFacet(SF_IDX_COL));
          RASSERT(p_idxCol);

          while (p_conj_keep->nChildCards()) {
            Card* p_child = p_conj_keep->pGetChild(0);
            p_conj_keep->vRemoveChild(p_child);
            DELETE_CARD(p_child);
          }
          Card_Term* p_term_idxCol = _pCard_Term((new Card_Term)->SYMPPY(p_idxCol->pSymbol()));
          Card_EXPRLIST* p_injectable = m_pCI->E.p_Op->pSub_injectable();
          Card_Expr* p_expr_tblCol = p_injectable ? p_injectable->pFind_expr(p_idxCol->pSymbol()) : nullptr;
          if (p_expr_tblCol)
            p_term_idxCol->vApplyCast_x(p_expr_tblCol->xCast());
          else
            p_term_idxCol->vApplyCast_p(p_bound->pGet_IfNot_idxCol());
          p_conj_keep->vAppendChild(p_term_idxCol);
          p_conj_keep->vAppendChild(p_bound->pGet_IfNot_idxCol()->pClone());

          if (p_conj_keep->pSymbol()->bHasFacet(SF_SQY_OP))
            p_conj_keep->pSymbol()->vDeleteFacet(SF_SQY_OP);
          if (! p_conj_keep->pSymbol()->bHasFacet(SF_RLN_OP))
            p_conj_keep->pSymbol()->vApplyFacet(SF_RLN_OP, "=="_Stz);
          p_conj_keep->vMark1(MARK_BLANK);
        }
      }

      nCulled += m_pCI->E.p_constraint->nCullMarked_rlnConj(MARK_AT);
    }
  }
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  if (nCulled)
    bOptRun = true;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // delete any filter if empty

  m_pCI->vClearRegisters();
  m_pCI->vBeginEach();
  Card_Filter::vDeleteEmptyFilters(m_pCI);

  m_pCI->vEndTransform(__LINE__);

  return bOptRun;
}


/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
