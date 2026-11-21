#!/usr/bin/env python3
"""Step 5 of the test: compare the rows of a run with the reference results.

  compare_results.py RUN_DIR

Sets the final status of every query in RUN_DIR/run.json and returns the number that passed.
The rules are in README.md ("How results are compared").
"""
import json, os, sys
from decimal import Decimal
import common as c


def compare_run(run_dir):
    res = json.load(open(os.path.join(run_dir, 'run.json')))
    src = json.load(open(os.path.join(c.EXPECTED, 'source.json')))
    tol = Decimal(src['numeric_tolerance']) if src.get('numeric_tolerance') else None
    order_by = json.load(open(os.path.join(c.QUERIES, 'order_by.json')))
    res['reference'] = src['engine']
    for q, d in res['queries'].items():
        d['result'] = 'NOT_RUN'
        if d['class'] != 'EXECUTED':
            d['status'] = d['class']
            continue
        got = c.load_rows(os.path.join(run_dir, 'results', 'q%s.csv' % q))
        want = c.load_rows(os.path.join(c.EXPECTED, 'q%s.csv' % q))
        ok, msg = c.compare(got, want, order_by.get(q), tol)
        d['result'] = 'PASS' if ok else 'FAIL'
        d['comparison'] = msg
        d['status'] = 'PASS' if ok else 'WRONG_RESULT'
        if not ok:
            d['error'] = msg
    c.write(os.path.join(run_dir, 'run.json'), json.dumps(res, indent=2) + '\n')
    return res


if __name__ == '__main__':
    r = compare_run(sys.argv[1])
    n = sum(1 for d in r['queries'].values() if d['status'] == 'PASS')
    print('%d/%d PASS' % (n, len(r['queries'])))
