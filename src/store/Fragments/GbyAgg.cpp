/*============================================================================*/
/* Code.cpp -- Code Fragments                                                 */
/*============================================================================*/

#include "BASE.h"
#include "Dynamic.hpp"
#include "IndexDef.hpp"
#include "CodeSynthCommon.h"
#include "ClangStuff.h"
#include "ClangDynamic.h"
#include "Pay.hpp"
#include "Key.hpp"
#include "Ent.hpp"
#include "Ent_int.h"

/*----------------------------------------------------------------------------*/
/*-- Code Fragments ----------------------------------------------------------*/
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*-- iGbyAggBegin ------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggBegin
i8 iGbyAggBegin(Dynamic_T* aDynamicT) {

  i8 CURRENT_DUP_OP;
  i8 PREV_DUP_OP;
  i8 PREV_OP_NO;

  struct __STRUCT_FROM___PREV_OP* p__STRUCT_FROM___PREV_OP;
  RowInfo __ROW_INFO_FROM__[C_MAX_DUP_OPS];

  char* szStrArea;

  __L_LABEL_LOOP__:

  // marker-start

  __L_LABEL__:

  p__STRUCT_FROM___PREV_OP = (struct __STRUCT_FROM___PREV_OP*)__ROW_INFO_FROM__[PREV_DUP_OP].m_aRowCellArray;
  szStrArea = __ROW_INFO_FROM__[PREV_DUP_OP].m_acStrArea;

  // marker-end

  return 0;
}

/*----------------------------------------------------------------------------*/
/*-- vGbyAggFindKeyAsc -------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-vGbyAggFindKeyAsc
void vGbyAggFindKeyAsc(Dynamic_T* aDynamicT) {

  void *aEntFound;

  // marker-start
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // find key in payload index
#ifdef WITH_CLANG
  aEntFound = aDynamicT->m_fp_aExtern_IndexTreeFindEntryAsc(aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aIndexTree, aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey);
#endif
  if (!aEntFound) {
    // marker-end
  }
}

/*----------------------------------------------------------------------------*/
/*-- vGbyAggFindIntKeyAsc ----------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// The rows of a group often come one after another (lineitem holds the lines
// of an order together), so the entry of the previous row is tried before the
// tree. It stays valid until the next insert, which replaces it (the int key
// end fragment below records it). One integer key only (KE_INT).
// (Fragments are found by name anywhere in a comment: do not name another
// fragment in a comment.)
// function-vGbyAggFindIntKeyAsc
void vGbyAggFindIntKeyAsc(Dynamic_T* aDynamicT) {

  i8 CURRENT_DUP_OP;
  void *aEntFound;

  // marker-start
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // find key: the previous row's entry, else the payload index
  if (aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntLast &&
      aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_iKeyLast == *(i8*)aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey) {
    aEntFound = aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntLast;
  }
  else {
#ifdef WITH_CLANG
    aEntFound = aDynamicT->m_fp_aExtern_IndexTreeFindEntryAsc(aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aIndexTree, aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey);
#endif
  }
  if (!aEntFound) {
    // marker-end
  }
}

/*----------------------------------------------------------------------------*/
/*-- vGbyAggHashIntKey -------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// The group of the row in a hash table (IndexTree.cpp, GbyHash), as DuckDB
// aggregates, instead of a find and an insert in the payload tree. A new
// group is laid out as a tree entry and its payload is initialised below.
// One integer key only (KE_INT, ascending).
// function-vGbyAggHashIntKey
void vGbyAggHashIntKey(Dynamic_T* aDynamicT) {

  i8 CURRENT_DUP_OP;
  void *aEntFound;

  // marker-start
  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // find or add the group of the key
  // (the rows of a group often come one after another: the group of the
  // previous row, recorded by the int key end fragment, is tried first; a
  // group's block never moves)
  bool bNewGroup = false;
#ifdef WITH_CLANG
  if (aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntLast &&
      aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_iKeyLast == *(i8*)aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey)
    aEntFound = aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntLast;
  else
    aEntFound = aExtern_GbyHashFindInt(&aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP],
                                       *(i8*)aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey,
                                       __PAYLOADBYTES__, &bNewGroup);
#endif
  if (bNewGroup) {
    aDynamicT->m_iRowsProcessed++;
    aDynamicT->m_aiPayload = (i8 *) ((char *) aEntFound
                                     + sizeof(struct Ent_int)
                                     + 8);
    // marker-end
  }
}

/*----------------------------------------------------------------------------*/
/*-- vGbyAggFindKeyAscDsc ------------------------------------------------------*/
/*------------------------------------------------------------------------------*/
// function-vGbyAggFindKeyAscDsc
  void vGbyAggFindKeyAscDsc(Dynamic_T* aDynamicT) {

    void *aEntFound;

    // marker-start
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // find key in payload index
#ifdef WITH_CLANG
    aEntFound = aDynamicT->m_fp_aExtern_IndexTreeFindEntryAscDsc(aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aIndexTree, aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey);
#endif
    if (!aEntFound) {
      // marker-end
    }
}

/*----------------------------------------------------------------------------*/
/*-- iGbyAggFindInsertGenAsc -------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggFindInsertGenAsc
i8 iGbyAggFindInsertGenAsc(Dynamic_T* aDynamicT) {

	void *aEntFound;

	// marker-start
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// find key in payload index
#ifdef WITH_CLANG
  bool bInsertedEntry = false;
	aEntFound = aDynamicT->m_fp_aExtern_IndexTreeFindInsertGenAsc(aDynamicT, 72, &bInsertedEntry);
#endif
	if (aDynamicT->m_iErrorCode != (i8) EC_NONE) {
		return 0;
	}
	// marker-end

	return 0;
}

/*----------------------------------------------------------------------------*/
/*-- iGbyAggGenAscBegin --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggGenAscBegin
i8 iGbyAggGenAscBegin(Dynamic_T* aDynamicT) {

	void *aEntFound;
	bool bInsertedEntry;

	// marker-start

	if (bInsertedEntry) {

		aDynamicT->m_iRowsProcessed++;

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// initialize the payload
		// marker-end

	}

	return 0;
}

/*----------------------------------------------------------------------------*/
/*-- iGbyAggGenAscEnd --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggGenAscEnd
i8 iGbyAggGenAscEnd(Dynamic_T* aDynamicT) {

	{
		// marker-start
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// aggregate the payload
		// marker-end

		return 0;
}

/*----------------------------------------------------------------------------*/
/*-- iGbyAggInsertIntKey -----------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggInsertIntKey
i8 iGbyAggInsertIntKey(Dynamic_T* aDynamicT) {

  void* aEntInserted;
  void* aEntFound;
  i8 __PAYLOADBYTES__;

  // marker-start

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // insert key in the payload
#ifdef WITH_CLANG
    aEntInserted = aExtern_IndexTreeInsertEnt(
        aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aIndexTree, aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntInt, &aDynamicT->m_iErrorCode);
#endif

    // check for error
    if (aDynamicT->m_iErrorCode != (i8)EC_NONE) {
      return 0;
    }

    // did we insert the key ?
    if (aEntInserted) {
      aDynamicT->m_iRowsProcessed++;
      aDynamicT->m_aiPayload = (i8 *) ((char *) aEntInserted
          + sizeof(struct Ent_int)
          + 8);
     }

     aEntFound = aEntInserted;

// marker-end

  return 0;

}

/*----------------------------------------------------------------------------*/
/*-- iGbyAggIntKeyEnd --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggIntKeyEnd
i8 iGbyAggIntKeyEnd(Dynamic_T* aDynamicT) {

  i8 CURRENT_DUP_OP;
  void* aEntFound;

  {
  // marker-start

  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // aggregate the payload
  aDynamicT->m_aiPayload = (i8 *) ((char *) aEntFound
                  + sizeof(struct Ent_int)
                  + 8);
  aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntLast = aEntFound;
  aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_iKeyLast = *(i8*)aDynamicT->m_xIndexInfoPL[CURRENT_DUP_OP].m_aEntKey;

  // marker-end

 return 0;

}

/*----------------------------------------------------------------------------*/
/*-- iGbyAggGenKeyEnd --------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
// function-iGbyAggGenKeyEnd
i8 iGbyAggGenKeyEnd(Dynamic_T* aDynamicT) {

  void* aEntFound;

  {
  // marker-start

    // check for error
    if (aDynamicT->m_iErrorCode != (i8) EC_NONE) {
      return 0;
    }
  }

  // - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
  // aggregate the payload
  aDynamicT->m_aiPayload = (i8 *) ((char *) aEntFound
                  + sizeof(struct Ent_int)
                  + ((struct Ent_int*)aEntFound)->m_iIntElm0
                  + 8);

  // marker-end

 return 0;
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
