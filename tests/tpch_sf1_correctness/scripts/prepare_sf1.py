#!/usr/bin/env python3
"""Step 2 of the test: make sure the TPC-H SF1 data set is complete.

Generates the eight tables with the DBGEN of this repository when they are missing, then checks
the row counts against the TPC-H specification and the checksums against the pinned ones in
reference/dataset.json. The run must not start on a data set that fails either check.

  prepare_sf1.py [--data-dir DIR] [--pin]

--pin writes the checksums of the present files to reference/dataset.json (first use only).
"""
import argparse, json, os, re, subprocess, sys
import common as c


def dbgen_version():
    try:
        text = open(os.path.join(c.REPO, 'dbgen', 'release.h')).read()
        v = dict(re.findall(r'#define\s+(VERSION|RELEASE|PATCH)\s+(\d+)', text))
        return '%s.%s.%s' % (v['VERSION'], v['RELEASE'], v['PATCH'])
    except Exception:
        return 'unknown'


def generate(data_dir):
    dbgen_dir = os.path.join(c.REPO, 'dbgen')
    if not os.path.exists(os.path.join(dbgen_dir, 'dbgen')):
        subprocess.run(['make', '-C', dbgen_dir, 'dbgen'], check=True)
    os.makedirs(data_dir, exist_ok=True)
    env = dict(os.environ, DSS_PATH=data_dir, DSS_CONFIG=dbgen_dir)
    # -s 1: scale factor 1. DBGEN is deterministic: it has fixed seeds and takes no seed argument.
    subprocess.run([os.path.join(dbgen_dir, 'dbgen'), '-s', '1', '-f'], cwd=dbgen_dir, env=env, check=True)


def describe(data_dir):
    tables = {}
    for t, want in c.SF1_ROWS.items():
        path = os.path.join(data_dir, t + '.tbl')
        if not os.path.exists(path):
            tables[t] = {'error': 'missing'}
            continue
        rows = int(subprocess.run(['wc', '-l', path], capture_output=True, text=True).stdout.split()[0])
        tables[t] = {'rows': rows, 'expected_rows': want, 'bytes': os.path.getsize(path),
                     'sha256': c.sha256(path)}
    return {'scale_factor': 1, 'generator': 'dbgen ' + dbgen_version() + ' (dbgen/ of this repository)',
            'seed': 'DBGEN built-in seeds (deterministic; no seed argument)', 'tables': tables}


def check(data_dir, pin=False, quiet=False):
    """Returns (ok, info). info is what goes into the report."""
    if any(not os.path.exists(os.path.join(data_dir, t + '.tbl')) for t in c.SF1_ROWS):
        generate(data_dir)
    info = describe(data_dir)
    problems = []
    for t, d in info['tables'].items():
        if 'error' in d:
            problems.append('%s: %s' % (t, d['error']))
        elif d['rows'] != d['expected_rows']:
            problems.append('%s: %d rows, expected %d' % (t, d['rows'], d['expected_rows']))
    pinned_path = os.path.join(c.SUITE, 'reference', 'dataset.json')
    if pin and not problems:
        c.write(pinned_path, json.dumps(info, indent=2) + '\n')
    if os.path.exists(pinned_path):
        pinned = json.load(open(pinned_path))['tables']
        for t, d in info['tables'].items():
            if 'sha256' in d and d['sha256'] != pinned[t]['sha256']:
                problems.append('%s: checksum differs from reference/dataset.json' % t)
        info['pinned'] = 'checksums match reference/dataset.json' if not problems else 'MISMATCH'
    else:
        problems.append('reference/dataset.json is missing: run prepare_sf1.py --pin once')
    info['problems'] = problems
    if not quiet:
        for t, d in info['tables'].items():
            print('  %-9s %9s rows  %s' % (t, d.get('rows', '-'), d.get('sha256', d.get('error'))[:16]))
        print('  ' + ('data set complete' if not problems else 'DATA SET REJECTED: ' + '; '.join(problems)))
    return not problems, info


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--data-dir', default=c.DATA)
    ap.add_argument('--pin', action='store_true')
    a = ap.parse_args()
    ok, _ = check(a.data_dir, a.pin)
    sys.exit(0 if ok else 1)
