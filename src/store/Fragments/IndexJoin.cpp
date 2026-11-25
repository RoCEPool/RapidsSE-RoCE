/*============================================================================*/
/* IndexJoin.cpp -- Code Fragments                                            */
/*============================================================================*/

#include "BASE.h"
#include "Dynamic.hpp"
#include "IndexDef.hpp"
#include "CodeSynthCommon.h"
#include "ClangDynamic.h"
#include "ClangStuff.h"
#include "Pay.hpp"
#include "Key.hpp"
#include "Ent.hpp"
#include "Ent_int.h"
#include "eKE.h"

/*----------------------------------------------------------------------------*/
/*-- Code Fragments ----------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinIntKey --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinIntKey
void vIndexJoinIntKey(Dynamic_T* aDynamicT) {

  bool bLeftOuterRow;
  i8 __KEY_KIND__;
  i8 CURRENT_DUP_OP;

  struct __STRUCT_FROM___PREV_OP* p__STRUCT_FROM___PREV_OP;
  RowInfo __ROW_INFO_FROM__;

  __L_LABEL_OUTER_LOOP__:

  // marker-start
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // init indexJoin (Integer key)
  if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_bInitialize) {

#ifdef WITH_CLANG
		aDynamicT->m_fp_vExtern_IndexFind(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP],"__TABLENAME__","__INDEXNAME__");
#endif
    bLeftOuterRow = false;
    aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_bInitialize = false;

  }

// marker-end
}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinGenKey --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinGenKey
void vIndexJoinGenKey(Dynamic_T* aDynamicT) {

  bool bLeftOuterRow;
  i8 __KEY_KIND__;
  i8 CURRENT_DUP_OP;

  __L_LABEL_OUTER_LOOP__:

  // marker-start
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // init indexJoin (general key)
  if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_bInitialize) {

#ifdef WITH_CLANG
		aDynamicT->m_fp_vExtern_IndexFind(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP],"__TABLENAME__","__INDEXNAME__");
#endif
    bLeftOuterRow = false;
    aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_bInitialize = false;

  }
  else {

#ifdef WITH_CLANG
		aDynamicT->m_fp_vExtern_EntWrkReset(aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntWrk);
#endif

  }

// marker-end
}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinContinueLO1 ---------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinContinueLO1
void vIndexJoinContinueLO1(Dynamic_T* aDynamicT) {

  bool bLeftOuterRow;
  // marker-start
	__L_LABEL_JOIN_NULLS__:

  // did we join any rows with current left row ?
  if (aDynamicT->m_iJoinedRows == 0) {

		aDynamicT->m_bProcessRow = true;
    bLeftOuterRow = true;
    //printf("leftOuter = 1\n");

  // marker-end

  }
}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinContinueLO2 ---------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinContinueLO2
void vIndexJoinContinueLO2(Dynamic_T* aDynamicT) {

  __L_LABEL_OUTER_LOOP__:

  if (true) {

  // marker-start
    goto __L_LABEL_NEXT__;

  } else {

		aDynamicT->m_bProcessRow = false;
      goto __L_LABEL_OUTER_LOOP__;

  }

  // marker-end
  __L_LABEL_NEXT__:
  return;
}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinCrossPosition ------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinCrossPosition
void vIndexJoinCrossPosition(Dynamic_T* aDynamicT) {

  IndexInfo __INDEX_INFO__;
  i8 CURRENT_DUP_OP;

  __L_LABEL_OUTER_LOOP__:

  // marker-start

#ifdef WITH_CLANG
  // position at beginning of the index
  __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt =(struct Ent_int*)aDynamicT->m_fp_aExtern_IndexTreeBegin(
        __INDEX_INFO__[CURRENT_DUP_OP].m_aIndexTree, &__INDEX_INFO__[CURRENT_DUP_OP].m_aEntBuffer);
#endif

  __L_LABEL_INDEX_DEREF__:

  // did we find the entry ?
  if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
		aDynamicT->m_bProcessRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }

	aDynamicT->m_bProcessRow = true;

  // marker-end

  L_Return:
  return;

}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinDereference ---------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinDereference
void vIndexJoinDereference(Dynamic_T* aDynamicT) {

#include "vFLAT_Parameters.h"

  i8 iIndexBuffer;
  i8 iIndexItem;
  i8 i_BaseOffsetDataX;
  i8 CURRENT_DUP_OP;
  char* aIndexCrw;

  IndexInfo __INDEX_INFO__[C_MAX_DUP_OPS];
  RowInfo   __ROW_INFO_TO__[C_MAX_DUP_OPS];
  i8 __COLUMNS_TO_INFLATE_;
  i8 __COLUMNS_INFLATED__;

  static const void* C_ADR_REGION = (void*)0x204800000000;
  static const i8 C_CRW_HEADBYTES = 32;
  static const i8 C_CRW_FIRSTHEAD = 640;

  L_InflateAColumn:

  // marker-start

  // process the row
	aDynamicT->m_bProcessRow = true;

  aHeadLineSav = aHeadLine;

  // dereference the entry
  iIndexBuffer = __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt->Rrf.m_uBuffer;
  iIndexItem   = __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt->Rrf.m_uItem;
  aIndexCrw    = ( (char*)C_ADR_REGION + iIndexBuffer * C_1MB + C_CRW_FIRSTHEAD + iIndexItem * C_CRW_HEADBYTES );

  // inflate the derefenced entry
  aHeadLine = (struct HeadLine_T *)aIndexCrw;
  auMetaCode = META_AREA(aHeadLine);
  ayDstByte = (byte*)__ROW_INFO_TO__[CURRENT_DUP_OP].m_aRowCellArray + 8*__COLUMNS_INFLATED__;
  acStrArea = (char*)__ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea;
  racStrArea = acStrArea;
  eStrInflateRule = SI_STRAREA_ADR;
  i_BaseOffsetDataX = C_1B  + C_8B  + __COLUMNS_TO_INFLATE_;
  ayDataByte = ADDR_ADD(byte*,aHeadLine,i_BaseOffsetDataX);

  // marker-end

  __L_LABEL_NEXT__:

  L_Return:
  return;

}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinPosition_gen --------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinPosition_gen
void vIndexJoinPosition_gen(Dynamic_T* aDynamicT) {

  IndexInfo __INDEX_INFO__;
  i8 CURRENT_DUP_OP;

  __L_LABEL_OUTER_LOOP__:

  // marker-start

#ifdef WITH_CLANG
  // position at lower bound
	__INDEX_POSITION_LOWER__(
      &__INDEX_INFO__[CURRENT_DUP_OP]);
#endif

  // did we find the entry ?
  if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
		aDynamicT->m_bProcessRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // find the upper bound entry

#ifdef WITH_CLANG
  aDynamicT->m_fp_vExtern_IndexTreePositionLE_gen(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP]);
#endif
  __L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;

  if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {

		aDynamicT->m_bProcessRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }

  // marker-end

  L_Return:
  return;

}

/*----------------------------------------------------------------------------*/
/*-- vIndexJoinPosition_int -----------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vHashJoinPosition_int
void vHashJoinPosition_int(Dynamic_T* aDynamicT) {

	IndexInfo __INDEX_INFO__[C_MAX_DUP_OPS];
  RowInfo __ROW_INFO_TO__[C_MAX_DUP_OPS];
  i8 CURRENT_DUP_OP;
  bool bPdsRowBuffer;
  char* szStrArea;
  struct __STRUCT_FROM___PREV_OP { i8 iCell; };
  struct __STRUCT_TO___CURRENT_OP { i8 iCell; };
  struct __STRUCT_FROM___PREV_OP* p__STRUCT_FROM___PREV_OP;
  struct __STRUCT_TO___CURRENT_OP* p__STRUCT_TO___CURRENT_OP;
  void* hjEnt;
  void* hjBuf;
  void* hjNextEnt;
  void* hjNextBuf;
  char* hjNode;
#define __HJ_VCH__ {-1}

	__L_LABEL_OUTER_LOOP__:

	// marker-start

#ifdef WITH_CLANG
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // hash join, build: store the arriving row under its key
  if ( ! aDynamicT->m_bHashProbe) {
    // the first rows, one per 1000 entries of the index, go straight to the
    // index as in an index join: a join of few rows (TPC-H Q17) stays one
    if (__INDEX_INFO__[CURRENT_DUP_OP].m_iHjEntries == 0)
      __INDEX_INFO__[CURRENT_DUP_OP].m_iHjEntries = iExtern_HashJoinEntries(&__INDEX_INFO__[CURRENT_DUP_OP]);
    if (__INDEX_INFO__[CURRENT_DUP_OP].m_iHjSeen * 1000 < __INDEX_INFO__[CURRENT_DUP_OP].m_iHjEntries) {
      __INDEX_INFO__[CURRENT_DUP_OP].m_iHjSeen++;
      goto L_HJPosition__HJ_N__;
    }
    char* aHjRow = (char*)aExtern_HashJoinInsert(&__INDEX_INFO__[CURRENT_DUP_OP],
        *(i8*)__INDEX_INFO__[CURRENT_DUP_OP].m_aEntKey, sizeof(struct __STRUCT_FROM___PREV_OP));
    memcpy(aHjRow, p__STRUCT_FROM___PREV_OP, sizeof(struct __STRUCT_FROM___PREV_OP));
    {
      static const i8 aiHjVch[] = __HJ_VCH__;
      for (i8 k = 0; aiHjVch[k] >= 0; k++) {
        i8* piHjCell = (i8*)p__STRUCT_FROM___PREV_OP + aiHjVch[k];
        if (*piHjCell != C_DATUM_NULL) {
          char* szHjStr = (char*)*piHjCell;
          if (bPdsRowBuffer)
            szHjStr += (i8)piHjCell;
          ((i8*)aHjRow)[aiHjVch[k]] = (i8)aExtern_HashJoinStr(&__INDEX_INFO__[CURRENT_DUP_OP], szHjStr);
        }
      }
    }
    aDynamicT->m_bProcessRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // hash join, probe (called by the EOS handler): the joined table's index in
  // key order; each entry with stored rows is joined with each of them
  L_HJProbe__HJ_N__:
  if (aDynamicT->m_bHashProbe == 2)
    goto L_HJReplay__HJ_N__;
  hjEnt = aExtern_IndexTreeBegin(__INDEX_INFO__[CURRENT_DUP_OP].m_aIndexTree, &hjBuf);

  L_HJEntry__HJ_N__:
  if ( ! hjEnt)
    goto L_Return;
  hjNextBuf = hjBuf;
  hjNextEnt = aExtern_EntBufferNextEntry(&hjNextBuf, hjEnt);
  // the Bloom filter skips most entries whose key has no stored row
  {
    u8 uHjKey = (u8)((struct Ent_int*)hjEnt)->m_iIntElm0 * 0x9E3779B97F4A7C15ull;
    uHjKey ^= uHjKey >> 29;
    u8 uHjMask = __INDEX_INFO__[CURRENT_DUP_OP].m_uHjBloomMask;
    u8* auHjBloom = __INDEX_INFO__[CURRENT_DUP_OP].m_auHjBloom;
    u8 vHjKey = uHjKey >> 32;
    if (auHjBloom &&
        ( ! (auHjBloom[(uHjKey & uHjMask) >> 6] & (1ull << (uHjKey & 63))) ||
          ! (auHjBloom[(vHjKey & uHjMask) >> 6] & (1ull << (vHjKey & 63))) )) {
      hjEnt = hjNextEnt;
      hjBuf = hjNextBuf;
      goto L_HJEntry__HJ_N__;
    }
  }
  hjNode = (char*)aExtern_HashJoinFirst(&__INDEX_INFO__[CURRENT_DUP_OP], ((struct Ent_int*)hjEnt)->m_iIntElm0);
  if ( ! hjNode) {
    hjEnt = hjNextEnt;
    hjBuf = hjNextBuf;
    goto L_HJEntry__HJ_N__;
  }

  L_HJUse__HJ_N__:
  p__STRUCT_FROM___PREV_OP = (struct __STRUCT_FROM___PREV_OP*)(hjNode + 24);
  bPdsRowBuffer = false;
  p__STRUCT_TO___CURRENT_OP = (struct __STRUCT_TO___CURRENT_OP*)__ROW_INFO_TO__[CURRENT_DUP_OP].m_aRowCellArray;
  __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrAreaSav;
  szStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea;
  __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt = (struct Ent_int*)hjEnt;
  __INDEX_INFO__[CURRENT_DUP_OP].m_aEntBuffer = hjBuf;
  __INDEX_INFO__[CURRENT_DUP_OP].m_aEntUpper = (struct Ent_int*)hjNextEnt;
  goto __L_LABEL_INDEX_DEREF__;

  L_HJNext__HJ_N__:
  hjNode = *(char**)hjNode;
  if (hjNode)
    goto L_HJUse__HJ_N__;
  hjEnt = hjNextEnt;
  hjBuf = hjNextBuf;
  goto L_HJEntry__HJ_N__;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // hash join, replay (few rows stored): each stored row, in arrival order,
  // is looked up in the index as an index join does
  L_HJReplay__HJ_N__:
  hjNode = (char*)aExtern_HashJoinAll(&__INDEX_INFO__[CURRENT_DUP_OP]);

  L_HJReplayRow__HJ_N__:
  if ( ! hjNode)
    goto L_Return;
  p__STRUCT_FROM___PREV_OP = (struct __STRUCT_FROM___PREV_OP*)(hjNode + 24);
  bPdsRowBuffer = false;
  p__STRUCT_TO___CURRENT_OP = (struct __STRUCT_TO___CURRENT_OP*)__ROW_INFO_TO__[CURRENT_DUP_OP].m_aRowCellArray;
  __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrAreaSav;
  szStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea;
  *(i8*)__INDEX_INFO__[CURRENT_DUP_OP].m_aEntKey = *(i8*)(hjNode + 16);

  L_HJPosition__HJ_N__:
  __INDEX_POSITION_LOWER__(&__INDEX_INFO__[CURRENT_DUP_OP]);
  if ( ! __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt) {
    aDynamicT->m_bProcessRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }
  aDynamicT->m_fp_vExtern_IndexTreePositionLE_int(&__INDEX_INFO__[CURRENT_DUP_OP]);
  goto __L_LABEL_INDEX_DEREF__;

  L_HJReplayNext__HJ_N__:
  hjNode = *(char**)(hjNode + 8);
  goto L_HJReplayRow__HJ_N__;

#endif
	__L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;

	if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {

		aDynamicT->m_bProcessRow = false;
		goto __L_LABEL_OUTER_LOOP__;
	}

	// marker-end

  L_Return:
  return;
}

// function-vIndexJoinPosition_int
void vIndexJoinPosition_int(Dynamic_T* aDynamicT) {

	IndexInfo __INDEX_INFO__;
  i8 CURRENT_DUP_OP;

	__L_LABEL_OUTER_LOOP__:

	// marker-start

#ifdef WITH_CLANG
	// position at lower bound
	__INDEX_POSITION_LOWER__(
      &__INDEX_INFO__[CURRENT_DUP_OP]);
#endif

	// did we find the entry ?
	if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
		aDynamicT->m_bProcessRow = false;
		goto __L_LABEL_OUTER_LOOP__;
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// find the upper bound entry

#ifdef WITH_CLANG
  aDynamicT->m_fp_vExtern_IndexTreePositionLE_int(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP]);
#endif
	__L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;

	if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {

		aDynamicT->m_bProcessRow = false;
		goto __L_LABEL_OUTER_LOOP__;
	}

	// marker-end

	L_Return:
	return;

}

/*----------------------------------------------------------------------------*/
/*-- vIndexAntiJoinPosition_gen ----------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexAntiJoinPosition_gen
void vIndexAntiJoinPosition_gen(Dynamic_T* aDynamicT) {

	IndexInfo __INDEX_INFO__;
  i8 CURRENT_DUP_OP;

	__L_LABEL_NEXT__:

	// marker-start

#ifdef WITH_CLANG
	// position at lower bound
	__INDEX_POSITION_LOWER__(
      &__INDEX_INFO__[CURRENT_DUP_OP]);
#endif

	// did we find the entry ?
	if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
		aDynamicT->m_bProcessRow = true;
		goto __L_LABEL_NEXT__;
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// find the upper bound entry

#ifdef WITH_CLANG
	aDynamicT->m_fp_vExtern_IndexTreePositionLE_gen(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP]);
#endif
	__L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;

	if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {

		aDynamicT->m_bProcessRow = true;
		goto __L_LABEL_NEXT__;
	}

	// marker-end

	L_Return:
	return;

}

/*----------------------------------------------------------------------------*/
/*-- vIndexAntiJoinPosition_int --------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexAntiJoinPosition_int
void vIndexAntiJoinPosition_int(Dynamic_T* aDynamicT) {

	IndexInfo __INDEX_INFO__;
  i8 CURRENT_DUP_OP;

	__L_LABEL_NEXT__:

	// marker-start

#ifdef WITH_CLANG
	// position at lower bound
	__INDEX_POSITION_LOWER__(
      &__INDEX_INFO__[CURRENT_DUP_OP]);
#endif

	// did we find the entry ?
	if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
		aDynamicT->m_bProcessRow = true;
		goto __L_LABEL_NEXT__;
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// find the upper bound entry

#ifdef WITH_CLANG
	aDynamicT->m_fp_vExtern_IndexTreePositionLE_int(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP]);
#endif
	__L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;

	if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {

		aDynamicT->m_bProcessRow = true;
		goto __L_LABEL_NEXT__;
	}

	// marker-end

	L_Return:
	return;

}
/*----------------------------------------------------------------------------*/
/*-- vIndexJoinReturn --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexJoinReturn
void vIndexJoinReturn(Dynamic_T* aDynamicT) {

  void* aHeadLine;
  void* aHeadLineSav;
  void* aSubReturnLabel;

  i8 CURRENT_DUP_OP;

  IndexInfo __INDEX_INFO__[C_MAX_DUP_OPS];
  RowInfo __ROW_INFO_TO__[C_MAX_DUP_OPS];

  bool bLeftOuterRow;

  // marker-start
  goto __L_LABEL_INDEX_RETURN__;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // INFLATE-CRW-RET
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  __L_LABEL_INDEX_RETURN__:

  aHeadLine = aHeadLineSav;
  aDynamicT->m_iJoinedRows++;

  goto __L_LABEL_NEXT__;

  __L_LABEL_LOOP__:

  __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrAreaSav;

  // check if last row was a leftOuter
  if ( bLeftOuterRow ) {
    //printf("got leftOuter \n");
    bLeftOuterRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }

  // move to next entry
#ifdef WITH_CLANG
  __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt = (struct Ent_int*)aDynamicT->m_fp_aExtern_EntBufferNextEntry(
      &__INDEX_INFO__[CURRENT_DUP_OP].m_aEntBuffer, __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt);
#endif

  goto __L_LABEL_INDEX_DEREF__;

  // marker-end

  __L_LABEL_NEXT__:
  __L_LABEL_INDEX_DEREF__:
  __L_LABEL_OUTER_LOOP__:

  return;
}

/*----------------------------------------------------------------------------*/
/*-- iIndexJoinConstraint ----------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iIndexJoinConstraint
i8 iIndexJoinConstraint(Dynamic_T* aDynamicT) {

  void* aHeadLine;
  void* aHeadLineSav;
  bool bFilterResult;
	char* szStrArea;

  i8 CURRENT_DUP_OP;

  IndexInfo __INDEX_INFO__;
  RowInfo __ROW_INFO_TO__[C_MAX_DUP_OPS];

  i8 __CONSTRAINTSTACK__;

  bool bLeftOuterRow;

  // marker-start
  goto __L_LABEL_INDEX_RETURN__;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // INFLATE-CRW-RET
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

  __L_LABEL_INDEX_RETURN__:

  aHeadLine = aHeadLineSav;

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // join constraint
	szStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea;

  __CONSTRAINTSTACK__;

  // check for error
  if (aDynamicT->m_iErrorCode != (i8)EC_NONE) {
    return 0;
  }

  // check for constraint result
  if (bFilterResult && bFilterResult != C_DATUM_NULL) {
		aDynamicT->m_bProcessRow = true;
    aDynamicT->m_iJoinedRows++;
  } else {
		aDynamicT->m_bProcessRow = false;
  }

  goto __L_LABEL_NEXT__;

  __L_LABEL_LOOP__:

  __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrArea = __ROW_INFO_TO__[CURRENT_DUP_OP].m_acStrAreaSav;

  // check if last row was a leftOuter
  if ( bLeftOuterRow ) {
    //printf("got leftOuter \n");
    bLeftOuterRow = false;
    goto __L_LABEL_OUTER_LOOP__;
  }

  // move to next entry
#ifdef WITH_CLANG
  __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt = (struct Ent_int*)aDynamicT->m_fp_aExtern_EntBufferNextEntry(
      &__INDEX_INFO__[CURRENT_DUP_OP].m_aEntBuffer, __INDEX_INFO__[CURRENT_DUP_OP].m_aEntInt);
#endif

  goto __L_LABEL_INDEX_DEREF__;

  // marker-end

  __L_LABEL_NEXT__:
  __L_LABEL_INDEX_DEREF__:
  __L_LABEL_OUTER_LOOP__:

  return 0;
}

/*----------------------------------------------------------------------------*/
/*-- iIndexAntiJoinConstraint ------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iIndexAntiJoinConstraint
i8 iIndexAntiJoinConstraint(Dynamic_T* aDynamicT) {

    void* aHeadLine;
    void* aHeadLineSav;
    bool bFilterResult;

    IndexInfo __INDEX_INFO__;
    RowInfo __ROW_INFO_TO__;

    i8 __CONSTRAINTSTACK__;

    __L_LABEL_INDEX_DEREF__:

    // marker-start
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // join constraint
    __CONSTRAINTSTACK__;

    // check for error
    if (aDynamicT->m_iErrorCode != (i8)EC_NONE) {
        return 0;
    }

    // check for constraint result
    if (bFilterResult && bFilterResult != C_DATUM_NULL) {
			aDynamicT->m_bProcessRow = false;
    } else {
			aDynamicT->m_bProcessRow = true;
    }

    goto __L_LABEL_INDEX_RETURN__;

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // INFLATE-CRW-RET
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    __L_LABEL_INDEX_RETURN__:

    aHeadLine = aHeadLineSav;

    if ( aDynamicT->m_bProcessRow ) {
#ifdef WITH_CLANG

			aDynamicT->m_bProcessRow = false;

    // move to next entry
    aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt = (struct Ent_int*)aDynamicT->m_fp_aExtern_EntBufferNextEntry(
        &aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntBuffer, aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt);
#endif
        goto __L_LABEL_INDEX_DEREF__;
    }

    // marker-end

    return 0;
}

/*----------------------------------------------------------------------------*/
/*-- iIndexSemiJoinConstraint ------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iIndexSemiJoinConstraint
i8 iIndexSemiJoinConstraint(Dynamic_T* aDynamicT) {

    void* aHeadLine;
    void* aHeadLineSav;
    bool bFilterResult;

    IndexInfo __INDEX_INFO__;
    RowInfo __ROW_INFO_TO__;

    i8 __CONSTRAINTSTACK__;

    __L_LABEL_INDEX_DEREF__:

    // marker-start
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // join constraint
    __CONSTRAINTSTACK__;

    // check for error
    if (aDynamicT->m_iErrorCode != (i8)EC_NONE) {
        return 0;
    }

    // check for constraint result
    if (bFilterResult && bFilterResult != C_DATUM_NULL) {
			aDynamicT->m_bProcessRow = true;
    } else {
			aDynamicT->m_bProcessRow = false;
    }

    goto __L_LABEL_INDEX_RETURN__;

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // INFLATE-CRW-RET
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

    __L_LABEL_INDEX_RETURN__:

    aHeadLine = aHeadLineSav;

    if ( ! aDynamicT->m_bProcessRow ) {
#ifdef WITH_CLANG
        // move to next entry
    aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt = (struct Ent_int*)aDynamicT->m_fp_aExtern_EntBufferNextEntry(
        &aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntBuffer, aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt);
#endif
        goto __L_LABEL_INDEX_DEREF__;
    }

    // marker-end

    return 0;
}

/*----------------------------------------------------------------------------*/
/*-- vIndexLeftOuterJoinPosition_gen ---------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexLeftOuterJoinPosition_gen
void vIndexLeftOuterJoinPosition_gen(Dynamic_T* aDynamicT) {

  IndexInfo __INDEX_INFO__;
  bool bLeftOuterRow;
  i8 CURRENT_DUP_OP;

	__L_LABEL_JOIN_NULLS__:
	__L_LABEL_OUTER_LOOP__:

  // marker-start
#ifdef WITH_CLANG
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // position at lower bound
__INDEX_POSITION_LOWER__(
      &__INDEX_INFO__[CURRENT_DUP_OP]);
#endif

  // did we find the entry ?
  if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
     goto __L_LABEL_JOIN_NULLS__;
  }

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// find the upper bound entry

#ifdef WITH_CLANG
	aDynamicT->m_fp_vExtern_IndexTreePositionLE_gen(&aDynamicT->m_xIndexInfoJ)[CURRENT_DUP_OP];
#endif
	__L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;;

	if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {
		// did we join any rows with current left row ?
		if (aDynamicT->m_iJoinedRows == 0)
			goto __L_LABEL_JOIN_NULLS__;
		else {
			aDynamicT->m_bProcessRow = false;
			goto __L_LABEL_OUTER_LOOP__;
		}
	}
  // marker-end

}

/*----------------------------------------------------------------------------*/
/*-- vIndexLeftOuterJoinPosition_int ---------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexLeftOuterJoinPosition_int
void vIndexLeftOuterJoinPosition_int(Dynamic_T* aDynamicT) {

	IndexInfo __INDEX_INFO__;
	bool bLeftOuterRow;
  i8 CURRENT_DUP_OP;

	__L_LABEL_JOIN_NULLS__:
	__L_LABEL_OUTER_LOOP__:

	// marker-start
#ifdef WITH_CLANG
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // position at lower bound
__INDEX_POSITION_LOWER__(
      &__INDEX_INFO__[CURRENT_DUP_OP]);
#endif

	// did we find the entry ?
	if ( ! aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt ) {
		goto __L_LABEL_JOIN_NULLS__;
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// find the upper bound entry

#ifdef WITH_CLANG
	aDynamicT->m_fp_vExtern_IndexTreePositionLE_int(&aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP]);
#endif
	__L_LABEL_INDEX_DEREF__:
	aDynamicT->m_bProcessRow = true;;

	if (aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntInt == aDynamicT->m_xIndexInfoJ[CURRENT_DUP_OP].m_aEntUpper) {
		// did we join any rows with current left row ?
		if (aDynamicT->m_iJoinedRows == 0)
			goto __L_LABEL_JOIN_NULLS__;
		else {
			aDynamicT->m_bProcessRow = false;
			goto __L_LABEL_OUTER_LOOP__;
		}
	}
	// marker-end

}


/*----------------------------------------------------------------------------*/
/*-- vIndexLeftOuterNullCheck-------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vIndexLeftOuterNullCheck
i8 vIndexLeftOuterNullCheck(Dynamic_T* aDynamicT) {

	bool bNullResult;
	i8 __NULLSTACK__;

	__L_LABEL_JOIN_NULLS__:

	__L_LABEL_OUTER_LOOP__:

	// marker-start
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// check for null key
	__NULLSTACK__;

	// check null result
	if (bNullResult) {
		goto __L_LABEL_JOIN_NULLS__;
	}

	// marker-end

	return 0; // ray-alka
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
