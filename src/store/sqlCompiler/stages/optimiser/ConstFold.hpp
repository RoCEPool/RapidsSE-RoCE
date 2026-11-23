/*============================================================================*/
/* ConstFold.hpp - date and interval constants computed once                  */
/*============================================================================*/
#pragma once

#include "PlanNode.hpp"

namespace Parser
{
  class ConstFold
  {
  private:
    ConstFold();

  public:
    // DATE literal +/- INTERVAL literal (and INTERVAL + DATE) is replaced by
    // its value, so that it is not computed again for every row.
    static void vConstFold(PlanNode* pRootNode);
  };
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
