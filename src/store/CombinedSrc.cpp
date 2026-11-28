
/*============================================================================*/
/* CombinedSrc.cpp -- Combined source                                         */
/*============================================================================*/

#include <string>
#include <cstring>
#include <vector>
#include "BASE.h"

#include "Stream.hpp"
#include "StreamDef.hpp"
#include "LogicUnit.hpp"
#include "Index.hpp"
#include "SN_Dte.hpp"
#include "SN_Tme.hpp"
#include "SN_Tsp.hpp"
#include "SN_Bol.hpp"
#include "SN_Flt.hpp"
#include "SN_Bcd.hpp"
#include "CodeSynthUtils.hpp"
#include "AM.hpp"

/*----------------------------------------------------------------------------*/
/*-- LifeCycle ---------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

__pCastFunction(STORE,CombinedSrc,CN_COMBINEDSRC)
__aNewFunction(CombinedSrc,CN_COMBINEDSRC)
__vDeleteFunction(CombinedSrc,CN_COMBINEDSRC)
__aFooterFunction(CombinedSrc)

/*----------------------------------------------------------------------------*/
/* constructor                                                                */
/*----------------------------------------------------------------------------*/
CombinedSrc::CombinedSrc(void)
: Object(CN_COMBINEDSRC)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

#ifdef WITH_CHECK_INITIALISED
  MemLib::vZero(m_caCode,sizeof(m_caCode),CN_COMBINEDSRC);
#endif

  m_azCodeCrs   = m_caCode;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Open the common code fragment files
  bool bOpen = bOpenCodeFiles();
  if ( ! bOpen ) {
    return;
  }

  _checkInitialised_(CN_COMBINEDSRC);
  
  _postcondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);
}


/*----------------------------------------------------------------------------*/
/* destructor                                                                 */
/*----------------------------------------------------------------------------*/
CombinedSrc::~CombinedSrc(void) noexcept(false)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  // close the files

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // Close the common code fragment files
  vCloseCodeFiles();

  delete(m_pFileHeader);
  delete(m_pFunctionHeader);
  delete(m_pFunctionFooter);
  delete(m_pCommonCode);
}

/*----------------------------------------------------------------------------*/
/*-- Static ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*-- Public ------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* Synthesize code for the combined operators                                 */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vSynthesizeCode(Stencil* pStencil, bool bFirstStage, bool bLastStage)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  bool bOpened;
  bool bHasIndexInflate = false;
	bool bHasOrdAgg = false;
  char* azPruneStart = nullptr;   // start of a table-scan inflate whose columns may be pruned
  char* azHandlerStart = nullptr; // the table-buffer handler being made
  i8 iPruneEnd = -1;              // sketch that ends the consumers of the inflated row
  std::vector<char*> vJoinPrune;  // starts of index joins whose unread columns may be skipped
  bool bStageGbyAgg = false;      // the stage has a group by (its EOS emits the groups)
  if (bFirstStage) {
    m_iHgBefore = -1;
    m_iHgInside = -1;
  }
  std::vector<std::string> vsHashJoinStages;   // stages whose first operator is a hash join
  const bool bJoinPrune = ! AM::JOB::bHasTag("<optimiser_off_join_prune>"_Stz) &&
                          ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz);
  // An index (or cross) join inflates the columns of the joined table into its
  // row; every later reader in the handler names the cells it reads, so a
  // column no code of the handler names is skipped (see vPruneInflate). This
  // runs when the handler is complete, just before its footer.
  // Then (columnar index join) the columns still decoded are read from the
  // column cache of the joined buffer when it has them (vColCacheJoin); last
  // join first, as each insertion moves the code after it. Job tag
  // /optimiser_off_col_cache_join decodes every joined row.
  const bool bColCacheJoin = bJoinPrune &&
                             ! AM::JOB::bHasTag("<optimiser_off_col_cache>"_Stz) &&
                             ! AM::JOB::bHasTag("<optimiser_off_col_cache_join>"_Stz);
  auto vJoinPruneFlush = [this, &vJoinPrune, bColCacheJoin]() {
    for (char* azStart : vJoinPrune)
      vPruneInflate(azStart, m_azCodeCrs);
    if (bColCacheJoin)
      for (auto it = vJoinPrune.rbegin(); it != vJoinPrune.rend(); ++it)
        vColCacheJoin(*it, m_azCodeCrs);
    vJoinPrune.clear();
  };
	s32 sLabel_ordAgg;

  eHK ePreviousHandler = HK_NONE;
  eHK eCurrentHandler  = HK_NONE;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // new the file pointers
  File* pOperatorFile = new File(this);
  File* pIndexFile    = new File(this);
  File* pInflateFile  = new File(this);
  File* pFilterFile   = new File(this);

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // append the file headers if firstStage
  if (bFirstStage)
    vAppendFileHeaders();

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // generate code for each operator
  for (i8 iOpSketch = 0; iOpSketch < pStencil->nItems(); iOpSketch++) {

    OpSketch* pOpSketch = pStencil->pGet(iOpSketch);

    eCurrentHandler = pOpSketch->m_eHandlerKind;

    switch(pOpSketch->eSketchKind()) {

      case SK_ANTIJOIN:
      case SK_SEMIJOIN:

        bHasIndexInflate = true;
        // (the joined row is read by the join's constraint only: the columns
        // it does not name are skipped, the others come from the column cache)
        if (bJoinPrune && ! AM::JOB::bHasTag("<optimiser_off_semi_join_prune>"_Stz))
          vJoinPrune.push_back(m_azCodeCrs);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the index, indexJoin, inflate  and filter code fragments file

        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/IndexJoin.cpp"_Stz);
        RASSERT ( bOpened );

        bOpened = pInflateFile->bOpenRead("../src/store/Fragments/InflateCrw.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator begin
        vOperatorBegin(pOpSketch);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // initialize the indexJoin
        if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT) {
          char* azInit = m_azCodeCrs;
          vAppendCodeFragment(pOpSketch, "vIndexJoinIntKey"_Stz, pOperatorFile);
          vJoinHashAttach(pOpSketch, azInit);
        }
        else
          vAppendCodeFragment(pOpSketch, "vIndexJoinGenKey"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // generate expressions for null check
        if (pOpSketch->m_pNullStack->nValues()) {
            char* azNullCheck = m_azCodeCrs;
            vAppendCodeFragment(pOpSketch, "vNullCheck"_Stz);
            // NOT EXISTS: a row whose key is NULL has no match and goes on
            if (pOpSketch->m_bNullPasses)
              vNullPasses(pOpSketch, azNullCheck);
        }

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // generate expressions for populating the key
        vAppendCodeFragment(pOpSketch, "vExprStack"_Stz);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // position at lower bound
				if (pOpSketch->eSketchKind() == SK_ANTIJOIN) {
					if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
						vAppendCodeFragment(pOpSketch, "vIndexAntiJoinPosition_int"_Stz, pOperatorFile);
					else
						vAppendCodeFragment(pOpSketch, "vIndexAntiJoinPosition_gen"_Stz, pOperatorFile);
				}
				else {
					if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
					  vAppendCodeFragment(pOpSketch, "vIndexJoinPosition_int"_Stz, pOperatorFile);
					else
						vAppendCodeFragment(pOpSketch, "vIndexJoinPosition_gen"_Stz, pOperatorFile);
				}

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // dereference the join
        vAppendCodeFragment(pOpSketch, "vIndexJoinDereference"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // inflate the right table columns
        vAppendCodeInflateRightCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // generate code for the constraint
        RASSERT(pOpSketch->m_pConstraintStack->nValues());

        if (pOpSketch->eSketchKind() == SK_ANTIJOIN)
          vAppendCodeFragment(pOpSketch, "iIndexAntiJoinConstraint"_Stz, pOperatorFile);
        else
          vAppendCodeFragment(pOpSketch, "iIndexSemiJoinConstraint"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the indexJoin code fragments file
        pInflateFile->vClose();
        pOperatorFile->vClose();

        break;

      case SK_EOS:
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // end previous handler (by appending its footer)
				pOpSketch->m_sLabelOrdAgg = sLabel_ordAgg;
        vJoinPruneFlush();
        vAppendFunctionFooter(pOpSketch, ePreviousHandler, bHasIndexInflate, bHasOrdAgg);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // begin new EOS handler (by appending its function header)
        vAppendFunctionHeader(pOpSketch, eCurrentHandler, pStencil->szStageNm(), bHasIndexInflate);

        // a hash join probes now, through the row handler of the stage
        for (const std::string& sStage : vsHashJoinStages)
          if (sStage == pStencil->szStageNm()->azChar()) {
            i8 iHjFlags = (AM::JOB::bHasTag("<hash_join_walk>"_Stz) ? 1 : 0) |
                          (AM::JOB::bHasTag("<hash_join_replay>"_Stz) ? 2 : 0) |
                          (AM::JOB::bHasTag("<optimiser_off_hash_join_bloom>"_Stz) ? 4 : 0) |
                          (AM::JOB::bHasTag("<hash_join_trace>"_Stz) ? 8 : 0) |
                          (AM::JOB::bHasTag("<optimiser_off_hash_join_scan>"_Stz) ? 16 : 0);
            std::string sProbe =
                "  if (aDynamicT->m_xIndexInfoJ[0].m_aHashJoin &&\n"
                "      (aDynamicT->m_bHashProbe = iExtern_HashJoinMode(&aDynamicT->m_xIndexInfoJ[0], " +
                std::to_string((long long)iHjFlags) + ")) != 0) {\n"
                "    iPdsRowHandler_" + sStage + "(aDynamicT);\n"
                "    aDynamicT->m_bHashProbe = 0;\n"
                "    if (aDynamicT->m_iErrorCode != (i8)EC_NONE)\n"
                "      return 0;\n"
                "  }\n";
            vAppendText(sProbe.c_str());
          }
        break;

      case SK_AGGREG:

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the aggregate code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/Aggreg.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator begin
        //vOperatorBegin(pOpSketch);

				vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
				vAppendStencilTo(pOpSketch);
				vAppendCodeFragment(pOpSketch, "vAggregBegin"_Stz, pOperatorFile);

        vAppendCodeFragment(pOpSketch, "vAggregInitBegin"_Stz, pOperatorFile);
        vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
        vAppendCodeFragment(pOpSketch, "iAggregInitEnd"_Stz, pOperatorFile);

        vAppendCodeFragment(pOpSketch, "vAggregColsBegin"_Stz, pOperatorFile);
        vGenerateExprCode(pOpSketch->m_pExprStack, "    "_Stz);
        vAppendCodeFragment(pOpSketch, "iAggregColsEnd"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the aggregate code fragments file
        pOperatorFile->vClose();

        break;

      case SK_FILTER:
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the filter code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/Filter.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator begin
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

        //vAppendStencilTo(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vFilterBegin"_Stz, pOperatorFile);
        vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);
        vAppendCodeFragment(pOpSketch, "vExprCheck"_Stz);
        vAppendCodeFragment(pOpSketch, "vFilterEnd"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the filter code fragments file
        pOperatorFile->vClose();
        break;

      case SK_GBYAGG:
        bStageGbyAgg = true;

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the gbyagg code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/GbyAgg.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

        // create indexDef and indexTree
        vAppendCodeFragment(pOpSketch, "iGbyAggBegin"_Stz, pOperatorFile);
        vAppendCodeFragment(pOpSketch, "vIndexDefBegin"_Stz);
        vAppendCodeIndexCols(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vIndexDefContinue"_Stz);

				// populate the payload key
				vAppendCodeFragment(pOpSketch, "iResetPLKey"_Stz);
				vGenerateExprCode(pOpSketch->m_pGbyStack, "  "_Stz);
				vAppendCodeFragment(pOpSketch, "vExprCheck"_Stz);

				if ( ! pOpSketch->m_pIndexDef->bHasDescending() &&
				       pOpSketch->m_pIndexDef->eKeyKind() == KE_GENERAL) {
					vAppendCodeFragment(pOpSketch, "iGbyAggFindInsertGenAsc"_Stz, pOperatorFile);
					vAppendCodeFragment(pOpSketch, "iGbyAggGenAscBegin"_Stz, pOperatorFile);
					vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
					vAppendCodeFragment(pOpSketch, "iGbyAggGenAscEnd"_Stz, pOperatorFile);
				}
				else if ( ! pOpSketch->m_pIndexDef->bHasDescending() &&
				          pOpSketch->m_pIndexDef->eKeyKind() == KE_INT &&
				          ! AM::JOB::bHasTag("<optimiser_off_hash_gbyagg>"_Stz) &&
				          ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz)) {
					// the groups in a hash table (Fragments/GbyAgg.cpp, IndexTree.cpp)
					vAppendCodeFragment(pOpSketch, "vGbyAggHashIntKey"_Stz, pOperatorFile);
					vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
					vAppendCodeFragment(pOpSketch, "iGbyAggIntKeyEnd"_Stz, pOperatorFile);
				}
				else {

					// find the key
					if (pOpSketch->m_pIndexDef->bHasDescending())
						vAppendCodeFragment(pOpSketch, "vGbyAggFindKeyAscDsc"_Stz, pOperatorFile);
					else if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
						vAppendCodeFragment(pOpSketch, "vGbyAggFindIntKeyAsc"_Stz, pOperatorFile);
					else
						vAppendCodeFragment(pOpSketch, "vGbyAggFindKeyAsc"_Stz, pOperatorFile);

					// create the payload
					if (pOpSketch->m_nPayloadBytes)
						vAppendCodeFragment(pOpSketch, "vCreatePayLoad"_Stz);

					// insert key
					if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT) {
						vAppendCodeFragment(pOpSketch, "iIndexInsertIntKey"_Stz);
						vAppendCodeFragment(pOpSketch, "vIndexInsertContinue"_Stz);
						vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
						vAppendCodeFragment(pOpSketch, "iGbyAggIntKeyEnd"_Stz, pOperatorFile);
					}
					else {
						vAppendCodeFragment(pOpSketch, "iIndexInsertVchKey"_Stz);
						vAppendCodeFragment(pOpSketch, "vIndexInsertContinue"_Stz);
						vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
						vAppendCodeFragment(pOpSketch, "iGbyAggGenKeyEnd"_Stz, pOperatorFile);
					}
				}

        // populate the payload
        vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);
        vAppendCodeFragment(pOpSketch, "vExprCheck"_Stz);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the gbyAgg code fragments file
        pOperatorFile->vClose();
        break;

      case SK_ORDAGG:

				bHasOrdAgg = true;
				sLabel_ordAgg = pOpSketch->m_sLabel;

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// open the aggregate code fragments file
				bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/OrdAgg.cpp"_Stz);
				RASSERT ( bOpened );

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// operator begin
				//vOperatorBegin(pOpSketch);

				vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
				vAppendStencilTo(pOpSketch);
				vAppendCodeFragment(pOpSketch, "iOrdAggBegin"_Stz, pOperatorFile);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// close the aggregate code fragments file
				pOperatorFile->vClose();

        break;

      case SK_PROJECT:

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the project code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/Project.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator begin
        vOperatorBegin(pOpSketch);

        vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);
        vAppendCodeFragment(pOpSketch, "iProjectEnd"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the project code fragments file
        pOperatorFile->vClose();

        break;

			case SK_PRJPOT:

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// open the project code fragments file
				bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/Project.cpp"_Stz);
				RASSERT ( bOpened );

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// operator stub
				vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
				vAppendStencilTo(pOpSketch);

				vAppendCodeFragment(pOpSketch, "iPotTubBegin"_Stz);
				vAppendCodePixCols(pOpSketch);
				vAppendCodeFragment(pOpSketch, "vPotTubRowPix"_Stz);

				vAppendCodeHashCols2(pOpSketch);

				vAppendCodeFragment(pOpSketch, "iPotTubContinue_pds"_Stz);

				vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// close the project code fragments file
				pOperatorFile->vClose();

				break;

			case SK_PRJTUB:

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// open the project code fragments file
				bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/Project.cpp"_Stz);
				RASSERT ( bOpened );

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// operator stub
				vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
				vAppendStencilTo(pOpSketch);

				vAppendCodeFragment(pOpSketch, "iPotTubBegin"_Stz);
				vAppendCodePixCols(pOpSketch);
				vAppendCodeFragment(pOpSketch, "vPotTubRowPix"_Stz);

				vAppendCodeFragment(pOpSketch, "iPotTubContinue_pds"_Stz);

				vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// close the project code fragments file
				pOperatorFile->vClose();

				break;

      case SK_INDEXSCAN:

        bHasIndexInflate = true;

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the indexScan code fragments file
        bOpened = pIndexFile->bOpenRead("../src/store/Fragments/Index.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the index code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/IndexScan.cpp"_Stz);
        RASSERT ( bOpened );
        
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // new INDEXBUFFER handler
        azHandlerStart = m_azCodeCrs;
        vAppendFunctionHeader(pOpSketch, eCurrentHandler, pStencil->szStageNm(), bHasIndexInflate);
        // a stream of replicated tables only: read in the first partition
        if (pOpSketch->m_bScanOnce)
          vScanOnce(azHandlerStart);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
        vAppendStencilTo(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vIndexScanBegin"_Stz, pOperatorFile);

        // No lower bound
        if (pOpSketch->m_eLowerIP == IP_NONE) {
          vAppendCodeFragment(pOpSketch, "vIndexPositionBegin"_Stz, pIndexFile);
        }
        else {
          vGenerateExprCode(pOpSketch->m_pLowerStack, "  "_Stz);
          vAppendCodeFragment(pOpSketch, "vExprCheck"_Stz);
          vAppendCodeFragment(pOpSketch, "vIndexPositionLower"_Stz, pIndexFile);
        }

        if (pOpSketch->m_eUpperIP != IP_NONE) {
          vGenerateExprCode(pOpSketch->m_pUpperStack, "  "_Stz);
          vAppendCodeFragment(pOpSketch, "vExprCheck"_Stz);
          vAppendCodeFragment(pOpSketch, "vIndexPositionUpper"_Stz, pIndexFile);
          vAppendCodeFragment(pOpSketch, "vIndexDereferenceUB"_Stz, pIndexFile);
        }
        else {
          vAppendCodeFragment(pOpSketch, "vIndexDereferenceLB"_Stz, pIndexFile);
        }

        // open the inflateCrw code fragments file
        bOpened = pInflateFile->bOpenRead("../src/store/Fragments/InflateCrw.cpp"_Stz);
        RASSERT ( bOpened );
        vAppendCodeInflateCols(pOpSketch, "L_RetT_ColIS"_Stz, pInflateFile);

        vAppendCodeFragment(pOpSketch, "vIndexReturn"_Stz, pIndexFile);

        vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the indexScan code fragments file
        pInflateFile->vClose();
        pOperatorFile->vClose();
        pIndexFile->vClose();

        break;

      case SK_INDEXJOIN:

        bHasIndexInflate = true;
        if (bJoinPrune)
          vJoinPrune.push_back(m_azCodeCrs);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the indexJoin code fragments file

        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/IndexJoin.cpp"_Stz);
        RASSERT ( bOpened );

        bOpened = pInflateFile->bOpenRead("../src/store/Fragments/InflateCrw.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator begin
				vOperatorBegin(pOpSketch);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // initialize with key kind
        if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT) {
          char* azInit = m_azCodeCrs;
          vAppendCodeFragment(pOpSketch, "vIndexJoinIntKey"_Stz, pOperatorFile);
          vJoinHashAttach(pOpSketch, azInit);
        }
        else
          vAppendCodeFragment(pOpSketch, "vIndexJoinGenKey"_Stz, pOperatorFile);

        if (pOpSketch->m_bCrossJoin) {

					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// generate expressions for null check
					if (pOpSketch->m_pNullStack->nValues()) {
							vAppendCodeFragment(pOpSketch, "vNullCheck"_Stz);
					}

					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// generate expressions for populating the key
					vAppendCodeFragment(pOpSketch, "vExprStack"_Stz);

          vAppendCodeFragment(pOpSketch, "vIndexJoinCrossPosition"_Stz, pOperatorFile);
          vAppendCodePpgCols(pOpSketch, "vCopyVchCol"_Stz);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // dereference the join
          vAppendCodeFragment(pOpSketch, "vIndexJoinDereference"_Stz, pOperatorFile);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // inflate the right tables injected columns
					//vAppendCodeInflateInjCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);
          vAppendCodeInflateCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);

          if (pOpSketch->m_pConstraintStack->nValues()) {
            vAppendCodeFragment(pOpSketch, "iIndexJoinConstraint"_Stz, pOperatorFile);
          }
          else {
            vAppendCodeFragment(pOpSketch, "vIndexJoinReturn"_Stz, pOperatorFile);
          }

        }

        else if (pOpSketch->m_bLeftOuter) {

					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// generate expressions for null check
					if (pOpSketch->m_pNullStack->nValues()) {
						vAppendCodeFragment(pOpSketch, "vIndexLeftOuterNullCheck"_Stz, pOperatorFile);
					}

					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// generate expressions for populating the key
					vAppendCodeFragment(pOpSketch, "vExprStack"_Stz);

					if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
						vAppendCodeFragment(pOpSketch, "vIndexLeftOuterJoinPosition_int"_Stz, pOperatorFile);
					else
						vAppendCodeFragment(pOpSketch, "vIndexLeftOuterJoinPosition_gen"_Stz, pOperatorFile);

					vAppendCodePpgCols(pOpSketch, "vCopyVchCol"_Stz);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // dereference the join
          vAppendCodeFragment(pOpSketch, "vIndexJoinDereference"_Stz, pOperatorFile);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // inflate the right tables injected columns
					//vAppendCodeInflateInjCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);
          vAppendCodeInflateCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);

          if (pOpSketch->m_pConstraintStack->nValues()) {
            vAppendCodeFragment(pOpSketch, "iIndexJoinConstraint"_Stz, pOperatorFile);
          }
          else {
            vAppendCodeFragment(pOpSketch, "vIndexJoinReturn"_Stz, pOperatorFile);
          }

					vAppendCodeFragment(pOpSketch, "vIndexJoinContinueLO1"_Stz, pOperatorFile);
					vAppendCodePpgCols(pOpSketch, "vCopyVchCol"_Stz);

					vAppendCodeNullRightCols(pOpSketch, "vCopyNulls"_Stz);
					vAppendCodeFragment(pOpSketch, "vIndexJoinContinueLO2"_Stz, pOperatorFile);
        }

        else {
					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// generate expressions for null check
					if (pOpSketch->m_pNullStack->nValues()) {
					  vAppendCodeFragment(pOpSketch, "vNullCheck"_Stz);
					}

					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// generate expressions for populating the key
					vAppendCodeFragment(pOpSketch, "vExprStack"_Stz);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // position the index (or build and probe a hash join)
          if (bHashJoinEligible(pStencil, iOpSketch, eCurrentHandler)) {
            vAppendCodeHashJoin(pOpSketch, pOperatorFile);
            vsHashJoinStages.push_back(pStencil->szStageNm()->azChar());
          }
					else if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
            vAppendCodeFragment(pOpSketch, "vIndexJoinPosition_int"_Stz, pOperatorFile);
					else
						vAppendCodeFragment(pOpSketch, "vIndexJoinPosition_gen"_Stz, pOperatorFile);

					vAppendCodePpgCols(pOpSketch, "vCopyVchCol"_Stz);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // dereference the join
          vAppendCodeFragment(pOpSketch, "vIndexJoinDereference"_Stz, pOperatorFile);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // inflate the right tables injected columns
					//vAppendCodeInflateInjCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);
          vAppendCodeInflateCols(pOpSketch, "L_RetT_ColIJ"_Stz, pInflateFile);

          if (pOpSketch->m_pConstraintStack->nValues()) {
            vAppendCodeFragment(pOpSketch, "iIndexJoinConstraint"_Stz, pOperatorFile);
          }
          else {
            vAppendCodeFragment(pOpSketch, "vIndexJoinReturn"_Stz, pOperatorFile);
          }
        }


        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the indexJoin code fragments file
        pInflateFile->vClose();
        pOperatorFile->vClose();

        break;

      case SK_INFLATE_CRW:
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the inflateCrw code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/InflateCrw.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // new CRWBUFFER handler
        azHandlerStart = m_azCodeCrs;
        vAppendFunctionHeader(pOpSketch, eCurrentHandler, pStencil->szStageNm(), bHasIndexInflate);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

        // a stream of replicated tables only: read in the first partition
        if (pOpSketch->m_bScanOnce)
          vScanOnce(azHandlerStart);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // append to-stencil
        vAppendStencilTo(pOpSketch);

        // Projection pushdown into the scan (DuckDB reads only the columns a query
        // uses): when the inflated row is read only by filters and then a project
        // in this handler, columns those operators never name are skipped, not
        // inflated (see vPruneInflate).
        if (! AM::JOB::bHasTag("<optimiser_off_scan_prune>"_Stz) &&
            ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz)) {
          i8 j = iOpSketch + 1;
          while (j < pStencil->nItems() &&
                 pStencil->pGet(j)->eSketchKind() == SK_FILTER &&
                 pStencil->pGet(j)->m_eHandlerKind == eCurrentHandler)
            j++;
          if (j < pStencil->nItems() &&
              pStencil->pGet(j)->eSketchKind() == SK_PROJECT &&
              pStencil->pGet(j)->m_eHandlerKind == eCurrentHandler) {
            azPruneStart = m_azCodeCrs;
            iPruneEnd = j;
          }
        }
        {
          char* azInflateStart = m_azCodeCrs;
          vAppendCodeFragment(pOpSketch, "vInflateCrwBegin"_Stz, pOperatorFile);
          vAppendCodeInflateCols(pOpSketch, "L_RetT_ColT"_Stz, pOperatorFile);
          vAppendCodeFragment(pOpSketch, "vInflateCrwReturn"_Stz, pOperatorFile);
          // a scan whose columns are not pruned (no filter and project behind
          // it, e.g. a GROUP BY right after the scan) reads all of them: the
          // column cache serves them now; a pruned scan is served after pruning
          if (! azPruneStart &&
              ! AM::JOB::bHasTag("<optimiser_off_col_cache>"_Stz) &&
              ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
            vColCacheInflate(azInflateStart, m_azCodeCrs);
        }

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the inflateCrw code fragments file
        pOperatorFile->vClose();
        break;

      case SK_INFLATE_ROW:

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// open the inflateRow code fragments file
				bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/InflateRow.cpp"_Stz);
				RASSERT ( bOpened );

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// new ROWBUFFER handler
				vAppendFunctionHeader(pOpSketch, eCurrentHandler, pStencil->szStageNm(), bHasIndexInflate);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// operator stub
				vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// append to stencil
				vAppendStencilTo(pOpSketch);

				vAppendCodeFragment(pOpSketch, "vInflatePdsRowBegin"_Stz, pOperatorFile);
				//vAppendCodeInflateRowCols(pOpSketch, "vInflatePdsRowCol"_Stz, pOperatorFile);
        vAppendCodeFragment(pOpSketch, "vInflateRowReturn"_Stz, pOperatorFile);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// close the inflateRow code fragments file
				pOperatorFile->vClose();

        break;

      case SK_LIMIT:
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // open the limit code fragments file
        bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/Limit.cpp"_Stz);
        RASSERT ( bOpened );

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
        vAppendStencilTo(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vLimit"_Stz, pOperatorFile);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // close the inflateRow code fragments file
        pOperatorFile->vClose();

        break;

      case SK_SORT:
      case SK_TOP:
#ifdef TOP_FULLBUFFER
        // Apply an optimisation where we can send the pdsBuffer as is to the indexTreeTop function
        if (pOpSketch->eSketchKind() == SK_TOP && pOpSketch->m_iOperatorNo == 1) {
          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // new ROWBUFFER handler
          vAppendFunctionHeader(pOpSketch, eCurrentHandler, pStencil->szStageNm(), bHasIndexInflate);

          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // operator stub
          vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

          vAppendCodeFragment(pOpSketch, "vIndexDefBegin"_Stz);
          vAppendCodeIndexCols(pOpSketch);
          vAppendCodeFragment(pOpSketch, "vIndexDefContinue"_Stz);
          vAppendCodeFragment(pOpSketch, "vIndexTreeTop"_Stz);
          break;
        }
#endif
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

        vAppendCodeFragment(pOpSketch, "iSortBegin"_Stz);
        vAppendCodeFragment(pOpSketch, "iResetPLKey"_Stz);
        vAppendCodeFragment(pOpSketch, "vIndexDefBegin"_Stz);
        vAppendCodeIndexCols(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vIndexDefContinue"_Stz);
        vGenerateExprCode(pOpSketch->m_pExprStack, "  "_Stz);
        vAppendCodeFragment(pOpSketch, "vExprCheck"_Stz);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // create the pay load
        if (pOpSketch->m_nPayloadBytes)
          vAppendCodeFragment(pOpSketch, "vCreatePayLoad"_Stz);

        if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT) {
          vAppendCodeFragment(pOpSketch, "iIndexInsertIntKey"_Stz);
        }
        else {
          vAppendCodeFragment(pOpSketch, "iIndexInsertVchKey"_Stz);
        }
        vAppendCodePopulatePayloadCols(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vCloseBraces"_Stz);
        break;

      case SK_STREAMDEF:

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // end previous handler function
				vJoinPruneFlush();
        vAppendFunctionFooter(pOpSketch, ePreviousHandler, bHasIndexInflate, bHasOrdAgg);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // begin new StreamDef handler function
        vAppendFunctionHeader(pOpSketch, eCurrentHandler, pStencil->szStageNm(), bHasIndexInflate);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // StreamDefOut generation
        vAppendCodeFragment(pOpSketch, "iStreamDefBegin"_Stz);
        vAppendCodePixCols(pOpSketch, bLastStage );
        vAppendCodeFragment(pOpSketch, "vStreamDefEnd"_Stz);
        break;

      case SK_TUB:
      case SK_POT:

#ifdef WITH_ROWBUFFER
        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // operator stub
        vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
				vAppendStencilFrom(pOpSketch);
        vAppendStencilTo(pOpSketch);

        vAppendCodeFragment(pOpSketch, "iPotTubBegin"_Stz);
        vAppendCodePixCols(pOpSketch);
        vAppendCodeFragment(pOpSketch, "vPotTubRowPix"_Stz);

        // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
        // POT/TUB in EOS handler
        if (pOpSketch->m_eHandlerKind == HK_EOS) {
          if (pOpSketch->m_eStencilFrom == SR_PL) {

            // (the groups of the last group by may be filtered: <havingGroups>)
            bool bHg = bStageGbyAgg && m_bHavingGroupsHere && pOpSketch->m_pIndexDef->eKeyKind() == KE_INT &&
                       AM::JOB::bHasTag("<havingGroups>"_Stz);
            if (bHg)
              m_iHgBefore = m_azCodeCrs - m_caCode;
            if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
              vAppendCodeFragment(pOpSketch, "vEosPLIntKeyTraversal"_Stz);
            else
              vAppendCodeFragment(pOpSketch, "vEosPLGenKeyTraversal"_Stz);
            if (bHg)
              m_iHgInside = m_azCodeCrs - m_caCode;

						// generate code for the hash value
						if (pOpSketch->eSketchKind() == SK_POT)
							vAppendCodePLToHashCols(pOpSketch);

            if ( ! pOpSketch->m_sLimitExpr.bSame(""_Stz)) {
              vAppendCodeFragment(pOpSketch, "vEosPotTubLimit"_Stz);
            }

            vAppendCodeFragment(pOpSketch, "iEosPLPotTub"_Stz);
            vAppendCodeToTubCols(pOpSketch);
            vAppendCodeFragment(pOpSketch, "vEosPLPotTubEnd"_Stz);
          }
          else {
            vAppendCodeFragment(pOpSketch, "iEosPotTubBegin"_Stz);
            vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
            vAppendCodeFragment(pOpSketch, "iEosPotTubBody"_Stz);
						vAppendCodeTubCols(pOpSketch, "vPotTubVchCol"_Stz);
          }

        }
        else {
          // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
          // POT/TUB in CRW/ROW/INDEX handler

					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// does the tub have an ordAgg constraint to apply ?
					if (pOpSketch->m_pConstraintStack->nValues()) {
						// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
						// open the ordAgg code fragments file
						bOpened = pOperatorFile->bOpenRead("../src/store/Fragments/OrdAgg.cpp"_Stz);
						RASSERT ( bOpened );

						pOpSketch->m_sLabelOrdAgg = sLabel_ordAgg;
						vAppendCodeFragment(pOpSketch, "iOrdAggConstraint"_Stz, pOperatorFile);

						// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
						// close the ordAgg code fragments file
						pOperatorFile->vClose();
					}

          // generate code for the hash value
          if (pOpSketch->eSketchKind() == SK_POT)
						vAppendCodeHashCols(pOpSketch);

          vAppendCodeFragment(pOpSketch, "iPotTubContinue"_Stz);
          vAppendCodeTubCols(pOpSketch, "vPotTubVchCol"_Stz);
        }
        break;

#endif

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// operator stub
				vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
				vAppendStencilTo(pOpSketch);

				vAppendCodeFragment(pOpSketch, "iPotTubBegin"_Stz);
				vAppendCodePixCols(pOpSketch);
				vAppendCodeFragment(pOpSketch, "vPotTubRowPix"_Stz);

				// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
				// POT/TUB in EOS handler
				if (pOpSketch->m_eHandlerKind == HK_EOS) {
					if (pOpSketch->m_eStencilFrom == SR_PL) {

            // (the groups of the last group by may be filtered: <havingGroups>)
            bool bHg = bStageGbyAgg && m_bHavingGroupsHere && pOpSketch->m_pIndexDef->eKeyKind() == KE_INT &&
                       AM::JOB::bHasTag("<havingGroups>"_Stz);
            if (bHg)
              m_iHgBefore = m_azCodeCrs - m_caCode;
						if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT)
							vAppendCodeFragment(pOpSketch, "vEosPLIntKeyTraversal"_Stz);
						else
							vAppendCodeFragment(pOpSketch, "vEosPLGenKeyTraversal"_Stz);
            if (bHg)
              m_iHgInside = m_azCodeCrs - m_caCode;

						// generate code for the hash value
						if (pOpSketch->eSketchKind() == SK_POT)
							vAppendCodePLToHashCols(pOpSketch);

						if ( ! pOpSketch->m_sLimitExpr.bSame(""_Stz)) {
							vAppendCodeFragment(pOpSketch, "vEosPotTubLimit"_Stz);
						}

						vAppendCodeFragment(pOpSketch, "iEosPLPotTub_pds"_Stz);
						vAppendCodeToTubCols(pOpSketch);
						vAppendCodeFragment(pOpSketch, "vEosPLPotTubEnd"_Stz);
					}
					else {
						vAppendCodeFragment(pOpSketch, "iEosPotTubBegin"_Stz);
						vGenerateExprCode(pOpSketch->m_pInitStack, "    "_Stz);
						vAppendCodeFragment(pOpSketch, "iEosPotTubBody_pds"_Stz);
						vAppendCodeTubCols(pOpSketch, "vPotTubVchCol"_Stz);
					}

				}
				else {
					// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
					// POT/TUB in CRW/ROW/INDEX handler

					// generate code for the hash value
					if (pOpSketch->eSketchKind() == SK_POT)
						vAppendCodeHashCols(pOpSketch);

					vAppendCodeFragment(pOpSketch, "iPotTubContinue_pds"_Stz);
					vAppendCodeTubCols(pOpSketch, "vPotTubVchCol"_Stz);
				}
				break;

      default:
        NEVER_REACH
    }

    if (iOpSketch == iPruneEnd) {
      vPruneInflate(azPruneStart, m_azCodeCrs);
      // Columnar scan (doc/design/columnar-scan.md): read the cells from the
      // column cache of the buffer. Job tag /optimiser_off_col_cache decodes
      // the rows as before.
      if (! AM::JOB::bHasTag("<optimiser_off_col_cache>"_Stz) &&
          ! AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
        vColCacheInflate(azPruneStart, m_azCodeCrs);
      azPruneStart = nullptr;
      iPruneEnd = -1;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // save previous handler
    ePreviousHandler = eCurrentHandler;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // end StreamDef handler
  RASSERT(eCurrentHandler == HK_STREAMDEF);
  vJoinPruneFlush();
  vAppendFunctionFooter(nullptr, eCurrentHandler, bHasIndexInflate);

  if (m_iHgInside >= 0)
    vAppendCodeHavingGroups();

  // The expression stack of a handler is an array of the handler
  // (vFLAT_FunctionLocals.h) and no longer the one of Dynamic_T: only
  // generated code uses it, a handler leaves it empty, and the C compiler can
  // keep a stack that nothing else points to in registers, where every push
  // to the shared one was a store that might change any other field read
  // through aDynamicT. Same length, in place (the code is found by offsets).
  if (! AM::JOB::bHasTag("<optimiser_off_local_stack>"_Stz)) {
    static const char szShared[] = "aDynamicT->m_aW[0]";
    static const char szLocal[]  = "aLocalW[0]        ";
    for (char* a = strstr(m_caCode, szShared); a; a = strstr(a, szShared))
      memcpy(a, szLocal, sizeof(szLocal) - 1);
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // delete the code file pointers
  delete pOperatorFile;
  delete pIndexFile;
  delete pInflateFile;
  delete pFilterFile;
}

/*----------------------------------------------------------------------------*/
/* HAVING over the groups kept in memory (job tag <havingGroups>, set by the  */
/* SQL compiler: Steps_SqlCompiler.cpp, bRewriteHavingGroups). At the end of  */
/* the input of the last group by (at the origin) all the groups are in its  */
/* payload: a first pass over them aggregates one DECIMAL payload column (sum */
/* or max), and the pass that emits the groups skips those whose value * den  */
/* does not compare as asked with that aggregate * num.                       */
/* <havingGroups> = <sum|max>.<payload column>.<gt|ge|lt|le|eq>.<num>.<den>   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeHavingGroups(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  std::string sSpec(AM::JOB::szTagValue("<havingGroups>"_Stz)->azChar());
  std::vector<std::string> as;
  for (size_t p = 0, e; p <= sSpec.size(); p = e + 1) {
    e = sSpec.find('.', p);
    if (e == std::string::npos) e = sSpec.size();
    as.push_back(sSpec.substr(p, e - p));
  }
  RASSERT(as.size() == 5);
  std::string sFn = as[0], sIdx = as[1], sCmp = as[2];
  std::string sNum = std::to_string(std::stoll(as[3])), sDen = std::to_string(std::stoll(as[4]));
  std::string sOp = sCmp == "gt" ? ">" : sCmp == "ge" ? ">=" : sCmp == "lt" ? "<" : sCmp == "le" ? "<=" : "==";
  RASSERT(sFn == "sum" || sFn == "max");

  std::string sInside =
    "    // having over the groups: skip a group that does not pass\n"
    "    {\n"
    "      union Bcd_T xHgV;\n"
    "      xHgV.m_iAsI8 = aDynamicT->m_aiPayload[" + sIdx + "];\n"
    "      bool bHgKeep = false;\n"
    "      if (bHgAny && xHgV.m_iAsI8 != C_DATUM_NULL) {\n"
    "        __int128 xV = xHgV.m_iMantissa, xA = xHgAgg;\n"
    "        int iSV = xHgV.m_iScale, iSA = iHgScale;\n"
    "        while (iSV < iSA) { xV *= 10; iSV++; }\n"
    "        while (iSA < iSV) { xA *= 10; iSA++; }\n"
    "        bHgKeep = xV * (__int128)" + sDen + " " + sOp + " xA * (__int128)" + sNum + ";\n"
    "      }\n"
    "      if ( ! bHgKeep) {\n"
    "        aDynamicT->m_xIndexInfoPL[0].m_aEntInt = (struct Ent_int*)aExtern_PLNext(\n"
    "            &aDynamicT->m_xIndexInfoPL[0], aDynamicT->m_xIndexInfoPL[0].m_aEntInt);\n"
    "        continue;\n"
    "      }\n"
    "    }\n";
  std::string sAccum = sFn == "sum" ? "      xHgAgg += xV;\n"
                                    : "      if ( ! bHgAny || xV > xHgAgg) xHgAgg = xV;\n";
  std::string sBefore =
    "  // having over the groups: first pass, the " + sFn + " of payload column " + sIdx + "\n"
    "  __int128 xHgAgg = 0;\n"
    "  int iHgScale = 0;\n"
    "  bool bHgAny = false;\n"
    "  for (void* aHgEnt = aExtern_PLBegin(&aDynamicT->m_xIndexInfoPL[0]); aHgEnt;\n"
    "       aHgEnt = aExtern_PLNext(&aDynamicT->m_xIndexInfoPL[0], aHgEnt)) {\n"
    "    union Bcd_T xHgV;\n"
    "    xHgV.m_iAsI8 = ((i8*)((char*)aHgEnt + sizeof(struct Ent_int) + 8))[" + sIdx + "];\n"
    "    if (xHgV.m_iAsI8 == C_DATUM_NULL)\n"
    "      continue;\n"
    "    {\n"
    "      __int128 xV = xHgV.m_iMantissa;\n"
    "      int iSV = xHgV.m_iScale;\n"
    "      while (iHgScale < iSV) { xHgAgg *= 10; iHgScale++; }\n"
    "      while (iSV < iHgScale) { xV *= 10; iSV++; }\n" + sAccum +
    "      bHgAny = true;\n"
    "    }\n"
    "  }\n";
  vInsertText(m_caCode + m_iHgInside, sInside.c_str());
  vInsertText(m_caCode + m_iHgBefore, sBefore.c_str());
  m_iHgBefore = m_iHgInside = -1;
}

/*----------------------------------------------------------------------------*/
/* Hash join (DuckDB's build and probe) in place of an index join. The rows   */
/* of the stage's input are stored by key as they arrive (build); at the end  */
/* of the input the EOS handler calls the row handler again to probe: it     */
/* walks the joined table's index once in key order and joins each entry     */
/* with the stored rows of its key, instead of descending the index for every */
/* arriving row (about 1 us each for a large table). For now only an inner    */
/* join on one integer key that is the first operator of a row handler, and   */
/* only with job tag /hash_join.                                              */
/*----------------------------------------------------------------------------*/
bool CombinedSrc::bHashJoinEligible(Stencil* pStencil, i8 iOpSketch, eHK eHandler)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (AM::JOB::bHasTag("<optimiser_off_hash_join>"_Stz) ||
      AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return false;
  OpSketch* pOpSketch = pStencil->pGet(iOpSketch);
  if (pOpSketch->m_bCrossJoin || pOpSketch->m_bLeftOuter || pOpSketch->m_iDupOpNo != 0 ||
      pOpSketch->m_pIndexDef->eKeyKind() != KE_INT)
    return false;
  if (eHandler != HK_PDSROWBUFFER && eHandler != HK_PDSROWBUFFER_FULL)
    return false;
  if (iOpSketch == 0 || pStencil->pGet(iOpSketch - 1)->eSketchKind() != SK_INFLATE_ROW)
    return false;
  // the joined rows come out in another order (stored rows at the end, in
  // key order): not behind an operator that needs the arriving order, nor a
  // second join of the same handler
  for (i8 j = iOpSketch + 1; j < pStencil->nItems(); j++) {
    eSK eKind = pStencil->pGet(j)->eSketchKind();
    if (eKind == SK_ORDAGG || eKind == SK_TOP || eKind == SK_LIMIT || eKind == SK_SORT ||
        eKind == SK_INDEXJOIN || eKind == SK_SEMIJOIN || eKind == SK_ANTIJOIN)
      return false;
  }
  return true;
}

// An index join on one integer key takes the ranges of its keys from the join
// hash of the index (IndexTree.cpp, JoinHash) when the index has one: the
// stage asks for it once, behind the lookup of the index in the join's
// initialisation (from azInit). Not a cross join, which reads every entry.
// Job tag /optimiser_off_join_hash: the index, or the scanning hash join.
void CombinedSrc::vJoinHashAttach(OpSketch* pOpSketch, char* azInit)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (pOpSketch->m_bCrossJoin || pOpSketch->m_pIndexDef->nColumns() != 1 ||
      AM::JOB::bHasTag("<optimiser_off_join_hash>"_Stz) || AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return;
  char* azFind = strstr(azInit, "m_fp_vExtern_IndexFind(&");
  if (! azFind)
    return;
  char* azInfo = azFind + strlen("m_fp_vExtern_IndexFind(");
  char* azComma = strchr(azInfo, ',');
  char* azEol = strchr(azFind, '\n');
  if (! azComma || ! azEol || azComma > azEol)
    return;
  std::string sCall = "    vExtern_JoinHashAttach(" + std::string(azInfo, azComma - azInfo) + ");\n";
  vInsertText(azEol + 1, sCall.c_str());
}

// A scan of a replicated table whose stream meets no inner join to a
// partitioned table before it leaves the partitions is read in the first
// partition of the grid only (Card_Deck::vMarkReplicatedScanOnce): the
// handler that starts at azHandler returns at once in the others. (A
// replicated table's buffers carry partition 0 in every partition: the
// partition of the running operator decides.)
void CombinedSrc::vScanOnce(char* azHandler)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  char* azBody = azHandler ? strstr(azHandler, "(struct Dynamic_T* aDynamicT) {\n") : nullptr;
  RASSERT(azBody);
  azBody = strchr(azBody, '\n') + 1;
  vInsertText(azBody, "\n  if (aDynamicT->m_iOpLglPtn != 0)\n"
                      "    return 1;\n");
}

// The NULL check of an anti join (from azNullCheck) drops the row whose key
// is NULL. For NOT EXISTS (OpSketch::m_bNullPasses) the row has no match and
// passes: it goes to the operator after the join.
void CombinedSrc::vNullPasses(OpSketch* pOpSketch, char* azNullCheck)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  static const char* szDrop = "aDynamicT->m_bProcessRow = false;\n";
  char* azDrop = strstr(azNullCheck, szDrop);
  RASSERT(azDrop);
  char* azGoto = strstr(azDrop, "goto ");
  char* azEnd = azGoto ? strchr(azGoto, ';') : nullptr;
  RASSERT(azEnd && azEnd < m_azCodeCrs);
  std::string sTail(azEnd);
  std::string sNew = std::string("aDynamicT->m_bProcessRow = true;\n") +
                     std::string(azDrop + strlen(szDrop), azGoto - azDrop - strlen(szDrop)) +
                     "goto " + pOpSketch->m_sLabelNext.azChar();
  m_azCodeCrs = azDrop;
  *m_azCodeCrs = '\0';
  vAppendText(sNew.c_str());
  vAppendText(sTail.c_str());
}

// insert text at a place in the code already generated
void CombinedSrc::vInsertText(char* azAt, const char* szText)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  size_t n = strlen(szText);
  RASSERT(m_azCodeCrs + n < m_caCode + sizeof(m_caCode));
  memmove(azAt + n, azAt, (size_t)(m_azCodeCrs - azAt));
  memcpy(azAt, szText, n);
  m_azCodeCrs += n;
  *m_azCodeCrs = '\0';
}

// append text to the code
void CombinedSrc::vAppendText(const char* szText)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  vInsertText(m_azCodeCrs, szText);
}

void CombinedSrc::vAppendCodeHashJoin(OpSketch* pOpSketch, File* pOperatorFile)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  char* azStart = m_azCodeCrs;
  vAppendCodeFragment(pOpSketch, "vHashJoinPosition_int"_Stz, pOperatorFile);

  // the string cells of the arriving row, and the operator number of the labels
  std::string sVch = "{";
  for (i8 i_Col = 0; i_Col < pOpSketch->m_pStreamDefLft->nColumns(); i_Col++)
    if (pOpSketch->m_pStreamDefLft->pGetColumn(i_Col)->eColumnType() == CT_VARCHAR)
      sVch += std::to_string(i_Col) + ", ";
  sVch += "-1}";
  std::string sNo = std::to_string(pOpSketch->m_iOperatorNo);
  std::string sCode(azStart, m_azCodeCrs - azStart);
  for (size_t p; (p = sCode.find("__HJ_VCH__")) != std::string::npos; )
    sCode.replace(p, 10, sVch);
  for (size_t p; (p = sCode.find("__HJ_N__")) != std::string::npos; )
    sCode.replace(p, 8, sNo);
  m_azCodeCrs = azStart;
  *m_azCodeCrs = '\0';
  vAppendText(sCode.c_str());

  // in the row handler: its locals, the entry to the probe, and the return to
  // the next stored row where an index join returns to the next arriving row
  char* azHandler = nullptr;
  for (char* a = azStart; a > m_caCode; a--)
    if (strncmp(a, "int iPdsRowHandler_", 19) == 0) {
      azHandler = a;
      break;
    }
  RASSERT(azHandler);
  const char* szPaW = "paW = &aDynamicT->m_aW[0];\n";
  char* azPaW = strstr(azHandler, szPaW);
  RASSERT(azPaW && azPaW < azStart);
  std::string sEntry = "  void* hjEnt = 0; void* hjBuf = 0; void* hjNextEnt = 0; void* hjNextBuf = 0; char* hjNode = 0;\n"
                       "  i8 hjScanI = 0, hjScanN = 0, hjScanItem = 0, hjScanBuf = 0; i8* hjScanKeys = 0;\n"
                       "  struct Ent_int hjScanEnt[2];\n"
                       "  if (aDynamicT->m_bHashProbe)\n    goto L_HJProbe" + sNo + ";\n";
  vInsertText(azPaW + strlen(szPaW), sEntry.c_str());
  std::string sLoop = std::string(pOpSketch->m_sLabelOuterLoop.azChar()) + ":\n";
  char* azLoop = strstr(azHandler, sLoop.c_str());
  RASSERT(azLoop);
  std::string sNext = "  if (aDynamicT->m_bHashProbe == 1)\n    goto L_HJNext" + sNo + ";\n"
                      "  if (aDynamicT->m_bHashProbe == 2)\n    goto L_HJReplayNext" + sNo + ";\n"
                      "  if (aDynamicT->m_bHashProbe == 3)\n    goto L_HJScanNext" + sNo + ";\n";
  vInsertText(azLoop + sLoop.size(), sNext.c_str());
}

/*----------------------------------------------------------------------------*/
/* Skip the inflation of columns no operator reads. The code between azStart  */
/* and azEnd is a table-scan inflate followed by its only consumers (filters  */
/* and a project). Each inflated column stores to "&<struct>-><cell>.m_iAsI8";*/
/* when "<struct>-><cell>." appears nowhere else in that code, its jump to    */
/* the inflate routine becomes a jump to the skip routine, in place and with  */
/* the same length, so the column is stepped over instead of decoded.         */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vPruneInflate(char* azStart, char* azEnd)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (! azStart || azEnd <= azStart)
    return;

  std::string sCode(azStart, azEnd - azStart);
  static const std::string sDst  = "ayDstByte = (byte*)&";
  static const std::string sCell = ".m_iAsI8;";
  static const std::string sJump = "goto L_InflateAColumn;";
  static const std::string sSkip = "goto L_SkipAColumn;   ";   // same length as sJump

  auto nCount = [&sCode](const std::string& sWhat) {
    i8 n = 0;
    for (size_t p = sCode.find(sWhat); p != std::string::npos; p = sCode.find(sWhat, p + 1))
      n++;
    return n;
  };

  for (size_t p = sCode.find(sDst); p != std::string::npos; p = sCode.find(sDst, p + 1)) {
    size_t q = sCode.find(sCell, p);
    size_t g = sCode.find(sJump, p);
    size_t n = sCode.find(sDst, p + 1);
    if (q == std::string::npos || g == std::string::npos || (n != std::string::npos && g > n))
      continue;
    std::string sRef = sCode.substr(p + sDst.size(), q - p - sDst.size()) + ".";
    // The null row of an outer join (L_JoinNulls) assigns NULL to every cell
    // of the joined table; such an assignment writes the cell and does not
    // read it, so it does not keep the column from being skipped.
    if (nCount(sRef) - nCount(sRef + "m_iAsI8 = C_DATUM_NULL;") == 1)
      memcpy(azStart + g, sSkip.data(), sSkip.size());
  }
}

/*----------------------------------------------------------------------------*/
/* columnar scan: read the inflated cells of a table scan from the column    */
/* cache of the CRW buffer (store/ColCache.cpp) instead of decoding each row. */
/* Runs after vPruneInflate on the inflate code of the scan: the columns that */
/* still jump to L_InflateAColumn are the ones the handler reads. Before the  */
/* first of them it inserts: at the first row of the buffer, a request for    */
/* those columns; then, if the cache has them all, one load per column and a  */
/* jump past the decoding.                                                    */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vColCacheInflate(char* azStart, char* azEnd)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (! azStart || azEnd <= azStart)
    return;

  std::string sCode(azStart, azEnd - azStart);
  static const std::string sColNo = "i_Col = ";
  static const std::string sDst   = "ayDstByte = (byte*)&";
  static const std::string sCell  = ".m_iAsI8;";
  static const std::string sJump  = "goto L_InflateAColumn;";

  size_t pFirst = sCode.find(sColNo);
  if (pFirst == std::string::npos)
    return;

  std::string sHead(m_caCode, azStart - m_caCode);
  std::vector<std::pair<i8, std::string>> vCols;
  u8 uMask = 0;
  for (size_t p = pFirst; p != std::string::npos; p = sCode.find(sColNo, p + 1)) {
    i8 i_Col = atoll(sCode.c_str() + p + sColNo.size());
    size_t pNext = sCode.find(sColNo, p + 1);
    size_t d = sCode.find(sDst, p);
    size_t g = sCode.find("goto L_", p);
    if (g == std::string::npos || (pNext != std::string::npos && g > pNext))
      return;
    // a column the scan steps over (it may have no destination: the stencil
    // of a scan that feeds a GROUP BY directly names only the columns read)
    if (sCode.compare(g, sJump.size(), sJump) != 0)
      continue;
    if (d == std::string::npos || d > g)
      return;
    size_t q = sCode.find(sCell, d);
    if (q == std::string::npos || i_Col < 0 || i_Col >= 64)
      return;
    std::string sRef = sCode.substr(d + sDst.size(), q - d - sDst.size());
    // the cell type, from the declaration of the row struct
    size_t a = sRef.rfind("->");
    if (a == std::string::npos)
      return;
    std::string sMember = sRef.substr(a + 2);
    size_t t = sHead.rfind(" " + sMember + ";");
    if (t == std::string::npos)
      return;
    size_t l = sHead.rfind("union ", t);
    if (l == std::string::npos)
      return;
    vCols.push_back({i_Col, sRef});
    uMask |= (u8)1 << i_Col;
  }
  if (vCols.empty())
    return;

  std::string sFast;
  sFast += "  // column cache (store/ColCache.cpp)\n";
  sFast += "  if (i_Row == 0)\n";
  sFast += "    aDynamicT->m_bColCache = aDynamicT->m_fp_iExtern_ColCache(aDynamicT, (u8)" +
           std::to_string((unsigned long long)uMask) + "ULL) != 0;\n";
  sFast += "  if (aDynamicT->m_bColCache) {\n";
  for (auto& rCol : vCols)
    sFast += "    " + rCol.second + ".m_iAsI8 = aDynamicT->m_aaColCache[" +
             std::to_string((long long)rCol.first) + "][i_Row];\n";
  // the copies of repeated columns (vPriorCrwCol: select a, a ...), after
  // the loads
  for (size_t a = pFirst; a < sCode.size(); ) {
    size_t e = sCode.find('\n', a);
    if (e == std::string::npos)
      break;
    std::string sLine = sCode.substr(a, e - a);
    size_t s0 = sLine.find_first_not_of(" \t");
    if (s0 != std::string::npos && sLine.compare(s0, 3, "pSR") == 0 &&
        sLine.find(".m_iAsI8 = pSR") != std::string::npos)
      sFast += "    " + sLine.substr(s0) + "\n";
    a = e + 1;
  }
  sFast += "    goto L_InflateCrwRet;\n";
  sFast += "  }\n";

  // insert at the start of the line of the first column
  char* azAt = azStart + pFirst;
  while (azAt > azStart && azAt[-1] != '\n')
    azAt--;
  vInsertText(azAt, sFast.c_str());
}

/*----------------------------------------------------------------------------*/
/* The filter on the groups of a GROUP BY (HAVING), applied before a group    */
/* leaves its stage. Runs when the code of every stage is made                */
/* (Stencil::eSynthesise), on the code of two consecutive stages: stage A     */
/* ends in a GROUP BY whose EOS handler writes one row per group to its tub,  */
/* and the first operator of stage B is a filter on those rows.               */
/*                                                                            */
/* TPC-H Q18 groups lineitem by l_orderkey: 1.5 million groups were written   */
/* to the tub and read back by the next stage, whose filter                   */
/* (sum(l_quantity) > 300) kept 57. DuckDB filters the groups as it scans its */
/* hash table. Here the filter of B is copied into the traversal of A's EOS   */
/* handler, in front of the allocation of the row, with each cell of B's row  */
/* replaced by the expression A stores in that cell. B keeps its filter (it   */
/* passes every row it now gets). Only for a filter that reads fixed-width    */
/* cells whose values A stores with a plain assignment. Job tag               */
/* /optimiser_off_eos_filter: as before.                                      */
/*----------------------------------------------------------------------------*/
bool CombinedSrc::bFilterInEos(std::string& rsStageA, const std::string& rsStageB)
{
  _static_

  if (AM::JOB::bHasTag("<optimiser_off_eos_filter>"_Stz) || AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return false;

  // B: a row handler whose first operator is a filter
  static const std::string sPds = "\nint iPdsRowHandler_";
  size_t pB = rsStageB.find(sPds);
  if (pB == std::string::npos)
    return false;
  pB++;
  size_t pBEnd = rsStageB.find("\nint ", pB);
  std::string sB = rsStageB.substr(pB, pBEnd == std::string::npos ? std::string::npos : pBEnd + 1 - pB);

  // A: its EOS handler, and no stage but S<n>, S<n+1>
  static const std::string sEos = "\nint iEosHandler_";
  size_t pA = rsStageA.find(sEos);
  if (pA == std::string::npos || rsStageA.find(sEos, pA + 1) != std::string::npos)
    return false;
  pA++;
  size_t pAEnd = rsStageA.find("\nint ", pA);
  if (pAEnd == std::string::npos)
    return false;
  pAEnd++;
  std::string sA = rsStageA.substr(pA, pAEnd - pA);
  auto iStageNo = [](const std::string& sFun) -> i8 {
    size_t u = sFun.find("_S");
    size_t e = sFun.find('(');
    if (u == std::string::npos || e == std::string::npos || u > e || e - u - 2 < 1 || e - u - 2 > 6)
      return -1;
    std::string sNo = sFun.substr(u + 2, e - u - 2);
    if (sNo.find_first_not_of("0123456789") != std::string::npos)
      return -1;
    return atoll(sNo.c_str());
  };
  i8 iA = iStageNo(sA), iB = iStageNo(sB);
  if (iA < 0 || iB != iA + 1 || sA.find("L_EosSkipGroup") != std::string::npos)
    return false;

  // the members of a struct, one a line: "<type> <name>"
  auto bMembers = [](const std::string& sCode, size_t pStruct, std::vector<std::string>& rvType,
                     std::vector<std::string>& rvName) -> bool {
    size_t a = sCode.find("{\n", pStruct);
    if (a == std::string::npos)
      return false;
    a += 2;
    for (;;) {
      size_t b = sCode.find('\n', a);
      if (b == std::string::npos)
        return false;
      std::string sLine = sCode.substr(a, b - a);
      a = b + 1;
      size_t f = sLine.find_first_not_of(" \t");
      if (f == std::string::npos)
        continue;
      if (sLine.compare(f, 2, "};") == 0)
        return ! rvName.empty();
      size_t l = sLine.rfind(' ');
      if (sLine.back() != ';' || l == std::string::npos || l <= f)
        return false;
      rvType.push_back(sLine.substr(f, l - f));
      rvName.push_back(sLine.substr(l + 1, sLine.size() - l - 2));
    }
  };

  // B's row struct is A's tub struct (the same members)
  static const std::string sIt = "  struct SR_IT_0 {\n";
  size_t pIt = sB.find(sIt);
  static const std::string sTo = "  struct SR_TO_";
  // (its declaration: "struct SR_TO_<n> {"; the pointer to it is declared too)
  size_t pTo = std::string::npos, pToBrace = std::string::npos;
  for (size_t p = sA.find(sTo); p != std::string::npos; p = sA.find(sTo, p + 1)) {
    size_t e = sA.find_first_not_of("0123456789", p + sTo.size());
    if (e == std::string::npos || sA.compare(e, 3, " {\n") != 0)
      continue;
    if (pTo != std::string::npos)
      return false;
    pTo = p;
    pToBrace = e;
  }
  if (pIt == std::string::npos || pTo == std::string::npos)
    return false;
  std::string sToName = sA.substr(pTo + 9, pToBrace - pTo - 9);         // SR_TO_<n>
  std::vector<std::string> vTypeB, vNameB, vTypeA, vNameA;
  if (! bMembers(sB, pIt, vTypeB, vNameB) || ! bMembers(sA, pTo, vTypeA, vNameA) ||
      vNameA != vNameB || vTypeA != vTypeB)
    return false;

  // B's filter: the first operator behind the inflate of the row
  static const std::string sFilter = "\n  // FILTER\n";
  size_t pF = sB.find(sFilter);
  if (pF == std::string::npos)
    return false;
  for (const char* szOp : {"\n  // INDEXJOIN\n", "\n  // SEMIJOIN\n", "\n  // ANTIJOIN\n", "\n  // PROJECT\n",
                           "\n  // GBYAGG\n", "\n  // ORDAGG\n", "\n  // SORT\n", "\n  // TOP\n", "\n  // LIMIT\n",
                           "\n  // CROSSJOIN\n", "\n  // TUB\n", "\n  // POT\n"}) {
    size_t pOp = sB.find(szOp);
    if (pOp != std::string::npos && pOp < pF)
      return false;
  }
  static const std::string sPre = "    goto L_AdvanceNextRowT;\n  }\n";
  static const std::string sAssign = "  _ASSIGN(bFilterResult)\n";
  size_t pPre = sB.find(sPre, pF);
  size_t pAssign = sB.find(sAssign, pF);
  if (pPre == std::string::npos || pAssign == std::string::npos || pPre > pAssign)
    return false;
  std::string sExpr = sB.substr(pPre + sPre.size(), pAssign - pPre - sPre.size());
  // macros only, one a line
  {
    size_t a = 0;
    bool bAny = false;
    while (a < sExpr.size()) {
      size_t b = sExpr.find('\n', a);
      if (b == std::string::npos)
        return false;
      size_t f = sExpr.find_first_not_of(" \t", a);
      if (f < b) {
        if (sExpr[f] != '_')
          return false;
        bAny = true;
      }
      a = b + 1;
    }
    if (! bAny || sExpr.find("szStrArea") != std::string::npos || sExpr.find("pSR_TJ_") != std::string::npos)
      return false;
  }

  // A's traversal of the groups: the allocation of the row, the stores of
  // its cells, the step to the next group
  static const std::string sLoop  = "  while (aDynamicT->m_xIndexInfoPL[0].m_aEntInt) {\n";
  static const std::string sAllot = "  // allocate a row from the tub/pot\n";
  static const std::string sNext  = "    // move to next entry\n";
  size_t pLoop = sA.find(sLoop);
  size_t pAllot = pLoop == std::string::npos ? pLoop : sA.find(sAllot, pLoop);
  size_t pNext = pAllot == std::string::npos ? pAllot : sA.find(sNext, pAllot);
  if (pNext == std::string::npos || sA.find(sLoop, pLoop + 1) != std::string::npos ||
      sA.find(sAllot, pAllot + 1) != std::string::npos || sA.find(sNext, pNext + 1) != std::string::npos)
    return false;
  // the rule above the comment of the step belongs to it
  size_t pNextLine = pNext;
  {
    size_t r = sA.rfind('\n', pNext - 2);
    if (r != std::string::npos && sA.compare(r + 1, 9, "    // - ") == 0)
      pNextLine = r + 1;
  }

  // each cell of B's row in the filter: the expression A stores in it
  const std::string sCellB = "pSR_IT_0->";
  for (size_t p = sExpr.find(sCellB); p != std::string::npos; p = sExpr.find(sCellB, p)) {
    size_t e = sExpr.find(".m_iAsI8", p);
    if (e == std::string::npos)
      return false;
    std::string sName = sExpr.substr(p + sCellB.size(), e - p - sCellB.size());
    size_t k = 0;
    while (k < vNameB.size() && vNameB[k] != sName)
      k++;
    if (k == vNameB.size() || vTypeB[k].find("Vch_T") != std::string::npos)
      return false;
    std::string sStore = "p" + sToName + "->" + sName + ".m_iAsI8 = ";
    size_t pStore = sA.find(sStore, pAllot);
    if (pStore == std::string::npos || pStore > pNext || sA.find(sStore, pStore + 1) < pNext)
      return false;
    size_t pEol = sA.find('\n', pStore);
    if (pEol == std::string::npos || sA[pEol - 1] != ';')
      return false;
    std::string sRhs = sA.substr(pStore + sStore.size(), pEol - 1 - pStore - sStore.size());
    if (sRhs.empty() || sRhs.find("SR_TO_") != std::string::npos || sRhs.find("StrArea") != std::string::npos ||
        sRhs.find("azStringDyn") != std::string::npos)
      return false;
    sExpr.replace(p, e + 8 - p, "(" + sRhs + ")");
    p += sRhs.size() + 2;
  }

  std::string sTest =
    "  // the filter of the next stage, before the group gets a row (bFilterInEos)\n" +
    sExpr +
    "  _ASSIGN(bFilterResult)\n"
    "  if (aDynamicT->m_iErrorCode != (i8) EC_NONE) {\n"
    "    return 0;\n"
    "  }\n"
    "  if ( ! (bFilterResult && bFilterResult != C_DATUM_NULL))\n"
    "    goto L_EosSkipGroup;\n\n";
  // (the later insertion first)
  sA.insert(pNextLine, "    L_EosSkipGroup: ;\n");
  size_t pAllotLine = pAllot;
  {
    size_t r = sA.rfind('\n', pAllot - 2);
    if (r != std::string::npos && sA.compare(r + 1, 7, "  // - ") == 0)
      pAllotLine = r + 1;
  }
  sA.insert(pAllotLine, sTest);
  rsStageA.replace(pA, pAEnd - pA, sA);
  return true;
}

/*----------------------------------------------------------------------------*/
/* Columns of a tub that the next stage never reads. Runs when the code of    */
/* every stage is made (Stencil::eSynthesise), on the code of two consecutive */
/* stages: it edits the row handler of stage A, whose tub stage B reads.      */
/*                                                                            */
/* A stage that joins hands every column of both tables to the next stage     */
/* when its projection could not be kept in the stage (two aliases of a       */
/* table, as nation in TPC-H Q7 and Q8: Card_Stage::vReinstate_stages). Q7's  */
/* join with lineitem fetched all 16 columns of every joined row and copied   */
/* 19 columns, comments included, into the tub, of which the next stage read  */
/* 5. DuckDB carries only the columns a later operator reads.                 */
/*                                                                            */
/* B's row struct SR_IT_0 is A's tub struct SR_TO_<n> (the same members: B    */
/* reads the rows A wrote). A member that B's handler names through no        */
/* pointer to its row struct is dead: A stores NULL in it instead of copying  */
/* the value (the row keeps its layout), and a column of A's join that        */
/* nothing else in A reads is no longer fetched from the column cache or      */
/* decoded. Job tag /optimiser_off_tub_prune: as before.                      */
/*----------------------------------------------------------------------------*/
bool CombinedSrc::bPruneTub(std::string& rsStageA, const std::string& rsStageB)
{
  _static_

  if (AM::JOB::bHasTag("<optimiser_off_tub_prune>"_Stz) || AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return false;

  // the row handler of each stage: from its name to the next function
  static const std::string sPds = "\nint iPdsRowHandler_";
  static const std::string sCrw = "\nint iCrwHandler_";
  size_t pA = rsStageA.find(sPds);
  if (pA == std::string::npos)
    pA = rsStageA.find(sCrw);
  size_t pB = rsStageB.find(sPds);
  if (pA == std::string::npos || pB == std::string::npos)
    return false;
  pA++;
  pB++;
  size_t pAEnd = rsStageA.find("\nint ", pA);
  size_t pBEnd = rsStageB.find("\nint ", pB);
  if (pAEnd == std::string::npos)
    return false;
  pAEnd++;
  std::string sA = rsStageA.substr(pA, pAEnd - pA);
  std::string sB = rsStageB.substr(pB, pBEnd == std::string::npos ? std::string::npos : pBEnd + 1 - pB);
  // one row handler a stage
  if (rsStageA.find(sPds, pAEnd - 1) != std::string::npos || rsStageA.find(sCrw, pAEnd - 1) != std::string::npos)
    return false;

  // consecutive stages: S<n> and S<n+1>
  auto iStageNo = [](const std::string& sFun) -> i8 {
    size_t u = sFun.find("_S");
    size_t e = sFun.find('(');
    if (u == std::string::npos || e == std::string::npos || u > e || e - u - 2 < 1 || e - u - 2 > 6)
      return -1;
    std::string sNo = sFun.substr(u + 2, e - u - 2);
    if (sNo.find_first_not_of("0123456789") != std::string::npos)
      return -1;
    return atoll(sNo.c_str());
  };
  i8 iA = iStageNo(sA), iB = iStageNo(sB);
  if (iA < 0 || iB != iA + 1)
    return false;

  // the members of a struct, one a line: "<type> <name>"
  auto bMembers = [](const std::string& sCode, size_t pStruct, std::vector<std::string>& rvType,
                     std::vector<std::string>& rvName) -> bool {
    size_t a = sCode.find("{\n", pStruct);
    if (a == std::string::npos)
      return false;
    a += 2;
    for (;;) {
      size_t b = sCode.find('\n', a);
      if (b == std::string::npos)
        return false;
      std::string sLine = sCode.substr(a, b - a);
      a = b + 1;
      size_t f = sLine.find_first_not_of(" \t");
      if (f == std::string::npos)
        continue;
      if (sLine.compare(f, 2, "};") == 0)
        return ! rvName.empty();
      size_t l = sLine.rfind(' ');
      if (sLine.back() != ';' || l == std::string::npos || l <= f)
        return false;
      rvType.push_back(sLine.substr(f, l - f));
      rvName.push_back(sLine.substr(l + 1, sLine.size() - l - 2));
    }
  };

  // B's row struct
  static const std::string sIt = "  struct SR_IT_0 {\n";
  size_t pIt = sB.find(sIt);
  if (pIt == std::string::npos || sB.find(sIt, pIt + 1) != std::string::npos)
    return false;
  std::vector<std::string> vTypeB, vNameB;
  if (! bMembers(sB, pIt, vTypeB, vNameB))
    return false;

  // A's tub: one, written by the row handler through the tub extern
  // (a tub, or a pot: the rows hashed to the partitions of the next stage)
  size_t pTub = sA.find("  L_AddRowToTubPot:\n");
  if (pTub == std::string::npos)
    return false;
  bool bPot = sA.find("m_fp_vExtern_PotAllotRowPds", pTub) != std::string::npos;
  if (bPot == (sA.find("m_fp_vExtern_TubAllotRowPds", pTub) != std::string::npos))
    return false;
  const std::string sToDef = bPot ? "  struct SR_PO_" : "  struct SR_TO_";
  size_t pTo = sA.rfind(sToDef, pTub);
  if (pTo == std::string::npos || sA.find(sToDef) != pTo)
    return false;
  size_t pToEnd = sA.find(" {\n", pTo);
  if (pToEnd == std::string::npos)
    return false;
  std::string sToStruct = sA.substr(pTo + 9, pToEnd - pTo - 9);       // "SR_TO_<n>"
  std::string sToPtr = "p" + sToStruct;
  std::vector<std::string> vTypeA, vNameA;
  if (! bMembers(sA, pTo, vTypeA, vNameA) || vTypeA != vTypeB || vNameA != vNameB)
    return false;
  if (sA.find("struct " + sToStruct + "* " + sToPtr + ";") == std::string::npos)
    return false;

  // the pointers through which B reads its row struct
  std::vector<std::string> vsPtr;
  for (size_t a = sB.find("struct SR_IT_0"); a != std::string::npos; a = sB.find("struct SR_IT_0", a + 1)) {
    size_t b = a + strlen("struct SR_IT_0");
    while (b < sB.size() && (sB[b] == ' ' || sB[b] == '\t'))
      b++;
    if (b >= sB.size() || sB[b] != '*')
      continue;                               // the definition
    b++;
    while (b < sB.size() && (sB[b] == ' ' || sB[b] == '\t'))
      b++;
    if (sB[b] == ')')
      continue;                               // a cast
    size_t c = b;
    while (c < sB.size() && (isalnum((unsigned char) sB[c]) || sB[c] == '_'))
      c++;
    if (c == b || sB[c] != ';')
      return false;                                 // a use that is not understood
    vsPtr.push_back(sB.substr(b, c - b));
  }
  if (vsPtr.empty())
    return false;
  // the row must be read member by member only
  if (sB.find("sizeof(struct SR_IT_0)") != std::string::npos)
    return false;

  // dead members
  std::vector<bool> vbDead(vNameB.size(), false);
  i8 nDead = 0;
  for (size_t k = 0; k < vNameB.size(); k++) {
    bool bRead = false;
    for (const std::string& sPtr : vsPtr)
      bRead = bRead || sB.find(sPtr + "->" + vNameB[k] + ".") != std::string::npos;
    vbDead[k] = ! bRead;
    nDead += ! bRead;
  }
  if (! nDead)
    return false;

  // A's tub: the dead columns become NULL (last first, the places stay valid)
  static const std::string sSep = "  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -\n";
  for (size_t k = vNameB.size(); k-- > 0; ) {
    if (! vbDead[k])
      continue;
    std::string sHead = "  // column " + std::to_string((long long) k) + "\n";
    size_t pCol = sA.find(sHead, pTub);
    if (pCol == std::string::npos || sA.find(sHead, pCol + 1) != std::string::npos)
      return false;
    size_t pBody = pCol + sHead.size();
    // the block ends at the separator of the next column, or at the jump
    // that ends the tub
    size_t pEnd = sA.find(sSep + "  // column ", pBody);
    size_t pGoto = sA.find("goto ", pBody);
    if (pGoto != std::string::npos)
      pGoto = sA.rfind('\n', pGoto) + 1;     // the start of its line
    if (pEnd == std::string::npos || (pGoto != std::string::npos && pGoto < pEnd))
      pEnd = pGoto;
    if (pEnd == std::string::npos)
      return false;
    std::string sBody = sA.substr(pBody, pEnd - pBody);
    std::string sCell = sToPtr + "->" + vNameB[k] + ".";
    if (sBody.find(sCell) == std::string::npos || sBody.find(':') != std::string::npos ||
        sBody.find("goto") != std::string::npos || sBody.find("return") != std::string::npos)
      return false;                                 // not a plain copy of the column
    sA.replace(pBody, pEnd - pBody, "\n  " + sCell + "m_iAsI8 = C_DATUM_NULL;   // not read by the next stage\n\n");
  }

  // A's join: a cached column that nothing reads any more is not fetched, and
  // not decoded on the row path
  {
    static const std::string sLoad = " = aaCcJ[";
    static const std::string sJump = "goto L_InflateAColumn;";
    static const std::string sSkip = "goto L_SkipAColumn;   ";
    size_t a = sA.find(sLoad);
    while (a != std::string::npos) {
      size_t l = sA.rfind('\n', a) + 1;
      size_t e = sA.find('\n', a);
      std::string sLine = sA.substr(l, e - l);
      size_t f = sLine.find_first_not_of(" \t");
      size_t m = sLine.find(".m_iAsI8 = aaCcJ[");
      bool bGone = false;
      if (f != std::string::npos && m != std::string::npos && sLine.compare(f, 3, "pSR") == 0) {
        std::string sRef = sLine.substr(f, m - f) + ".";
        i8 n = 0;
        for (size_t q = sA.find(sRef); q != std::string::npos; q = sA.find(sRef, q + 1))
          n++;
        std::string sDst = "ayDstByte = (byte*)&" + sRef + "m_iAsI8;";
        size_t d = sA.find(sDst);
        if (n == 2 && d != std::string::npos && sA.find(sDst, d + 1) == std::string::npos) {
          size_t g = sA.find("goto L_", d);
          if (g != std::string::npos && sA.compare(g, sJump.size(), sJump) == 0) {
            sA.replace(g, sJump.size(), sSkip);
            sA.erase(l, e + 1 - l);
            bGone = true;
          }
        }
      }
      a = sA.find(sLoad, bGone ? l : a + 1);
    }
  }

  // A's join: a cell of its row that is only copied from the row it read and
  // never read is not copied; the member of the row it read may then be dead
  // for the stage before A
  {
    static const std::string sMid = ".m_iAsI8 = pSR_";
    size_t a = sA.find(sMid);
    while (a != std::string::npos) {
      size_t l = sA.rfind('\n', a) + 1;
      size_t e = sA.find('\n', a);
      std::string sLine = sA.substr(l, e - l);
      size_t f = sLine.find_first_not_of(" \t");
      size_t m = sLine.find(sMid);
      bool bGone = false;
      // "<cell of the join's row> = <cell>;"
      if (f != std::string::npos && m != std::string::npos && sLine.compare(f, 7, "pSR_TJ_") == 0 &&
          sLine.size() > m + 21 && sLine.compare(sLine.size() - 9, 9, ".m_iAsI8;") == 0) {
        std::string sRef = sLine.substr(f, m - f) + ".";
        std::string sFrom = sLine.substr(m + 12, sLine.size() - 9 - m - 12);
        bool bPlain = sRef.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_->.") == std::string::npos &&
                      sFrom.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_->") == std::string::npos;
        i8 n = 0;
        for (size_t q = sA.find(sRef); q != std::string::npos; q = sA.find(sRef, q + 1))
          n++;
        if (bPlain && n == 1) {
          sA.erase(l, e + 1 - l);
          bGone = true;
        }
      }
      a = sA.find(sMid, bGone ? l : a + 1);
    }
  }

  rsStageA.replace(pA, pAEnd - pA, sA);
  return true;
}

/*----------------------------------------------------------------------------*/
/* GROUP BY on strings: a cache of the groups of short keys. Runs on a        */
/* complete row handler, before its footer. For every row the general-key     */
/* GROUP BY copies each key string into a work entry (an extern call each),   */
/* then hashes and compares the entry in the hash table of the groups         */
/* (another call): TPC-H Q1 does this 6 million times for 4 groups. When all  */
/* keys are strings, the handler first packs each key shorter than 8 bytes    */
/* into a word and looks the words up in a small direct-mapped cache of its   */
/* own (group -> payload); only a miss, a longer or a NULL key goes the old   */
/* way, and its group is then cached. The payload of a group never moves      */
/* while the groups are in the hash table (GbyHash); with the tree            */
/* (/optimiser_off_hash_gbyagg) nothing is cached. Job tag                    */
/* /optimiser_off_gby_key_cache: as before.                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vGbyKeyCache(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (AM::JOB::bHasTag("<optimiser_off_gby_key_cache>"_Stz) || AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return;

  // the handler
  char* azHandler = nullptr;
  for (char* a = m_azCodeCrs - 20; a > m_caCode; a--)
    if (a[-1] == '\n' && (strncmp(a, "int iCrwHandler_", 16) == 0 || strncmp(a, "int iPdsRowHandler_", 19) == 0 ||
                          strncmp(a, "int iRowHandler_", 16) == 0)) {
      azHandler = a;
      break;
    }
  if (! azHandler)
    return;
  if (m_iHgBefore >= azHandler - m_caCode || m_iHgInside >= azHandler - m_caCode)
    return;
  std::string sCode(azHandler, m_azCodeCrs - azHandler);
  if (sCode.find("\nint ", 1) != std::string::npos)
    return;                                   // not the last function
  size_t u = sCode.find('_');
  size_t e = sCode.find('(');
  if (u == std::string::npos || e == std::string::npos || u > e)
    return;
  std::string sStage = sCode.substr(u + 1, e - u - 1);
  if (sStage.empty() || sStage.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
    return;

  // one general-key GROUP BY on the first payload index
  static const std::string sFind = "aEntFound = aDynamicT->m_fp_aExtern_IndexTreeFindInsertGenAsc(aDynamicT, ";
  static const std::string sKey  = "  aDynamicT->m_xIndexInfoPL[0].m_aEntInt->m_iIntElm0 = 0;\n";
  static const std::string sAgg  = "// aggregate the payload\n";
  size_t pFind = sCode.find(sFind);
  if (pFind == std::string::npos || sCode.find(sFind, pFind + 1) != std::string::npos)
    return;
  size_t pKey = sCode.rfind(sKey, pFind);
  if (pKey == std::string::npos || sCode.find(sKey) != pKey)
    return;
  size_t pAgg = sCode.find(sAgg, pFind);
  if (pAgg == std::string::npos)
    return;
  while (pAgg > 0 && sCode[pAgg - 1] != '\n')
    pAgg--;
  // nothing after the group is found reads how it was found
  if (sCode.find("aEntFound", pAgg) != std::string::npos || sCode.find("bInsertedEntry", pAgg) != std::string::npos ||
      sCode.find("L_GbyKeyCached_") != std::string::npos)
    return;

  // the keys: each a string cell pushed and appended to the work entry
  std::vector<std::string> vsCell;
  {
    static const std::string sPush = "_PSH_COL(";
    static const std::string sApp  = "_ASSIGN_TO_ENTSTZ(aDynamicT->m_xIndexInfoPL[0].m_aEntWrk)";
    size_t pEnd = sCode.find("  // check for error\n", pKey);
    if (pEnd == std::string::npos || pEnd > pFind)
      return;
    bool bPushed = false;
    for (size_t a = pKey + sKey.size(); a < pEnd; ) {
      size_t b = sCode.find('\n', a);
      std::string sLine = sCode.substr(a, b - a);
      a = b + 1;
      size_t f = sLine.find_first_not_of(" \t");
      if (f == std::string::npos)
        continue;
      sLine = sLine.substr(f);
      if (! bPushed) {
        if (sLine.compare(0, sPush.size(), sPush) != 0 || sLine.size() < sPush.size() + 10 ||
            sLine.compare(sLine.size() - 9, 9, ".m_iAsI8)") != 0)
          return;
        std::string sCell = sLine.substr(sPush.size(), sLine.size() - sPush.size() - 1);
        if (sCell.compare(0, 3, "pSR") != 0 ||
            sCell.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_->.") != std::string::npos)
          return;
        vsCell.push_back(sCell);
        bPushed = true;
      }
      else {
        if (sLine != sApp)
          return;
        bPushed = false;
      }
    }
    if (bPushed || vsCell.empty() || vsCell.size() > 4)
      return;
  }
  std::string sN = std::to_string((long long) vsCell.size());

  // back to front, the places stay valid
  //  - after the group is found the old way: cache it
  std::string sStore;
  sStore += "  // cache the group of a short key (CombinedSrc::vGbyKeyCache)\n";
  sStore += "  if (bGkShort && (u8)aDynamicT->m_xIndexInfoPL[0].m_aGbyHash > 1) {\n";
  for (size_t k = 0; k < vsCell.size(); k++)
    sStore += "    axGk[uGkSlot].auKey[" + std::to_string((long long) k) + "] = auGkKey[" + std::to_string((long long) k) + "];\n";
  sStore += "    axGk[uGkSlot].aiPayload = aDynamicT->m_aiPayload;\n  }\n";
  sStore += "  L_GbyKeyCached_" + sStage + ":;\n";
  sCode.insert(pAgg, sStore);
  //  - before the key is built: pack the keys and try the cache
  std::string sTry;
  sTry += "  // the group of a short key from the cache of the handler (CombinedSrc::vGbyKeyCache)\n";
  sTry += "  bGkShort = 1;\n";
  for (size_t k = 0; k < vsCell.size(); k++) {
    std::string sK = std::to_string((long long) k);
    sTry += "  {\n    i8 iGkCell = " + vsCell[k] + ";\n    auGkKey[" + sK + "] = 0;\n";
    sTry += "    if (iGkCell == C_DATUM_NULL || iGkCell == 0)\n      bGkShort = 0;\n    else {\n";
    sTry += "      const unsigned char* ayGk = (const unsigned char*)iGkCell;\n      int nGk;\n";
    sTry += "      for (nGk = 0; nGk < 8 && ayGk[nGk]; nGk++)\n        auGkKey[" + sK + "] |= (u8)ayGk[nGk] << (8 * nGk);\n";
    sTry += "      if (nGk == 8)\n        bGkShort = 0;\n    }\n  }\n";
  }
  sTry += "  if (bGkShort) {\n    uGkSlot = 0;\n";
  for (size_t k = 0; k < vsCell.size(); k++)
    sTry += "    uGkSlot = (uGkSlot ^ auGkKey[" + std::to_string((long long) k) + "]) * 0x9E3779B97F4A7C15ull;\n";
  sTry += "    uGkSlot >>= 60;\n    if (axGk[uGkSlot].aiPayload";
  for (size_t k = 0; k < vsCell.size(); k++)
    sTry += " && axGk[uGkSlot].auKey[" + std::to_string((long long) k) + "] == auGkKey[" + std::to_string((long long) k) + "]";
  sTry += ") {\n      aDynamicT->m_aiPayload = axGk[uGkSlot].aiPayload;\n      goto L_GbyKeyCached_" + sStage + ";\n    }\n  }\n";
  sCode.insert(pKey, sTry);
  //  - its locals
  size_t pBrace = sCode.find("{\n");
  if (pBrace == std::string::npos || pBrace > pKey)
    return;
  sCode.insert(pBrace + 2, "  u8 auGkKey[" + sN + "]; int bGkShort = 0; u8 uGkSlot = 0;\n"
                           "  struct { u8 auKey[" + sN + "]; i8* aiPayload; } axGk[16] = {{{0}, 0}};\n");

  RASSERT(azHandler + sCode.size() < m_caCode + sizeof(m_caCode));
  memcpy(azHandler, sCode.data(), sCode.size());
  m_azCodeCrs = azHandler + sCode.size();
  *m_azCodeCrs = '\0';
}

/*----------------------------------------------------------------------------*/
/* Vectorized filter of a columnar scan. Runs on a complete CRW handler,      */
/* before its footer. When the scan reads its columns from the column cache   */
/* (vColCacheInflate) and its first operator is a filter made only of pure    */
/* expression macros over cached cells and constants, the filter is also      */
/* emitted as a function of its own, before the handler: one tight loop over  */
/* the cached column arrays of the buffer that writes the numbers of the rows */
/* that pass to a selection vector (as DuckDB's filter does): the conjuncts   */
/* that compare dates or integers as a byte mask over all rows (vector        */
/* compares, the generated code is compiled with -march=native), the others   */
/* with their macros for the rows left. The handler then visits only the      */
/* selected rows and skips its own filter; when a conjunct cannot be          */
/* evaluated here (strings, functions) the handler still filters the rows it  */
/* visits.                                                                     */
/* Without the cache for a buffer the handler runs as before. Job tag         */
/* /optimiser_off_vec_filter: as before.                                      */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vVecFilter(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (AM::JOB::bHasTag("<optimiser_off_vec_filter>"_Stz) || AM::JOB::bHasTag("<optimiser_off_all>"_Stz))
    return;

  // the handler
  char* azHandler = nullptr;
  for (char* a = m_azCodeCrs - 16; a > m_caCode; a--)
    if (strncmp(a, "int iCrwHandler_", 16) == 0) {
      azHandler = a;
      break;
    }
  if (! azHandler || (azHandler > m_caCode && azHandler[-1] != '\n'))
    return;
  if (m_iHgBefore >= azHandler - m_caCode || m_iHgInside >= azHandler - m_caCode)
    return;
  std::string sCode(azHandler, m_azCodeCrs - azHandler);
  size_t e = sCode.find('(');
  std::string sStage = sCode.substr(16, e - 16);
  if (sStage.empty() || sStage.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
    return;

  // the column cache block of the scan: its request and its loads
  static const std::string sBlock = "  // column cache (store/ColCache.cpp)\n  if (i_Row == 0)\n";
  static const std::string sIf    = "  if (aDynamicT->m_bColCache) {\n";
  size_t pBlock = sCode.find(sBlock);
  if (pBlock == std::string::npos || sCode.find(sBlock, pBlock + 1) != std::string::npos)
    return;
  size_t pReq = pBlock + sBlock.size();
  size_t pIf = sCode.find(sIf, pReq);
  size_t pReqEnd = sCode.find('\n', pReq);
  if (pIf == std::string::npos || pReqEnd + 1 != pIf)
    return;
  std::vector<std::pair<std::string, std::string>> vCols;     // cell, column number
  size_t p = pIf + sIf.size();
  for (;;) {
    size_t q = sCode.find('\n', p);
    if (q == std::string::npos)
      return;
    std::string sLine = sCode.substr(p, q - p);
    if (sLine == "    goto L_InflateCrwRet;")
      break;
    static const std::string sMid = ".m_iAsI8 = aDynamicT->m_aaColCache[";
    static const std::string sEnd = "][i_Row];";
    // the copy of a repeated column: the same cached column
    {
      static const std::string sCopy = ".m_iAsI8 = pSR";
      size_t c = sLine.find(sCopy);
      if (sLine.compare(0, 7, "    pSR") == 0 && c != std::string::npos && sLine.back() == ';') {
        std::string sFrom = sLine.substr(c + sCopy.size() - 3, sLine.size() - 1 - (c + sCopy.size() - 3));
        std::string sColNo;
        for (auto& rCol : vCols)
          if (rCol.first == sFrom)
            sColNo = rCol.second;
        if (sColNo.empty())
          return;
        vCols.push_back({sLine.substr(4, c - 4) + ".m_iAsI8", sColNo});
        p = q + 1;
        continue;
      }
    }
    size_t m = sLine.find(sMid);
    if (sLine.compare(0, 4, "    ") != 0 || m == std::string::npos ||
        sLine.size() < sEnd.size() || sLine.compare(sLine.size() - sEnd.size(), sEnd.size(), sEnd) != 0)
      return;
    vCols.push_back({sLine.substr(4, m - 4) + ".m_iAsI8",
                     sLine.substr(m + sMid.size(), sLine.size() - sEnd.size() - m - sMid.size())});
    p = q + 1;
  }
  if (vCols.empty())
    return;

  // the first operator after the scan is a filter
  size_t pRet = sCode.find("  L_InflateCrwRet:\n", p);
  if (pRet == std::string::npos)
    return;
  size_t pName = std::string::npos;
  for (size_t a = sCode.find('\n', pRet); a != std::string::npos; a = sCode.find('\n', a + 1)) {
    if (sCode.compare(a + 1, 5, "  // ") != 0)
      continue;
    size_t b = sCode.find('\n', a + 1);
    if (b == std::string::npos)
      return;
    std::string sName = sCode.substr(a + 6, b - a - 6);
    if (sName.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") == std::string::npos ||
        sName.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_ -") != std::string::npos)
      continue;
    if (sName != "FILTER")
      return;
    pName = a + 6;
    break;
  }
  if (pName == std::string::npos)
    return;
  static const std::string sPre = "  if ( ! aDynamicT->m_bProcessRow ) {\n    aDynamicT->m_bProcessRow = true;\n    goto L_AdvanceNextCrwT;\n  }\n";
  size_t pPre = sCode.find(sPre, pName);
  if (pPre == std::string::npos)
    return;
  size_t pExpr = pPre + sPre.size();
  static const std::string sAssign = "  _ASSIGN(bFilterResult)\n";
  size_t pAssign = sCode.find(sAssign, pExpr);
  if (pAssign == std::string::npos)
    return;
  static const std::string sPost =
    "  if (bFilterResult && bFilterResult != C_DATUM_NULL) {\n    aDynamicT->m_bProcessRow = true;\n  } else {\n    aDynamicT->m_bProcessRow = false;\n  }\n";
  size_t pPost = sCode.find(sPost, pAssign);
  if (pPost == std::string::npos)
    return;
  // between the expression and the result: only the error check
  {
    std::string sBetween = sCode.substr(pAssign + sAssign.size(), pPost - pAssign - sAssign.size());
    if (sBetween.find("goto") != std::string::npos || sBetween.find(':') != std::string::npos ||
        sBetween.find("_PSH") != std::string::npos)
      return;
  }

  // The expression is in postfix, one macro a line. The cells of the scan
  // become elements of the column arrays.
  std::string sExpr = sCode.substr(pExpr, pAssign - pExpr);
  std::vector<bool> vbUsed(vCols.size(), false);
  for (size_t i = 0; i < vCols.size(); i++) {
    const std::string& sCell = vCols[i].first;
    std::string sElem = "aVecCol" + vCols[i].second + "[i_VecRow]";
    for (size_t a = sExpr.find(sCell); a != std::string::npos; a = sExpr.find(sCell, a + sElem.size()))
      sExpr.replace(a, sCell.size(), sElem);
  }
  static const std::string sErr = "aDynamicT->m_iErrorCode";
  for (size_t a = sExpr.find(sErr); a != std::string::npos; a = sExpr.find(sErr, a + 1))
    sExpr.replace(a, sErr.size(), "iVecError");

  // Its tree: the operands of a node are the lines before it. The number of
  // operands is known for a push (none), NOT (1), BETWEEN (3), IN (the list,
  // its length and the value) and the macros named _<OP>_<type><type> (2);
  // with any other macro the filter stays as it is.
  auto bTypeCode = [](const std::string& sType) {
    static const char* aszType[] = {"BCD", "DTE", "INT", "BOL", "FLT", "TSP", "TME", "VCH", "IVD", "IVL", "IVY"};
    for (const char* sz : aszType)
      if (sType == sz)
        return true;
    return false;
  };
  struct Node { std::string sMacro, sArg; size_t iFirst; std::vector<int> viKid; };
  std::vector<Node> vNode;
  std::vector<std::string> vsLine;
  {
    std::vector<int> viStack;
    size_t a = 0;
    while (a < sExpr.size()) {
      size_t b = sExpr.find('\n', a);
      if (b == std::string::npos)
        b = sExpr.size();
      std::string sLine = sExpr.substr(a, b - a);
      a = b + 1;
      size_t f = sLine.find_first_not_of(" \t");
      if (f == std::string::npos)
        continue;
      size_t g = sLine.find('(', f);
      size_t h = sLine.rfind(')');
      if (sLine[f] != '_' || g == std::string::npos || h == std::string::npos || h < g ||
          sLine.find_first_not_of(" \t", h + 1) != std::string::npos)
        return;
      Node x;
      x.sMacro = sLine.substr(f, g - f);
      x.sArg = sLine.substr(g + 1, h - g - 1);
      x.iFirst = vsLine.size();
      size_t u = x.sMacro.find('_', 1);
      size_t nKids;
      if (x.sMacro.compare(0, 5, "_PSH_") == 0)
        nKids = 0;
      else if (x.sMacro == "_NOT_BOLBOL")
        nKids = 1;
      else if (x.sMacro.compare(0, 5, "_BET_") == 0 && x.sMacro.size() == 11)
        nKids = 3;
      else if (x.sMacro.compare(0, 5, "_INX_") == 0 && x.sMacro.size() == 11) {
        // <item> ... <number of items> <value>
        if (viStack.size() < 2)
          return;
        const Node& rCount = vNode[viStack[viStack.size() - 2]];
        if (rCount.sMacro != "_PSH_INT" || rCount.sArg.empty() || rCount.sArg.size() > 6 ||
            rCount.sArg.find_first_not_of("0123456789") != std::string::npos)
          return;
        nKids = (size_t) atoll(rCount.sArg.c_str()) + 2;
      }
      else if (u != std::string::npos && x.sMacro.size() == u + 7 &&
               x.sMacro.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 1) == u &&
               bTypeCode(x.sMacro.substr(u + 1, 3)) && bTypeCode(x.sMacro.substr(u + 4, 3)))
        nKids = 2;
      else
        return;
      if (viStack.size() < nKids)
        return;
      for (size_t k = 0; k < nKids; k++) {
        x.viKid.insert(x.viKid.begin(), viStack.back());
        viStack.pop_back();
      }
      if (nKids)
        x.iFirst = vNode[x.viKid[0]].iFirst;
      vsLine.push_back(sLine);
      viStack.push_back((int) vNode.size());
      vNode.push_back(x);
    }
    if (viStack.size() != 1)
      return;
  }
  // the conjuncts: the operands of the top-level ANDs; a row passes when
  // each of them is true
  std::vector<int> viConj;
  {
    std::vector<int> viTodo{(int) vNode.size() - 1};
    while (! viTodo.empty()) {
      int i = viTodo.back();
      viTodo.pop_back();
      if (vNode[i].sMacro == "_AND_BOLBOL") {
        viTodo.push_back(vNode[i].viKid[1]);
        viTodo.push_back(vNode[i].viKid[0]);
      }
      else
        viConj.push_back(i);
    }
  }
  // A comparison of two dates or two integers, each a cached cell or a
  // literal, is a plain comparison of the cells (false when one is NULL):
  // these conjuncts are evaluated for every row of the buffer into a byte
  // mask, in a loop the compiler turns into vector compares.
  auto sFast = [&vNode](int i) -> std::string {
    const Node& x = vNode[i];
    static const char* aszCmp[][2] = {{"_EQ_", "=="}, {"_NE_", "!="}, {"_LT_", "<"}, {"_LE_", "<="},
                                      {"_GT_", ">"}, {"_GE_", ">="}};
    const char* szOp = nullptr;
    for (auto& r : aszCmp)
      if (x.sMacro.compare(0, strlen(r[0]), r[0]) == 0 &&
          (x.sMacro.compare(strlen(r[0]), std::string::npos, "DTEDTE") == 0 ||
           x.sMacro.compare(strlen(r[0]), std::string::npos, "INTINT") == 0))
        szOp = r[1];
    if (! szOp || x.viKid.size() != 2)
      return "";
    std::string sOut;
    std::string asArg[2];
    for (int k = 0; k < 2; k++) {
      const Node& y = vNode[x.viKid[k]];
      bool bLiteral = ! y.sArg.empty() && y.sArg.size() < 19 &&
                      y.sArg.find_first_not_of("0123456789", y.sArg[0] == '-' ? 1 : 0) == std::string::npos;
      bool bCell = y.sMacro == "_PSH_COL" && y.sArg.compare(0, 7, "aVecCol") == 0 &&
                   y.sArg.find_first_not_of("0123456789", 7) == y.sArg.find("[i_VecRow]") &&
                   y.sArg.size() == y.sArg.find("[i_VecRow]") + 10;
      if (! (bCell || (bLiteral && (y.sMacro == "_PSH_DTE" || y.sMacro == "_PSH_INT"))))
        return "";
      asArg[k] = bLiteral ? "((i8)" + y.sArg + "LL)" : y.sArg;
      if (bCell)
        sOut += " & (" + y.sArg + " != C_DATUM_NULL)";
    }
    return "(" + asArg[0] + " " + szOp + " " + asArg[1] + ")" + sOut;
  };
  // Another conjunct is evaluated with its macros, for the rows that pass the
  // mask, when its macros read only their operands (comparisons, BETWEEN,
  // arithmetic and logic on fixed-width cells and literals) and it names
  // nothing of the row or of the handler but cached cells.
  auto bPure = [&vNode, &vsLine](int i) -> bool {
    for (size_t l = vNode[i].iFirst; l <= (size_t) i; l++) {
      const std::string& sMacro = vNode[l].sMacro;
      static const char* aszPush[] = {"_PSH_COL", "_PSH_BCD", "_PSH_DTE", "_PSH_INT", "_PSH_BOL", "_PSH_FLT",
                                      "_PSH_TSP", "_PSH_TME"};
      bool bOk = false;
      for (const char* sz : aszPush)
        bOk = bOk || sMacro == sz;
      static const char* aszOp[] = {"_EQ_", "_NE_", "_LT_", "_LE_", "_GT_", "_GE_", "_BET_", "_ADD_", "_SUB_",
                                    "_MUL_", "_AND_", "_OR_", "_NOT_"};
      static const char* aszType[] = {"BCD", "DTE", "INT", "BOL", "FLT", "TSP", "TME"};
      for (const char* sz : aszOp)
        if (sMacro.compare(0, strlen(sz), sz) == 0 && sMacro.size() == strlen(sz) + 6) {
          bool bTypes = true;
          for (size_t t = strlen(sz); t < sMacro.size(); t += 3) {
            bool bOne = false;
            for (const char* szT : aszType)
              bOne = bOne || sMacro.compare(t, 3, szT) == 0;
            bTypes = bTypes && bOne;
          }
          bOk = bOk || bTypes;
        }
      const std::string& sLine = vsLine[l];
      if (! bOk || sLine.find("->") != std::string::npos || sLine.find("szStrArea") != std::string::npos ||
          sLine.find("i_Row") != std::string::npos || sLine.find("goto") != std::string::npos ||
          sLine.find('&') != std::string::npos)
        return false;
    }
    return true;
  };
  std::string sMask, sRest;
  bool bAll = true;                     // every conjunct is evaluated here
  for (int i : viConj)
    if (sFast(i).empty() && ! bPure(i))
      bAll = false;
  for (int i : viConj) {
    std::string sOne = sFast(i);
    if (! sOne.empty()) {
      sMask += (sMask.empty() ? "" : " & ") + sOne;
      continue;
    }
    if (! bAll)
      continue;                         // left to the filter of the handler
    bool bFirst = sRest.empty();
    for (size_t l = vNode[i].iFirst; l <= (size_t) i; l++)
      sRest += "    " + vsLine[l] + "\n";
    if (! bFirst)
      sRest += "      _AND_BOLBOL(iVecError)\n";
  }
  if (sMask.empty() && ! bAll)
    return;
  for (size_t i = 0; i < vCols.size(); i++)
    vbUsed[i] = (sMask + sRest).find("aVecCol" + vCols[i].second + "[") != std::string::npos;

  // the function
  std::string sFun;
  sFun += "/* vectorized filter of the scan of " + sStage + " (CombinedSrc::vVecFilter): the rows of the\n";
  sFun += "   buffer that pass, from the cached columns, to a selection vector */\n";
  sFun += "static __thread unsigned short aiVecSel_" + sStage + "[65536];\n";
  if (! sMask.empty())
    sFun += "static __thread unsigned char abVecMask_" + sStage + "[65536];\n";
  sFun += "static i8 nVecFilter_" + sStage + "(struct Dynamic_T* aDynamicT, i8 nVecRows, unsigned short* aiVecSel) __attribute__((noinline));\n";
  sFun += "static i8 nVecFilter_" + sStage + "(struct Dynamic_T* aDynamicT, i8 nVecRows, unsigned short* aiVecSel)\n{\n";
  sFun += "  #include \"../src/store/Fragments/vFLAT_FunctionLocals.h\"\n";
  sFun += "  i8 aVecW[64];\n  i8 nVecSel = 0;\n  i8 iVecError = (i8)EC_NONE;\n  i8 i_VecRow;\n";
  for (size_t i = 0; i < vCols.size(); i++) {
    bool bSeen = false;                 // a repeated column: one array
    for (size_t j = 0; j < i; j++)
      bSeen = bSeen || vCols[j].second == vCols[i].second;
    if (vbUsed[i] && ! bSeen)
      sFun += "  const i8* __restrict aVecCol" + vCols[i].second + " = aDynamicT->m_aaColCache[" + vCols[i].second + "];\n";
  }
  if (! sMask.empty()) {
    sFun += "  unsigned char* __restrict abVecMask = abVecMask_" + sStage + ";\n";
    sFun += "  for (i_VecRow = 0; i_VecRow < nVecRows; i_VecRow++)\n";
    sFun += "    abVecMask[i_VecRow] = (unsigned char)(" + sMask + ");\n";
    sFun += "  for (i_VecRow = 0; i_VecRow < nVecRows; i_VecRow++) {\n";
    sFun += "    aiVecSel[nVecSel] = (unsigned short)i_VecRow;\n";
    sFun += "    nVecSel += abVecMask[i_VecRow];\n  }\n";
  }
  if (! sRest.empty()) {
    if (! sMask.empty()) {
      sFun += "  i8 nVecIn = nVecSel;\n  i8 i_VecSel;\n  nVecSel = 0;\n";
      sFun += "  for (i_VecSel = 0; i_VecSel < nVecIn; i_VecSel++) {\n";
      sFun += "    i_VecRow = aiVecSel[i_VecSel];\n    paW = aVecW;\n";
    }
    else
      sFun += "  for (i_VecRow = 0; i_VecRow < nVecRows; i_VecRow++) {\n    paW = aVecW;\n";
    sFun += sRest;
    sFun += "    bFilterResult = *(--paW);\n";
    sFun += "    aiVecSel[nVecSel] = (unsigned short)i_VecRow;\n";
    sFun += "    nVecSel += (bFilterResult && bFilterResult != C_DATUM_NULL) ? 1 : 0;\n  }\n";
  }
  sFun += "  if (iVecError != (i8)EC_NONE)\n    aDynamicT->m_iErrorCode = iVecError;\n";
  sFun += "  return nVecSel;\n}\n\n";

  // the handler: (back to front, the places stay valid)
  //  - skip the filter of a selected row
  if (bAll) {
    sCode.insert(pPost + sPost.size(), "  L_VecFilterDone_" + sStage + ":;\n");
    sCode.insert(pExpr, "  if (bVecSel)\n    goto L_VecFilterDone_" + sStage + ";\n");
  }
  //  - at the first row filter the buffer; then each time take the next
  //    selected row
  std::string sReq = sCode.substr(pReq, pReqEnd - pReq);        // "    aDynamicT->m_bColCache = ...;"
  std::string sSel;
  sSel += "  if (i_Row == 0) {\n  " + sReq + "\n";
  sSel += "    bVecSel = 0;\n";
  sSel += "    if (aDynamicT->m_bColCache && aDynamicT->m_nRows <= 65536) {\n";
  sSel += "      nVecSel = nVecFilter_" + sStage + "(aDynamicT, aDynamicT->m_nRows, aiVecSel_" + sStage + ");\n";
  sSel += "      if (aDynamicT->m_iErrorCode != (i8)EC_NONE)\n        return 0;\n";
  sSel += "      iVecSel = 0;\n      bVecSel = 1;\n    }\n  }\n";
  sSel += "  if (bVecSel) {\n";
  sSel += "    if (iVecSel >= nVecSel)\n      goto L_Return;\n";
  sSel += "    i8 iVecRow = aiVecSel_" + sStage + "[iVecSel++];\n";
  sSel += "    aHeadLine = ADDR_ADD(struct HeadLine_T*,aHeadLine,(iVecRow - i_Row) * iHeadSize);\n";
  sSel += "    i_Row = iVecRow;\n  }\n";
  size_t pIfLine = pBlock + sBlock.size() - strlen("  if (i_Row == 0)\n");
  sCode.replace(pIfLine, pIf - pIfLine, sSel);
  //  - its locals
  size_t pBrace = sCode.find("{\n");
  if (pBrace == std::string::npos)
    return;
  sCode.insert(pBrace + 2, "  i8 bVecSel = 0, iVecSel = 0, nVecSel = 0;\n");

  std::string sNew = sFun + sCode;
  RASSERT(azHandler + sNew.size() < m_caCode + sizeof(m_caCode));
  memcpy(azHandler, sNew.data(), sNew.size());
  m_azCodeCrs = azHandler + sNew.size();
  *m_azCodeCrs = '\0';
}

/*----------------------------------------------------------------------------*/
/* columnar index join: read the cells of the joined row from the column      */
/* cache of its buffer (store/ColCache.cpp aExtern_ColCacheJoin) instead of   */
/* decoding the row. Runs after vPruneInflate on the code of one index join   */
/* (from azStart): the columns of the join's first inflate sequence that      */
/* still jump to L_InflateAColumn are the ones the handler reads. Before the  */
/* first column it inserts a request for those columns of the row's buffer;  */
/* if the cache answers, one load per column (and the copies of repeated     */
/* columns) and a jump past the decoding, to a label after its last column.  */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vColCacheJoin(char* azStart, char* azEnd)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  if (! azStart || azEnd <= azStart)
    return;

  std::string sCode(azStart, azEnd - azStart);
  static const std::string sDeref = "aIndexCrw    = (";
  static const std::string sColNo = "i_Col = ";
  static const std::string sDst   = "ayDstByte = (byte*)&";
  static const std::string sCell  = ".m_iAsI8;";
  static const std::string sRet   = "aSubReturnLabel = && L_RetT_ColIJ_";
  static const std::string sJump  = "goto L_InflateAColumn;";

  // only a row reached by iIndexBuffer and iIndexItem (the request names them)
  static const std::string sDerefLine =
    "aIndexCrw    = ( (char*)C_ADR_REGION + iIndexBuffer * C_1MB + C_CRW_FIRSTHEAD + iIndexItem * C_CRW_HEADBYTES );";
  size_t pDeref = sCode.find(sDeref);
  if (pDeref == std::string::npos || sCode.compare(pDeref, sDerefLine.size(), sDerefLine) != 0)
    return;
  size_t pFirst = sCode.find(sColNo, pDeref);
  if (pFirst == std::string::npos ||
      sCode.substr(pDeref, pFirst - pDeref).find("m_fp_aExtern_ColCacheJoin") != std::string::npos)
    return;                                   // none, or done by a later join
  // the join's return labels: L_RetT_ColIJ_<op>_<column>
  size_t r = sCode.find(sRet, pFirst);
  if (r == std::string::npos)
    return;
  size_t r2 = sCode.find('_', r + sRet.size());
  if (r2 == std::string::npos)
    return;
  std::string sLabel = "L_RetT_ColIJ_" + sCode.substr(r + sRet.size(), r2 - r - sRet.size()) + "_";

  std::vector<std::pair<i8, std::string>> vCols;
  u8 uMask = 0;
  size_t pLast = std::string::npos;          // end of the line of the last label
  size_t p = pFirst;
  while (p != std::string::npos) {
    i8 i_Col = atoll(sCode.c_str() + p + sColNo.size());
    // the column's return label, as its own jump names it (a semi or anti
    // join numbers them from 1, an index join from 0)
    size_t pNext = sCode.find(sColNo, p + 1);
    static const std::string sRetAny = "aSubReturnLabel = && ";
    size_t pRet = sCode.find(sRetAny, p);
    size_t pSemi = pRet == std::string::npos ? pRet : sCode.find(';', pRet);
    if (pSemi == std::string::npos || (pNext != std::string::npos && pSemi > pNext))
      break;                                  // not a column of this join
    std::string sThis = sCode.substr(pRet + sRetAny.size(), pSemi - pRet - sRetAny.size()) + ":";
    if (sThis.compare(0, sLabel.size(), sLabel) != 0)
      break;
    size_t pLab = sCode.find(sThis, pSemi);
    if (pLab == std::string::npos || (pNext != std::string::npos && pLab > pNext))
      break;
    // (a column that is not in the join's row at all is stepped over without
    // a destination: TPC-H Q8, l_orderkey in the join of lineitem)
    size_t d = sCode.find(sDst, p);
    size_t g = sCode.find("goto L_", p);
    if (g == std::string::npos || g > pLab)
      return;
    if (sCode.compare(g, sJump.size(), sJump) == 0) {
      if (d == std::string::npos || d > pLab)
        return;
      size_t q = sCode.find(sCell, d);
      if (q == std::string::npos || q > pLab || i_Col < 0 || i_Col >= 64)
        return;
      vCols.push_back({i_Col, sCode.substr(d + sDst.size(), q - d - sDst.size())});
      uMask |= (u8)1 << i_Col;
    }
    pLast = sCode.find('\n', pLab);
    if (pLast == std::string::npos)
      return;
    if (pNext == std::string::npos)
      break;
    p = pNext;
  }
  if (vCols.empty() || pLast == std::string::npos)
    return;

  // copies of repeated columns (vPriorCrwCol) within the sequence
  std::vector<std::string> vCopies;
  {
    size_t a = pFirst;
    while (a < pLast) {
      size_t e = sCode.find('\n', a);
      if (e == std::string::npos || e > pLast)
        break;
      std::string sLine = sCode.substr(a, e - a);
      size_t s0 = sLine.find_first_not_of(" \t");
      if (s0 != std::string::npos && sLine.compare(s0, 3, "pSR") == 0 &&
          sLine.find(".m_iAsI8 = pSR") != std::string::npos)
        vCopies.push_back(sLine.substr(s0));
      a = e + 1;
    }
  }

  // named after the join's operator (the code must be the same each time it
  // is generated: the cache of compiled code is keyed by its text)
  std::string sDone = "L_CcJoinRet_" + sLabel.substr(13);   // "<op>_"

  std::string sFast;
  sFast += "  // column cache of the joined row (store/ColCache.cpp)\n";
  sFast += "  {\n";
  sFast += "    i8** aaCcJ = (i8**)aDynamicT->m_fp_aExtern_ColCacheJoin(iIndexBuffer, iIndexItem, (u8)" +
           std::to_string((unsigned long long)uMask) + "ULL);\n";
  sFast += "    if (aaCcJ) {\n";
  for (auto& rCol : vCols)
    sFast += "      " + rCol.second + ".m_iAsI8 = aaCcJ[" +
             std::to_string((long long)rCol.first) + "][iIndexItem];\n";
  for (auto& rCopy : vCopies)
    sFast += "      " + rCopy + "\n";
  sFast += "      goto " + sDone + ";\n";
  sFast += "    }\n";
  sFast += "  }\n";

  // the label after the last column first (it is later in the code)
  vInsertText(azStart + pLast + 1, ("  " + sDone + ":\n").c_str());
  char* azAt = azStart + pFirst;
  while (azAt > azStart && azAt[-1] != '\n')
    azAt--;
  vInsertText(azAt, sFast.c_str());
}

/*----------------------------------------------------------------------------*/
/* print the synthesized code                                                 */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vPrint(void) {
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  vTrace("%s\n"_Stz,m_caCode);
}

/*----------------------------------------------------------------------------*/
/* return source code                                                         */
/*----------------------------------------------------------------------------*/
  char *CombinedSrc::azCode(void) {
    _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

    return m_caCode;
  }

/*----------------------------------------------------------------------------*/
/* reset                                                                      */
/*----------------------------------------------------------------------------*/
  void CombinedSrc::vReset(void) {
    _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

    MemLib::vZero(m_caCode, sizeof(m_caCode), CN_COMBINEDSRC);
    m_azCodeCrs   = m_caCode;
  }

/*----------------------------------------------------------------------------*/
/*-- Private -----------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/* append code fragment                                                       */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, File* pOperatorFile)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;
  bool bExtracted;

  sCode = ""_Stz;
  if ( ! pOperatorFile )
    pOperatorFile = m_pCommonCode;

  pOperatorFile->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(pOperatorFile, pOpSketch, szFunction, sCode, nullptr, C_UNKNOWN);
  RASSERT(bExtracted);
  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
}

/*----------------------------------------------------------------------------*/
/* append code fragment                                                       */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, Column* pColumn, File* pOperatorFile)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;
  bool bExtracted;

  sCode = ""_Stz;
  if ( ! pOperatorFile )
    pOperatorFile = m_pCommonCode;

  pOperatorFile->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(pOperatorFile, pOpSketch, szFunction, sCode, pColumn, C_UNKNOWN);
  RASSERT(bExtracted);
  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
}

/*----------------------------------------------------------------------------*/
/* append code fragment                                                       */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, Column* pColumnFrom, Column* pColumnTo, File* pOperatorFile)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	s16KB sCode;
	bool bExtracted;

	sCode = ""_Stz;
	if ( ! pOperatorFile )
		pOperatorFile = m_pCommonCode;

	pOperatorFile->vSeekStart();
	bExtracted = CodeSynthUtils::bExtract(pOperatorFile, pOpSketch, szFunction, sCode, pColumnFrom, pColumnTo, C_UNKNOWN);
	RASSERT(bExtracted);
	StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
}

/*----------------------------------------------------------------------------*/
/* append code fragment                                                       */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeFragmentPL(OpSketch* pOpSketch, Stz* szFunction, Column* pColumn,
                                      i8 iIndexColumn)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;
  bool bExtracted;

  sCode = ""_Stz;
  m_pCommonCode->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(m_pCommonCode, pOpSketch, szFunction, sCode, pColumn,
                                        iIndexColumn);
  RASSERT(bExtracted);
  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
}

/*----------------------------------------------------------------------------*/
/* append code fragment for inflate columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeInflateCols(OpSketch* pOpSketch, Stz* sReturnPrefix, File* pOperatorFile)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s256 sReturnToColumn;
  s16KB sCode;
  bool bExtracted;

	Column* pColumn;
	Column* pColumnPrior;
	i8 i_StreamLeftCol = 0;

	if ( ! pOpSketch->m_pColInj )
		return;

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pColInj->m_nCols; i_Col++) {

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// same as prior column
		if (pOpSketch->m_pColInj->m_axCol[i_Col].m_bPriorCrwCol) {
			sCode = ""_Stz;
			pOperatorFile->vSeekStart();
			pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColInj->m_axCol[i_Col].m_i_StructCell);
			pColumnPrior = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColInj->m_axCol[i_Col-1].m_i_StructCell);
			bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, pColumn, pColumnPrior, "vPriorCrwCol"_Stz, sCode);
			RASSERT(bExtracted);
			StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
			continue;
		}

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// skip columns not required
		while (i_StreamLeftCol < pOpSketch->m_pColInj->m_axCol[i_Col].m_i_CrwCol) {
			sReturnToColumn.vSprint("%s_%lld_%lld"_Stz, sReturnPrefix, pOpSketch->m_iOperatorNo, i_StreamLeftCol);

			sCode = ""_Stz;
			pOperatorFile->vSeekStart();

			//pColumn = pOpSketch->m_pStreamDefLft->pGetColumn(i_StreamLeftCol);

			bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, nullptr, i_StreamLeftCol, sReturnToColumn, "vSkipCrwCol"_Stz, sCode);
			RASSERT(bExtracted);
			StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
			i_StreamLeftCol++;
		}

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// inflate columns required
		if (i_StreamLeftCol == pOpSketch->m_pColInj->m_axCol[i_Col].m_i_CrwCol) {
			sReturnToColumn.vSprint("%s_%lld_%lld"_Stz, sReturnPrefix, pOpSketch->m_iOperatorNo, i_StreamLeftCol);

			sCode = ""_Stz;
			pOperatorFile->vSeekStart();
			pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColInj->m_axCol[i_Col].m_i_StructCell);
			bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, pColumn, i_StreamLeftCol, sReturnToColumn, "vInflateCrwCol"_Stz, sCode);
			RASSERT(bExtracted);
			StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
			i_StreamLeftCol++;
		}

	}
}

/*----------------------------------------------------------------------------*/
/* append code fragment for inflate of injected columns                       */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeInflateInjCols(OpSketch* pOpSketch, Stz* sReturnPrefix, File* pOperatorFile)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	s256 sReturnToColumn;
	s16KB sCode;
	bool bExtracted;

	Column* pColumn;
	i8 i_StreamRightCol = 0;

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pColInj->m_nCols; i_Col++) {

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// skip columns not required
		while (i_StreamRightCol < pOpSketch->m_pColInj->m_axCol[i_Col].m_i_CrwCol) {
			sReturnToColumn.vSprint("%s_%lld_%lld"_Stz, sReturnPrefix, pOpSketch->m_iOperatorNo, i_StreamRightCol);

			sCode = ""_Stz;
			pOperatorFile->vSeekStart();

			pColumn = pOpSketch->m_pStreamDefRht->pGetColumn(i_StreamRightCol);

			bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, pColumn, sReturnToColumn, "vSkipCrwCol"_Stz, sCode);
			RASSERT(bExtracted);
			StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
			i_StreamRightCol++;
		}

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// inflate columns required
		if (i_StreamRightCol == pOpSketch->m_pColInj->m_axCol[i_Col].m_i_CrwCol) {
			sReturnToColumn.vSprint("%s_%lld_%lld"_Stz, sReturnPrefix, pOpSketch->m_iOperatorNo, i_StreamRightCol);

			sCode = ""_Stz;
			pOperatorFile->vSeekStart();
			pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColInj->m_axCol[i_Col].m_i_StructCell);
      //pColumn = pOpSketch->m_pStreamDefRht->pGetColumn(i_StreamRightCol);
      bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, pColumn, sReturnToColumn, "vInflateCrwCol"_Stz, sCode);
			RASSERT(bExtracted);
			StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
			i_StreamRightCol++;
		}

	}

}

/*----------------------------------------------------------------------------*/
/* append code fragment for inflate columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeInflateRightCols(OpSketch* pOpSketch, Stz* sReturnPrefix, File* pOperatorFile)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	s256 sReturnToColumn;
	s16KB sCode;
	bool bExtracted;

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pStreamDefRht->nColumns(); i_Col++) {

		Column* pColumn = pOpSketch->m_pStreamDefRht->pGetColumn(i_Col);
		sReturnToColumn.vSprint("%s_%lld_%lld"_Stz, sReturnPrefix, pOpSketch->m_iOperatorNo,i_Col+1);

		sCode = ""_Stz;
		pOperatorFile->vSeekStart();

		bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, pColumn, sReturnToColumn, "vInflateCrwCol"_Stz, sCode);
		RASSERT(bExtracted);
		StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
	}
}

/*----------------------------------------------------------------------------*/
/* append code fragment for inflate columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeInflateRowCols(OpSketch* pOpSketch, Stz* szFunction, File* pOperatorFile)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s256 sReturnToColumn;
  s16KB sCode;
  bool bExtracted;

  for (i8 i_Col = 0; i_Col < pOpSketch->m_pStreamDefLft->nColumns(); i_Col++) {

    Column *pColumn = pOpSketch->m_pStreamDefLft->pGetColumn(i_Col);

    if (pColumn->eColumnType() == CT_VARCHAR ||
        pColumn->eColumnType() == CT_CHAR) {
      sCode = ""_Stz;
      pOperatorFile->vSeekStart();

      bExtracted = CodeSynthUtils::bExtractInflate(pOperatorFile, pOpSketch, pColumn, ""_Stz, szFunction, sCode);
      RASSERT(bExtracted);
      StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
    }
  }
}

/*----------------------------------------------------------------------------*/
/* append code fragment for populating payload columns                        */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodePopulatePayloadCols(OpSketch* pOpSketch)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;
  i8 iPayLoad = 0;

  i8 nColumns = pOpSketch->m_pStreamDefLft->nColumns();
  RASSERT(nColumns == pOpSketch->m_pColMap->m_nCols);

  for (i8 i_Col = 0; i_Col < nColumns; i_Col++) {

    i8 i_EmittedCol = pOpSketch->m_pColMap->m_axCol[i_Col].m_i_EmittedCol;
    Column *pColumn = pOpSketch->m_pStreamDefLft->pGetColumn(i_EmittedCol-1);

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // is column in the payload ?
    bool bIsPayload = pOpSketch->bIsPayload(i_Col);

//    // is the column part of the aggregate group by key ?
//    Column* pIndexColumn = pOpSketch->m_pIndexDef->pFindColumn(i_Col);
//
//    if (pIndexColumn) {
//      continue;
//    }

    if ( ! bIsPayload ) {
      continue;
    }

    if ( pColumn->eColumnType() == CT_VARCHAR ||
         pColumn->eColumnType() == CT_CHAR ) {

      sCode = ""_Stz;
      m_pCommonCode->vSeekStart();

      vAppendCodeFragmentPL(pOpSketch, "vPopulateVchPL"_Stz, pColumn, iPayLoad);
      StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
    }
    else {
      sCode = ""_Stz;
      m_pCommonCode->vSeekStart();

      vAppendCodeFragmentPL(pOpSketch, "vPopulateIntPL"_Stz, pColumn, iPayLoad);
      StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
    }
    iPayLoad++;
  }
}

/*----------------------------------------------------------------------------*/
/* append file headers                                                        */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendFileHeaders(void) {
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  i8 nFileSize;
  s4K sCode;

  // zero the string area as reading into it and will not be null terminated
  sCode.vZero();

  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), "#define WITH_CLANG");

  // read the file header in
  sCode = ""_Stz;
  nFileSize = m_pFileHeader->iSize();
  m_pFileHeader->nReadBytes((void *) sCode.azChar(), nFileSize);
  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
}

/*----------------------------------------------------------------------------*/
/* append function footer fragment                                            */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendFunctionFooter(OpSketch* pOpSketch, eHK eHandlerKind, bool &bHasIndexInflate, bool bHasOrdAgg)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s256 szFunction;

  switch(eHandlerKind) {
    case HK_CRWBUFFER:
			if (bHasOrdAgg)
				szFunction = "vInflateCrwEnd_OA";
			else
        szFunction = "vInflateCrwEnd";
      break;
    case HK_EOS:
      szFunction = "iEosEnd";
      break;
    case HK_INDEXBUFFER:
			if (bHasOrdAgg)
        szFunction = "vIndexScanEnd_OA";
			else
			  szFunction = "vIndexScanEnd";
      break;

    case HK_ROWBUFFER:
		case HK_PDSROWBUFFER:
			if (bHasOrdAgg) {
				if (bHasIndexInflate)
					szFunction = "vInflateRowIndexEnd_OA";
				else
					szFunction = "vInflateRowEnd_OA";
			}
			else {
				if (bHasIndexInflate)
					szFunction = "vInflateRowIndexEnd";
				else
					szFunction = "vInflateRowEnd";
			}

      break;

    case HK_PDSROWBUFFER_FULL:
      szFunction = "vNoInflateRowEnd";
      break;

    case HK_STREAMDEF:
      szFunction = "iStreamDefEnd";
      break;
    default:
      NEVER_REACH;
  }
  // the groups of short string keys, found without the hash table
  if ((eHandlerKind == HK_CRWBUFFER || eHandlerKind == HK_PDSROWBUFFER || eHandlerKind == HK_ROWBUFFER) && ! bHasOrdAgg)
    vGbyKeyCache();
  // vectorized filter of a columnar scan (see vVecFilter)
  if (eHandlerKind == HK_CRWBUFFER && ! bHasOrdAgg)
    vVecFilter();

  s16KB sCode;
  bool bExtracted;

  sCode = ""_Stz;
  m_pFunctionFooter->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(m_pFunctionFooter, pOpSketch, szFunction, sCode, nullptr, C_UNKNOWN);
  RASSERT(bExtracted);

  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
  bHasIndexInflate = false;
}

/*----------------------------------------------------------------------------*/
/* append function header fragment                                            */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendFunctionHeader(OpSketch* pOpSketch, eHK eHandlerKind, Stz* szStageNm, bool bHasIndexInflate )
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;
  s256 sFunSignature;
  s256 sFunctionPrefix;
  bool bExtracted;
  bool bResult;

  s256 szFunction;

  switch(eHandlerKind) {
    case HK_CRWBUFFER:
      szFunction = "__FUNCTION_CRW__";
      sFunSignature = "iCrwHandler_";
      break;
    case HK_EOS:
      szFunction = "__FUNCTION_EOS__";
      sFunSignature = "iEosHandler_";
      break;
    case HK_INDEXBUFFER:
      szFunction = "__FUNCTION_INDEX__";
      sFunSignature = "iIndexHandler_";
      break;
    case HK_PDSROWBUFFER:
    case HK_PDSROWBUFFER_FULL:
      szFunction = "__FUNCTION_PDSROW__";
      sFunSignature = "iPdsRowHandler_";
      break;
    case HK_ROWBUFFER:
      szFunction = "__FUNCTION_ROW__";
      sFunSignature = "iRowHandler_";
      break;
    case HK_STREAMDEF:
      szFunction = "__FUNCTION_STREAMDEF__";
      sFunSignature = "iStreamDef_";
      break;
    default:
      NEVER_REACH;
  }

  // append header

  sCode = ""_Stz;
  m_pFunctionHeader->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(m_pFunctionHeader, nullptr, szFunction, sCode, nullptr, C_UNKNOWN);
  RASSERT(bExtracted);

  sFunSignature.vConcat(szStageNm);
  bResult = StrLib::bReplace(sCode.azChar(), sCode.nAreaBytes(), szFunction.azChar(), sFunSignature.azChar());
  RASSERT(bResult);

  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());

  if (eHandlerKind == HK_EOS || eHandlerKind == HK_STREAMDEF)
    return;

  // append vSetColumns

  sCode = ""_Stz;
  m_pFunctionHeader->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(m_pFunctionHeader, pOpSketch, "vSetColumns"_Stz, sCode, nullptr, C_UNKNOWN);
  RASSERT(bExtracted);

  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());

  // append function continue

  switch(eHandlerKind) {
    case HK_CRWBUFFER:
      szFunction = "vCrwContinue";
      break;
		case HK_PDSROWBUFFER:
    case HK_PDSROWBUFFER_FULL:
    case HK_ROWBUFFER:
      if (bHasIndexInflate)
        szFunction = "vRowIndexContinue";
      else
        szFunction = "vRowContinue";
      break;
    case HK_INDEXBUFFER:
      szFunction = "vIndexContinue";
      break;
    case HK_EOS:
      szFunction = "__FUNCTION_EOS__";
      break;
    case HK_STREAMDEF:
      szFunction = "__FUNCTION_STREAMDEF__";
      break;
    default:
      NEVER_REACH;
  }

  sCode = ""_Stz;
  m_pFunctionHeader->vSeekStart();
  bExtracted = CodeSynthUtils::bExtract(m_pFunctionHeader, nullptr, szFunction, sCode, nullptr, C_UNKNOWN);
  RASSERT(bExtracted);

  StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCode.azChar());
}

/*----------------------------------------------------------------------------*/
/* append stencil to                                                          */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendStencilTo(OpSketch* pOpSketch) {

  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;
  File* pStencilFile = new File(this);
  StreamDef* pStreamDef;

  bool bOpened = pStencilFile->bOpenRead("../src/store/Fragments/Stencil.cpp"_Stz);
  RASSERT ( bOpened );

  if (pOpSketch->m_eSketchKind == SK_SEMIJOIN || pOpSketch->m_eSketchKind == SK_ANTIJOIN) {
    pStreamDef = pOpSketch->m_pStreamDefRht;
  }
  else {
    pStreamDef = pOpSketch->m_pStreamDefOut;
  }

  sCode = ""_Stz;
  pStencilFile->vSeekStart();
  vAppendCodeFragment(pOpSketch, "vStencilToBegin"_Stz, pStencilFile);

  for (i8 i_Col = 0; i_Col < pStreamDef->nColumns(); i_Col++) {

    Column *pColumn = pStreamDef->pGetColumn(i_Col);

    sCode = ""_Stz;
    pStencilFile->vSeekStart();
    vAppendCodeFragment(pOpSketch, "vStencilToCol"_Stz, pColumn, pStencilFile);
  }

  sCode = ""_Stz;
  pStencilFile->vSeekStart();
  vAppendCodeFragment(pOpSketch, "vStencilToEnd"_Stz, pStencilFile);

  // close the stencil code fragments file
  pStencilFile->vClose();
  delete pStencilFile;
}

/*----------------------------------------------------------------------------*/
/* append code fragment for copying columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeCopyCols(OpSketch* pOpSketch, Stz* szFunction)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  for (i8 i_Col = 0; i_Col < pOpSketch->m_pStreamDefLft->nColumns(); i_Col++) {

    m_pCommonCode->vSeekStart();

    Column *pColumn = pOpSketch->m_pStreamDefLft->pGetColumn(i_Col);

    switch (pColumn->eColumnType()) {

			case CT_VARCHAR:
        vAppendCodeFragment(pOpSketch, szFunction, pColumn);
        break;

      default:
        vAppendCodeFragment(pOpSketch, "vCopyCol"_Stz, pColumn);
        break;
    }
  }
}

/*----------------------------------------------------------------------------*/
/* append code fragment for copying propagated columns                        */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodePpgCols(OpSketch* pOpSketch, Stz* szFunction)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	if ( ! pOpSketch->m_pColPpg ) {
		return;
	}

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pColPpg->m_nPropagated; i_Col++) {

		m_pCommonCode->vSeekStart();

		Column* pColumnIn = pOpSketch->m_pStreamDefLft->pGetColumn(pOpSketch->m_pColPpg->m_axCol[i_Col].m_i_CellIn);
		Column* pColumnOut = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColPpg->m_axCol[i_Col].m_i_StructCell);

		switch (pColumnIn->eColumnType()) {

			case CT_VARCHAR:
				vAppendCodeFragment(pOpSketch, szFunction, pColumnIn, pColumnOut);
				break;

			default:
				vAppendCodeFragment(pOpSketch, "vCopyCol"_Stz, pColumnIn, pColumnOut);
				break;
		}
	}
}

/*----------------------------------------------------------------------------*/
/* append code fragment for copying columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeTubCols(OpSketch* pOpSketch, Stz* szFunction)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pStreamDefLft->nColumns(); i_Col++) {

    m_pCommonCode->vSeekStart();

    Column *pColumn = pOpSketch->m_pStreamDefLft->pGetColumn(i_Col);

    switch (pColumn->eColumnType()) {

      case CT_VARCHAR:

        if (pOpSketch->m_eStencilTo == SR_TO)
          vAppendCodeFragment(pOpSketch, "vPotTubVchCol"_Stz, pColumn);
        else
          vAppendCodeFragment(pOpSketch, szFunction, pColumn);
        break;

      default:
        vAppendCodeFragment(pOpSketch, "vCopyCol"_Stz, pColumn);
        break;
    }
  }

}

/*----------------------------------------------------------------------------*/
/* append code fragment for hash columns                                      */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodePLToHashCols(OpSketch* pOpSketch)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	s16KB sCode;

	vAppendCodeFragment(pOpSketch, "vPLToHashBegin"_Stz);

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pColHsh->m_nCols; i_Col++) {

		m_pCommonCode->vSeekStart();

		i8 i_StreamCol = pOpSketch->m_pColHsh->m_axCols[i_Col].m_i_Col;

		Column *pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(i_StreamCol);

		bool bIsPayload = pOpSketch->bIsEmittedColPayload(i_StreamCol+1);

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// payload column
		if (bIsPayload) {

			switch (pColumn->eColumnType()) {

				case CT_VARCHAR:
				case CT_CHAR:
					RASSERT(0);
					break;

				default:
					vAppendCodeFragmentPL(pOpSketch, "vPayLoadToHash"_Stz, pColumn,
                                pOpSketch->m_pColMap->m_axCol[i_Col].m_i_Cell - 1);
					break;
			}
			continue;
		}

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// key column
		sCode = ""_Stz;
		m_pCommonCode->vSeekStart();

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// KE_INT
		if (pOpSketch->m_pIndexDef->eKeyKind() == KE_INT) {
			switch (pColumn->eColumnType()) {

				case CT_VARCHAR:
				case CT_CHAR:
					RASSERT(0);
					break;

				default:
					vAppendCodeFragment(pOpSketch, "vPLIntKeyToHash"_Stz, pColumn);
					break;
			}

			continue;
		}

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// KE_GENERAL
		RASSERT(pOpSketch->m_pIndexDef->eKeyKind() == KE_GENERAL);

		// get colMap columns
		i8 nColumns = pOpSketch->m_pColMap->m_nCols;

		for (i8 i_ColMap = 0; i_ColMap < nColumns; i_ColMap++) {

			// skip payloads
			if ( pOpSketch->m_pColMap->m_axCol[i_ColMap].m_bPayload )
				continue;

			// found the key column
			if (pOpSketch->m_pColMap->m_axCol[i_ColMap].m_i_EmittedCol - 1 == i_StreamCol) {
				switch (pColumn->eColumnType()) {

					case CT_VARCHAR:
					case CT_CHAR:
						RASSERT(0);
						break;

					default:
						vAppendCodeFragment(pOpSketch, "vPLIntKeyToHash"_Stz, pColumn);
						break;
				}
				break;
			}

			// skip the key column
			Column* pMapColumn = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColMap->m_axCol[i_ColMap].m_i_EmittedCol - 1);

			switch (pMapColumn->eColumnType()) {
				// skip the key column
				case CT_VARCHAR:
				case CT_CHAR:
					vAppendCodeFragment(pOpSketch, "vPLSkipVchKey"_Stz, pMapColumn);
					break;

				default:
					vAppendCodeFragment(pOpSketch, "vPLSkipIntKey"_Stz, pMapColumn);
			}
		}

	}

}

/*----------------------------------------------------------------------------*/
/* append code fragment for hash columns                                      */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeHashCols(OpSketch* pOpSketch)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	vAppendCodeFragment(pOpSketch, "vHashBegin"_Stz);

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pColHsh->m_nCols; i_Col++) {

		m_pCommonCode->vSeekStart();

		i8 i_StreamCol = pOpSketch->m_pColHsh->m_axCols[i_Col].m_i_Col;

		Column *pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(i_StreamCol);

		switch (pColumn->eColumnType()) {

			case CT_VARCHAR:
				vAppendCodeFragment(pOpSketch, "vAssignVchHash"_Stz, pColumn);
				break;

			default:
				vAppendCodeFragment(pOpSketch, "vAssignHash"_Stz, pColumn);
				break;
		}
	}

}

/*----------------------------------------------------------------------------*/
/* append code fragment for hash columns                                      */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeHashCols2(OpSketch* pOpSketch)
{
	_precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

	vAppendCodeFragment(pOpSketch, "vHashBegin"_Stz);

	for (i8 i_Col = 0; i_Col < pOpSketch->m_pColHsh->m_nCols; i_Col++) {

		m_pCommonCode->vSeekStart();

		i8 i_StreamCol = pOpSketch->m_pColHsh->m_axCols[i_Col].m_i_Col;

		Column *pColumn = pOpSketch->m_pStreamDefLft->pGetColumn(i_StreamCol);

		switch (pColumn->eColumnType()) {

			case CT_VARCHAR:
				vAppendCodeFragment(pOpSketch, "vAssignVchHash"_Stz, pColumn);
				break;

			default:
				vAppendCodeFragment(pOpSketch, "vAssignHash"_Stz, pColumn);
				break;
		}
	}

}

/*----------------------------------------------------------------------------*/
/* append code fragment for copying columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodePixCols(OpSketch* pOpSketch, bool bLastStage)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s256 sCopy;
  i8 nColumns = pOpSketch->m_pStreamDefOut->nColumns();

  for (i8 i_Col = 0; i_Col < nColumns; i_Col++) {

    m_pCommonCode->vSeekStart();

    Column *pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(i_Col);

    if (bLastStage)
      StrLib::vRemovePrefix(pColumn->szColNm()->azChar(), pColumn->iColNmBufSize(), '_');

    if (i_Col == nColumns - 1)
      sCopy.vSprint("      {%lld, \"%s\", %lld, %lld, %lld, %s, %s, %s}\n };\n"_Stz,
                    pColumn->iColumnNo(),
                    pColumn->szColNm(),
                    pColumn->xColumnTypeId().iIdVal(),
                    pColumn->iColumnLength(),
                    pColumn->iColumnScale(),
                    pColumn->bIsNullable() ? "true" : "false",
                    pColumn->bIsPtnKey() ? "true" : "false",
                    pColumn->bIsAscending() ? "true" : "false");
    else
      sCopy.vSprint("      {%lld, \"%s\", %lld, %lld, %lld, %s, %s, %s},\n"_Stz,
                    pColumn->iColumnNo(),
                    pColumn->szColNm(),
										pColumn->xColumnTypeId().iIdVal(),
                    pColumn->iColumnLength(),
                    pColumn->iColumnScale(),
                    pColumn->bIsNullable() ? "true" : "false",
                    pColumn->bIsPtnKey() ? "true" : "false",
                    pColumn->bIsAscending() ? "true" : "false");
    StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCopy.azChar());
  }

}

/*----------------------------------------------------------------------------*/
/* append code fragment for copying columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeIndexCols(OpSketch* pOpSketch)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s256 sCopy;
  i8 nColumns = pOpSketch->m_pIndexDef->nColumns();

  for (i8 i_Col = 0; i_Col < nColumns; i_Col++) {

    m_pCommonCode->vSeekStart();

    Column *pColumn = pOpSketch->m_pIndexDef->pGetColumn(i_Col);

    if (i_Col == nColumns - 1)
      sCopy.vSprint("      {%lld, \"%s\", %lld, %lld, %lld, %s, %s, %s}\n   };\n"_Stz,
                    pColumn->iColumnNo(),
                    pColumn->szColNm(),
										pColumn->xColumnTypeId().iIdVal(),
                    pColumn->iColumnLength(),
                    pColumn->iColumnScale(),
                    pColumn->bIsNullable() ? "true" : "false",
                    pColumn->bIsPtnKey() ? "true" : "false",
                    pColumn->bIsAscending() ? "true" : "false");
    else
      sCopy.vSprint("      {%lld, \"%s\", %lld, %lld, %lld, %s, %s, %s},\n"_Stz,
                    pColumn->iColumnNo(),
                    pColumn->szColNm(),
										pColumn->xColumnTypeId().iIdVal(),
                    pColumn->iColumnLength(),
                    pColumn->iColumnScale(),
                    pColumn->bIsNullable() ? "true" : "false",
                    pColumn->bIsPtnKey() ? "true" : "false",
                    pColumn->bIsAscending() ? "true" : "false");
    StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCopy.azChar());
  }

}
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/* append code fragment for copying columns                                   */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeNullRightCols(OpSketch* pOpSketch, Stz* szFunction)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  for (i8 i_Col = 0; i_Col < pOpSketch->m_pColInj->m_nCols; i_Col++) {
    m_pCommonCode->vSeekStart();
    Column *pColumn = pOpSketch->m_pStreamDefOut->pGetColumn(pOpSketch->m_pColInj->m_axCol[i_Col].m_i_StructCell);
    vAppendCodeFragment(pOpSketch, szFunction, pColumn);
  }

}

/*----------------------------------------------------------------------------*/
/* append code fragment for key, payload columns to tub                       */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vAppendCodeToTubCols(OpSketch* pOpSketch) {
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s16KB sCode;

  IndexDef* pIndexDef = pOpSketch->m_pIndexDef;
  StreamDef *pStreamDefOut = pOpSketch->m_pStreamDefOut;

  s256 sColNm;
  i8 nColumns = pStreamDefOut->nColumns();

  RASSERT(nColumns == pOpSketch->m_pColMap->m_nCols);

  for (i8 i_Col = 0; i_Col < nColumns; i_Col++) {

    i8 i_Cell = pOpSketch->m_pColMap->m_axCol[i_Col].m_i_Cell;
    i8 i_EmittedCol = pOpSketch->m_pColMap->m_axCol[i_Col].m_i_EmittedCol;
    Column *pColumn = pStreamDefOut->pGetColumn(i_EmittedCol-1);

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // is column in the payload ?
    bool bIsPayload = pOpSketch->bIsPayload(i_Col);

    if (bIsPayload) {

			switch (pColumn->eColumnType()) {

				case CT_VARCHAR:
				case CT_CHAR:
					vAppendCodeFragmentPL(pOpSketch, "vPayLoadToTubVchCol"_Stz, pColumn, i_Cell-1);
					break;

				default:
					vAppendCodeFragmentPL(pOpSketch, "vPayLoadToTubCol"_Stz, pColumn, i_Cell-1);
					break;
			}
		}

    else {
      sCode = ""_Stz;
      m_pCommonCode->vSeekStart();

      if (pIndexDef->eKeyKind() == KE_GENERAL) {
        switch (pColumn->eColumnType()) {

          case CT_VARCHAR:
          case CT_CHAR:
            vAppendCodeFragmentPL(pOpSketch, "vPLVchKeyToTub"_Stz, pColumn, i_Cell-1);
            break;

          default:
            vAppendCodeFragment(pOpSketch, "vPLIntKeyToTub"_Stz, pColumn);
            break;
        }
      }
      else {
        vAppendCodeFragment(pOpSketch, "vPayLoadKeyToTub"_Stz, pColumn);
      }
    }
}

}

/*----------------------------------------------------------------------------*/
/* close the code fragment files                                             */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vCloseCodeFiles(void)
{
  _precondition_(_ADDRESS_,_MAGIC_,CN_COMBINEDSRC,_FOOTER_);

  // close the files
  m_pFileHeader->vClose();
  m_pFunctionHeader->vClose();
  m_pFunctionFooter->vClose();
  m_pCommonCode->vClose();

}

/*----------------------------------------------------------------------------*/
/* generate code for the expression                                           */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vGenerateExprCode(TypedStack* pTypedStack, Stz* sIndent)
{
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  s4K sCopy;

  i8 nExpr = pTypedStack ? pTypedStack->nValues() - 1 : C_UNKNOWN ;

  TypeValueArea* pTypedCell;
  while ( nExpr >= 0) {
    pTypedCell = pTypedStack->aPeekItem(nExpr--);
    Stz* sExpr = pTypedCell->szStz();
    sCopy.vSprint("\n%s%s"_Stz, sIndent, sExpr->azChar());
    StrLib::vAppendString(&m_azCodeCrs, azCodeLastChar(), sCopy.azChar());
  }
}


/*----------------------------------------------------------------------------*/
/* print the synthesized code                                                 */
/*----------------------------------------------------------------------------*/
bool CombinedSrc::bOpenCodeFiles(void) {
  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  bool  bOpened;

  // open the file header file
  m_pFileHeader = new File(this);
  bOpened = m_pFileHeader->bOpenRead("../src/store/Fragments/fileHeader.cpp"_Stz);
  if ( ! bOpened )
    return false;

  // open the function headers file
  m_pFunctionHeader = new File(this);
  bOpened = m_pFunctionHeader->bOpenRead("../src/store/Fragments/functionHeaders.cpp"_Stz);
  if ( ! bOpened )
    return false;

  // open the code fragments file
  m_pCommonCode = new File(this);
  bOpened = m_pCommonCode->bOpenRead("../src/store/Fragments/CommonCode.cpp"_Stz);
  if ( ! bOpened )
    return false;

  // open the function footer file
  m_pFunctionFooter = new File(this);
  bOpened = m_pFunctionFooter->bOpenRead("../src/store/Fragments/functionFooters.cpp"_Stz);
  if ( ! bOpened )
    return false;

  return true;
}

/*----------------------------------------------------------------------------*/
/* generate code for operator begin                                           */
/*----------------------------------------------------------------------------*/
void CombinedSrc::vOperatorBegin(OpSketch* pOpSketch) {

  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  vAppendCodeFragment(pOpSketch, "vOperatorStub"_Stz);
  vAppendStencilTo(pOpSketch);
  vAppendCodeFragment(pOpSketch, "vOperatorBegin"_Stz);
}

/*----------------------------------------------------------------------------*/
/* Return a pointer to the last valid char in the m_caCode char array         */
/*----------------------------------------------------------------------------*/
const char* CombinedSrc::azCodeLastChar(void) {

  _precondition_(_ADDRESS_, _MAGIC_, CN_COMBINEDSRC, _FOOTER_);

  return m_caCode + sizeof(m_caCode) - 1;
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
