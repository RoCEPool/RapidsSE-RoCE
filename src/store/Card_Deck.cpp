/*============================================================================*/
/* Card_Deck.cpp -- Board                                                    */
/*============================================================================*/

#include "Card_Deck.hpp"
#include "AM.hpp"
#include "Card_COLUMN.hpp"
#include "Card_TablePtn.hpp"
#include "Card_Cast.hpp"
#include "Card_Emit.hpp"
#include "Card_Expr.hpp"
#include "Card_Key.hpp"
#include "Card_Level.hpp"
#include "Card_StreamPtn.hpp"

#include "Card_OP.hpp"
#include "CardIter.hpp"
#include "Grid.hpp"
#include "MY.hpp"
#include "Error.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE, Card_Deck, CK_DECK)
__aNewFunction(Card_Deck, CN_CARD)
__vDeleteFunction(Card_Deck, CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_Deck::Card_Deck(void)
: Card_symbol(CK_DECK, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);

  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_Deck::~Card_Deck(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* add next casts                                                             */
/*----------------------------------------------------------------------------*/
void Card_Deck::vAddStageCasts(CardIter* pCI)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  Card_OP*         p_OP_prev          = nullptr;
  Card_Project*    p_project_prior    = nullptr;
  Card_Emit* p_arriving = nullptr;
  Card_Cast*       p_cast_prior       = nullptr;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  eXK eStageCast;

  Card_StreamPtn* p_streamPtn = new Card_StreamPtn;

  pCI->vBeginEach();
  while (pCI->bEachOp()) {

    if (CK_o_INDEXJOIN == pCI->E.e_Op ||
        CK_o_INDEXSCAN == pCI->E.e_Op ||
        CK_o_KEYEXISTS == pCI->E.e_Op ||
        CK_o_KEYNOTEXISTS == pCI->E.e_Op ||
        CK_o_CROSSJOIN == pCI->E.e_Op ||
        CK_o_SCAN == pCI->E.e_Op) {

      if (! p_OP_prev) {
        Card_Table* p_table_first = pCI->E.p_Op->pSub_table();
        //p_streamPtn->vAdd_streamKey(p_table_first->pSub_tablePtn()->pSymbol());
        pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_table_first->pSub_tablePtn()->pSymbol());
      }
      else {
        eStageCast = pCI->E.p_deck->eAddStageCast_toPrev_cast(pCI,
                                                              p_streamPtn,
                                                              p_project_prior,
                                                              p_cast_prior,
                                                              p_arriving,
                                                              pCI->E.p_Op);
        UNUSED(eStageCast);
        pCI->vRepositionOp();
       // pCI->PRINTBOARD_dbg();
        vDebugPoint();
      }

      p_OP_prev = pCI->E.p_Op;
      p_project_prior  = nullptr;
      p_cast_prior     = nullptr;
    }
    else if (CK_o_PROJECT == pCI->E.e_Op) {
      p_project_prior  = pCI->E.p_project;
      p_cast_prior     = nullptr;
    }
    else if (CK_o_CAST == pCI->E.e_Op) {
      p_cast_prior = pCI->E.p_cast;
    }

    // -- -- -- -- --
    if (pCI->E.p_Op->pSub_emit())
      p_arriving = pCI->E.p_Op->pSub_emit();
  }

  delete p_streamPtn;

 // pCI->PRINTBOARD_dbg();
  vDebugPoint();
}

/*----------------------------------------------------------------------------*/
/* local aggregation when the stream is partitioned on a group-by column      */
/*----------------------------------------------------------------------------*/
// A GROUP BY runs in three steps: each partition groups its own rows, each
// node merges the groups of its partitions, and the origin merges those of
// the nodes. When the stream is partitioned on a column it groups by, all the
// rows of a group are in one partition, so the groups of the partitions are
// already final and the two merges only cost time (Q18 groups lineitem on
// l_orderkey, its partition column, and merged 1.5M groups twice; Q16 groups
// on ps_suppkey after the stream is hash cast on it). DuckDB skips the merge
// the same way when the data is partitioned on the grouping keys
// (PhysicalPartitionedAggregate).
//
// This drops the node and origin merges, and lets the operators that work row
// by row on the groups (HAVING, a filter, a projection) run in the partitions
// too, so only the groups that pass leave them. The last operator of the deck
// stays at the origin, where the result is gathered. Job tag
// /optimiser_off_local_gbyagg leaves the plan as it was.

// the facets that make up a term of an emitted or key column
namespace
{
  struct ColShape
  {
    Stz* szAggFn = nullptr;   // aggregate function, if any
    i8   nAggFn  = 0;
    i8   nExpCol = 0;         // $n
    i8   iExpCol = 0;
    i8   nKeyCol = 0;         // $Kn
    i8   nOther  = 0;         // anything else but parentheses
  };

  bool bShapeOf(Card* p_card, ColShape& rxShape)
  {
    if (! p_card || CK_EXPR != p_card->eCard())
      return false;
    Card_Expr* p_expr = _pCard_Expr(p_card);
    for (i8 i = 0; i < p_expr->nChildCards(); i++) {
      Card* p_child = p_expr->pGetChild(i);
      if (CK_TERM != p_child->eCard())
        return false;
      Symbol* pSym = p_child->pSymbol();
      if (pSym->bHasFacet(SF_AGG_FN)) {
        rxShape.szAggFn = pSym->szFacet(SF_AGG_FN);
        rxShape.nAggFn++;
      }
      else if (pSym->bHasFacet(SF_EXP_COL)) {
        rxShape.iExpCol = pSym->iTo_ordinal(SF_EXP_COL);
        rxShape.nExpCol++;
      }
      else if (pSym->bHasFacet(SF_KEY_COL))
        rxShape.nKeyCol++;
      else if (! pSym->bHasFacet(SF_PARENTHESIS))
        rxShape.nOther++;
    }
    return true;
  }

  // Is p_super a merge of the groups of p_part, column by column: the same
  // keys, and each aggregate of p_part merged by the matching function on the
  // same column (SUM of a SUM or COUNT, MIN of a MIN, MAX of a MAX)? Anything
  // else (an AVG put together from a SUM and a COUNT, say) is left alone.
  bool bMergesGroupsOf(Card_OP* p_super, Card_OP* p_part)
  {
    Card_Emit* p_emit_part  = p_part->pSub_emit();
    Card_Emit* p_emit_super = p_super->pSub_emit();
    Card_Key*  p_key_part   = p_part->pSub_key();
    Card_Key*  p_key_super  = p_super->pSub_key();
    if (! p_emit_part || ! p_emit_super || ! p_key_part || ! p_key_super)
      return false;
    if (p_emit_part->nChildCards() != p_emit_super->nChildCards() ||
        p_key_part->nChildCards()  != p_key_super->nChildCards())
      return false;

    for (i8 i = 0; i < p_emit_super->nChildCards(); i++) {
      ColShape xPart, xSuper;
      if (! bShapeOf(p_emit_part->pGetChild(i), xPart) ||
          ! bShapeOf(p_emit_super->pGetChild(i), xSuper))
        return false;

      if (0 == xSuper.nAggFn) {
        // a key column on both
        if (1 != xSuper.nKeyCol || xSuper.nExpCol || xSuper.nOther ||
            1 != xPart.nKeyCol  || xPart.nAggFn  || xPart.nOther)
          return false;
        continue;
      }

      if (1 != xSuper.nAggFn || 1 != xSuper.nExpCol || xSuper.iExpCol != i + 1 ||
          xSuper.nKeyCol || xSuper.nOther || 1 != xPart.nAggFn)
        return false;

      bool bMatch =
        (xSuper.szAggFn->bSame("SUM"_Stz) && (xPart.szAggFn->bSame("SUM"_Stz) ||
                                               xPart.szAggFn->bSame("COUNT"_Stz) ||
                                               xPart.szAggFn->bSame("COUNTALL"_Stz))) ||
        (xSuper.szAggFn->bSame("MIN"_Stz) && xPart.szAggFn->bSame("MIN"_Stz)) ||
        (xSuper.szAggFn->bSame("MAX"_Stz) && xPart.szAggFn->bSame("MAX"_Stz));
      if (! bMatch)
        return false;
    }
    return true;
  }

  eAL eLevelOf(Card* p_OP)
  {
    if (Card_Level* p_level = p_OP->pSub_level())
      return (eAL) p_level->pSymbol()->Y_iGet(P_Level);
    return AL_NONE;
  }

  // the op after i_Main, or C_UNKNOWN
  i8 iNextOp(Card_Deck* p_deck, i8 i_Main)
  {
    for (i8 i = i_Main + 1; i < p_deck->nChildCards(); i++)
      if (p_deck->pGetChild(i)->bIsOp())
        return i;
    return C_UNKNOWN;
  }
}

bool Card_Deck::bLocalise_gbyAgg(CardIter* pCI)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  if (AM::PTNMAP::nPtnsInGrid() <= 1)
    return false;

  // The columns the stream is partitioned on, as the stage casts will make it
  // (vAddStageCasts): the first table's partition column, then after each
  // join the partition column of the joined table, and on a hash cast the
  // column it is hashed on, which has the same values.
  Card_StreamPtn* p_streamPtn = new Card_StreamPtn;
  bool bStarted = false;
  bool bDone = false;

  for (i8 i_Main = 0; i_Main < nChildCards() && ! bDone; i_Main++) {
    Card* p_main = pGetChild(i_Main);
    if (! p_main->bIsOp())
      continue;
    Card_OP* p_OP = (Card_OP*) p_main;
    eCK e_main = p_OP->eCard();

    // the stream has left the partitions
    if (AL_PARTITION != eLevelOf(p_OP))
      break;

    switch (e_main) {

      case CK_o_SCAN:
      case CK_o_INDEXSCAN:
      case CK_o_INDEXJOIN:
      case CK_o_KEYEXISTS:
      case CK_o_KEYNOTEXISTS:
      case CK_o_CROSSJOIN: {
        Card_TablePtn* p_tablePtn = p_OP->pSub_tablePtn();
        if (! p_tablePtn) {
          bDone = true;
          break;
        }
        Symbol* p_Y_tablePtn = p_tablePtn->pSymbol();

        if (! bStarted) {
          if (Card_COLUMN::bIsReplicated(p_Y_tablePtn)) {
            bDone = true;
            break;
          }
          pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_Y_tablePtn);
          bStarted = true;
          break;
        }

        if (CK_o_CROSSJOIN == e_main) {
          if (! Card_COLUMN::bIsReplicated(p_Y_tablePtn)) {
            p_streamPtn->vDeleteAllChildren();
            pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_Y_tablePtn);
          }
          break;
        }

        Card_IndexList* p_indexList = p_OP->pSub_indexList();
        Card_Index* p_index = p_indexList ? p_indexList->pFindChosenIndex() : nullptr;
        Card_Bound* p_bound = (p_index && p_index->pSub_bounds())
                                ? p_index->pSub_bounds()->pSub_bound() : nullptr;
        if (! p_bound || ! p_bound->pGet_idxCol() || ! p_index->pSub_idxColList()) {
          bDone = true;
          break;
        }
        Card_IdxCol* p_idxCol = p_index->pSub_idxColList()->pGetIdxCol_byIdxCol(
                                  p_bound->pGet_idxCol()->szFacet(SF_IDX_COL));
        Symbol* p_Y_tblCol_prior = p_bound->pGet_tblCol()
                                     ? p_bound->pGet_tblCol()->pSymbol() : nullptr;
        if (! p_idxCol) {
          bDone = true;
          break;
        }

        eXK eCast = Card_Cast::eStageCast(pCI, p_streamPtn, p_Y_tblCol_prior,
                                          p_Y_tablePtn, p_idxCol->pSymbol());
        if (XK_HASHCAST == eCast && p_Y_tblCol_prior &&
            ! Card_COLUMN::bIsReplicated(p_Y_tblCol_prior))
          pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_Y_tblCol_prior);
        break;
      }

      case CK_o_PROJECT:
      case CK_o_WHERE:
      case CK_o_FILTER:
      case CK_o_HASH:
      case CK_o_INFLATE:
      case CK_o_SORT:
        break;

      case CK_o_GBYAGG: {
        bDone = true;
        if (! bStarted)
          break;

        // grouped on a column the stream is partitioned on?
        bool bOnPtnCol = false;
        if (Card_Key* p_key = p_OP->pSub_key()) {
          for (i8 i = 0; i < p_key->nChildCards() && ! bOnPtnCol; i++) {
            Card* p_child = p_key->pGetChild(i);
            if (CK_EXPR != p_child->eCard())
              continue;
            Card_Expr* p_expr = _pCard_Expr(p_child);
            if (1 != p_expr->nChildCards())
              continue;
            Symbol* pSym = p_expr->pGet_Symbol(0);
            if (pSym && pSym->bHasFacet(SF_TBL_COL) &&
                ! Card_COLUMN::bIsReplicated(pSym) &&
                p_streamPtn->bHasSymbolRecursive(CK_p_STREAMKEY, pSym))
              bOnPtnCol = true;
          }
        }
        if (! bOnPtnCol)
          break;

        // followed by the node and origin merges of its groups?
        i8 i_node   = iNextOp(this, i_Main);
        i8 i_origin = C_UNKNOWN == i_node ? C_UNKNOWN : iNextOp(this, i_node);
        if (C_UNKNOWN == i_origin)
          break;
        Card_OP* p_node   = (Card_OP*) pGetChild(i_node);
        Card_OP* p_origin = (Card_OP*) pGetChild(i_origin);
        if (CK_o_GBYAGG != p_node->eCard()   || AL_NODE   != eLevelOf(p_node) ||
            CK_o_GBYAGG != p_origin->eCard() || AL_ORIGIN != eLevelOf(p_origin) ||
            ! bMergesGroupsOf(p_node, p_OP) || ! bMergesGroupsOf(p_origin, p_OP))
          break;

        // drop the merges
        vRemoveChild(p_origin);
        delete p_origin;
        vRemoveChild(p_node);
        delete p_node;

        // the row-by-row operators on the groups run in the partitions too
        for (i8 i = iNextOp(this, i_Main); C_UNKNOWN != i; i = iNextOp(this, i)) {
          Card_OP* p_next = (Card_OP*) pGetChild(i);
          eCK e_next = p_next->eCard();
          if (AL_ORIGIN != eLevelOf(p_next) ||
              (CK_o_HAVING != e_next && CK_o_WHERE != e_next &&
               CK_o_FILTER != e_next && CK_o_PROJECT != e_next) ||
              C_UNKNOWN == iNextOp(this, i))
            break;
          p_next->pSub_level()->pSymbol()->Y_vSet(P_Level, AL_PARTITION);
        }

        delete p_streamPtn;
        return true;
      }

      default:
        bDone = true;
        break;
    }
  }

  delete p_streamPtn;
  return false;
}

/*----------------------------------------------------------------------------*/
/* A GROUP BY that runs in the partitions is merged twice: on each node (one  */
/* row per group leaves the node instead of one per group and partition) and  */
/* at the origin. On a cluster of one node the node merge saves no traffic,   */
/* and the origin merge repeats it on the same groups (TPC-H Q10: 38,000      */
/* groups with long string keys were merged, written out and read twice).     */
/* The node merge is dropped there; the origin merges the partitions. Only    */
/* when both merge the groups column by column (bMergesGroupsOf), so that the */
/* origin reads the rows of the partitions as it read those of the node.      */
/* Optimisation name: single_node_gbyagg.                                     */
/*----------------------------------------------------------------------------*/
bool Card_Deck::bDropNodeMerge_gbyAgg(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  if (AM::GRID::nKnownNodes() > 1)
    return false;

  bool bDropped = false;
  for (i8 i_Main = 0; i_Main < nChildCards(); i_Main++) {
    Card* p_main = pGetChild(i_Main);
    if (! p_main->bIsOp() || CK_o_GBYAGG != p_main->eCard() || AL_PARTITION != eLevelOf(p_main))
      continue;
    Card_OP* p_OP = (Card_OP*) p_main;
    i8 i_node   = iNextOp(this, i_Main);
    i8 i_origin = C_UNKNOWN == i_node ? C_UNKNOWN : iNextOp(this, i_node);
    if (C_UNKNOWN == i_origin)
      continue;
    Card_OP* p_node   = (Card_OP*) pGetChild(i_node);
    Card_OP* p_origin = (Card_OP*) pGetChild(i_origin);
    if (CK_o_GBYAGG != p_node->eCard()   || AL_NODE   != eLevelOf(p_node) ||
        CK_o_GBYAGG != p_origin->eCard() || AL_ORIGIN != eLevelOf(p_origin) ||
        ! bMergesGroupsOf(p_node, p_OP) || ! bMergesGroupsOf(p_origin, p_OP))
      continue;
    vRemoveChild(p_node);
    delete p_node;
    bDropped = true;
  }
  return bDropped;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
eXK Card_Deck::eAddStageCast_toPrev_cast(CardIter*        pCI,
                                         Card_StreamPtn*  p_streamPtn,
                                         Card_Project*    p_project_prev,
                                         Card_Cast*       p_cast_prev,
                                         Card_Emit* p_emitted_prev,
                                         Card_OP*         p_OP_curr)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  UNUSED(p_project_prev);

  RASSERT(CK_o_INDEXJOIN == p_OP_curr->eCard() ||
          CK_o_INDEXSCAN == p_OP_curr->eCard() ||
          CK_o_KEYEXISTS == p_OP_curr->eCard() ||
          CK_o_KEYNOTEXISTS == p_OP_curr->eCard() ||
          CK_o_CROSSJOIN == p_OP_curr->eCard() ||
          CK_o_SCAN == p_OP_curr->eCard());

  RASSERT(p_streamPtn);

  // A cross join (a join without an index on its column, read in full) pairs
  // every arriving row with every row of the table: each arriving row must
  // reach every partition, unless the table is replicated. It had no stage
  // cast, so each partition paired only its own rows (rows were lost when the
  // join order put such a join after the first table).
  if (CK_o_CROSSJOIN == p_OP_curr->eCard()) {
    Card_TablePtn* p_tablePtn_cross = p_OP_curr->pSub_tablePtn();
    bool bReplicated = p_tablePtn_cross &&
                       Card_COLUMN::bIsReplicated(p_tablePtn_cross->pSymbol());
    if (bReplicated || AM::PTNMAP::nPtnsInGrid() <= 1 || ! p_cast_prev) {
      if (p_tablePtn_cross)
        pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_tablePtn_cross->pSymbol());
      return XK_UNICAST;
    }
    p_cast_prev->SYM(SF_STG_CAST,"broadcast"_Stz);
    p_streamPtn->vDeleteAllChildren();
    if (p_tablePtn_cross)
      pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_tablePtn_cross->pSymbol());
    return XK_BROADCAST;
  }

  //Card_Table*      p_table_join      = p_OP_curr->pSub_table();
  //Card_From*       p_from_join       = p_OP_curr->pSub_from();
  Card_TablePtn*   p_tablePtn_join   = p_OP_curr->pSub_tablePtn();
  Card_IndexList*  p_indexList_join  = p_OP_curr->pSub_indexList();
  Card_Index*      p_index_join      = p_indexList_join->pFindChosenIndex();
  Card_IdxColList* p_idxColList_join = p_index_join->pSub_idxColList();
  Card_Bounds*     p_bounds_join     = p_index_join->pSub_bounds();
  Card_Bound*      p_bound_join      = p_bounds_join->pSub_bound();

  Card_Term* p_term_tblCol_hash      = p_bound_join->pGet_tblCol();
  Card_Term* p_term_idxCol_join   = p_bound_join->pGet_idxCol();

  Card_IdxCol* p_idxCol_join =
      p_idxColList_join->pGetIdxCol_byIdxCol(p_term_idxCol_join->szFacet(SF_IDX_COL));
  Symbol* p_Y_idxCol_join = p_idxCol_join->pSymbol();

  Symbol* p_Y_tblCol_prior = p_bound_join->pGet_tblCol()
                               ? p_bound_join->pGet_tblCol()->pSymbol()
                               : nullptr;

  Symbol* p_Y_tablePtn_join = p_tablePtn_join->pSymbol();

  eXK eStageCast = Card_Cast::eStageCast(pCI,
                                         p_streamPtn,
                                         p_Y_tblCol_prior,
                                         p_Y_tablePtn_join,
                                         p_Y_idxCol_join);
  Card_Hash* p_hash_prior_new;
  switch (eStageCast) {

    // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    case XK_BROADCAST:
      // A broadcast sends each arriving row to every partition of the table,
      // and each partition answers for the rows it holds. That is right for
      // an inner join. It is wrong for the operators below: whether a row
      // has a match, or none, is a fact about all partitions together.
      //   keyExists    (IN, EXISTS)         one result row per partition
      //                                     that has a match
      //   keyNotExists (NOT IN, NOT EXISTS) one result row per partition
      //                                     that has no match
      //   left outer join                   one null-extended row per
      //                                     partition that has no match
      // These need the table to be partitioned on the column that is looked
      // up (PARTITION BY), so that one partition holds all matches. Refuse
      // the query instead of returning a wrong result.
      if (AM::PTNMAP::nPtnsInGrid() > 1) {
        bool bOuterJoin = CK_o_INDEXJOIN == p_OP_curr->eCard() && p_OP_curr->pSub_outerJoin();
        if (CK_o_KEYEXISTS    == p_OP_curr->eCard() ||
            CK_o_KEYNOTEXISTS == p_OP_curr->eCard() ||
            bOuterJoin) {
          Error xError(EC_NOT_IMPLEMENTED,
                       bOuterJoin
                         ? "an outer join needs its inner table to be partitioned on the join column"_Stz
                         : "IN, EXISTS and their negations over one table need that table to be partitioned on the column that is compared"_Stz,
                       (Stz*) __FILE__, __LINE__);
          throw xError;
        }
      }

      p_cast_prev->SYM(SF_STG_CAST,"broadcast"_Stz);

      break;

    // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    case XK_HASHCAST:
      p_cast_prev->SYM(SF_STG_CAST,"hashcast"_Stz);

      pCI->E.p_expr = pCI->pBackSearch_expr_For_col(p_term_tblCol_hash->pSymbol(), pCI->E.i_Op);
      p_hash_prior_new = Card_Hash::pDerive(pCI->E.p_expr, p_emitted_prev);

      pCI->vInsertBefore(p_hash_prior_new, p_cast_prev);

      break;

    // -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
    case XK_UNICAST:
      // no change to next card; default is unicast
      break;

    default:
      NEVER_REACH
  }

  return eStageCast;
}

/*----------------------------------------------------------------------------*/
/* find i_fold (reverse)                                                      */
/*----------------------------------------------------------------------------*/
Card_Expr* Card_Deck::p_expr_fold(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_Emit* p_emitted_fold = this->p_emit_fold())
    return p_emitted_fold->pSub_expr();

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/* find i_fold (reverse)                                                      */
/*----------------------------------------------------------------------------*/
i8 Card_Deck::i_Fold(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  i8 nChildren = nChildCards();
  for (i8 i_Main = nChildren - 1; i_Main >= 0; i_Main--) {
    Card *pMain = pGetChild(i_Main);
    if (pMain->bIsCard(CK_o_FOLD))
      return i_Main;
  }

  return C_UNKNOWN;
}

/*----------------------------------------------------------------------------*/
/* find p_fold (reverse)                                                      */
/*----------------------------------------------------------------------------*/
Card_Fold* Card_Deck::p_fold(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  i8 nChildren = nChildCards();
  for (i8 i_Main = nChildren - 1; i_Main >= 0; i_Main--) {
    Card *pMain = pGetChild(i_Main);
    if (pMain->bIsCard(CK_o_FOLD))
      return _pCard_Fold(pMain);
  }

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/* find emit prior to fold (reverse)                                          */
/*----------------------------------------------------------------------------*/
Card_Emit* Card_Deck::p_emit_fold(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  i8 i_Fold = this->i_Fold();
  for (i8 i_Main = i_Fold - 1; i_Main >= 0; i_Main--) {
    Card *pMain = pGetChild(i_Main);
    if (Card_Emit* p_emit = pMain->pSub_emit())
      return p_emit;
  }

  return nullptr;
}


/*----------------------------------------------------------------------------*/
/* validate children                                                          */
/*----------------------------------------------------------------------------*/
void Card_Deck::vValidate(CardIter* pCI)
{
  _precondition_(_MAGIC_, _FOOTER_);

  //vMarksTestRecursive();

  vMarksClearRecursive();
  vMark1Recursive(MARK_UNVALID);

  RASSERT(0 == pCI->nAncestors());

  for (Kid xDeck(this); xDeck.bEach(); /**/) {

    Card* pChild = xDeck.pChild();

    eCK eChild = pChild->eCard();

    pCI->vMember(eChild,pChild);

    switch (eChild) {

      case CK_o_ABSJOIN:      pCI->E.p_absJoin->vValidate(pCI, 1, this); break;
      case CK_o_AGGREG:       pCI->E.p_aggreg->vValidate(pCI, 1, this); break;
      case CK_ANDCONJ:      pCI->E.p_andConj->vValidate(pCI, 1, this); break;

      case CK_PREDICATE:     pCI->E.p_predicate->vValidate(pCI, 1, this); break;
      case CK_BOUND:        pCI->E.p_bound->vValidate(pCI, 1, this); break;
      case CK_BOUNDS:       pCI->E.p_bounds->vValidate(pCI, 1, this); break;

      case CK_o_CALL:         pCI->E.p_call->vValidate(pCI, 1, this); break;
      case CK_o_CAST:         pCI->E.p_cast->vValidate(pCI, 1, this); break;

      case CK_p_CONSTRAINT:   pCI->E.p_constraint->vValidate(pCI, 1, this); break;
      case CK_CORRCOLS:     pCI->E.p_corrCols->vValidate(pCI, 1, this); break;
      case CK_o_CORRELATION:  pCI->E.p_correlation->vValidate(pCI, 1, this); break;
      case CK_COSTJOINING:  pCI->E.p_costJoining->vValidate(pCI, 1, this); break;
      case CK_COSTNETWORK:  pCI->E.p_costNetwork->vValidate(pCI, 1, this); break;
      case CK_COSTSTREAM:   pCI->E.p_costQuery->vValidate(pCI, 1, this); break;
      case CK_COSTUNICAST:  pCI->E.p_costUnicast->vValidate(pCI, 1, this); break;
      case CK_COUNT:        pCI->E.p_count->vValidate(pCI, 1, this); break;
      case CK_o_CROSSJOIN:    pCI->E.p_crossJoin->vValidate(pCI, 1, this); break;

      case CK_p_EMIT:         pCI->E.p_emit->vValidate(pCI, 1, this); break;
      case CK_EXPR:         pCI->E.p_expr->vValidate(pCI, 1, this); break;

      case CK_o_FILTER:       pCI->E.p_filter->vValidate(pCI, 1, this); break;
      case CK_FILTPRED:     pCI->E.p_filtPred->vValidate(pCI, 1, this); break;
      case CK_FILTPREDLIST: pCI->E.p_filtPredList->vValidate(pCI, 1, this); break;
      case CK_o_FOLD:         pCI->E.p_fold->vValidate(pCI, 1, this); break;
      case CK_o_FROM:         pCI->E.p_from->vValidate(pCI, 1, this); break;

      case CK_o_GBYAGG:       pCI->E.p_gbyAgg->vValidate(pCI, 1, this); break;

      case CK_o_HASH:         pCI->E.p_hash->vValidate(pCI, 1, this); break;
      case CK_o_HAVING:       pCI->E.p_having->vValidate(pCI, 1, this); break;
      case CK_HSHCOL:       pCI->E.p_hshCol->vValidate(pCI, 1, this); break;

      case CK_IDXCOL:       pCI->E.p_idxCol->vValidate(pCI, 1, this); break;
      case CK_IDXCOLLIST:   pCI->E.p_idxColList->vValidate(pCI, 1, this); break;
      case CK_INDEX:        pCI->E.p_index->vValidate(pCI, 1, this); break;
      case CK_o_INDEXJOIN:    pCI->E.p_indexJoin->vValidate(pCI, 1, this); break;
      case CK_INDEXLIST:    pCI->E.p_indexList->vValidate(pCI, 1, this); break;
      case CK_INDEXNM:      pCI->E.p_indexNm->vValidate(pCI, 1, this); break;
      case CK_o_INDEXSCAN:    pCI->E.p_indexScan->vValidate(pCI, 1, this); break;
      case CK_o_INFLATE:      pCI->E.p_inflate->vValidate(pCI, 1, this); break;
      case CK_p_INJECTABLE:     pCI->E.p_injectable->vValidate(pCI, 1, this); break;

      case CK_JOINCAST:     pCI->E.p_joinCast->vValidate(pCI, 1, this); break;
      case CK_JOINESTIMATE: pCI->E.p_joinEstimate->vValidate(pCI, 1, this); break;
      case CK_JOINORDER:    pCI->E.p_joinOrder->vValidate(pCI, 1, this); break;
      case CK_JOINORDERING: pCI->E.p_joinOrdering->vValidate(pCI, 1, this); break;
      case CK_JOINPREDLIST:   pCI->E.p_joinPredList->vValidate(pCI, 1, this); break;

      case CK_p_KEY:          pCI->E.p_key->vValidate(pCI, 1, this); break;
      case CK_KEYCELLS:       pCI->E.p_keyCells->vValidate(pCI, 1, this); break;
      case CK_o_KEYEXISTS:    pCI->E.p_keyExists->vValidate(pCI, 1, this); break;
      case CK_o_KEYNOTEXISTS: pCI->E.p_keyNotExists->vValidate(pCI, 1, this); break;

      case CK_p_LEVEL:        pCI->E.p_level->vValidate(pCI, 1, this); break;
      case CK_o_LIMIT:        pCI->E.p_limit->vValidate(pCI, 1, this); break;

      case CK_MAP:            pCI->E.p_map->vValidate(pCI, 1, this); break;
      case CK_p_MAPPING:      pCI->E.p_mapping->vValidate(pCI, 1, this); break;

      case CK_p_NEEDED:       pCI->E.p_needed->vValidate(pCI, 1, this); break;
      case CK_NOTE:           pCI->E.p_note->vValidate(pCI, 1, this); break;
      case CK_NOTCONJ:        pCI->E.p_notConj->vValidate(pCI, 1, this); break;

      case CK_ON:             pCI->E.p_on->vValidate(pCI, 1, this); break;
      case CK_o_ORDAGG:       pCI->E.p_ordAgg->vValidate(pCI, 1, this); break;
      case CK_ORCONJ:         pCI->E.p_orConj->vValidate(pCI, 1, this); break;
      case CK_OUTERCOLNM:     pCI->E.p_outerColNm->vValidate(pCI, 1, this); break;
      case CK_OUTERJOIN:      pCI->E.p_outerJoin->vValidate(pCI, 1, this); break;

      case CK_PAYLOADCELLS:   pCI->E.p_payloadCells->vValidate(pCI, 1, this); break;
      case CK_o_PDSROW:       pCI->E.p_pdsRow->vValidate(pCI, 1, this); break;
      case CK_o_PROJECT:      pCI->E.p_project->vValidate(pCI, 1, this); break;

      case CK_RLNCONJ:        pCI->E.p_rlnConj->vValidate(pCI, 1, this); break;
      case CK_ROWCOUNT:       pCI->E.p_rowCount->vValidate(pCI, 1, this); break;
      case CK_ROWSIZE:        pCI->E.p_rowSize->vValidate(pCI, 1, this); break;

      case CK_o_SCAN:         pCI->E.p_scan->vValidate(pCI, 1, this); break;
      case CK_SECTION:        pCI->E.p_section->vValidate(pCI, 1, this); break;
      case CK_o_SEND:         pCI->E.p_send->vValidate(pCI, 1, this); break;
      case CK_o_SORT:         pCI->E.p_sort->vValidate(pCI, 1, this); break;
      case CK_o_STAGE:        pCI->E.p_stage->vValidate(pCI, 1, this); break;
      case CK_STREAMHEAD:     pCI->E.p_streamHead->vValidate(pCI, 1, this); break;
      case CK_STREAMJOIN:     pCI->E.p_streamJoin->vValidate(pCI, 1, this); break;
      case CK_p_STREAMKEY:    pCI->E.p_streamKey->vValidate(pCI, 1, this); break;
      case CK_STREAMPER:      pCI->E.p_streamPer->vValidate(pCI, 1, this); break;
      case CK_STREAMPTN:      pCI->E.p_streamPtn->vValidate(pCI, 1, this); break;
      case CK_STREAMSET:      pCI->E.p_streamSet->vValidate(pCI, 1, this); break;
      case CK_SUBQUERY:       pCI->E.p_subQuery->vValidate(pCI, 1, this); break;
      case CK_SUBSET:         pCI->E.p_subSet->vValidate(pCI, 1, this); break;

      case CK_p_TABLE:        pCI->E.p_table->vValidate(pCI, 1, this); break;
      case CK_TABLELIST:      pCI->E.p_tableList->vValidate(pCI, 1, this); break;
      case CK_TABLENM:        pCI->E.p_tableNm->vValidate(pCI, 1, this); break;
      case CK_TABLEPTN:       pCI->E.p_tablePtn->vValidate(pCI, 1, this); break;
      case CK_TABLESIZE:      pCI->E.p_tableSize->vValidate(pCI, 1, this); break;
      case CK_TERM:           pCI->E.p_term->vValidate(pCI, 1, this); break;
      case CK_o_TOP:          pCI->E.p_top->vValidate(pCI, 1, this); break;
      case CK_TOPOLOGY:       pCI->E.p_topology->vValidate(pCI, 1, this); break;

      case CK_UNIQUE:         pCI->E.p_unique->vValidate(pCI, 1, this); break;

      case CK_o_WHERE:        pCI->E.p_where->vValidate(pCI, 1, this); break;

      default:
        NEVER_REACH
    }

  }

  RASSERT(0 == pCI->nAncestors());

  vValidationPassed();

  vMark1RecursiveIf(MARK_BLANK, MARK_VALID);

  if (Card* pCard_notValidate = pFindMark1Recursive(MARK_UNVALID)) {
   // pCI->PRINTBOARD_dbg();
    pCard_notValidate->vDump(__func__);
    vDebugPoint();
    NEVER_REACH
  }

  if (Card* pCard_notValidate = pFindMark1Recursive(MARK_DUPLICATE)) {
   // pCI->PRINTBOARD_dbg();
    pCard_notValidate->vDump(__func__);
    vDebugPoint();
    NEVER_REACH
  }

  vMarksClearRecursive();
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
