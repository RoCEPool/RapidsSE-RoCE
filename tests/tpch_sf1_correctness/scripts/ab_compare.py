#!/usr/bin/env python3
"""Time the 22 TPC-H SF1 queries on two builds of RapidsSE on the same machine.

  ab_compare.py --base DIR --head DIR [--data-dir DIR] [--order base,head,head,base] [--out FILE]

DIR is a source tree with its engine built (DIR/bin/rapidsse). The generated query code of a
build is compiled against the headers of its own tree, so each tree runs its own node, one at a
time, in the order given: each run loads the data, runs every query once (its code is compiled
and cached) and then times it with run_rapidsse.engine_time (the best of TPCH_TIMED_RUNS runs of
the time in the engine). A tree's time for a query is its best over its runs. Alternating the
trees (base, head, head, base) spreads a drift of the machine over both.

The comparison of two CI runs is not reliable: the runners differ in speed by tens of percent
between runs, and RapidsSE and DuckDB do not scale alike with the runner. Here both builds run
on one runner. Writes a markdown table (base ms, head ms, change) and prints it.
"""
import argparse, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common as c
import prepare_sf1
import run_rapidsse as se


def time_tree(repo, data_dir, log):
    c.REPO = repo
    se.ENGINE_LOG = os.path.join(repo, 'se.log')
    counts = se.start(data_dir, log)
    # region and nation are replicated: count(*) adds up a copy per partition
    bad = [t for t, n in counts.items() if t not in ('region', 'nation') and n != c.SF1_ROWS[t]]
    if bad:
        se.stop()
        raise SystemExit('%s: wrong row counts after the load: %s' % (repo, bad))
    times, phases = {}, {}
    for q in c.QIDS:
        stmt = open(os.path.join(c.QUERIES, 'q%s.sql' % q)).read().strip().rstrip(';')
        se.shell(stmt + ';', 1200)                     # compiles and caches the code
        phases[q] = {}
        times[q] = se.engine_time(stmt, 1200, phases[q])
        if not se.alive():
            se.stop()
            raise SystemExit('%s: the node died on Q%s' % (repo, q))
    se.stop()
    return times, phases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', required=True)
    ap.add_argument('--head', required=True)
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--order', default='base,head,head,base')
    ap.add_argument('--out', default=os.path.join(c.REPORTS, 'AB_TIMES.md'))
    a = ap.parse_args()
    trees = {'base': os.path.abspath(a.base), 'head': os.path.abspath(a.head)}
    best = {'base': {}, 'head': {}}
    best_ph = {'base': {}, 'head': {}}       # query -> phase -> best ms
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    ok, _ = prepare_sf1.check(a.data_dir)          # generates the data with this tree's DBGEN
    if not ok:
        raise SystemExit('the SF1 data set is not right')
    for i, name in enumerate(a.order.split(',')):
        t0 = time.time()
        times, phases = time_tree(trees[name], a.data_dir, os.path.join(os.path.dirname(a.out), 'ab_node_%d_%s.log' % (i, name)))
        for q, ms in times.items():
            if ms is not None:
                best[name][q] = min(best[name].get(q, ms), ms)
        for q, ph in phases.items():
            d = best_ph[name].setdefault(q, {})
            for k, v in ph.items():
                d[k] = v if k not in d else min(d[k], v)
        print('run %d (%s) done in %.0f s' % (i + 1, name, time.time() - t0), flush=True)

    md = ['## RapidsSE: base against head on the same machine', '',
          'Time in the engine (SQL compile to end of job, compiled code from the cache), best of %d runs '
          'in each of the runs of a build; runs in the order %s, %d partitions, %d threads, %d CPUs.'
          % (se.TIMED_RUNS, a.order, se.PARTITIONS, se.THREADS, os.cpu_count()), '',
          '| Query | base ms | head ms | change |', '|---|---:|---:|---:|']
    tb = th = 0
    for q in c.QIDS:
        b, h = best['base'].get(q), best['head'].get(q)
        if b is None or h is None:
            md.append('| Q%s | %s | %s | |' % (q, b, h))
            continue
        tb += b
        th += h
        md.append('| Q%s | %d | %d | %+.1f%% |' % (q, b, h, 100.0 * (h - b) / max(b, 1)))
    if tb:
        md.append('| **Total** | **%d** | **%d** | **%+.1f%%** |' % (tb, th, 100.0 * (th - tb) / tb))
    # where the change went: the phases of the engine time, summed over the queries (each the
    # best of its runs, so the sum of a column is not the total above)
    md += ['', 'Phases (best of each, summed over the queries):', '',
           '| Phase | base ms | head ms | change |', '|---|---:|---:|---:|']
    for k, label in (('sql', 'SQL compile'), ('optimise', 'Optimise'), ('weave', 'Weave'),
                     ('execute', 'Execute'), ('other', 'Other')):
        b = sum(d.get(k, 0) for d in best_ph['base'].values())
        h = sum(d.get(k, 0) for d in best_ph['head'].values())
        if b or h:
            md.append('| %s | %d | %d | %+.1f%% |' % (label, b, h, 100.0 * (h - b) / max(b, 1)))
    md.append('')
    open(a.out, 'w').write('\n'.join(md) + '\n')
    print('\n'.join(md))


if __name__ == '__main__':
    main()
