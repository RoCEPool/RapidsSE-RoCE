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
    // statement cache (Steps_SqlCompiler.cpp): the pipeline statement of a
    // cache hit (no plan then), or the key to store it under (both owned)
    char*             m_azCachedPipeline;
    char*             m_azCacheKey;
    i8                m_iCacheEpoch;
    
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
    void              vCachedPipeline(const char* azPipeline);
    const char*       azCachedPipeline(void);
    void              vCacheKey(const char* azKey, i8 iEpoch);
    const char*       azCacheKey(void);
    i8                iCacheEpoch(void);
  
  private:
    Footer* aFooter(void);
  };

  PlanOperand* _pPlanOperand(void* aAddr);
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
