/*============================================================================*/
/* Card_FiltPredList.cpp -- Table List                                        */
/*============================================================================*/

#include "Card_FiltPredList.hpp"

#include "CardIter.hpp"

#include "Card_Predicate.hpp"
#include "Card_RlnConj.hpp"
#include "Card_Deck.hpp"
#include "Card_Filter.hpp"
#include "Card_Term.hpp"

#include "CardIter.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE,Card_FiltPredList,CK_FILTPREDLIST)
__aNewFunction(Card_FiltPredList,CN_CARD)
__vDeleteFunction(Card_FiltPredList,CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_FiltPredList::Card_FiltPredList(void)
: Card_symbol(CK_FILTPREDLIST, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);

  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_FiltPredList::~Card_FiltPredList(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_FiltPredList* Card_FiltPredList::pExtractList(Card_Deck* p_deck)
{
  _static_

  i8 nFilters = 0;

  auto bLambda = [&nFilters] (Card* p_card) {

    if (!_pCard_RlnConj(p_card)->bIsJoinConj())
      nFilters++;

    return C_CONTINUE_RECURSION;
  };

  Card_FiltPredList* p_filtPredList = new Card_FiltPredList;

  // the filters of the tables of the <from>s of the deck (the deck's cards are
  // <from>s holding a <table> and maybe a <filter>; iterating <table> cards of
  // the deck found none, so join ordering never saw a filter)
  Kid xDeck(p_deck);
  while (xDeck.bEach(CK_o_FROM, CK_p_TABLE)) {

    Card_Table* p_table = _pCard_Table(xDeck.pSub1());
    Card_Filter* p_filter = xDeck.pChild()->pSub_filter();
    if (! p_filter)
      p_filter = p_table->pSub_filter();
    if (! p_filter)
      continue;

    Card_Predicate* p_predicate = p_filter->pSub_predicate();
    if (! p_predicate)
      continue;

    nFilters = 0;
    p_predicate->bLambdaWalk(CK_RLNCONJ, bLambda);

    if (nFilters) {
      Card_FiltPred* p_filtPred = new Card_FiltPred;
      char azTblSyn[4] = { '$', 'T', p_table->pSymbol()->cTblSyn(), '\0' };
      p_filtPred->SYM(SF_TBL_SYN, (Stz*)azTblSyn);
      p_filtPred->pSymbol()->Y_vSet(P_Count,nFilters);
      p_filtPredList->vAppendChild(p_filtPred);
    }
  }

  return p_filtPredList;
}


/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
/* find the first cmpPred which uses given table                              */
/*----------------------------------------------------------------------------*/
i8 Card_FiltPredList::nFiltersFor(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  Kid xCI_filtPred(this);
  while (xCI_filtPred.bEach()) {
    Card_FiltPred* p_filtPred = _pCard_FiltPred(xCI_filtPred.pChild());

    if (p_filtPred->pSymbol()->cTblSyn() == cTblSyn) {
      i8 nFilter = p_filtPred->pSymbol()->Y_iGet(P_Count);
      return nFilter;
    }
  }

  return 0;

}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_FiltPredList::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
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
