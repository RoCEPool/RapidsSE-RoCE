/*============================================================================*/
/* ViewCatalog.hpp -- View definitions (CREATE VIEW / DROP VIEW)              */
/*============================================================================*/

#pragma once

#include <string>

namespace STORE
{
  /**
   * A view is a named query. Its definition is kept as text in the "views"
   * metadata table of every node:
   *
   *   catalogName, schemaName, viewName, columnList, definition
   *
   * columnList is either empty or the parenthesised column list given in
   * CREATE VIEW, e.g. "(supplier_no, total_revenue)". definition is the text
   * of the query that follows AS.
   *
   * A view is expanded where it is used: the SQL compiler treats a statement
   * that refers to view v as if it began with
   *
   *   WITH v <columnList> AS ( <definition> )
   *
   * so a view behaves exactly like a WITH query that every statement can see.
   * View names are matched without regard to case, as table names are.
   */
  class ViewCatalog
  {
  public:
    // Column positions in the "views" metadata table
    static const i8 C_COL_CATALOG    = 0;
    static const i8 C_COL_SCHEMA     = 1;
    static const i8 C_COL_NAME       = 2;
    static const i8 C_COL_COLUMNLIST = 3;
    static const i8 C_COL_DEFINITION = 4;

    // Finds a view on this node. Returns false if there is no such view.
    // sStoredNm receives the name exactly as it was given in CREATE VIEW.
    static bool bFind(Stz* szViewNm, s256& sStoredNm, s4K& sColumnList, s4K& sDefinition);

    // Is there a view of this name on this node?
    static bool bExists(Stz* szViewNm);

    // Adds a view to this node's "views" metadata table.
    static void vInsert(Stz* szViewNm, Stz* szColumnList, Stz* szDefinition);

    // Removes a view from this node's "views" metadata table. szViewNm must
    // be the stored name (see bFind). Does nothing if there is no such view.
    static void vDelete(Stz* szViewNm);

    // Do two object names refer to the same object?
    static bool bSameName(Stz* szA, Stz* szB);

    // The column list and query of a view travel from the node that receives
    // CREATE VIEW to every node as parameters of the "create view" procedure.
    // They are free text, so they are sent as the letter V followed by two
    // hexadecimal digits for each character.
    static std::string sEncodeText(const std::string& text);
    static std::string sDecodeText(const std::string& encoded);
  };
}

/*============================================================================*/
/* end                                                                        */
/*============================================================================*/
