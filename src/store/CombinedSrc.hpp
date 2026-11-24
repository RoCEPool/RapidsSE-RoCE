/*============================================================================*/
/* CombinedSrc.hpp -- Combined Source                                         */
/*============================================================================*/
#pragma once

#include "Tub.hpp"
#include "ePE.h"
#include "ePL.h"
#include "eEK.h"
#include "eFM.h"
#include "eKE.h"
#include "IndexDef.hpp"
#include "FunMatrix.h"
#include "OpSketch.hpp"

namespace STORE {

  static const i8 C_FLATTEN_SOURCE_V2 = C_256K; //C_SIZE_LARGE_OBJECT - C_1KB;

  class Stencil;

  //----------------------------------------------------------------------------

  class CombinedSrc : public Object {
  private:

    File* m_pFileHeader;
    File* m_pFunctionHeader;
    File* m_pFunctionFooter;
    File* m_pCommonCode;

    char m_caCode[C_FLATTEN_SOURCE_V2];
    char *m_azCodeCrs;
    bool m_bHavingGroupsHere = false;   // this stage's group by is the last one (Stencil.cpp)
    i8 m_iHgBefore = -1;    // having over the groups: where its first pass goes
    i8 m_iHgInside = -1;    // and where its test goes

    // must be last
    Footer m_xFooter;

  public:
    void vHavingGroupsHere(bool b) { m_bHavingGroupsHere = b; }
    void *operator new(size_t);

    void operator delete(void *);

    CombinedSrc(void);
    ~CombinedSrc(void) noexcept(false);

    void vSynthesizeCode(Stencil* pStencil, bool bFirstStage, bool bLastStage);

    char *azCode(void);

    void vPrint(void);

    void vReset(void);


  private:
    void vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, i8 iKeyColumn);
    void vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, File* pCodeFile = nullptr);
    void vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, Column* pColumn, File* pCodeFile = nullptr);
		void vAppendCodeFragment(OpSketch* pOpSketch, Stz* szFunction, Column* pColumnFrom, Column* pColumnTo, File* pCodeFile = nullptr);
    void vAppendCodeFragmentPL(OpSketch* pOpSketch, Stz* szFunction, Column* pColumn, i8 iPayLoad);
    void vAppendCodeFragmentKEY(OpSketch* pOpSketch, Stz* szFunction, File* pCodeFile, i8 iKeyColumn);
		void vAppendCodePLToHashCols(OpSketch* pOpSketch);
		void vAppendCodePpgCols(OpSketch* pOpSketch, Stz* szFunction);
		void vAppendCodeHashCols(OpSketch* pOpSketch);
		void vAppendCodeHashCols2(OpSketch* pOpSketch);
    void vPruneInflate(char* azStart, char* azEnd);
    void vColCacheInflate(char* azStart, char* azEnd);
    void vAppendCodeHavingGroups(void);
    void vInsertText(char* azAt, const char* szText);
    void vAppendCodeInflateCols(OpSketch* pOpSketch, Stz* szReturnPrefix, File* pCodeFile = nullptr);
    void vAppendCodeInflateInjCols(OpSketch* pOpSketch, Stz* szReturnPrefix, File* pCodeFile = nullptr);
		void vAppendCodeInflateRightCols(OpSketch* pOpSketch, Stz* szReturnPrefix, File* pCodeFile = nullptr);
    void vAppendCodeInflateRowCols(OpSketch* pOpSketch, Stz* szFunction, File* pCodeFile);
    void vAppendFileHeaders(void);
    void vAppendFunctionFooter(OpSketch* pOpSketch, eHK eHandlerKind, bool& bHasIndexInflate, bool bHasOrdAgg = false);
    void vAppendFunctionHeader(OpSketch* pOpSketch, eHK eHandlerKind, Stz* szStageNm, bool bHasIndexInflate);
    void vAppendStencilTo(OpSketch* pOpSketch);
    void vAppendCodeCopyCols(OpSketch* pOpSketch, Stz* szFunction);
		void vAppendCodeTubCols(OpSketch* pOpSketch, Stz* szFunction);
    void vAppendCodeNullRightCols(OpSketch* pOpSketch, Stz* szFunction);
    void vAppendCodePopulatePayloadCols(OpSketch* pOpSketch);
    void vAppendCodeToTubCols(OpSketch* pOpSketch);
    void vAppendCodePixCols(OpSketch* pOpSketch, bool bLastStage = false);
    void vAppendCodeIndexCols(OpSketch* pOpSketch);
    void vCloseCodeFiles(void);
    void vGenerateExprCode(TypedStack* pTypedStack, Stz* sIndent);
    bool bOpenCodeFiles(void);

    void vOperatorBegin(OpSketch* pOpSketch);

    const char* azCodeLastChar();

    Footer *aFooter(void);
  };

  CombinedSrc *_pCombinedSrc(void *aAddr);

}
/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
