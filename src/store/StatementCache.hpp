/*============================================================================*/
/* StatementCache.hpp -- results of the SQL compiler and of the run-time      */
/* optimiser kept for a statement that comes again (Steps_SqlCompiler.cpp,    */
/* Steps_Optimiser.cpp)                                                       */
/*============================================================================*/
#pragma once
#include <string>
#include <vector>

namespace STORE
{
  // what the optimiser hands to the job: the optimised statement and the C
  // code, block by block; valid while the data and catalogue count is iEpoch
  struct OptCacheItem
  {
    std::vector<std::string> vsStmt;
    std::vector<std::string> vsCode;
    bool bHasCode = false;
    i8 iEpoch = 0;
  };

  i8   iColCacheDataEpoch(void);
  bool bOptCacheGet(const std::string& rsKey, i8 iEpoch, OptCacheItem& rxItem);
  void vOptCachePut(const std::string& rsKey, const OptCacheItem& rxItem);
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
