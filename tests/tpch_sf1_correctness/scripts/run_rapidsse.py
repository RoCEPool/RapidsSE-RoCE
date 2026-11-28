#!/usr/bin/env python3
"""Steps 3, 4 and 6 of the test: run the 22 queries on RapidsSE and keep the evidence.

  run_rapidsse.py --out DIR [--data-dir DIR] [--queries 01,05] [--timeout SECONDS]

Every query goes through the SQL compiler and the execution engine of this repository's build
(bin/rapidsse), over JDBC, with the text in queries/. Per query this writes

  results/qNN.csv        the rows returned (only when the query returned without an error)
  plans/qNN.explain.txt  the plan of the SQL compiler (EXPLAIN)
  plans/qNN.pipeline.txt the pipeline of the run-time optimiser, as it was executed
  logs/qNN.log           everything the client printed

and run.json with the stage results, the failure class and the times.
"""
import argparse, csv, glob, io, json, os, re, subprocess, sys, time
import common as c

ENV = dict(os.environ, SE_ENV_METACLUE='1', SE_ENV_INTKEY='1', SE_USERNAME='', SE_PASSWORD='')
PORT = os.environ.get('TPCH_SE_PORT', '9123')
PARTITIONS = int(os.environ.get('TPCH_SE_PARTITIONS', '2'))   # partitions of the one node
THREADS = int(os.environ.get('TPCH_SE_THREADS', '2'))
ENGINE_LOG = os.path.join(c.REPO, 'se.log')
# the JVM options of shell/seshell.sh (a short-lived client: serial collector,
# first JIT compiler only)
JVM_OPTIONS = os.environ.get('TPCH_SE_JVM_OPTIONS', '-XX:+UseSerialGC -XX:TieredStopAtLevel=1').split()


def shell(sql, timeout):
    t0 = time.time()
    p = subprocess.run(['timeout', str(timeout), 'java'] + JVM_OPTIONS + ['-cp', os.path.join(c.REPO, 'shell', '*'),
                        'com.rapidsdata.shell.RapidsShell', '--continueBatchOnError', '-s',
                        '-h', '127.0.0.1', '-p', PORT], input=sql, capture_output=True, text=True, env=ENV)
    out = '\n'.join(l for l in p.stdout.splitlines() if not l.startswith('Picked up'))
    return p.returncode, out, time.time() - t0


def alive():
    return subprocess.run(['pgrep', '-x', 'rapidsse'], capture_output=True).returncode == 0


def stop():
    subprocess.run(['pkill', '-9', '-x', 'rapidsse'])
    time.sleep(1)
    for f in glob.glob('/dev/shm/SE_*'):
        os.remove(f)


def start(data_dir, node_log):
    """Starts one node with TPCH_SE_PARTITIONS partitions (default two), loads the schema and the data. Returns the loaded row
    counts per table."""
    stop()
    log = open(node_log, 'a')
    subprocess.Popen(['setarch', 'x86_64', '-R', './rapidsse', '--partitionsPerNode', str(PARTITIONS), '--threads', str(THREADS),
                      '--size', os.environ.get('TPCH_SE_SIZE', '6GB'), '--node', 'n1', '127.0.0.1', '9101', PORT,
                      '--name', 'n1', '--start', '--munged', '--script', '--metadata', '--commands', '../commands',
                      '--cid', '1', '--heartbeat', '239.255.10.10', '9201'],
                     cwd=os.path.join(c.REPO, 'bin'), env=ENV, stdout=log, stderr=log)
    for _ in range(90):
        time.sleep(1)
        if '"COL0"' in shell('select count(*) from nodes;', 20)[1]:
            break
    schema = open(os.path.join(c.SUITE, 'schema.sql')).read().replace('${DATA}', data_dir)
    shell(schema, 5400)
    counts = {}
    for t in c.SF1_ROWS:
        out = shell('select count(*) from %s;' % t, 120)[1].split()
        counts[t] = int(out[-1]) if out and out[-1].isdigit() else None
    return counts


def parse_rows(out):
    lines = [l for l in out.splitlines() if l.strip() and l.strip() != 'Retrying command...']
    rows = list(csv.reader(io.StringIO('\n'.join(lines))))
    return [[None if v == 'null' else v for v in r] for r in rows[1:]]   # first line: column names


def error_of(out):
    for l in out.splitlines():
        if l.startswith('Error') or 'could not connect' in l.lower() or 'Socket' in l:
            return l.strip()[:300]
    return None


def run_query(q, sql, out_dir, timeout, data_dir, node_log):
    r = {'parser': 'NOT_RUN', 'binder': 'NOT_RUN', 'planner': 'NOT_RUN', 'optimizer': 'NOT_RUN',
         'physical_plan': 'NOT_RUN', 'runtime': 'NOT_RUN', 'class': 'NOT_RUN', 'error': None,
         'wall_ms': None, 'rows': None}
    log = []
    stmt = sql.strip().rstrip(';')

    # -- SQL compiler: parser, name binding, logical plan
    rc, out, _ = shell('explain ' + stmt + ';', 120)
    log.append('--- EXPLAIN\n' + out)
    err = error_of(out)
    if err:
        if 'EC_SYNTAX_ERROR' in err:
            r.update(parser='FAIL', **{'class': 'PARSE_ERROR'})
        elif re.search(r'does not exist|ambiguous|EC_NO_SUCH|not found', err, re.I):
            r.update(parser='PASS', binder='FAIL', **{'class': 'PLAN_ERROR'})
        else:
            r.update(parser='PASS', binder='PASS', planner='FAIL', **{'class': 'PLAN_ERROR'})
        r['error'] = err
        c.write(os.path.join(out_dir, 'logs', 'q%s.log' % q), '\n'.join(log))
        return r
    r.update(parser='PASS', binder='PASS', planner='PASS')
    c.write(os.path.join(out_dir, 'plans', 'q%s.explain.txt' % q), out + '\n')

    # -- run-time optimiser and plan: a run with the optimiser trace on
    # The engine starts its log file anew from time to time, so the trace is not found by an
    # offset: it is the pipeline that follows the last "<stmt> = ..." line of this statement.
    # When the log was restarted during the run, the traced run is repeated once.
    first_line = stmt.split('\n')[0].strip()
    for attempt in (1, 2):
        rc, out, _ = shell('/optimiser_trace\n' + stmt + ';', timeout)
        log.append('--- RUN WITH OPTIMISER TRACE\n' + out)
        trace = []
        if os.path.exists(ENGINE_LOG):
            with open(ENGINE_LOG, errors='replace') as f:
                # the log stamps the time into the text wherever a write starts, also inside a line
                trace = [re.sub(r'\[\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\] ?', '', l.rstrip('\n')) for l in f]
        try:
            s0 = max(i for i, l in enumerate(trace) if '<stmt> = ' in l and first_line in l)
            a = next(i for i, l in enumerate(trace) if i > s0 and 'Optimised pipeline' in l)
            b = next(i for i, l in enumerate(trace) if i > a and 'End of optimised pipeline' in l)
            if any('<stmt> = ' in l for l in trace[s0 + 1:a]):
                raise StopIteration          # that pipeline belongs to a later statement
            c.write(os.path.join(out_dir, 'plans', 'q%s.pipeline.txt' % q), '\n'.join(trace[a:b + 1]) + '\n')
            r.update(optimizer='PASS', physical_plan='PASS')
            break
        except (StopIteration, ValueError):
            if error_of(out) or not alive() or rc == 124:
                break
    crashed = not alive()
    err = error_of(out)
    if r['optimizer'] != 'PASS' and (err or crashed or rc == 124):
        r.update(optimizer='FAIL', error=err or 'no plan', **{'class': 'CRASH' if crashed else
                                                              'TIMEOUT' if rc == 124 else 'OPTIMIZER_ERROR'})
    if crashed:
        r['class'] = 'CRASH'
        start(data_dir, node_log)

    # -- execution: a plain run, which is the one that is timed and whose rows are compared
    if r['class'] == 'NOT_RUN':
        rc, out, dt = shell(stmt + ';', timeout)
        log.append('--- RUN\n' + out)
        err = error_of(out)
        r['wall_ms'] = int(dt * 1000)
        if not alive():
            r.update(runtime='FAIL', error=err or 'the node went down', **{'class': 'CRASH'})
            start(data_dir, node_log)
        elif rc == 124:
            r.update(runtime='FAIL', error='no result within %d s' % timeout, **{'class': 'TIMEOUT'})
        elif err:
            r.update(runtime='FAIL', error=err, **{'class': 'RUNTIME_ERROR'})
        else:
            rows = parse_rows(out)
            c.save_rows(os.path.join(out_dir, 'results', 'q%s.csv' % q), rows)
            r.update(runtime='PASS', rows=len(rows), **{'class': 'EXECUTED'})
            r['engine_ms'] = engine_time(stmt, timeout)
    c.write(os.path.join(out_dir, 'logs', 'q%s.log' % q), '\n'.join(log) + '\n')
    return r


TIMED_RUNS = int(os.environ.get('TPCH_TIMED_RUNS', '3'))


def engine_time(stmt, timeout, phases_out=None):
    """Best of TIMED_RUNS runs of the time the engine spends on the statement, in ms: from the start
    of the SQL compiler to the end of the pipeline job, read from the /phaseTimes marks in the engine
    log (see phase_times.py). It leaves out the JDBC client (a new JVM per run, about half a second
    whose spread is larger than many queries) and the printing of the rows, so that it compares with
    DuckDB timed in process. None when no run left its marks. A dict passed as phases_out gets
    the best time of each phase of phase_times.phases (sql, optimise, execute, ...)."""
    import phase_times            # imports this module, so it is imported here and not at the top
    best = None
    for _ in range(TIMED_RUNS):
        n0 = sum(1 for _ in open(ENGINE_LOG, errors='replace')) if os.path.exists(ENGINE_LOG) else 0
        rc, out, dt = shell('/phaseTimes\n' + stmt + ';', timeout)
        time.sleep(0.3)               # the engine writes its log lines after the result is sent
        if rc or error_of(out) or not alive():
            break
        marks = phase_times.marks_since(n0)
        p = phase_times.phases(marks, dt * 1000)
        if p:
            span = p['wall'] - p['client']
            best = span if best is None else min(best, span)
            if phases_out is not None:
                for k, v in p.items():
                    phases_out[k] = v if k not in phases_out else min(phases_out[k], v)
    return None if best is None else int(round(best))


def run(out_dir, data_dir=c.DATA, queries=None, timeout=1200):
    os.makedirs(out_dir, exist_ok=True)
    node_log = os.path.join(out_dir, 'node.log')
    res = {'commit': c.git_commit(), 'started': time.strftime('%Y-%m-%d %H:%M:%S %Z'),
           'node': '1 node, %d partitions, %d threads' % (PARTITIONS, THREADS), 'queries': {}}
    if not os.path.exists(os.path.join(c.REPO, 'bin', 'rapidsse')):
        res['blocked'] = 'bin/rapidsse is missing: build the source first'
    else:
        res['loaded_rows'] = start(data_dir, node_log)
        # Known defect: a scan that starts at a replicated table returns every row once per
        # partition, so count(*) of region and nation is the row count times the partitions.
        # The check accepts that and the report shows it under "Known defects".
        bad, res['warnings'] = {}, []
        for t, n in res['loaded_rows'].items():
            if n == c.SF1_ROWS[t]:
                continue
            if t in c.REPLICATED and n == c.SF1_ROWS[t] * PARTITIONS:
                res['warnings'].append('count(*) of the replicated table %s returns %d, not %d' % (t, n, c.SF1_ROWS[t]))
            else:
                bad[t] = n
        if bad:
            res['blocked'] = 'the loaded tables are not SF1: %s' % bad
    if 'blocked' not in res:
        times = sorted(shell('select count(*) from region;', 60)[2] for _ in range(3))
        res['client_overhead_ms'] = int(times[1] * 1000)   # JDBC client start and a trivial query
    for q in (queries or c.QIDS):
        if 'blocked' in res:
            res['queries'][q] = {'class': 'BLOCKED', 'error': res['blocked']}
            continue
        if not alive():
            start(data_dir, node_log)
        sql = open(os.path.join(c.QUERIES, 'q%s.sql' % q)).read()
        res['queries'][q] = run_query(q, sql, out_dir, timeout, data_dir, node_log)
        d = res['queries'][q]
        print('  Q%s %-15s %6s ms  %s' % (q, d['class'], d.get('wall_ms') or '-', d.get('error') or ''), flush=True)
    stop()
    c.write(os.path.join(out_dir, 'run.json'), json.dumps(res, indent=2) + '\n')
    return res


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--queries')
    ap.add_argument('--timeout', type=int, default=1200)
    a = ap.parse_args()
    run(a.out, a.data_dir, a.queries.split(',') if a.queries else None, a.timeout)
