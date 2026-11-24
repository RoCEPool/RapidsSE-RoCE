/*============================================================================*/
/* Card_Stage.cpp -- Stage                                                    */
/*============================================================================*/

#include "Card_Stage.hpp"
#include <vector>
#include <string>
#include "Card_Term.hpp"
#include "Card_Emit.hpp"
#include "AM.hpp"

#include "CardIter.hpp"
#include "SawOpCards.hpp"

#include "CardUsage.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE, Card_Stage, CK_o_STAGE)
__aNewFunction(Card_Stage,CN_CARD)
__vDeleteFunction(Card_Stage,CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_Stage::Card_Stage(void)
: Card_CONSTRUCTION(CK_o_STAGE, 1)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
  
  _checkInitialised_(CN_CARD);
  
  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_Stage::~Card_Stage(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_Stage::vReinstate_stages(CardIter* pCI)
{
  _static_


  vReinstate_stages_v0(pCI);
//  vReinstate_stages_v1(pCI);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_Stage::vReinstate_stages_v0(CardIter* pCI)
{
  _static_

  i8 nStages = 0;

  Card_Stage* p_stage = new Card_Stage;
  p_stage->SYM(SF_STG_NM,++nStages);
  Card_Topology* p_topology = new Card_Topology(YG_ALL_PARTITIONS);
  p_stage->vAppendChild(p_topology);
  pCI->E.p_deck->vPushChild(p_stage);

  eAL e_level_running = Card_Level::e_stageTopo_To_aggLevel(YG_ALL_PARTITIONS);

  bool bSawAggOp = false;
  bool bSawFilterOp = false;
  bool bSawIndexJoinOp = false;
  bool bSawProjectOp = false;
  bool bSawSortOp = false;
  // the stage holds the scan of a table and its filters only (see PROJECT)
  bool bScanStage = pCI->E.p_deck->nChildCards() > 1 &&
                    (CK_o_SCAN == pCI->E.p_deck->pGetChild(1)->eCard() ||
                     CK_o_INDEXSCAN == pCI->E.p_deck->pGetChild(1)->eCard());
  // the stage holds an index join and its filters only (see PROJECT)
  bool bJoinStage = false;

  pCI->vClearRegisters();
  pCI->vBeginEach();
  while (pCI->bEachOp(CK_o_FOLD))
    pCI->vRemoveDeleteMain(CK_o_FOLD);

  Card* p_main = p_stage;
  eCK   e_main = p_main->eCard();

  for (i8 i_Main = 1; i_Main < pCI->E.p_deck->nChildCards(); i_Main++) {


    p_main = pCI->E.p_deck->pGetChild(i_Main);
    e_main = p_main->eCard();



    eAL e_level_main = (eAL) p_main->pSub_level()->pSymbol()->Y_iGet(P_Level);

    // change of level
    if (e_level_main != e_level_running) {
      if (e_level_running == AL_ORIGIN &&
          e_level_main == AL_PARTITION)
        vAddNext(pCI, p_main, i_Main,"broadcast"_Stz);
      else
        vAddNext(pCI, p_main, i_Main,""_Stz);
      e_level_running = e_level_main;
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawFilterOp = false;
      if (CK_o_GBYAGG == e_main || CK_o_AGGREG == e_main || CK_o_ORDAGG == e_main)
        bSawAggOp = true;
      else
        bSawAggOp = false;
      if (CK_o_PROJECT == e_main)
        bSawProjectOp = true;
      else
        bSawProjectOp = false;
      continue;
    }

    if (CK_o_CROSSJOIN == e_main || CK_o_INDEXJOIN == e_main
        || CK_o_KEYEXISTS == e_main
        || CK_o_KEYNOTEXISTS == e_main) {
      vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = false;
      bSawFilterOp = false;
      bSawIndexJoinOp = true;
      bSawProjectOp = false;
      bSawSortOp = false;
      // (an outer join too: the project stays a card of its own, see
      // XF_Epilog_Fuse_project, and also runs on the NULL-extended rows)
      bJoinStage = CK_o_INDEXJOIN == e_main;
      continue;
    }

    if ((CK_o_AGGREG == e_main)
        && (bSawAggOp || bSawFilterOp || bSawIndexJoinOp || bSawProjectOp || bSawSortOp) ) {
       vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = false;
      bSawFilterOp = false;
      bSawIndexJoinOp = false;
      bSawProjectOp = true;
      bSawSortOp = false;
      continue;
    }

    // A projection behind the filter of a scan stays in the stage of the scan:
    // it keeps the columns that the next stages need, and the scan no longer
    // copies every column of the table out of the stage (TPC-H Q1 copied the
    // 16 columns of lineitem, comments included, to keep 6). Job tag
    // /optimiser_off_scan_project splits them as before.
    bool bProjectInScan = bScanStage && bSawFilterOp && ! bSawAggOp && ! bSawIndexJoinOp &&
                          ! bSawProjectOp && ! bSawSortOp &&
                          ! AM::JOB::bHasTag("<optimiser_off_scan_project>"_Stz) &&
                          ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz);
    // The same for a projection behind an index join (and its filters): the join
    // no longer copies every column of both tables out of the stage (TPC-H Q13
    // copied the 9 columns of orders, comments included, to keep 2). Job tag
    // /optimiser_off_join_project splits them as before.
    bool bProjectInJoin = bJoinStage && ! bSawAggOp && ! bSawProjectOp && ! bSawSortOp &&
                          ! AM::JOB::bHasTag("<optimiser_off_join_project>"_Stz) &&
                          ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz);
    // Not when the project reads one table column twice (two aliases of one
    // table, as nation in TPC-H Q7): inside one stage both name the same cell.
    if (bProjectInJoin && CK_o_PROJECT == e_main && p_main->pSub_emit()) {
      std::vector<std::string> vSeen;
      p_main->pSub_emit()->bLambdaWalk_term([&vSeen, &bProjectInJoin](Card_Term* p_term) {
        Symbol* pSym = p_term->pSymbol();
        if (! pSym || ! pSym->bHasFacet(SF_TBL_COL))
          return true;
        std::string sCol = pSym->szFacet(SF_TBL_COL)->azChar();
        for (const std::string& s : vSeen)
          if (s == sCol) {
            bProjectInJoin = false;
            return false;
          }
        vSeen.push_back(sCol);
        return true;
      });
    }
    if (CK_o_PROJECT == e_main && (bProjectInScan || bProjectInJoin)) {
      bSawProjectOp = true;
      continue;
    }

    if ((CK_o_PROJECT == e_main)
        && (bSawAggOp || bSawFilterOp || bSawIndexJoinOp || bSawProjectOp || bSawSortOp) ) {
      vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = false;
      bSawFilterOp = false;
      bSawIndexJoinOp = false;
      bSawProjectOp = true;
      bSawSortOp = false;
      continue;
    }


    // A GROUP BY behind the project kept in a scan stage stays in that stage, as
    // DuckDB aggregates in the pipeline of the scan: the scan no longer hands
    // every row to the next stage through a tub (TPC-H Q1 handed on 5.9 million
    // rows to make 4 groups). Job tag /optimiser_off_scan_gbyagg splits them.
    // Likewise behind the project kept in an index-join stage (DuckDB aggregates
    // in the pipeline of the join probe); job tag /optimiser_off_join_gbyagg.
    bool bGbyAggInScan = bScanStage && ! bSawIndexJoinOp &&
                         ! AM::JOB::bHasTag("<optimiser_off_scan_gbyagg>"_Stz);
    bool bGbyAggInJoin = bJoinStage &&
                         ! AM::JOB::bHasTag("<optimiser_off_join_gbyagg>"_Stz);
    if (CK_o_GBYAGG == e_main && (bGbyAggInScan || bGbyAggInJoin) &&
        bSawProjectOp && ! bSawAggOp && ! bSawSortOp &&
        ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz)) {
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = true;
      bSawProjectOp = false;
      bSawFilterOp = false;
      continue;
    }

    if ((CK_o_GBYAGG == e_main || CK_o_ORDAGG == e_main) &&
        (bSawAggOp || bSawIndexJoinOp || bSawProjectOp)) {
       vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      // The new stage holds an aggregation: what follows it starts the next stage.
      // (This was "false": a <project> behind the second of two aggregations at one
      // level stayed in the stage of that aggregation, and the generated code did
      // not compile. Example: sum(q) + 1 over a derived table that groups.)
      bSawAggOp = true;
      bSawFilterOp = false;
      bSawIndexJoinOp = false;
      bSawProjectOp = false;
      bSawSortOp = false;
      continue;
    }

    if (CK_o_SORT == e_main) {

      vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = false;
      bSawFilterOp = false;
      bSawIndexJoinOp = false;
      bSawProjectOp = false;
      bSawSortOp = true;
      continue;
    }


    // if filter then continue?
    if (CK_o_FILTER == e_main || CK_o_HAVING == e_main) {

      bSawFilterOp = true;
      if (CK_o_FILTER == e_main && ! bSawAggOp)
        continue;

      vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = false;
      bSawFilterOp = false;
      bSawIndexJoinOp = false;
      bSawProjectOp = false;
      bSawSortOp = true;
      continue;

    }

    if (bSawAggOp || bSawIndexJoinOp) {
      vAddNext(pCI, p_main, i_Main,""_Stz);
      vAddStage(pCI, p_main, i_Main, e_level_running,nStages);
      bScanStage = false;
      bJoinStage = false;
      bSawAggOp = false;
      bSawFilterOp = false;
      bSawIndexJoinOp = false;
      bSawProjectOp = false;
      bSawSortOp = false;
      continue;
    }

    if (CK_o_GBYAGG == e_main || CK_o_ORDAGG == e_main)
      bSawAggOp = true;
    if (CK_o_PROJECT == e_main || CK_o_AGGREG == e_main)
      bSawProjectOp = true;
  }

  i8 i_End = C_UNKNOWN;
  vAddNext(pCI, p_main, i_End,""_Stz);
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_Stage::vReinstate_stages_v1(CardIter* pCI)
{
  _static_

  CardUsage* pStager = new CardUsage;
  RASSERT(0 == pStager->nSaw());

  i8 nStages = 0;
  //SawOpCards* pSawOpCards = new SawOpCards;
  eAL e_level_running = Card_Level::e_stageTopo_To_aggLevel(YG_ALL_PARTITIONS);

  for (i8 i_Main = 1; i_Main < pCI->E.p_deck->nChildCards(); i_Main++) {

    Card_OP* p_card_OP = (Card_OP*)(pCI->E.p_deck->pGetChild(i_Main));

    RASSERT(Card_OP::bIsOp(p_card_OP->eCard()));

    if (pStager->bAddNext(p_card_OP))
      vAddNext(pCI, p_card_OP, i_Main, ""_Stz);

    if (pStager->bAddStage(p_card_OP)) {
      vAddStage(pCI, p_card_OP, i_Main, e_level_running, nStages);
      pStager->vClearAll();
    }

   //pSawOpCards->vSaw(p_card_OP);

    pStager->vSaw(p_card_OP);
  }

  //delete pSawOpCards;

  delete pStager;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_Stage::vAddNext(CardIter* pCI, Card* p_main, i8& ri_main, Stz* szNextValue)
{
  _static_

  Card_Cast* p_next = new Card_Cast;
  if (*szNextValue->azChar()) {
    eSF eSymbol = Symbol::eEval_facet(szNextValue);
    RASSERT(SF_STG_CAST == eSymbol);
    p_next->SYM(SF_STG_CAST, szNextValue);
  }
  if (C_UNKNOWN == ri_main) {
    pCI->E.p_deck->vAppendChild(p_next);
    return;
  }
  pCI->E.p_deck->vInsertChildBefore(p_next, p_main);
  ri_main++;
  RASSERT(pCI->E.p_deck->pGetChild(ri_main) == p_main);

}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_Stage::vAddStage(CardIter* pCI, Card* p_main, i8& ri_main, eAL e_level_running, i8& nStages)
{
  _static_

  Card_Stage *p_stage = new Card_Stage;
  p_stage->SYM(SF_STG_NM,++nStages);
  eYG eStageTopo = Card_Level::e_aggLevel_To_stageTopo(e_level_running);
  Card_Topology *p_topology = new Card_Topology(eStageTopo);
  p_stage->vAppendChild(p_topology);
  pCI->E.p_deck->vInsertChildBefore(p_stage, p_main);
  ri_main++;
  RASSERT(pCI->E.p_deck->pGetChild(ri_main) == p_main);

}

/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_Stage::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  vValidate_child_notBe(CK_p_LEVEL, CK_NONE);

  RASSERT(m_xSymbol.bSingleFaceted() && m_xSymbol.bHasFacet(SF_STG_NM));

  Card::vValidate(pCI,iDepth,pParent);
  pCI->vValidateChildren(this,iDepth+1);
  vValidationPassed();
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
