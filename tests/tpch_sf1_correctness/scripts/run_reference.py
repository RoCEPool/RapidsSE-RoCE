#!/usr/bin/env python3
"""Produce the reference results in reference/expected_results/ from a source that is independent
of RapidsSE.

  run_reference.py [--engine duckdb|official] [--data-dir DIR] [--out DIR]

duckdb    Loads the same .tbl files into DuckDB and runs the same 22 queries. This is the
          reference the acceptance asks for. It needs the Python package duckdb.
official  The answer set that ships with DBGEN (dbgen/answers), which the TPC publishes for scale
          factor 1 and the default substitution parameters used in queries/. Its values are
          rounded to two decimals.

Without --engine, duckdb is used when it can be imported, otherwise official. source.json records
which one produced the files and the numeric tolerance that goes with it.
"""
import argparse, json, os, re, sys
import common as c

DDL = {
    'region': 'r_regionkey INTEGER, r_name VARCHAR, r_comment VARCHAR',
    'nation': 'n_nationkey INTEGER, n_name VARCHAR, n_regionkey INTEGER, n_comment VARCHAR',
    'supplier': 's_suppkey INTEGER, s_name VARCHAR, s_address VARCHAR, s_nationkey INTEGER, s_phone VARCHAR, '
                's_acctbal DECIMAL(15,2), s_comment VARCHAR',
    'customer': 'c_custkey INTEGER, c_name VARCHAR, c_address VARCHAR, c_nationkey INTEGER, c_phone VARCHAR, '
                'c_acctbal DECIMAL(15,2), c_mktsegment VARCHAR, c_comment VARCHAR',
    'part': 'p_partkey INTEGER, p_name VARCHAR, p_mfgr VARCHAR, p_brand VARCHAR, p_type VARCHAR, p_size INTEGER, '
            'p_container VARCHAR, p_retailprice DECIMAL(15,2), p_comment VARCHAR',
    'partsupp': 'ps_partkey INTEGER, ps_suppkey INTEGER, ps_availqty INTEGER, ps_supplycost DECIMAL(15,2), '
                'ps_comment VARCHAR',
    'orders': 'o_orderkey INTEGER, o_custkey INTEGER, o_orderstatus VARCHAR, o_totalprice DECIMAL(15,2), '
              'o_orderdate DATE, o_orderpriority VARCHAR, o_clerk VARCHAR, o_shippriority INTEGER, o_comment VARCHAR',
    'lineitem': 'l_orderkey INTEGER, l_partkey INTEGER, l_suppkey INTEGER, l_linenumber INTEGER, '
                'l_quantity DECIMAL(15,2), l_extendedprice DECIMAL(15,2), l_discount DECIMAL(15,2), '
                'l_tax DECIMAL(15,2), l_returnflag VARCHAR, l_linestatus VARCHAR, l_shipdate DATE, '
                'l_commitdate DATE, l_receiptdate DATE, l_shipinstruct VARCHAR, l_shipmode VARCHAR, l_comment VARCHAR',
}


def official(out):
    for q in c.QIDS:
        path = os.path.join(c.REPO, 'dbgen', 'answers', 'q%d.out' % int(q))
        lines = open(path).read().splitlines()
        rows = [[v.strip() for v in l.split('|')] for l in lines[1:] if l.strip()]
        c.save_rows(os.path.join(out, 'q%s.csv' % q), rows)
    return {'engine': 'official TPC-H answer set (dbgen/answers), scale factor 1, default parameters',
            'numeric_tolerance': '0.01'}


def duckdb_reference(data_dir, out):
    import duckdb
    con = duckdb.connect()
    for t, ddl in DDL.items():
        con.execute('create table %s (%s)' % (t, ddl))
        # DBGEN ends every line with the delimiter: read it as one extra, empty column
        cols = ', '.join("'%s': '%s'" % tuple(x.strip().split(' ', 1)) for x in ddl.split(', '))
        names = ', '.join(x.strip().split(' ', 1)[0] for x in ddl.split(', '))
        con.execute("insert into %s select %s from read_csv('%s', delim='|', header=false, "
                    "columns={%s, 'eol': 'VARCHAR'})" % (t, names, os.path.join(data_dir, t + '.tbl'), cols))
    for q in c.QIDS:
        sql = open(os.path.join(c.QUERIES, 'q%s.sql' % q)).read()
        sql = re.sub(r"(interval '\d+' day) \(\d+\)", r'\1', sql)   # DuckDB has no interval precision
        rows = [['NULL' if v is None else str(v) for v in r] for r in con.execute(sql).fetchall()]
        c.save_rows(os.path.join(out, 'q%s.csv' % q), rows)
    return {'engine': 'DuckDB ' + duckdb.__version__, 'numeric_tolerance': '0.01'}


def run(engine=None, data_dir=c.DATA, out=c.EXPECTED):
    os.makedirs(out, exist_ok=True)
    if engine is None:
        try:
            import duckdb  # noqa: F401
            engine = 'duckdb'
        except ImportError:
            engine = 'official'
    src = duckdb_reference(data_dir, out) if engine == 'duckdb' else official(out)
    src['queries'] = 'tests/tpch_sf1_correctness/queries (TPC-H validation parameters)'
    c.write(os.path.join(out, 'source.json'), json.dumps(src, indent=2) + '\n')
    return src


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', choices=['duckdb', 'official'])
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--out', default=c.EXPECTED)
    a = ap.parse_args()
    print(run(a.engine, a.data_dir, a.out))
