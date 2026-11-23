/*============================================================================*/
/* PlanOperand.cpp -- Plan Operand                                            */
/*============================================================================*/

#include "BASE.h"

#include "PlanOperand.hpp"


/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction(STORE,PlanOperand,CN_PLANOPERAND)
__aNewFunction(PlanOperand,CN_PLANOPERAND)
__vDeleteFunction(PlanOperand,CN_PLANOPERAND)
__aFooterFunction(PlanOperand)


/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
PlanOperand::PlanOperand(Parser::PlanNode* pPlanNode)
: Operand(CN_PLANOPERAND)
{
  _precondition_(_MAGIC_,CN_PLANOPERAND);
  
  m_pPlanNode = pPlanNode;
  m_azSqlText = nullptr;
}

/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
PlanOperand::~PlanOperand(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_);
  free(m_azSqlText);
  m_azSqlText = nullptr;
}

/*----------------------------------------------------------------------------*/
/* repair the plan operand                                                    */
/*----------------------------------------------------------------------------*/
void PlanOperand::vRepair(Object* pObject)
{
  _static_

  PlanOperand* pPlanOperand = _pPlanOperand(pObject);

  // delete the planOperand
  delete pPlanOperand;
}

/*----------------------------------------------------------------------------*/
/* get the object                                                             */
/*----------------------------------------------------------------------------*/
Parser::PlanNode* PlanOperand::pPlanNode(void)
{
  _precondition_(_ADDRESS_,_MAGIC_);
  
  return m_pPlanNode;
}

/*----------------------------------------------------------------------------*/
/* replace the plan (the old one is the caller's to delete)                   */
/*----------------------------------------------------------------------------*/
void PlanOperand::vPlanNode(Parser::PlanNode* pPlanNode)
{
  _precondition_(_ADDRESS_,_MAGIC_);
  m_pPlanNode = pPlanNode;
}

/*----------------------------------------------------------------------------*/
/* the statement text                                                         */
/*----------------------------------------------------------------------------*/
void PlanOperand::vSqlText(const char* azSqlText)
{
  _precondition_(_ADDRESS_,_MAGIC_);
  free(m_azSqlText);
  m_azSqlText = azSqlText ? strdup(azSqlText) : nullptr;
}

const char* PlanOperand::azSqlText(void)
{
  _precondition_(_ADDRESS_,_MAGIC_);
  return m_azSqlText;
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
