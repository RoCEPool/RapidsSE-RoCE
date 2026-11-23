/*============================================================================*/
/* PlanOperand.hpp -- Plan Operand                                            */
/*============================================================================*/
#pragma once

#include "eOM.h"

namespace Parser
{
  class PlanNode;
}

namespace STORE
{
  class PlanOperand : public Operand
  {
  protected:
    Parser::PlanNode* m_pPlanNode;
    char*             m_azSqlText;   // the statement the plan was made from (owned)
    
    // must be last
    Footer m_xFooter;

  public:
    void* operator new(size_t);
    void operator delete(void*);
  
    PlanOperand(Parser::PlanNode* pPlanNode);
   ~PlanOperand(void) noexcept(false);

    static void vRepair(Object* pObject);

    Parser::PlanNode* pPlanNode(void);
    void              vPlanNode(Parser::PlanNode* pPlanNode);
    void              vSqlText(const char* azSqlText);
    const char*       azSqlText(void);
  
  private:
    Footer* aFooter(void);
  };

  PlanOperand* _pPlanOperand(void* aAddr);
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
