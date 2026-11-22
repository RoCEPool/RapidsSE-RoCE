/*============================================================================*/
/* Card_StreamPer.cpp -- Join Permutation Sequence                          */
/*============================================================================*/

#include "Card_StreamPer.hpp"

#include "Card_Predicate.hpp"
#include "Card_RlnConj.hpp"
#include "Card_RlnConj.hpp"
#include "Card_FiltPred.hpp"
#include "Card_FiltPredList.hpp"
#include "Card_StreamSet.hpp"
#include "Card_JoinPredList.hpp"
#include "Card_Table.hpp"
#include "Card_TableList.hpp"
#include "Card_Term.hpp"
#include "CardIter.hpp"
#include "AM.hpp"
#include "Card_RowCount.hpp"
#include "Card_Index.hpp"
#include "Card_IndexList.hpp"
#include "Card_IdxColList.hpp"
#include "Card_TablePtn.hpp"
#include "Card_COLUMN.hpp"
#include "Card_Deck.hpp"


/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE, Card_StreamPer, CK_STREAMPER)
__aNewFunction(Card_StreamPer, CN_CARD)
__vDeleteFunction(Card_StreamPer, CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_StreamPer::Card_StreamPer(void)
: Card_symbol(CK_STREAMPER, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);

  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_StreamPer::~Card_StreamPer(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

// added to the network cost of a join sequence whose intermediate result
// explodes (see bExplodes), so that it ranks after every other sequence
static const i8 C_EXPLOSION_PENALTY = (i8)1 << 50;

/*----------------------------------------------------------------------------*/
/* estimate the query cost of this join permutation sequence                  */
/*----------------------------------------------------------------------------*/
void Card_StreamPer::vEstStreamCost(CardIter* pCI,
                                    Card_TableList* p_tableList,
                                    Card_FiltPredList* p_filtPredList,
                                    Card_Deck* p_pipeline,
                                    Card_JoinPredList* p_joinPredList)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  i8 i_Permutation = pSymbol()->Y_iGet(P_Permutation);
  if (571 == i_Permutation)
    vDebugPoint();

  i8 nUnfiltBuff_prev_join  = 0;
  i8 nUnfiltBuff_producedBy_join = 0;

  i8 nSumNetworkCost = 0;
  i8 nSumUnicastCost = 0;

  i8 i_Join = 0;

  i8 nFilterPred_prev = 0;

  Card_StreamPtn* p_streamPtn = new Card_StreamPtn;

  Kid xStreamPer(this);
  while (xStreamPer.bEach()) {

    nUnfiltBuff_prev_join = nUnfiltBuff_producedBy_join;

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // stream head

    // calculate the buffers produced at the start of join sequence
    if (xStreamPer.pChild()->bIsCard(CK_STREAMHEAD)) {

      // get info of stream start
      char cTblSyn_head = xStreamPer.p_streamHead()->pSymbol()->cTblSyn();

      nUnfiltBuff_producedBy_join = p_tableList->nGetTableSize(cTblSyn_head) * C_EXPANSION_COEFF;

      nFilterPred_prev = p_filtPredList->nFiltersFor(cTblSyn_head);

      //p_streamPtn->vAdd_streamKey(p_tableList->pGetTablePtn(cTblSyn_head)->pSymbol());
      pCI->vAppendNewChild(p_streamPtn, CK_p_STREAMKEY, p_tableList->pGetTablePtn(cTblSyn_head)->pSymbol());
      continue;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // stream join

    if (!xStreamPer.pChild()->bIsCard(CK_STREAMJOIN))
      continue;

    // get info for this join
    char cTblSyn_join = xStreamPer.p_streamJoin()->pSymbol()->cTblSyn();

    eXK eJoinCast = this->eJoinCast(pCI,
                                    p_streamPtn,
                                    p_tableList,
                                    xStreamPer.p_streamJoin());

    // calculate the various costs of join sequence
    i8 nBuff_joining_with = p_tableList->nGetTableSize(cTblSyn_join);

    nUnfiltBuff_producedBy_join =
        xStreamPer.p_streamJoin()->nEstJoinCost(i_Permutation,
                                                i_Join,
                                                nUnfiltBuff_prev_join,
                                                nFilterPred_prev,
                                                eJoinCast,
                                                nBuff_joining_with
        );

    // running sum of the network and unicast costs
    Card_JoinEstimate* p_joinEstimate = xStreamPer.p_streamJoin()->pSub_joinEstimate();
    Card_CostNetwork* p_costNetwork = p_joinEstimate->pSub_costNetwork();
    nSumNetworkCost += p_costNetwork->pSymbol()->iTo_integer(SF_INT_LIT);
    Card_CostUnicast* p_costUnicast = p_joinEstimate->pSub_costUnicast();
    nSumUnicastCost += p_costUnicast->pSymbol()->iTo_integer(SF_INT_LIT);

    nFilterPred_prev = p_filtPredList->nFiltersFor(cTblSyn_join);

    i_Join++;
  }

  // The buffer model above does not see how many rows a join produces. A
  // join on a column that is not a key (e.g. customer to supplier on the
  // nation key) can multiply the stream by thousands and exhaust memory, so
  // rank any sequence whose estimated intermediate result explodes after
  // every sequence that does not.
  bool bExplodesEst = false;
  d8 dEstRowsRead = dEstRows(p_tableList, p_filtPredList, p_pipeline, p_joinPredList, bExplodesEst);
  if (bExplodesEst)
    nSumNetworkCost += C_EXPLOSION_PENALTY;

  // record the sum network and unicast costs
  Card_CostNetwork* p_costNetwork = new Card_CostNetwork;
  p_costNetwork->SYM(SF_INT_LIT,nSumNetworkCost);

  Card_CostUnicast* p_costUnicast = new Card_CostUnicast;
  p_costUnicast->SYM(SF_INT_LIT,nSumUnicastCost);

  Card_CostStream* p_costQuery = new Card_CostStream;
  p_costQuery->pSymbol()->Y_vSet(P_EstRows, (i8)(dEstRowsRead > 9.0e18 ? 9.0e18 : dEstRowsRead));
  p_costQuery->vAppendChild(p_costNetwork);
  p_costQuery->vAppendChild(p_costUnicast);

  vPushChild(p_costQuery);

  delete p_streamPtn;
}

/*----------------------------------------------------------------------------*/
/* does the estimated intermediate result of this sequence explode?           */
/*----------------------------------------------------------------------------*/
bool Card_StreamPer::bExplodes(Card_TableList* p_tableList,
                               Card_FiltPredList* p_filtPredList,
                               Card_Deck* p_pipeline,
                               Card_JoinPredList* p_joinPredList)
{
  bool bExplodes = false;
  dEstRows(p_tableList, p_filtPredList, p_pipeline, p_joinPredList, bExplodes);
  return bExplodes;
}

/*----------------------------------------------------------------------------*/
/* estimated rows that this join sequence reads (C_out)                       */
/*----------------------------------------------------------------------------*/
// Rows are estimated as in index nested-loop execution: the head table's rows,
// then for every join the arriving rows times the rows per key of the join
// column on the joined table (Index::nEstKeyFanOut), using the join predicate
// to the stream with the fewest rows per key. Every other join predicate
// between the joined table and the stream is then checked on those rows:
// with keys and foreign keys each arriving row keeps at least one match, so
// it divides the rows by its own rows per key, down to one row per arriving
// row (TPC-H Q9: partsupp joins lineitem on partkey and suppkey). Every filter
// on a table keeps C_FILTER_COEFF of its rows.
//
// The cost of the sequence is the sum of the rows read by its steps (the head
// table: the rows it keeps and a tenth of the rows it scans), as
// DuckDB's join order optimiser sums the rows of intermediate results: on one
// node the network model gives every sequence the same cost, and starting
// from the smallest table (TPC-H Q9: nation) can read all of lineitem where
// starting from the filtered table (part) reads a twentieth of it.
//
// The sequence explodes when a stream grows beyond C_EXPLOSION_FACTOR times
// the largest table of the query.
d8 Card_StreamPer::dEstRows(Card_TableList* p_tableList,
                            Card_FiltPredList* p_filtPredList,
                            Card_Deck* p_pipeline,
                            Card_JoinPredList* p_joinPredList,
                            bool& rbExplodes)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  static const d8 C_EXPLOSION_FACTOR = 4.0;
  static const d8 C_SCAN_WEIGHT      = 0.1;

  d8 dRows    = 0.0;
  d8 dMaxRows = 0.0;
  d8 dLargest = 1.0;
  d8 dSumRead = 0.0;

  char acJoined[64];   // table syntaxes already in the stream
  i8 nJoined = 0;

  Kid xStreamPer(this);
  while (xStreamPer.bEach()) {

    char cTblSyn;
    d8 dRead;
    d8 dReduce = 1.0;
    d8 dProbes = 0.0;

    if (xStreamPer.pChild()->bIsCard(CK_STREAMHEAD)) {
      cTblSyn = xStreamPer.p_streamHead()->pSymbol()->cTblSyn();
      dRead = 0.0;
    }
    else
    if (xStreamPer.pChild()->bIsCard(CK_STREAMJOIN)) {
      cTblSyn = xStreamPer.p_streamJoin()->pSymbol()->cTblSyn();

      // the join predicates between this table and the stream
      i8 anFanOut[64];
      i8 anProbe[64];
      i8 nPreds = 0;
      Kid xJoinPredList(p_joinPredList);
      while (xJoinPredList.bEach(CK_RLNCONJ)) {
        Card_RlnConj* p_rlnConj = xJoinPredList.p_rlnConj();
        if (! p_rlnConj->bUsesTable(cTblSyn))
          continue;
        char cTblSyn_other = p_rlnConj->cOtherTable(cTblSyn);
        bool bInStream = false;
        for (i8 i = 0; i < nJoined; i++)
          if (acJoined[i] == cTblSyn_other)
            bInStream = true;
        if (! bInStream || nPreds >= (i8)(sizeof(anFanOut) / sizeof(anFanOut[0])))
          continue;
        anFanOut[nPreds] = nJoinFanOut(p_pipeline, p_tableList, cTblSyn, p_rlnConj, &anProbe[nPreds]);
        nPreds++;
      }

      // read through the most selective one, check the others
      i8 iMin = -1;
      for (i8 i = 0; i < nPreds; i++)
        if (iMin < 0 || anFanOut[i] < anFanOut[iMin])
          iMin = i;
      i8 nFanOut = (iMin >= 0) ? anFanOut[iMin] : 1;
      if (nFanOut < 1)
        nFanOut = 1;
      for (i8 i = 0; i < nPreds; i++)
        if (i != iMin)
          dReduce *= (d8)(anFanOut[i] > 1 ? anFanOut[i] : 1);
      if (dReduce > (d8)nFanOut)
        dReduce = (d8)nFanOut;

      dRead = dRows * (d8)nFanOut;
      dProbes = dRows * (d8)((iMin >= 0) ? anProbe[iMin] : 1);
    }
    else
      continue;

    Card_RowCount* p_rowCount = p_tableList->pGetRowCount(cTblSyn);
    d8 dTableRows = p_rowCount ? (d8)p_rowCount->pSymbol()->iTo_integer(SF_INT_LIT) : 1.0;
    if (dTableRows > dLargest)
      dLargest = dTableRows;
    if (xStreamPer.pChild()->bIsCard(CK_STREAMHEAD))
      dRead = dTableRows;

    i8 nFilters = p_filtPredList->nFiltersFor(cTblSyn);
    dRows = dRead / dReduce;
    while (nFilters-- > 0)
      dRows *= C_FILTER_COEFF;
    if (dRows < 1.0)
      dRows = 1.0;

    if (dRead > dMaxRows)
      dMaxRows = dRead;

    // the head table is scanned once: its filters are applied as it is read
    // (on an index when they are bounds, as l_shipdate in TPC-H Q14), so it
    // counts as the rows it keeps plus a tenth of the rows it reads; a join
    // makes its index lookups (one per arriving row in every partition that the
    // stream is sent to) and reads every row they find
    if (xStreamPer.pChild()->bIsCard(CK_STREAMHEAD))
      dSumRead += dRows + C_SCAN_WEIGHT * dRead;
    else
      dSumRead += dProbes + dRead;

    if (nJoined < (i8)sizeof(acJoined))
      acJoined[nJoined++] = cTblSyn;
  }

  rbExplodes = dMaxRows > C_EXPLOSION_FACTOR * dLargest;
  return dSumRead;
}

/*----------------------------------------------------------------------------*/
/* rows of the joined table matched by one arriving row (1 if unknown)        */
/*----------------------------------------------------------------------------*/
// Uses the fan-out estimated for the index whose leading column is the join
// column on the joined side (Index::nEstKeyFanOut, recorded as P_FanOut),
// scaled to all partitions.
i8 Card_StreamPer::nJoinFanOut(Card_Deck* p_pipeline,
                               Card_TableList* p_tableList,
                               char cTblSyn_join,
                               Card_RlnConj* p_rlnConj,
                               i8* pnProbePtns)
{
  if (pnProbePtns)
    *pnProbePtns = 1;
  _static_

  if (! p_rlnConj)
    return 1;

  Symbol* p_Y_tablePtn_join = p_tableList->pGetTablePtn(cTblSyn_join)->pSymbol();
  Symbol* p_Y_term_0 = p_rlnConj->pGetTerm_0()->pSymbol();
  Symbol* p_Y_term_1 = p_rlnConj->pGetTerm_1()->pSymbol();

  Symbol* p_Y_col_join = nullptr;
  if (p_Y_term_0->bHasFacet(SF_TBL_COL) && Card_COLUMN::bMatchingTblSyn(p_Y_tablePtn_join, p_Y_term_0))
    p_Y_col_join = p_Y_term_0;
  else
  if (p_Y_term_1->bHasFacet(SF_TBL_COL) && Card_COLUMN::bMatchingTblSyn(p_Y_tablePtn_join, p_Y_term_1))
    p_Y_col_join = p_Y_term_1;

  if (! p_Y_col_join)
    return 1;

  i8 iOrdinal = p_Y_col_join->iTo_ordinal(SF_TBL_COL);

  Kid xDeck(p_pipeline);
  while (xDeck.bEach(CK_o_FROM, CK_p_TABLE)) {
    Card_Table* p_table = _pCard_Table(xDeck.pSub1());
    if (p_table->pSymbol()->cTblSyn() != cTblSyn_join)
      continue;

    Card_IndexList* p_indexList = p_table->pSub_indexList();
    if (! p_indexList)
      return 1;

    i8 nFanOut = 0;
    Kid xIndexList(p_indexList);
    while (xIndexList.bEach(CK_INDEX)) {
      Card_Index* p_index = xIndexList.p_index();
      Card_IdxColList* p_idxColList = p_index->pSub_idxColList();
      if (! p_idxColList || ! p_idxColList->nChildCards())
        continue;
      Symbol* p_Y_idxCol = p_idxColList->pGetChild(0)->pSymbol();
      if (! p_Y_idxCol->bHasFacet(SF_TBL_COL) ||
          p_Y_idxCol->iTo_ordinal(SF_TBL_COL) != iOrdinal ||
          ! p_index->pSymbol()->Y_bHas(P_FanOut))
        continue;
      i8 n = p_index->pSymbol()->Y_iGet(P_FanOut);
      if (! nFanOut || n < nFanOut)
        nFanOut = n;
    }
    if (! nFanOut)
      return 1;

    // The fan-out is sampled in one partition. A lookup on a column that the
    // table is not partitioned by finds rows in every partition (the stream is
    // broadcast to them), so it finds the fan-out times the partitions; on the
    // partition column, or in a replicated table, it finds them in one.
    Card_Table* p_table_join = p_tableList->pFind(cTblSyn_join);
    bool bReplicated = p_table_join && p_table_join->pSymbol()->bHasFacet(SF_TBL_SYN_rep);
    bool bPtnColumn = false;
    if (Card_TablePtn* p_tablePtn = p_tableList->pGetTablePtn(cTblSyn_join)) {
      Symbol* p_Y_ptn = p_tablePtn->pSymbol();
      bPtnColumn = p_Y_ptn->bHasFacet(SF_TBL_COL) && p_Y_ptn->iTo_ordinal(SF_TBL_COL) == iOrdinal;
    }
    if (! bReplicated && ! bPtnColumn) {
      i8 nPtns = AM::PTNMAP::nPtnsInGrid();
      if (nPtns > 1) {
        nFanOut *= nPtns;
        if (pnProbePtns)
          *pnProbePtns = nPtns;
      }
    }
    return nFanOut;
  }

  return 1;
}

/*----------------------------------------------------------------------------*/
/* compute the cast from prev join to curr/given stream join                  */
/*----------------------------------------------------------------------------*/
eXK Card_StreamPer::eJoinCast(CardIter* pCI,
                              Card_StreamPtn*  p_streamPtn,
                              Card_TableList*  p_tableList,
                              Card_StreamJoin* p_streamJoin)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Card_RlnConj* p_rlnConj = p_streamJoin->pSub_rlnConj();
  Card_Term* p_term_0_cmp = p_rlnConj->pGetTerm_0();
  Card_Term* p_term_1_cmp = p_rlnConj->pGetTerm_1();

  char cTblSyn_join  = p_streamJoin->pSymbol()->cTblSyn();
  char cTblSyn_prior = p_rlnConj->cOtherTable(cTblSyn_join);

  Card_TablePtn* p_tablePtn_prior = p_tableList->pGetTablePtn(cTblSyn_prior);
  Card_TablePtn* p_tablePtn_join  = p_tableList->pGetTablePtn(cTblSyn_join);

  Symbol* p_Y_tablePtn_prior = p_tablePtn_prior->pSymbol();
  Symbol* p_Y_tablePtn_join  = p_tablePtn_join->pSymbol();

  Symbol* p_Y_term_0_cmp = p_term_0_cmp->pSymbol();
  Symbol* p_Y_term_1_cmp = p_term_1_cmp->pSymbol();

  Symbol* p_Y_tblCol_prior = nullptr;
  Symbol* p_Y_idxCol_join  = nullptr;

  if (Card_COLUMN::bMatchingTblSyn(p_Y_tablePtn_prior, p_Y_term_0_cmp)) {
    p_Y_tblCol_prior = p_Y_term_0_cmp;
    p_Y_idxCol_join  = p_Y_term_1_cmp;
  }
  else {
    p_Y_tblCol_prior = p_Y_term_1_cmp;
    p_Y_idxCol_join  = p_Y_term_0_cmp;
  }

  eXK eStageCast = Card_Cast::eStageCast(pCI,
                                         p_streamPtn,
                                         p_Y_tblCol_prior,
                                         p_Y_tablePtn_join,
                                         p_Y_idxCol_join);
  return eStageCast;
}

/*----------------------------------------------------------------------------*/
/* create the join sequence for given stream permutation                      */
/*----------------------------------------------------------------------------*/
i8 Card_StreamPer::iCreatePerSeq(Card_TableList*    p_tableList,
                                 Card_JoinPredList* p_joinPredList,
                                 PermGen*           pPermGen)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  i8 i_InvalidAtJoin = C_UNKNOWN;

  i8 nTables   = p_tableList->nChildCards();
  i8 nElements = pPermGen->nElements();
  RASSERT(nTables == pPermGen->nElements());

  char cTblSyn_ith;
  char cTblSyn_other;

  //vTrace("%lld :"_Stz,pPermGen->nPermutations());
  for (i8 i_Elm = 0; i_Elm < nElements; i_Elm++) {

    // get the ith table of this permutation
    i8 i_Table = pPermGen->iElement(i_Elm);
    Card_Table* p_table_ith = _pCard_Table(p_tableList->pGetChild(i_Table));
    cTblSyn_ith = p_table_ith->pSymbol()->cTblSyn();

    if ('F' == cTblSyn_ith)
      vDebugPoint();

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // stream head : table is the head of this stream permutation

    if (0 == nChildCards()) {
      //+ table is the head of this stream permutation

      Card_StreamHead *p_streamHead = new Card_StreamHead;
      p_streamHead->SYM(SF_TBL_SYN,cTblSyn_ith);
      vAppendChild(p_streamHead);

      p_streamHead->vAppendChild((new Card_TableNm) ->SYM(SF_TBL_NM, p_table_ith->pSub_tableNm()->szFacet(SF_TBL_NM)));

      if (p_table_ith->pSub_keyExists() || p_table_ith->pSub_keyNotExists()) {
        p_streamHead->vAppendChild((new Card_Note) ->SYM(SF_NOTE,"**CAN'T BE FIRST**"_Stz));
        pSymbol()->Y_vSet(P_Invalid);
        i_InvalidAtJoin = i_Elm;
        break;
      }

      if (p_table_ith->pSymbol()->Y_bHas(P_FixedPos)) {
        if (p_table_ith->pSymbol()->Y_iGet(P_FixedPos) != i_Elm) {
          p_streamHead->vAppendChild((new Card_Note) ->SYM(SF_NOTE,"**BAD POS**"_Stz));
          pSymbol()->Y_vSet(P_Invalid);
          i_InvalidAtJoin = i_Elm;
          break;
        }
      }


      continue;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // stream join : table is a join in this stream permutation

    Card_StreamJoin *p_streamJoin = new Card_StreamJoin;
    p_streamJoin->SYM(SF_TBL_SYN,cTblSyn_ith);
    vAppendChild(p_streamJoin);

    p_streamJoin->vAppendChild((new Card_TableNm) ->SYM(SF_TBL_NM, p_table_ith->pSub_tableNm()->szFacet(SF_TBL_NM)));

    if (p_table_ith->pSymbol()->Y_bHas(P_FixedPos)) {
      if (p_table_ith->pSymbol()->Y_iGet(P_FixedPos) != i_Elm) {
        p_streamJoin->vAppendChild((new Card_Note) ->SYM(SF_NOTE,"**BAD POS**"_Stz));
        pSymbol()->Y_vSet(P_Invalid);
        i_InvalidAtJoin = i_Elm;
        break;
      }
    }


    bool bSupported = false;

    // test each join predicate which uses the ith table to see if a join
    // can be supported at this point in the sequence
    Kid xJoinPredList(p_joinPredList);
    while (xJoinPredList.bEach(CK_RLNCONJ)) {
      if (!xJoinPredList.p_rlnConj()->bUsesTable(cTblSyn_ith))
        continue;

      // have a join cmpPred which uses the ith table

      // get the other table in the cmpPred
      cTblSyn_other = xJoinPredList.p_rlnConj()->cOtherTable(cTblSyn_ith);

      if (bHasKnownJoin(cTblSyn_other)) {
        bSupported = true;
        p_streamJoin->vAppendChild(xJoinPredList.p_rlnConj()->pDupCardRecursive());
        break;
      }
    }

    if (bSupported)
      //+ join of ith table is possible at this point
      continue;

    // join of ith table is not possible at this point
    p_streamJoin->vAppendChild((new Card_Note) ->SYM(SF_NOTE,"**DOESN'T FOLLOW**"_Stz));
    pSymbol()->Y_vSet(P_Invalid);

    i_InvalidAtJoin = i_Elm;
    break;
  }

//  vTrace("\n"_Stz);

  return i_InvalidAtJoin;
}

/*----------------------------------------------------------------------------*/
/* compute one permuted join sequence                                         */
/*----------------------------------------------------------------------------*/
i8 Card_StreamPer::iOnePermutedJoin_v2(Card_TableList*    p_tableList,
                                       Card_JoinPredList* p_joinPredList,
                                       PermGen*           pPermGen)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  i8 i_NotValid = C_UNKNOWN;

  i8 nTables = p_tableList->nChildCards();

  i8 nCards = 0;
  for (i8 i_Item = 0; i_Item < nTables; i_Item++) {

    // get the ith table of this permutation
    i8 i_Table = pPermGen->iElement(i_Item);
    Card_Table* p_table = _pCard_Table(p_tableList->pGetChild(i_Table));
    char cTblSyn_ith = p_table->pSymbol()->cTblSyn();

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    if (0 == nCards) {
      //+ table is head/first of join sequence
      nCards++;
      continue;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // table is next/subsequent of join sequence

    bool bSupported = false;

    // test each join predicate which uses the ith table to see if a join
    // can be supported at this point in the sequence
    Kid xJoinPredList(p_joinPredList);
    while (xJoinPredList.bEach(CK_RLNCONJ)) {
      if (!xJoinPredList.p_rlnConj()->bUsesTable(cTblSyn_ith))
        continue;

      // have a join cmpPred which uses the ith table

      // get the other table in the cmpPred
      char cTblSyn_other = xJoinPredList.p_rlnConj()->cOtherTable(cTblSyn_ith);

      if (bHasKnownJoin(cTblSyn_other)) {
        bSupported = true;
        nCards++;
        break;
      }
    }

    if (! bSupported) {
      i_NotValid = i_Item;
      break;
    }
  }

//  vTrace("\n"_Stz);

  for (i8 i_Item = 0; i_Item < nTables; i_Item++) {

    // get the ith table of this permutation
    i8 i_Table = pPermGen->iElement(i_Item);
    Card_Table *p_table = _pCard_Table(p_tableList->pGetChild(i_Table));
    char cTblSyn_ith = p_table->pSymbol()->cTblSyn();

    if (0 == i_Item) {
      Card_StreamHead *p_dataStart = new Card_StreamHead;
      p_dataStart->SYM(SF_TBL_SYN,cTblSyn_ith);
      vAppendChild(p_dataStart);
    }
    else {
      Card_StreamJoin *p_joinLocus = new Card_StreamJoin;
      p_joinLocus->SYM(SF_TBL_SYN,cTblSyn_ith);
      vAppendChild(p_joinLocus);

      if (i_NotValid == i_Item) {
        p_joinLocus->vAppendChild((new Card_Note) ->SYM(SF_NOTE,"**DOESN'T FOLLOW**"_Stz));
        pSymbol()->Y_vSet(P_Invalid);
        break;
      }

      Kid xJoinPredList(p_joinPredList);
      while (xJoinPredList.bEach(CK_RLNCONJ)) {
        if (!xJoinPredList.p_rlnConj()->bUsesTable(cTblSyn_ith))
          continue;

        // have a join cmpPred which uses the ith table

        // get the other table in the cmpPred
        char cTblSyn_other = xJoinPredList.p_rlnConj()->cOtherTable(cTblSyn_ith);

        if (bHasKnownJoin(cTblSyn_other)) {
          p_joinLocus->vAppendChild(xJoinPredList.p_rlnConj()->pDupCardRecursive());
          break;
        }
      }
    }
  }

  return i_NotValid;
}

/*----------------------------------------------------------------------------*/
/* does this join sequence already have a join with given table at this point?*/
/*----------------------------------------------------------------------------*/
bool Card_StreamPer::bHasKnownJoin(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Kid xStreamPer(this);
  while (xStreamPer.bEach()) {
    ASSERT(xStreamPer.pChild()->bIsCard(CK_STREAMHEAD) || xStreamPer.pChild()->bIsCard(CK_STREAMJOIN))
    if (xStreamPer.pChild()->pSymbol()->cTblSyn() == cTblSyn)
      return true;
  }

  return false;
}

/*----------------------------------------------------------------------------*/
/* is load of this permutation sequence greater than given arg?               */
/*----------------------------------------------------------------------------*/
bool Card_StreamPer::bIsHigherCost(Card_StreamPer* p_joinPermSeq_arg)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Card_CostStream* p_costQuery_this = pSub_costQuery();
  Card_CostStream* p_costQuery_arg  = p_joinPermSeq_arg->pSub_costQuery();

  if (p_costQuery_this->bIsHigherCost(p_costQuery_arg))
    return true;
  else
    return false;
}

/*----------------------------------------------------------------------------*/
/* is load of this permutation sequence less than given arg?                  */
/*----------------------------------------------------------------------------*/
bool Card_StreamPer::bIsLowerCost(Card_StreamPer* p_joinPermSeq_arg)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Card_CostStream* p_costQuery_this = pSub_costQuery();
  Card_CostStream* p_costQuery_arg  = p_joinPermSeq_arg->pSub_costQuery();

  if (p_costQuery_this->bIsLowerCost(p_costQuery_arg))
    return true;
  else
    return false;
}

/*----------------------------------------------------------------------------*/
/* is a possible unicast permitted at table r ?                               */
/*----------------------------------------------------------------------------*/
bool Card_StreamPer::bUnicastPermitted(char cTblSyn_prior,
                                       char cTblSyn_prev,
                                       char cTblSyn_locus)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);
  UNUSED(cTblSyn_locus);

  Kid xStreamPer(this);

  while (xStreamPer.bEach()) {

    if (xStreamPer.pChild()->pSymbol()->cTblSyn() == cTblSyn_prior)
      break;

    if (! xStreamPer.pChild()->bIsCard(CK_STREAMJOIN))
      continue;

 }

  while (xStreamPer.bEach()) {

    if (! xStreamPer.pChild()->bIsCard(CK_STREAMJOIN))
      continue;

    Card_JoinCast* p_joinCast = xStreamPer.p_streamJoin()->pSub_joinCast();
    if (p_joinCast && ! p_joinCast->pSymbol()->bHasFacet("XK_UNICAST"_Stz))
      return false;

    if (xStreamPer.pChild()->pSymbol()->cTblSyn() == cTblSyn_prev)
      break;
  }

  return true;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_StreamPer::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  UNUSED(pParent);
  Card::vValidate(pCI,iDepth,pParent);
  pCI->vValidateChildren(this,iDepth+1);
  vValidationPassed();
}


/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
