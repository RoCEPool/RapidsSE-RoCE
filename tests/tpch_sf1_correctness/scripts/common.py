"""Shared code of the TPC-H SF1 correctness test. See ../README.md."""
import csv, glob, hashlib, io, json, os, re, subprocess, time
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP

HERE = os.path.dirname(os.path.abspath(__file__))
SUITE = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(SUITE))
QUERIES = os.path.join(SUITE, 'queries')
EXPECTED = os.path.join(SUITE, 'reference', 'expected_results')
REPORTS = os.path.join(SUITE, 'reports')
DATA = os.environ.get('TPCH_SF1_DATA', os.path.join(SUITE, 'data'))
QIDS = ['%02d' % i for i in range(1, 23)]

# Row counts of TPC-H at scale factor 1 (TPC-H specification, clause 4.2.5).
SF1_ROWS = {'region': 5, 'nation': 25, 'supplier': 10000, 'customer': 150000, 'part': 200000,
            'partsupp': 800000, 'orders': 1500000, 'lineitem': 6001215}

REPLICATED = ('region', 'nation')
STATUSES = ['PASS', 'PARSE_ERROR', 'PLAN_ERROR', 'OPTIMIZER_ERROR', 'RUNTIME_ERROR', 'WRONG_RESULT',
            'TIMEOUT', 'CRASH', 'BLOCKED', 'NOT_RUN']


def git_commit():
    r = subprocess.run(['git', '-C', REPO, 'rev-parse', 'HEAD'], capture_output=True, text=True)
    d = subprocess.run(['git', '-C', REPO, 'status', '--porcelain', '--untracked-files=no', 'src'],
                       capture_output=True, text=True)
    return r.stdout.strip() + (' (with uncommitted changes in src)' if d.stdout.strip() else '')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(text)


# --------------------------------------------------------------------------------------------------
# result files: CSV, one row per line, no header; NULL is written as the bare word NULL
# --------------------------------------------------------------------------------------------------
def save_rows(path, rows):
    out = io.StringIO()
    w = csv.writer(out, lineterminator='\n')
    for r in rows:
        w.writerow(['NULL' if v is None else v for v in r])
    write(path, out.getvalue())


def load_rows(path):
    with open(path, newline='') as f:
        return [[None if v == 'NULL' else v for v in r] for r in csv.reader(f)]


# --------------------------------------------------------------------------------------------------
# comparison (the rules are described in README.md and are fixed before any run)
# --------------------------------------------------------------------------------------------------
def norm(v):
    """A value as (kind, value): ('null',), ('num', Decimal) or ('str', text)."""
    if v is None:
        return ('null', None)
    s = str(v).strip()
    if s.lower() == 'null':
        return ('null', None)
    m = re.match(r"^(DATE|TIMESTAMP) '(.*)'$", s)
    if m:
        s = m.group(2)
    if re.match(r'^\d{4}-\d{2}-\d{2}( 00:00:00(\.0+)?)?$', s):
        return ('str', s[:10])
    try:
        d = Decimal(s)
        if d.is_finite():
            return ('num', d)
    except InvalidOperation:
        pass
    return ('str', s.rstrip())


def same_value(got, want, numeric_tolerance):
    g, w = norm(got), norm(want)
    if g[0] != w[0]:
        return False
    if g[0] != 'num':
        return g[1] == w[1]
    if g[1] == w[1]:
        return True
    if numeric_tolerance is None:
        return False
    # The reference has a fixed number of decimals: round the engine value to it, half away
    # from zero, and allow the last digit to differ by the tolerance.
    scale = max(0, -w[1].as_tuple().exponent)
    q = Decimal(1).scaleb(-scale)
    return abs(g[1].quantize(q, rounding=ROUND_HALF_UP) - w[1]) <= numeric_tolerance


def same_row(got, want, tol):
    return len(got) == len(want) and all(same_value(g, w, tol) for g, w in zip(got, want))


def sort_key_cmp(a, b, order_by):
    for col, direction in order_by:
        x, y = norm(a[col]), norm(b[col])
        if x == y:
            continue
        lt = (x[1] < y[1]) if x[0] == y[0] and x[0] != 'null' else (x[0] < y[0])
        c = -1 if lt else 1
        return c if direction == 'asc' else -c
    return 0


def compare(got, want, order_by, tol):
    """Returns (ok, message). got and want are lists of rows."""
    if len(got) != len(want):
        return False, 'row count %d, expected %d' % (len(got), len(want))
    if got and want and len(got[0]) != len(want[0]):
        return False, 'column count %d, expected %d' % (len(got[0]), len(want[0]))

    # every row of the result must be in the reference, as often as it is there (multiset)
    left = list(range(len(want)))
    by_first = {}
    for i in left:
        by_first.setdefault(norm(want[i][0]) if want[i] else None, []).append(i)
    used = set()
    for n, g in enumerate(got):
        k = None
        cand = by_first.get(norm(g[0]) if g else None)
        pool = cand if cand is not None else left
        for i in pool:
            if i not in used and same_row(g, want[i], tol):
                k = i
                break
        if k is None:   # numeric first column within tolerance: search everything
            for i in left:
                if i not in used and same_row(g, want[i], tol):
                    k = i
                    break
        if k is None:
            return False, 'row %d is not in the reference: %s' % (n + 1, g[:6])
        used.add(k)

    if not order_by:
        return True, 'multiset equal (no ORDER BY)'

    # the result must be in the order of its ORDER BY ...
    for n in range(1, len(got)):
        if sort_key_cmp(got[n - 1], got[n], order_by) > 0:
            return False, 'rows %d and %d are not in ORDER BY order' % (n, n + 1)
    # ... and may differ from the reference only where the sort keys are equal
    ties = 0
    for n, (g, w) in enumerate(zip(got, want)):
        if not same_row(g, w, tol):
            if any(not same_value(g[c], w[c], tol) for c, _ in order_by):
                return False, 'row %d differs from the reference in a sort key: %s' % (n + 1, g[:6])
            ties += 1
    return True, 'equal' if not ties else 'equal; %d rows with equal sort keys are in another order' % ties
