#!/usr/bin/env python3
"""Sample the thread stacks of RapidsSE while TPC-H SF1 queries run, and sum them up.

  sample_stacks.py --queries 18,09,07 [--samples 60] [--out DIR] [--data-dir DIR]

For each query a thread runs it again and again while the main thread takes --samples snapshots
of all thread stacks of the node with gdb (sudo, where ptrace of a non-child is restricted).
Frames of generated query code (no symbols in the engine) are resolved with nm on the .so the
address falls in, read from /proc/<pid>/maps. Threads that wait (sleep, poll, condition wait)
are left out. For each query it writes the raw stacks (stacks_<q>.txt) and a summary:

- the share of active stacks by kind of work (the first matching rule in KINDS, innermost frame
  outwards), so that the same rules compare queries and builds;
- the innermost frames (self) and the frames anywhere in the stack (inclusive) most seen.

The summary of all queries is SAMPLES.md in --out, written for the CI job summary.
"""
import argparse, collections, os, re, shutil, subprocess, sys, threading, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common as c
import run_rapidsse as se

WAITING = ('nanosleep', 'epoll_wait', 'pthread_cond_wait', 'pthread_cond_timedwait', '__futex_abstimed_wait',
           'accept', 'recv', 'select', 'poll', 'vWaitStop', 'vTaskWait', 'sigtimedwait', 'pause')

# kind of work: the first rule whose pattern matches a frame of the stack (innermost first)
KINDS = [
    ('scheduler handoff', r'vScheduleTask|vThread_WaitAssignment|vTriggerBidingOperand|vGrabThreadIdleQueue|bThread_AssignIdle'),
    ('partition send', r'vParcelSendOut|vDistribute|Parcel'),
    ('index lookup', r'IndexTreePosition|EntBuffer|pMoveToGreaterEqual|aFindEntryBefore|IndexFind'),
    ('hash join / group by', r'HashJoin|GbyHash'),
    ('sort', r'std::sort|__introsort|__insertion_sort|PLBegin'),
    ('column cache', r'ColCache'),
    ('memory', r'malloc|_int_free|cfree|Heap::|Heaps::|pAllot'),
    ('SQL compile', r'vSqlCompiler'),
    ('optimiser', r'Optimis|vOptimise|XF_'),
    ('generated code', r'^gen:'),
]


def gdb_cmd(pid):
    cmd = ['gdb', '-p', str(pid), '-batch', '-ex', 'set pagination off', '-ex', 'thread apply all bt 40']
    if os.geteuid() != 0 and shutil.which('sudo'):
        cmd = ['sudo', '-n'] + cmd
    return cmd


def generated_code_maps(pid, copy_dir):
    """(low, high, file offset, copy) of the mapped .so files of generated query code. The file a
    mapping names is replaced on disk once loaded, so the library is copied from
    /proc/<pid>/map_files (root, or sudo) when its mapping is first seen."""
    out = []
    try:
        lines = open('/proc/%d/maps' % pid).read().splitlines()
    except OSError:
        return out
    for line in lines:
        m = re.match(r'(\w+)-(\w+) \S+ (\w+) \S+ \S+\s+(\S+\.so)', line.strip())
        if not m or 'queryLibs' not in m.group(4):
            continue
        lo, hi = m.group(1), m.group(2)
        copy = os.path.join(copy_dir, 'gen_%s_%s' % (lo, os.path.basename(m.group(4))))
        if not os.path.exists(copy):
            cmd = ['cat', '/proc/%d/map_files/%s-%s' % (pid, lo, hi)]
            if os.geteuid() != 0 and shutil.which('sudo'):
                cmd = ['sudo', '-n'] + cmd
            r = subprocess.run(cmd, capture_output=True)
            if r.returncode != 0 or not r.stdout:
                continue
            open(copy, 'wb').write(r.stdout)
        out.append((int(lo, 16), int(hi, 16), int(m.group(3), 16), copy))
    return out


class Resolver:
    """Names the functions of generated code from its .so files (nm), cached per file."""

    def __init__(self):
        self.maps = []
        self.syms = {}

    def add_maps(self, maps):
        for x in maps:
            if x not in self.maps:
                self.maps.append(x)

    def name(self, addr):
        for lo, hi, off, so in self.maps:
            if lo <= addr < hi:
                if so not in self.syms:
                    try:
                        out = subprocess.run(['nm', '-n', so], capture_output=True, text=True).stdout
                    except OSError:
                        out = ''
                    self.syms[so] = [(int(p[0], 16), p[2]) for p in (l.split() for l in out.splitlines())
                                     if len(p) == 3 and p[1] in 'tT']
                rel = addr - lo + off
                best = None
                for a, n in self.syms[so]:
                    if a <= rel:
                        best = n
                    else:
                        break
                return 'gen:' + (best or os.path.basename(so))
        return None


def frames(thread_text, resolver):
    out = []
    for m in re.finditer(r'^#\d+\s+(?:(0x[0-9a-f]+) in )?(.+?)(?: \(| from |$)', thread_text, re.M):
        addr, fn = m.group(1), m.group(2).strip()
        if fn == '??' and addr:
            fn = resolver.name(int(addr, 16)) or '??'
        out.append(re.sub(r'\(.*', '', fn))
    return out


def summarise(text, resolver, top):
    kinds, selfc, inc = collections.Counter(), collections.Counter(), collections.Counter()
    n = 0
    for thr in re.split(r'\n(?=#0 )', text):
        fr = frames(thr, resolver)
        if not fr or any(w in f for f in fr[:6] for w in WAITING):
            continue
        n += 1
        selfc[fr[0]] += 1
        for f in set(fr):
            inc[f] += 1
        for name, pat in KINDS:
            if any(re.search(pat, f) for f in fr):
                kinds[name] += 1
                break
        else:
            kinds['other'] += 1
    return n, kinds, selfc.most_common(top), [x for x in inc.most_common(top * 3)
                                              if not re.search(r'start_thread|clone|aEntryPoint|vTaskLoop|vLoop_general', x[0])][:top]


def sample_query(q, samples, out_dir, resolver):
    stmt = open(os.path.join(c.QUERIES, 'q%s.sql' % q)).read().strip().rstrip(';')
    se.shell(stmt + ';', 1200)                      # compiles and caches the code first
    stop = threading.Event()

    def loop():
        while not stop.is_set():
            se.shell(stmt + ';', 1200)

    t = threading.Thread(target=loop, daemon=True)
    t.start()
    time.sleep(1.0)
    pid = int(subprocess.run(['pgrep', '-x', 'rapidsse'], capture_output=True, text=True).stdout.split()[0])
    chunks = []
    for _ in range(samples):
        resolver.add_maps(generated_code_maps(pid, out_dir))
        r = subprocess.run(gdb_cmd(pid), capture_output=True, text=True)
        chunks.append('\n'.join(l for l in r.stdout.splitlines() if l.startswith('#')))
        if not se.alive():
            break
    stop.set()
    t.join(timeout=1200)
    text = '\n=====\n'.join(chunks)
    open(os.path.join(out_dir, 'stacks_%s.txt' % q), 'w').write(text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--queries', required=True)
    ap.add_argument('--samples', type=int, default=60)
    ap.add_argument('--top', type=int, default=12)
    ap.add_argument('--out', default=os.path.join(c.REPORTS, 'samples'))
    ap.add_argument('--data-dir', default=c.DATA)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    se.start(a.data_dir, os.path.join(a.out, 'sample_node.log'))
    resolver = Resolver()
    md = ['## Stack samples', '',
          '%d snapshots of all thread stacks per query while it runs again and again; %d partitions, '
          '%d threads, %d CPUs. Waiting threads left out. Kind: the first rule of KINDS '
          '(scripts/sample_stacks.py) that matches a frame.' % (a.samples, se.PARTITIONS, se.THREADS, os.cpu_count()), '']
    rows = []
    for q in a.queries.split(','):
        text = sample_query(q, a.samples, a.out, resolver)
        n, kinds, selfc, inc = summarise(text, resolver, a.top)
        rows.append((q, n, kinds))
        md += ['### Q%s (%d active stacks)' % (q, n), '',
               ', '.join('%s %d%%' % (k, 100 * v // max(n, 1)) for k, v in kinds.most_common()), '',
               '| self | count | | inclusive | count |', '|---|---:|---|---|---:|']
        for i in range(max(len(selfc), len(inc))):
            s = selfc[i] if i < len(selfc) else ('', '')
            f = inc[i] if i < len(inc) else ('', '')
            md.append('| `%s` | %s | | `%s` | %s |' % (s[0][:70], s[1], f[0][:70], f[1]))
        md.append('')
        print('Q%s: %d active stacks; %s' % (q, n, ', '.join('%s %d%%' % (k, 100 * v // max(n, 1))
                                                              for k, v in kinds.most_common())), flush=True)
    se.stop()
    names = [k for k, _ in KINDS] + ['other']
    md[4:4] = ['| Query | ' + ' | '.join(names) + ' |', '|---|' + '---:|' * len(names)] + \
              ['| Q%s | ' % q + ' | '.join('%d%%' % (100 * kinds.get(k, 0) // max(n, 1)) for k in names) + ' |'
               for q, n, kinds in rows] + ['']
    open(os.path.join(a.out, 'SAMPLES.md'), 'w').write('\n'.join(md) + '\n')


if __name__ == '__main__':
    main()
