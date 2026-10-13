"""Names the samples of bfgscene --sample: fatgl's (opengl32.dll's) functions via
nm, JIT compiled shader code (addresses outside every module), other modules
(ntdll / kernelbase: threads waiting) by name.

    python tools/bfgscene/profile.py <samples.txt> [--dll build-dev/opengl32.dll] [--top 40] [--waits]
        [--fn <function>]   (the function's hot instructions, objdump disassembly)
        [--main]            (only the main thread: the GL calls, recording; not the workers)

Percentages are of the busy samples (without --waits: the samples in fatgl or
the JIT, the work; idle pool threads sit in ntdll).
"""
import argparse
import bisect
import collections
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def symbols(dll):
    base = 0
    for line in subprocess.run(['objdump', '-p', dll], capture_output=True, text=True).stdout.splitlines():
        if line.startswith('ImageBase'):
            base = int(line.split()[1], 16)
    syms = []
    for line in subprocess.run(['nm', '-n', dll], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'tT':
            syms.append((int(p[0], 16) - base, p[2]))
    syms.sort()
    return [a for a, _ in syms], [n for _, n in syms]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('samples')
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build-dev', 'opengl32.dll'))
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--waits', action='store_true', help='also count samples outside fatgl / the JIT')
    ap.add_argument('--fn', help='show the hot instructions of this function')
    ap.add_argument('--main', action='store_true', help='only the samples of the main thread')
    a = ap.parse_args()
    addrs, names = symbols(a.dll)
    dll = os.path.basename(a.dll).lower()
    count = collections.Counter()
    hot = collections.Counter()
    total = 0
    for line in open(a.samples):
        line = line.rstrip('\n')
        on_main = line.endswith(' M')
        if on_main:
            line = line[:-2]
        elif a.main:
            continue
        mod, off = line.rsplit(' ', 1)
        off = int(off, 16)
        m = os.path.basename(mod).lower()
        if m == dll:
            i = bisect.bisect_right(addrs, off) - 1
            name = names[i] if i >= 0 else '?'
            if name.endswith('.cold'):
                name = name[:-5]
            if a.fn and names[i] == a.fn:
                hot[off] += 1
        elif mod == '-':
            name = '[jit code]'
        elif a.waits:
            name = '[%s]' % m
        else:
            continue
        count[name] += 1
        total += 1
    print(f'{total} samples' + ('' if a.waits else ' in fatgl and the JIT'))
    acc = 0
    for name, n in count.most_common(a.top):
        acc += n
        print(f'{100.0 * n / total:6.2f}% {100.0 * acc / total:6.1f}%  {name}')
    if a.fn:
        show_function(a.dll, a.fn, addrs, names, hot)
    return 0


def show_function(dll, fn, addrs, names, hot):
    """the disassembly with sample counts (a sample lands on the instruction after a slow one)"""
    base = 0
    for line in subprocess.run(['objdump', '-p', dll], capture_output=True, text=True).stdout.splitlines():
        if line.startswith('ImageBase'):
            base = int(line.split()[1], 16)
    i = names.index(fn)
    lo, hi = addrs[i], addrs[i + 1] if i + 1 < len(addrs) else addrs[i] + 65536
    d = subprocess.run(['objdump', '-d', '--no-show-raw-insn', '-M', 'intel', '--start-address=0x%x' % (lo + base),
                        '--stop-address=0x%x' % (hi + base), dll], capture_output=True, text=True).stdout
    n = sum(hot.values()) or 1
    print()
    print(f'{fn}: {n} samples')
    for line in d.splitlines():
        p = line.split(':', 1)
        try:
            off = int(p[0].strip(), 16) - base
        except ValueError:
            continue
        c = hot.get(off, 0)
        print(f'{(100.0 * c / n):5.1f}% {p[1].strip()}' if c else f'       {p[1].strip()}')


if __name__ == '__main__':
    sys.exit(main())
