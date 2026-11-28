/*============================================================================*/
/* Card_TableList.cpp -- Table List                                           */
/*============================================================================*/

#include "Card_TableList.hpp"

#include "CardIter.hpp"
#include "Card_RlnConj.hpp"
#include "Card_Deck.hpp"
#include "Card_TableSize.hpp"
#include "Card_Table.hpp"
#include "Card_TableNm.hpp"
#include "Card_TablePtn.hpp"
#include "Card_Term.hpp"


/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction_Card(STORE,Card_TableList,CK_TABLELIST)
__aNewFunction(Card_TableList,CN_CARD)
__vDeleteFunction(Card_TableList,CN_CARD)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
Card_TableList::Card_TableList(void)
: Card_symbol(CK_TABLELIST, C_UNKNOWN)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  _checkInitialised_(CN_CARD);

  _postcondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
Card_TableList::~Card_TableList(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_TableList* Card_TableList::pExtractList(Card_Deck* p_deck)
{
  _static_

  Card_TableList* p_tableList = new Card_TableList;

  Kid xDeck(p_deck);

  xDeck.vBegin_tail();
  if (! xDeck.bEach_reverse(CK_o_FOLD)) {
    xDeck.vEnded();
    xDeck.vBegin();
  }

  while (xDeck.bEach(CK_o_FROM, CK_p_TABLE)) {

    Card_Table* p_table = _pCard_Table(xDeck.pSub1());

    Card* p_table_dup = _pCard_Table(p_table->pDupCard());
    p_tableList->vAppendChild(p_table_dup);

    // pDupCard copies facets but not properties: carry over the position
    // fixed by XF_Flatten_Anchor_table, or join ordering ignores it and may
    // put a table with no join predicate next to the sub-query result
    if (p_table->pSymbol()->Y_bHas(P_FixedPos))
      p_table_dup->pSymbol()->Y_vSet(P_FixedPos, p_table->pSymbol()->Y_iGet(P_FixedPos));

    if (Card_TableNm* p_tableNm = p_table->pSub_tableNm()) {
      p_tableNm = _pCard_TableNm(p_tableNm->pDupCard());
      p_table_dup->vAppendChild(p_tableNm);
    }
    if (Card_TablePtn* p_tablePtn = p_table->pSub_tablePtn()) {
      p_tablePtn = _pCard_TablePtn(p_tablePtn->pDupCard());
      p_table_dup->vAppendChild(p_tablePtn);
    }
    if (Card_TableSize* p_tableSize = p_table->pSub_tableSize()) {
      p_tableSize = _pCard_TableSize(p_tableSize->pDupCard());
      p_table_dup->vAppendChild(p_tableSize);
    }
    if (Card_RowSize* p_rowSize = p_table->pSub_rowSize()) {
      p_rowSize = _pCard_RowSize(p_rowSize->pDupCard());
      p_table_dup->vAppendChild(p_rowSize);
    }
    if (Card_RowCount* p_rowCount = p_table->pSub_rowCount()) {
      p_rowCount = _pCard_RowCount(p_rowCount->pDupCard());
      p_table_dup->vAppendChild(p_rowCount);
    }

    // A <from> with a <constraint> is the inner table of an outer join (the
    // constraint is its ON condition, evaluated by the index join that reads
    // it). It cannot head the join order; without this, an order that puts it
    // first (an empty table has the fewest estimated rows) fails validation.
    // (An outer join on the join columns alone has no constraint, only its
    // LEFT_OUTER member, XF_Prolog_Move_on: with a cost-based order the inner
    // table came first and the rows without a match were lost.)
    if (xDeck.pChild() && (xDeck.pChild()->pSub_constraint() || xDeck.pChild()->pSub_outerJoin()))
      p_table_dup->pSymbol()->Y_vSet(P_NotFirst);

    // XF_FLATTEN::vInto_KEYnotEXISTS puts the <keyExists>/<keyNotExists> on
    // the <from>, not on its <table>. Look there, or join ordering does not
    // know that this table is only probed and may put it first.
    Card* p_from = xDeck.pChild();
    Card_KeyExists*    p_keyExists    = p_table->pSub_keyExists();
    Card_KeyNotExists* p_keyNotExists = p_table->pSub_keyNotExists();
    if (! p_keyExists && ! p_keyNotExists && p_from) {
      p_keyExists    = p_from->pSub_keyExists();
      p_keyNotExists = p_from->pSub_keyNotExists();
    }

    if (p_keyExists) {
      p_keyExists = _pCard_KeyExists(p_keyExists->pDupCard());
      p_table_dup->vAppendChild(p_keyExists);
    }
    else
    if (p_keyNotExists) {
      bool bNullPasses = p_keyNotExists->pSymbol()->Y_bHas(P_NullPasses);
      p_keyNotExists = _pCard_KeyNotExists(p_keyNotExists->pDupCard());
      // (pDupCard copies no properties: NOT EXISTS, see XF_FLATTEN)
      if (bNullPasses)
        p_keyNotExists->pSymbol()->Y_vSet(P_NullPasses);
      p_table_dup->vAppendChild(p_keyNotExists);
    }
  }

  return p_tableList;
}


/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_Table* Card_TableList::pFind(char cTblSyn)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_CARD,_FOOTER_);

  i8 nChildren = nChildCards();
  for (i8 i_Child = 0; i_Child < nChildren; i_Child++) {
    Card* pChildCard = pGetChild(i_Child);
    if (CK_p_TABLE != pChildCard->eCard())
      continue;

    Card_Table* p_table = _pCard_Table(pChildCard);
    char cTableSyn = p_table->pSymbol()->cTblSyn();
    if (cTableSyn == cTblSyn)
      return p_table;
  }

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_RowCount* Card_TableList::pGetRowCount(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_Table* p_table = pFind(cTblSyn))
    return p_table->pSub_rowCount();

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_RowSize* Card_TableList::pGetRowSize(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_Table* p_table = pFind(cTblSyn))
    return p_table->pSub_rowSize();

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_TablePtn* Card_TableList::pGetTablePtn(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_Table* p_table = pFind(cTblSyn))
    return p_table->pSub_tablePtn();

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
i8 Card_TableList::nGetTableSize(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_TableSize* p_tableSize = pGetTableSize(cTblSyn))
    return p_tableSize->pSymbol()->iTo_integer(SF_INT_LIT);

  return 0;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Card_TableSize* Card_TableList::pGetTableSize(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_Table* p_table = pFind(cTblSyn))
    return p_table->pSub_tableSize();

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
Stz* Card_TableList::szToTableNm(char cTblSyn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_CARD, _FOOTER_);

  if (Card_Table* p_table = pFind(cTblSyn))
    return p_table->pSub_tableNm()->szFacet(SF_TBL_NM);

  return nullptr;
}

/*----------------------------------------------------------------------------*/
/*                                                                            */
/*----------------------------------------------------------------------------*/
void Card_TableList::vValidate(CardIter* pCI, i8 iDepth, Card* pParent)
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
