#!/usr/bin/env python3
"""Optimiser on/off equivalence: the result of a query must not depend on the optimisations.

  run_optimiser_equivalence.py [--data-dir DIR] [--queries 01,05] [--switches all,cut_sort]

The run-time optimiser of RapidsSE cannot be switched off as a whole (flattening the
sub-queries and laying out the stages is what makes a pipeline executable). What can be switched
off are the transformations that only make a plan cheaper, with the job tags
/optimiser_off_<name> and /optimiser_off_all (src/store/XF.cpp). This script runs each of the
22 queries with every switch and compares the rows with the reference, by the rules of the
correctness test. It also runs each query once with /optimiser_validate (every card is checked
after every transformation, not only at the end of each phase). It writes
reports/OPTIMISER_EQUIVALENCE.md and exits 0 only when every
combination passes.
"""
import argparse, json, os, sys, time
from decimal import Decimal
import common as c
import prepare_sf1, run_reference, run_rapidsse as se

SWITCHES = ['all', 'join_rows', 'join_dp', 'local_gbyagg', 'corr_semijoin', 'eq_closure', 'const_fold', 'scan_project', 'sll_parse', 'hash_gbyagg', 'scan_prune', 'scan_gbyagg', 'join_project', 'join_gbyagg', 'join_prune', 'having_groups', 'scan_to_indexscan', 'cut_sort', 'early_projection', 'reduce_gbyagg_key', 'static_calc',
            'max_to_top', 'aggreg_to_sort_ordagg', 'sortdesc_limit_to_top', 'sort_limit_all_levels',
            'early_filter', 'late_projection', 'duplicate_filter']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--queries')
    ap.add_argument('--switches')
    ap.add_argument('--timeout', type=int, default=1200)
    a = ap.parse_args()
    queries = a.queries.split(',') if a.queries else c.QIDS
    switches = a.switches.split(',') if a.switches else SWITCHES

    ok, _ = prepare_sf1.check(a.data_dir, quiet=True)
    if not ok:
        sys.exit('the data set is not a complete SF1 data set: nothing was run')
    src = run_reference.run(None, a.data_dir)
    tol = Decimal(src['numeric_tolerance'])
    order_by = json.load(open(os.path.join(c.QUERIES, 'order_by.json')))

    node_log = os.path.join(c.REPORTS, 'optimiser_equivalence_node.log')
    se.start(a.data_dir, node_log)
    res = {}          # query -> switch -> (status, time ms, detail)
    for q in queries:
        stmt = open(os.path.join(c.QUERIES, 'q%s.sql' % q)).read().strip().rstrip(';')
        want = c.load_rows(os.path.join(c.EXPECTED, 'q%s.csv' % q))
        res[q] = {}
        for sw in ['on', 'validate'] + switches:
            if not se.alive():
                se.start(a.data_dir, node_log)
            tag = '' if sw == 'on' else '/optimiser_validate\n' if sw == 'validate' else '/optimiser_off_%s\n' % sw
            rc, out, dt = se.shell(tag + stmt + ';', a.timeout)
            err = se.error_of(out)
            if not se.alive():
                r = ('CRASH', None, err or 'the node went down')
            elif rc == 124:
                r = ('TIMEOUT', None, '')
            elif err:
                r = ('ERROR', None, err)
            else:
                good, msg = c.compare(se.parse_rows(out), want, order_by.get(q), tol)
                r = ('PASS' if good else 'WRONG_RESULT', int(dt * 1000), '' if good else msg)
            res[q][sw] = r
            print('  Q%s %-22s %-12s %6s ms  %s' % (q, sw, r[0], r[1] or '-', r[2][:100]), flush=True)
    se.stop()

    bad = [(q, sw, r) for q in res for sw, r in res[q].items() if r[0] != 'PASS']
    cols = ['on', 'validate'] + switches
    md = ['# Optimiser on/off equivalence: %s' % ('all combinations PASS' if not bad else '%d combination(s) FAIL' % len(bad)),
          '', '| | |', '|---|---|', '| Commit | `%s` |' % c.git_commit(),
          '| Run | %s |' % time.strftime('%Y-%m-%d %H:%M:%S %Z'),
          '| Engine | RapidsSE, 1 node, %d partitions, %d threads, TPC-H SF1 |' % (se.PARTITIONS, se.THREADS),
          '| Reference | %s |' % src['engine'], '',
          'Every cell is one execution of the query with the job tag `/optimiser_off_<column>`; `on` is the '
          'normal run; `validate` is the normal run with every card checked after every transformation '
          '(`/optimiser_validate`), which the optimiser otherwise only does at the end of each phase. The rows are compared with the reference by the rules of the correctness test. '
          'The number is the wall time in ms (it includes the start of the client).', '',
          '| Query | ' + ' | '.join(cols) + ' |', '|---|' + '---:|' * len(cols)]
    for q in res:
        md.append('| Q%s | ' % q + ' | '.join(str(res[q][sw][1]) if res[q][sw][0] == 'PASS' else '**%s**' % res[q][sw][0]
                                              for sw in cols) + ' |')
    tot = ['%d' % sum((res[q][sw][1] or 0) for q in res) for sw in cols]
    md.append('| **Total** | ' + ' | '.join(tot) + ' |')
    if bad:
        md += ['', '## Failures', ''] + ['- **Q%s, %s off**: %s %s' % (q, sw, r[0], r[2]) for q, sw, r in bad]
    c.write(os.path.join(c.REPORTS, 'OPTIMISER_EQUIVALENCE.md'), '\n'.join(md) + '\n')
    print('%d combinations, %d failed' % (len(res) * len(cols), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
