#!/usr/bin/env python3
"""The whole test with one command.

  run_all.py [--data-dir DIR] [--rounds N] [--reference duckdb|official] [--timeout SECONDS]

1. checks (or generates) the SF1 data set          prepare_sf1.py
2. produces the reference results                  run_reference.py
3. runs the 22 queries on RapidsSE, N rounds       run_rapidsse.py
4. compares every result with the reference        compare_results.py
5. writes reports/<time>_<commit>/ with the matrix (matrix.md, matrix.csv, summary.json) and,
   per round, the results, plans and logs; reports/LATEST.md is a copy of the newest matrix.

The exit status is 0 only when every query passed in every round.
"""
import argparse, csv, json, os, shutil, sys, time
import common as c
import prepare_sf1, run_reference, run_rapidsse, compare_results

COLS = ['parser', 'binder', 'planner', 'optimizer', 'physical_plan', 'runtime', 'result']


def fixes():
    """queries/../fixes.json: which change made which query pass (maintained with the fixes)."""
    p = os.path.join(c.SUITE, 'fixes.json')
    return json.load(open(p)) if os.path.exists(p) else {}


def matrix(rounds, info, src, out_dir):
    fx = fixes()
    last = rounds[-1]
    rows = []
    for q in c.QIDS:
        d = last['queries'][q]
        statuses = [r['queries'][q]['status'] for r in rounds]
        final = statuses[0] if len(set(statuses)) == 1 else 'UNSTABLE (' + ', '.join(statuses) + ')'
        f = fx.get(q, {})
        rows.append({'query': 'Q' + q, **{k: d.get(k, 'NOT_RUN') for k in COLS},
                     'time_ms': d.get('wall_ms') or '', 'failure_class': '' if final == 'PASS' else d['status'],
                     'failure_reason': '' if final == 'PASS' else (d.get('error') or ''),
                     'source_files': f.get('source_files', ''), 'fix_commit': f.get('fix', ''),
                     'final_status': final})
    with open(os.path.join(out_dir, 'matrix.csv'), 'w', newline='') as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    passed = sum(1 for r in rows if r['final_status'] == 'PASS')
    blockers = [r for r in rows if r['final_status'] != 'PASS']
    md = ['# TPC-H SF1 correctness: %d/22 PASS' % passed, '',
          '| | |', '|---|---|',
          '| Commit | `%s` |' % last['commit'],
          '| Run | %s, %d round(s) |' % (rounds[0]['started'], len(rounds)),
          '| Engine | RapidsSE `bin/rapidsse`, %s |' % last['node'],
          '| Data | TPC-H scale factor 1, %s, checksums pinned in `reference/dataset.json` |' % info['generator'],
          '| Reference | %s |' % src['engine'],
          '| Numeric tolerance | %s on decimal columns; everything else exact |' % src['numeric_tolerance'],
          '| Client overhead | about %s ms of every time below is the JDBC client starting |'
          % last.get('client_overhead_ms', '?'), '',
          '| Query | Parser | Binder | Logical plan | Optimizer | Physical plan | Runtime | Result | Time ms | '
          'Failure class | Final status | Fixed by |', '|---|---|---|---|---|---|---|---|---:|---|---|---|']
    for r in rows:
        md.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | **%s** | %s |' % (
            r['query'], r['parser'], r['binder'], r['planner'], r['optimizer'], r['physical_plan'], r['runtime'],
            r['result'], r['time_ms'], r['failure_class'], r['final_status'], r['fix_commit']))
    md += ['', '## Remaining blockers', '']
    md += ['- **%s** %s: %s' % (r['query'], r['failure_class'], r['failure_reason']) for r in blockers] or ['None.']
    warn = last.get('warnings') or []
    if warn:
        md += ['', '## Known defects seen in this run', ''] + ['- ' + w for w in warn]
    if len(rounds) > 1:
        md += ['', '## Rounds', '', '| Round | PASS | Total time ms |', '|---|---|---:|']
        for i, r in enumerate(rounds, 1):
            md.append('| %d | %d/22 | %d |' % (i, sum(1 for d in r['queries'].values() if d['status'] == 'PASS'),
                                              sum(d.get('wall_ms') or 0 for d in r['queries'].values())))
    md += ['', 'Stage columns: Parser, Binder and Logical plan come from `EXPLAIN` (SQL compiler); Optimizer and '
           'Physical plan from the optimised pipeline of the run-time optimiser; Runtime from the execution; '
           'Result from the comparison with the reference. Plans, logs and rows of every query are next to '
           'this file.', '']
    c.write(os.path.join(out_dir, 'matrix.md'), '\n'.join(md))
    c.write(os.path.join(out_dir, 'summary.json'), json.dumps(
        {'commit': last['commit'], 'passed': passed, 'total': 22, 'rounds': len(rounds),
         'reference': src['engine'], 'blockers': [r['query'] for r in blockers],
         'status': {r['query']: r['final_status'] for r in rows},
         'time_ms': {r['query']: r['time_ms'] for r in rows}}, indent=2) + '\n')
    return passed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--rounds', type=int, default=1)
    ap.add_argument('--reference', choices=['duckdb', 'official'])
    ap.add_argument('--timeout', type=int, default=1200)
    a = ap.parse_args()

    print('1. data set')
    ok, info = prepare_sf1.check(a.data_dir)
    if not ok:
        sys.exit('the data set is not a complete SF1 data set: nothing was run')
    print('2. reference results')
    src = run_reference.run(a.reference, a.data_dir)
    print('   ' + src['engine'])
    out_dir = os.path.join(c.REPORTS, '%s_%s' % (time.strftime('%Y%m%d-%H%M%S'), c.git_commit()[:10]))  # the directory name: the commit only
    rounds = []
    for i in range(1, a.rounds + 1):
        print('3. RapidsSE, round %d of %d' % (i, a.rounds))
        rd = os.path.join(out_dir, 'round%d' % i)
        run_rapidsse.run(rd, a.data_dir, None, a.timeout)
        rounds.append(compare_results.compare_run(rd))
        # the status after every round, so that an interrupted run still leaves its evidence
        passed = matrix(rounds, info, src, out_dir)
        print('   round %d: %d/22 PASS' % (i, passed))
    c.write(os.path.join(out_dir, 'dataset.json'), json.dumps(info, indent=2) + '\n')
    shutil.copy(os.path.join(out_dir, 'matrix.md'), os.path.join(c.REPORTS, 'LATEST.md'))
    shutil.copy(os.path.join(out_dir, 'summary.json'), os.path.join(c.REPORTS, 'LATEST.json'))
    print('report: ' + os.path.relpath(out_dir, c.REPO))
    sys.exit(0 if passed == 22 else 1)


if __name__ == '__main__':
    main()
