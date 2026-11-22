/*============================================================================*/
/* Card_CostStream.cpp -- JLoad of join Permutation Sequence                 */
/*============================================================================*/

#include "Card_CostStream.hpp"
#include "AM.hpp"

#include "Card_Predicate.hpp"
#include "Card_RlnConj.hpp"
#include "Card_CostNetwork.hpp"
#include "Card_CostUnicast.hpp"
#include "Card_Table.hpp"
#include "Card_Term.hpp"

#include "CardIter.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE, Card_CostStream, CK_COSTSTREAM)
__aNewFunction(Card_CostStream, CN_CARD)
__vDeleteFunction(Card_CostStream, CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_CostStream::Card_CostStream(void)
: Card_symbol(CK_COSTSTREAM, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);

  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_CostStream::~Card_CostStream(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* is this permutation sequence load greater than given arg?                  */
/*----------------------------------------------------------------------------*/
bool Card_CostStream::bIsHigherCost(Card_CostStream* p_costQuery_arg)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  Card_CostNetwork* p_costNetwork_this = pSub_costNetwork();
  Card_CostNetwork* p_costNetwork_arg  = p_costQuery_arg->pSub_costNetwork();

  i8 iCostNetwork_this = p_costNetwork_this->pSymbol()->iTo_integer(SF_INT_LIT);
  i8 iCostNetwork_arg  = p_costNetwork_arg->pSymbol()->iTo_integer(SF_INT_LIT);

  if (iCostNetwork_this > iCostNetwork_arg)
    return true;

  if (iCostNetwork_this < iCostNetwork_arg)
    return false;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  Card_CostUnicast* p_costUnicast_this = pSub_costUnicast();
  Card_CostUnicast* p_costUnicast_arg  = p_costQuery_arg->pSub_costUnicast();

  i8 iloadUnicast_this = p_costUnicast_this->pSymbol()->iTo_integer(SF_INT_LIT);
  i8 iloadUnicast_arg  = p_costUnicast_arg->pSymbol()->iTo_integer(SF_INT_LIT);

  if (iloadUnicast_this > iloadUnicast_arg)
    return true;

  return false;
}

/*----------------------------------------------------------------------------*/
/* is this permutation sequence load less than given arg?                     */
/*----------------------------------------------------------------------------*/
bool Card_CostStream::bIsLowerCost(Card_CostStream* p_costQuery_arg)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  Card_CostNetwork* p_costNetwork_this = pSub_costNetwork();
  Card_CostNetwork* p_costNetwork_arg  = p_costQuery_arg->pSub_costNetwork();

  i8 iCostNetwork_this = p_costNetwork_this->pSymbol()->iTo_integer(SF_INT_LIT);
  i8 iCostNetwork_arg  = p_costNetwork_arg->pSymbol()->iTo_integer(SF_INT_LIT);

  if (iCostNetwork_this < iCostNetwork_arg)
    return true;

  if (iCostNetwork_this > iCostNetwork_arg)
    return false;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // the rows the sequence reads (Card_StreamPer::dEstRows); on one node every
  // sequence has the same network cost

  if (pSymbol()->Y_bHas(P_EstRows) && p_costQuery_arg->pSymbol()->Y_bHas(P_EstRows) &&
      ! AM::JOB::bHasTag("<optimiser_off_join_rows>"_Stz) && ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz)) {
    // estimates within C_ROWS_TIE of each other are a tie: the estimate does
    // not know which rows lie together (lineitem rows of an order, found
    // through the partition column), so the comparison below decides
    static const d8 C_ROWS_TIE = 0.9;
    d8 dRows_this = (d8)pSymbol()->Y_iGet(P_EstRows);
    d8 dRows_arg  = (d8)p_costQuery_arg->pSymbol()->Y_iGet(P_EstRows);
    if (dRows_this < dRows_arg * C_ROWS_TIE)
      return true;
    if (dRows_arg < dRows_this * C_ROWS_TIE)
      return false;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  Card_CostUnicast* p_costUnicast_this = pSub_costUnicast();
  Card_CostUnicast* p_costUnicast_arg  = p_costQuery_arg->pSub_costUnicast();

  i8 iloadUnicast_this = p_costUnicast_this->pSymbol()->iTo_integer(SF_INT_LIT);
  i8 iloadUnicast_arg  = p_costUnicast_arg->pSymbol()->iTo_integer(SF_INT_LIT);

  if (iloadUnicast_this < iloadUnicast_arg)
    return true;

  return false;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_CostStream::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  RASSERT(pSymbol()->bHasFacet(SF_INT_LIT));

  Card::vValidate(pCI,iDepth,pParent);
  pCI->vValidateChildren(this,iDepth+1);
  vValidationPassed();
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
