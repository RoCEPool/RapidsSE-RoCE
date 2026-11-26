/*============================================================================*/
/* Card_StreamSet.cpp -- Join Permutation Set (of lists)                    */
/*============================================================================*/

#include "Card_StreamSet.hpp"

#include "Card_Predicate.hpp"
#include "Card_RlnConj.hpp"
#include "Card_StreamHead.hpp"
#include "Card_StreamPer.hpp"
#include "Card_Table.hpp"
#include "Card_TableList.hpp"
#include "Card_Term.hpp"
#include "PermGen.hpp"
#include "CardIter.hpp"
#include "Card_FiltPredList.hpp"
#include "Card_JoinPredList.hpp"
#include "Card_RowCount.hpp"
#include "AM.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>


/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE, Card_StreamSet, CK_STREAMSET)
__aNewFunction(Card_StreamSet, CN_CARD)
__vDeleteFunction(Card_StreamSet, CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_StreamSet::Card_StreamSet(void)
: Card_symbol(CK_STREAMSET, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);

  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_StreamSet::~Card_StreamSet(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* compute the set of stream join permutations                                */
/*----------------------------------------------------------------------------*/
Card_StreamSet* Card_StreamSet::pComputeAllPermutations(Card_TableList* p_tableList,
                                                        Card_JoinPredList* p_joinPredList)
{
  _static_

  Card_StreamSet* p_streamPrmAll = new Card_StreamSet;

  i8 nTables = p_tableList->nChildCards();
  PermGen* pPermGen = new PermGen(nTables);

  i8 i_Permutation = 0;
  while (i_Permutation < pPermGen->nPermutations(nTables)) {

    // calc the join sequence for this stream permutation
    pPermGen->vCalcPermutation(i_Permutation);

    Card_StreamPer* p_streamPer = new Card_StreamPer;
    p_streamPer->pSymbol()->Y_vSet(P_Permutation, i_Permutation);

    // create the permuted join sequence for the stream, testing if sequence is valid
    i8 i_InvalidAtJoin = p_streamPer->iCreatePerSeq(p_tableList,
                                                    p_joinPredList,
                                                    pPermGen);
    p_streamPrmAll->vAppendChild(p_streamPer);

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // valid permutation

    if (C_UNKNOWN == i_InvalidAtJoin) {
      //vTrace("VALID PERMUTATION %lld\n"_Stz, i_Permutation);
      i_Permutation++;
      continue;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // invalid permutation

    //vTrace("PERMUTATION INVALID %lld at %lld\n"_Stz, i_Permutation,i_InvalidAtJoin);

    i_Permutation = pPermGen->iNextValid(i_InvalidAtJoin, i_Permutation);
    //vTrace("RESTARTING FROM %lld\n"_Stz, i_Permutation);
    vDebugPoint();
  }

  delete pPermGen;

  return p_streamPrmAll;
}

/*----------------------------------------------------------------------------*/
/* the connected join sequences, pruned by the rows they read                 */
/*----------------------------------------------------------------------------*/
// pComputeAllPermutations walks all n! orders of the tables, builds the cards
// of each and drops those that join a table to nothing in the stream: with 8
// tables (TPC-H Q8) that is 40320 orders for 352 valid ones, and each valid one
// is then costed in full. This builds only the valid orders: it extends a
// sequence one table at a time with a table that has a join predicate to the
// stream, as DuckDB's join order optimiser (DPhyp) enumerates only connected
// subgraphs, never cross products.
//
// With bPrune it also keeps, as DuckDB does, only the cheapest way to join
// each set of tables. The cost is the rows the sequence reads
// (Card_StreamPer::dEstRows, computed step by step here): what the next tables
// cost depends on the set of tables joined so far and on the rows it holds,
// not on their order. So a sequence over a set is dropped when another one
// over the same set reads no more rows, holds no more rows and never held
// more rows (the explosion test). The caller prunes on one node only, where
// every sequence has the same network cost and the rows decide.
//
// The cards are then built and costed for the sequences that are left, as
// before; each keeps the number PermGen gives its order (/jo_pick).
namespace
{
  struct JoEdge
  {
    i8 iOther;    // the other table of the join predicate
    i8 nFanOut;   // rows of this table per arriving row (nJoinFanOut)
    i8 nProbe;    // partitions each arriving row is looked up in
  };

  struct JoCost
  {
    d8 dSum;      // rows read so far
    d8 dRows;     // rows in the stream
    d8 dMax;      // largest step so far
  };

  struct JoGraph
  {
    i8   n = 0;
    char acSyn[64];
    bool abHeadOk[64];
    i8   aiFixed[64];
    d8   adRows[64];
    i8   anFilt[64];
    u8   auAdj[64];
    std::vector<JoEdge> axEdge[64];   // in the order of the join predicates
  };

  // the cost after joining iTable to the stream uMask (dEstRows, one step)
  JoCost xStep(const JoGraph& G, u8 uMask, const JoCost& xPrev, i8 iTable, bool bHead)
  {
    static const d8 C_SCAN_WEIGHT = 0.1;
    JoCost x = xPrev;
    d8 dRead;
    d8 dReduce = 1.0;
    d8 dProbes = 0.0;

    if (bHead) {
      dRead = G.adRows[iTable];
    }
    else {
      i8 anFan[64], anProbe[64], nPreds = 0;
      for (const JoEdge& e : G.axEdge[iTable]) {
        if (! (uMask & (1ULL << e.iOther)) || nPreds >= 64)
          continue;
        anFan[nPreds] = e.nFanOut;
        anProbe[nPreds] = e.nProbe;
        nPreds++;
      }
      i8 iMin = -1;
      for (i8 i = 0; i < nPreds; i++)
        if (iMin < 0 || anFan[i] < anFan[iMin])
          iMin = i;
      i8 nFanOut = (iMin >= 0) ? anFan[iMin] : 1;
      if (nFanOut < 1)
        nFanOut = 1;
      for (i8 i = 0; i < nPreds; i++)
        if (i != iMin)
          dReduce *= (d8)(anFan[i] > 1 ? anFan[i] : 1);
      if (dReduce > (d8)nFanOut)
        dReduce = (d8)nFanOut;
      dRead = xPrev.dRows * (d8)nFanOut;
      dProbes = xPrev.dRows * (d8)((iMin >= 0) ? anProbe[iMin] : 1);
    }

    x.dRows = dRead / dReduce;
    for (i8 i = 0; i < G.anFilt[iTable]; i++)
      x.dRows *= C_FILTER_COEFF;
    if (x.dRows < 1.0)
      x.dRows = 1.0;
    if (bHead || dRead > x.dMax)
      x.dMax = dRead;
    if (bHead)
      x.dSum = x.dRows + C_SCAN_WEIGHT * dRead;
    else
      x.dSum += dProbes + dRead;
    return x;
  }

  struct JoSearch
  {
    const JoGraph& G;
    bool bPrune;
    std::unordered_map<u8, std::vector<JoCost>> xBest;   // set of tables -> costs kept
    std::vector<std::vector<i8>> xOrders;
    i8 aiOrder[64];

    JoSearch(const JoGraph& rG, bool b) : G(rG), bPrune(b) {}

    // is xCost no better than a cost kept for the same set? else keep it
    bool bDominated(u8 uMask, const JoCost& xCost)
    {
      std::vector<JoCost>& rKept = xBest[uMask];
      for (const JoCost& k : rKept)
        if (k.dSum <= xCost.dSum && k.dRows <= xCost.dRows && k.dMax <= xCost.dMax)
          return true;
      rKept.erase(std::remove_if(rKept.begin(), rKept.end(), [&](const JoCost& k) {
                    return xCost.dSum <= k.dSum && xCost.dRows <= k.dRows && xCost.dMax <= k.dMax; }),
                  rKept.end());
      rKept.push_back(xCost);
      return false;
    }

    void vExtend(i8 nOrder, u8 uMask, const JoCost& xCost)
    {
      if (nOrder == G.n) {
        xOrders.emplace_back(aiOrder, aiOrder + G.n);
        return;
      }

      // a table fixed at this position goes here
      i8 iFixedHere = -1;
      for (i8 t = 0; t < G.n; t++)
        if (! (uMask & (1ULL << t)) && G.aiFixed[t] == nOrder)
          iFixedHere = t;

      std::vector<std::pair<JoCost, i8>> xNext;
      for (i8 t = 0; t < G.n; t++) {
        if (uMask & (1ULL << t))
          continue;
        if (iFixedHere >= 0 && t != iFixedHere)
          continue;
        if (G.aiFixed[t] >= 0 && G.aiFixed[t] != nOrder)
          continue;
        if (0 == nOrder ? ! G.abHeadOk[t] : ! (G.auAdj[t] & uMask))
          continue;
        xNext.push_back({xStep(G, uMask, xCost, t, 0 == nOrder), t});
      }

      // the cheapest first, so that the sets are first reached cheaply
      if (bPrune)
        std::stable_sort(xNext.begin(), xNext.end(),
                         [](const std::pair<JoCost, i8>& a, const std::pair<JoCost, i8>& b) {
                           return a.first.dSum < b.first.dSum; });

      for (const auto& rNext : xNext) {
        u8 uMaskNext = uMask | (1ULL << rNext.second);
        if (bPrune && bDominated(uMaskNext, rNext.first))
          continue;
        aiOrder[nOrder] = rNext.second;
        vExtend(nOrder + 1, uMaskNext, rNext.first);
      }
    }
  };

  // the number PermGen::vCalcPermutation gives to an order (its Lehmer code)
  i8 iPermutationOf(const std::vector<i8>& rOrder)
  {
    i8 n = (i8) rOrder.size();
    i8 iPerm = 0;
    for (i8 p = 0; p < n; p++) {
      i8 iRank = 0;
      for (i8 q = p + 1; q < n; q++)
        if (rOrder[q] < rOrder[p])
          iRank++;
      i8 nFact = 1;
      for (i8 k = 2; k <= n - 1 - p; k++)
        nFact *= k;
      iPerm += iRank * nFact;
    }
    return iPerm;
  }
}

Card_StreamSet* Card_StreamSet::pComputeConnectedPermutations(Card_TableList*    p_tableList,
                                                              Card_JoinPredList* p_joinPredList,
                                                              Card_FiltPredList* p_filtPredList,
                                                              Card_Deck*         p_pipeline,
                                                              bool               bPrune)
{
  _static_

  i8 nTables = p_tableList->nChildCards();
  if (nTables > PermGen::C_PERMGEN_ELEMENTS || nTables > 64)
    return pComputeAllPermutations(p_tableList, p_joinPredList);

  JoGraph G;
  G.n = nTables;
  for (i8 i = 0; i < nTables; i++) {
    Card_Table* p_table = _pCard_Table(p_tableList->pGetChild(i));
    char cSyn = p_table->pSymbol()->cTblSyn();
    G.acSyn[i]    = cSyn;
    G.abHeadOk[i] = ! p_table->pSub_keyExists() && ! p_table->pSub_keyNotExists() &&
                    ! p_table->pSymbol()->Y_bHas(P_NotFirst);
    G.aiFixed[i]  = p_table->pSymbol()->Y_bHas(P_FixedPos) ? p_table->pSymbol()->Y_iGet(P_FixedPos) : -1;
    Card_RowCount* p_rowCount = p_tableList->pGetRowCount(cSyn);
    G.adRows[i]   = p_rowCount ? (d8)p_rowCount->pSymbol()->iTo_integer(SF_INT_LIT) : 1.0;
    G.anFilt[i]   = p_filtPredList->nFiltersFor(cSyn);
    G.auAdj[i]    = 0;
  }

  for (i8 i = 0; i < nTables; i++) {
    Kid xJoinPredList(p_joinPredList);
    while (xJoinPredList.bEach(CK_RLNCONJ)) {
      Card_RlnConj* p_rlnConj = xJoinPredList.p_rlnConj();
      if (! p_rlnConj->bUsesTable(G.acSyn[i]))
        continue;
      char cOther = p_rlnConj->cOtherTable(G.acSyn[i]);
      i8 iOther = -1;
      for (i8 j = 0; j < nTables; j++)
        if (j != i && G.acSyn[j] == cOther)
          iOther = j;
      if (iOther < 0)
        continue;
      JoEdge e;
      e.iOther = iOther;
      e.nFanOut = Card_StreamPer::nJoinFanOut(p_pipeline, p_tableList, G.acSyn[i], p_rlnConj, &e.nProbe);
      G.axEdge[i].push_back(e);
      G.auAdj[i] |= (1ULL << iOther);
    }
  }

  JoSearch xSearch(G, bPrune);
  JoCost xNone = {0.0, 0.0, 0.0};
  xSearch.vExtend(0, 0, xNone);

  // the cards of the orders that are left, in the order of their numbers
  std::vector<std::pair<i8, i8>> xNumbered;   // permutation number, order
  for (i8 i = 0; i < (i8) xSearch.xOrders.size(); i++)
    xNumbered.push_back({iPermutationOf(xSearch.xOrders[i]), i});
  std::sort(xNumbered.begin(), xNumbered.end());

  Card_StreamSet* p_streamPrmAll = new Card_StreamSet;
  PermGen* pPermGen = new PermGen(nTables);
  for (const auto& rNumbered : xNumbered) {
    pPermGen->vCalcPermutation(rNumbered.first);
    Card_StreamPer* p_streamPer = new Card_StreamPer;
    p_streamPer->pSymbol()->Y_vSet(P_Permutation, rNumbered.first);
    p_streamPer->iCreatePerSeq(p_tableList, p_joinPredList, pPermGen);   // valid by construction
    p_streamPrmAll->vAppendChild(p_streamPer);
  }
  delete pPermGen;

  return p_streamPrmAll;
}

/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* compute the load of all permutations                                       */
/*----------------------------------------------------------------------------*/
void Card_StreamSet::vEstCostsForAllPermutations(CardIter* pCI,
                                                 Card_TableList*    p_tableList,
                                                 Card_FiltPredList* p_filtPredList,
                                                 Card_Deck*         p_pipeline,
                                                 Card_JoinPredList* p_joinPredList)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Kid xStreamSet(this);
  while (xStreamSet.bEach())
    xStreamSet.p_streamPer()->vEstStreamCost(pCI,p_tableList, p_filtPredList, p_pipeline, p_joinPredList);
}

/*----------------------------------------------------------------------------*/
/* pick permutation with highest cost                                         */
/*----------------------------------------------------------------------------*/
Card_StreamPer* Card_StreamSet::pHighestCost(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Card_StreamPer* pHighest = nullptr;

  Kid xStreamSet(this);
  while (xStreamSet.bEach(CK_STREAMPER)) {
    if (! pHighest)
      pHighest = xStreamSet.p_streamPer();
    else
    if (xStreamSet.p_streamPer()->bIsHigherCost(pHighest))
      pHighest = xStreamSet.p_streamPer();
  }

  return pHighest;
}

/*----------------------------------------------------------------------------*/
/* pick given order                                                           */
/*----------------------------------------------------------------------------*/
Card_StreamPer* Card_StreamSet::pJoinOrder(i8 iJoinOrder)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  i8 iPermutation;
  Card_StreamPer* p_joinPermSeq = nullptr;

  Kid xStreamSet(this);
  while (xStreamSet.bEach(CK_STREAMPER)) {
    p_joinPermSeq = xStreamSet.p_streamPer();
    iPermutation = p_joinPermSeq->pSymbol()->Y_iGet(P_Permutation);
    if (iJoinOrder == iPermutation)
      return (p_joinPermSeq);
  }

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/* pick permutation with lowest cost                                          */
/*----------------------------------------------------------------------------*/
Card_StreamPer* Card_StreamSet::pLowestCost(void)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Card_StreamPer* pLowest = nullptr;

  Kid xStreamSet(this);
  while (xStreamSet.bEach(CK_STREAMPER)) {
    if (! pLowest)
      pLowest = xStreamSet.p_streamPer();
    else
    if (xStreamSet.p_streamPer()->bIsLowerCost(pLowest))
      pLowest = xStreamSet.p_streamPer();
  }

  return pLowest;
}

/*----------------------------------------------------------------------------*/
/* compute the set of join permutations                                       */
/*----------------------------------------------------------------------------*/
void Card_StreamSet::vRemoveInvalid(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  Kid xStreamSet(this);
  while (xStreamSet.bEach()) {
    if (xStreamSet.p_streamPer()->pSymbol()->Y_bHas(P_Invalid)) {
      Card_StreamPer* p_streamPer = xStreamSet.p_streamPer();
      xStreamSet.vRemove();
      DELETE_CARD(p_streamPer);
    }
  }
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_StreamSet::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
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
