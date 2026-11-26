/*============================================================================*/
/* Dynamic.hpp -- Dynamic Code                                                */
/*============================================================================*/
#pragma once

#include "ClangDynamic.h"
#include "CrwBuffer.hpp"

namespace STORE
{

  class Dynamic : public Object
  {
  private:
    Dynamic_T m_xDynamicT;

  public:
    // job tag /stageTimes: the time in the generated handlers of this stage
    // and partition, the calls and the rows passed in; logged when it goes
    // (a line "stageTime <stage> ..." per stage and partition)
    bool m_bStageTimes = false;
    i8   m_nStageNs = 0;
    i8   m_nStageCalls = 0;
    i8   m_nStageRows = 0;
    s256 m_sStageName;

  private:
    // must be last
    Footer m_xFooter;
    
  public:
    void* operator new(size_t);
    void  operator delete(void*);

    Dynamic(LogicUnit* pLogicUnit);
   ~Dynamic(void) noexcept(false);
    
    static void vRepair(Object* pObject);


  public:
    Dynamic_T* aDynamicT(void);
  
    Footer* aFooter(void);
  };
  
  Dynamic* _pDynamic(void* aAddr);
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
