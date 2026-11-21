#!/usr/bin/env python3
"""Cross-check of two reference result sets, with the rules of the test itself.

  check_reference.py DIR_A DIR_B

Used to show that the DuckDB results and the official TPC-H answer set agree, so that either
can serve as the reference. Exit status 0 when all 22 agree.
"""
import json, os, sys
from decimal import Decimal
import common as c

a, b = sys.argv[1], sys.argv[2]
order_by = json.load(open(os.path.join(c.QUERIES, 'order_by.json')))
bad = 0
for q in c.QIDS:
    ok, msg = c.compare(c.load_rows(os.path.join(a, 'q%s.csv' % q)), c.load_rows(os.path.join(b, 'q%s.csv' % q)),
                        order_by.get(q), Decimal('0.01'))
    print('Q%s %s %s' % (q, 'agree ' if ok else 'DIFFER', msg))
    bad += not ok
print('%d/22 agree' % (22 - bad))
sys.exit(1 if bad else 0)
