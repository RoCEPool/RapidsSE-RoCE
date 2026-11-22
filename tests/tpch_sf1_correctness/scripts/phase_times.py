#!/usr/bin/env python3
"""Where the time of a query goes: SQL compile, run-time optimiser, C compile, execution.

  phase_times.py [--queries 02,20] [--repeat 2] [--out FILE] [--no-start]

Runs each query with the job tag /phaseTimes, which makes the engine write one trace line per
point of the job (src/store/stepsSet/Steps_Optimiser.cpp, Step::vPhaseMark), and reads the
times from the engine log. Columns, in ms:

  client      wall time seen by the JDBC client minus the phases below (client start,
              connection, sending the statement, receiving and printing the rows)
  sql         SQL compiler (parser to transliterate)
  optimise    run-time optimiser (dissect, flatten, optimise, epilog)
  weave       writing the optimised statement and the C code
  ccompile    gcc on the generated C code (largest of the participants)
  execute     from "executing" to "executed" of the pipeline job
  other       the rest between the start of the SQL compiler and the end of the job
              (job set-up between the phases, begin and end of the participants)

With --repeat N the query runs N times; the table shows the last run (warm caches).
"""
import argparse, os, re, sys, time
import common as c
import run_rapidsse as se

POINTS = ['sqlcompile_start', 'sqlcompile_end', 'optimise_start', 'optimise_end', 'weave_end',
          'ccompile_start', 'ccompile_end', 'begun', 'executing', 'executed', 'ended']


def marks_since(n0):
    """[(job, point, ms)] of the phase lines written after line n0 of the engine log."""
    out = []
    with open(se.ENGINE_LOG, errors='replace') as f:
        for i, l in enumerate(f):
            if i < n0:
                continue
            for m in re.finditer(r'\[phase\] \S+ (J\d+\.\d+) (\w+) ([\d.]+)', l):
                out.append((m.group(1), m.group(2), float(m.group(3))))
    return out


def phases(marks, wall_ms):
    """The pipeline job is the one with optimise_start; the C compile runs in jobs of its own."""
    by = {}
    for job, p, t in marks:
        by.setdefault(job, {}).setdefault(p, []).append(t)
    pipe = next((j for j, d in by.items() if 'optimise_start' in d), None)
    sqlj = next((j for j, d in by.items() if 'sqlcompile_start' in d), None)
    if not pipe or not sqlj:
        return None
    P, S = by[pipe], by[sqlj]
    cc = [(min(d['ccompile_start']), max(d['ccompile_end'])) for d in by.values()
          if 'ccompile_start' in d and 'ccompile_end' in d]
    r = {'sql': S['sqlcompile_end'][-1] - S['sqlcompile_start'][0],
         'optimise': P['optimise_end'][0] - P['optimise_start'][0],
         'weave': P['weave_end'][0] - P['optimise_end'][0],
         'ccompile': max((b - a for a, b in cc), default=0.0),
         'execute': P['executed'][-1] - P['executing'][-1]}
    span = P['ended'][-1] - S['sqlcompile_start'][0]
    r['other'] = span - sum(r.values())
    r['client'] = wall_ms - span
    r['wall'] = wall_ms
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--queries')
    ap.add_argument('--repeat', type=int, default=2)
    ap.add_argument('--out', default=os.path.join(c.REPORTS, 'PHASE_TIMES.md'))
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--no-start', action='store_true', help='use the node that is running')
    a = ap.parse_args()
    queries = a.queries.split(',') if a.queries else c.QIDS
    if not a.no_start:
        se.start(a.data_dir, os.path.join(c.REPORTS, 'phase_times_node.log'))
    cols = ['wall', 'client', 'sql', 'optimise', 'weave', 'ccompile', 'execute', 'other']
    rows = {}
    for q in queries:
        stmt = open(os.path.join(c.QUERIES, 'q%s.sql' % q)).read().strip().rstrip(';')
        r = None
        for _ in range(a.repeat):
            n0 = sum(1 for _ in open(se.ENGINE_LOG, errors='replace'))
            rc, out, dt = se.shell('/phaseTimes\n' + stmt + ';', 1200)
            time.sleep(0.5)
            r = phases(marks_since(n0), dt * 1000) if not se.error_of(out) else None
        rows[q] = r
        print('  Q%s ' % q + ('  '.join('%s %6.0f' % (k, r[k]) for k in cols) if r else 'no marks'), flush=True)
    if not a.no_start:
        se.stop()
    md = ['# Where the time of each query goes', '',
          'Commit `%s`, %s, 1 node, %d partitions, %d threads, TPC-H SF1. '
          'Last of %d runs of each query, in ms.' % (c.git_commit(), time.strftime('%Y-%m-%d %H:%M %Z'),
                                                    se.PARTITIONS, se.THREADS, a.repeat), '',
          '| Query | Wall | Client | SQL compile | Optimise | Weave | C compile | Execute | Other |',
          '|---|' + '---:|' * 8]
    tot = dict.fromkeys(cols, 0.0)
    for q, r in rows.items():
        if not r:
            md.append('| Q%s | no marks |' % q)
            continue
        for k in cols:
            tot[k] += r[k]
        md.append('| Q%s | ' % q + ' | '.join('%.0f' % r[k] for k in cols) + ' |')
    md.append('| **Total** | ' + ' | '.join('**%.0f**' % tot[k] for k in cols) + ' |')
    if tot['wall']:
        md.append('| **Share** | | ' + ' | '.join('%.0f%%' % (100 * tot[k] / tot['wall']) for k in cols[1:]) + ' |')
    md += ['', 'Columns: see the docstring of `scripts/phase_times.py`.', '']
    c.write(a.out, '\n'.join(md))
    print('\n'.join(md))


if __name__ == '__main__':
    main()
