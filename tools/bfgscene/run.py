"""Bakes the Doom 3 BFG frames of scenes.txt from the game's assets (once) and
replays them with bfgscene.exe in every fatgl mode: frame times (best /
median), image hashes, and checks that the modes which must agree do.

    python tools/bfgscene/run.py [--game <DOOM 3 BFG Edition dir>] [--build build-dev]
        [--scenes <dir>] [--bake] [--frames 10] [--modes jit,avx2,...] [--sections] [--update]

The scenes hold id's textures: they live in <build>/bfgscenes (git ignored),
with golden.txt there (--update rewrites it after an intended change).
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
STEAM = [r'C:\Program Files (x86)\Steam\steamapps\common\DOOM 3 BFG Edition',
         r'D:\SteamLibrary\steamapps\common\DOOM 3 BFG Edition', r'E:\SteamLibrary\steamapps\common\DOOM 3 BFG Edition']

MODES = [
    ('jit', {}),
    ('interpreter', {'FATGL_SHADERS': 'interp'}),
    ('avx2', {'FM_SIMD': 'avx2'}),
    ('sse2', {'FM_SIMD': 'sse2'}),
    ('1thread', {'FATGL_THREADS': '1'}),
    ('msaa4', {'FATGL_MSAA': '4'}),
    ('fast_textures', {'FATGL_TEXTURES': 'fast'}),
]
SAME = ['jit', 'interpreter', 'avx2', 'sse2', '1thread']
CLOSE = {'msaa4': 2.0, 'fast_textures': 3.0}


def read_scenes():
    out = []
    for line in open(os.path.join(HERE, 'scenes.txt')):
        line = line.split('#')[0].split()
        if line:
            n, m, x, y, z, yaw, pitch, t = line
            out.append((n, m, (x, y, z), yaw, pitch, t))
    return out


def run_pinned(args, env, pin):
    """single thread runs go on CCD0's cores 4..15 (mask 0xFFF0): stable clocks, the cache die"""
    p = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if pin and os.name == 'nt':
        import ctypes
        ctypes.windll.kernel32.SetProcessAffinityMask(ctypes.c_void_p(int(p._handle)), ctypes.c_size_t(0xFFF0))
    out, err = p.communicate(timeout=1800)
    return subprocess.CompletedProcess(args, p.returncode, out, err)


def find_game(arg):
    if arg:
        return arg
    for p in STEAM:
        if os.path.isdir(os.path.join(p, 'base')):
            return p
    raise SystemExit('DOOM 3 BFG Edition not found: pass --game')


def read_ppm(p):
    d = open(p, 'rb').read()
    parts = d.split(None, 4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][: w * h * 3]


def write_png(p, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)

    open(p, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                        chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build-dev'))
    ap.add_argument('--scenes', help='scene directory (default <build>/bfgscenes)')
    ap.add_argument('--bake', action='store_true', help='bake the scenes again')
    ap.add_argument('--frames', type=int, default=10)
    ap.add_argument('--modes', help='comma separated subset of ' + ','.join(m for m, _ in MODES))
    ap.add_argument('--only', help='comma separated scene names')
    ap.add_argument('--sections', action='store_true', help='also time depth / interactions / shader passes (jit)')
    ap.add_argument('--update', action='store_true', help='rewrite golden.txt from this run')
    a = ap.parse_args()
    sdir = a.scenes or os.path.join(a.build, 'bfgscenes')
    os.makedirs(sdir, exist_ok=True)
    scenes = [s for s in read_scenes() if not a.only or s[0] in a.only.split(',')]
    for n, m, pos, yaw, pitch, t in scenes:
        path = os.path.join(sdir, n + '.bfgs')
        if a.bake or not os.path.exists(path):
            r = subprocess.run([sys.executable, os.path.join(HERE, 'bake.py'), '--game', find_game(a.game), '--map', m,
                                '--pos', *pos, '--yaw', yaw, '--pitch', pitch, '--time', t, '--out', path],
                               capture_output=True, text=True)
            if r.returncode:
                raise SystemExit(r.stdout + r.stderr)
            print(r.stdout.strip().splitlines()[-1] if r.stdout.strip() else n)
    modes = [(k, e) for k, e in MODES if not a.modes or k in a.modes.split(',')]
    exe = os.path.join(a.build, 'bfgscene.exe')
    golden_path = os.path.join(sdir, 'golden.txt')
    golden = {}
    if os.path.exists(golden_path):
        for line in open(golden_path):
            if line.strip() and not line.startswith('#'):
                k, v = line.split()
                golden[k] = v
    res, fails = {}, []
    for n, *_ in scenes:
        imgs = {}
        for mode, env in modes:
            e = dict(os.environ)
            for k in ('FATGL_SHADERS', 'FM_SIMD', 'FATGL_THREADS', 'FATGL_MSAA', 'FATGL_TEXTURES'):
                e.pop(k, None)
            e.update(env)
            e['FATGL_LOG'] = '0'
            ppm = os.path.join(sdir, f'{n}_{mode}.ppm')
            args = [exe, os.path.join(sdir, n + '.bfgs'), str(a.frames), ppm]
            if a.sections and mode == 'jit':
                args.append('--sections')
            r = run_pinned(args, e, mode == '1thread')
            best = med = h = None
            sect = []
            for line in r.stdout.splitlines():
                if line.startswith('bfgscene '):
                    best = float(line.split(':')[1].split('ms')[0])
                    med = float(line.split('best,')[1].split('ms')[0])
                elif line.startswith('hash '):
                    h = line.split()[1]
                elif line.startswith('  '):
                    sect.append(line.strip())
            if r.returncode or h is None:
                fails.append(f'{n} {mode}: failed (exit {r.returncode}) {r.stdout[-300:]}')
                continue
            w, hh, rgb = read_ppm(ppm)
            write_png(ppm[:-4] + '.png', w, hh, rgb)
            os.remove(ppm)
            imgs[mode] = (w, hh, rgb)
            res[(n, mode)] = (best, med, h, sect)
        base = imgs.get('jit')
        for mode in SAME:
            if (n, mode) in res and (n, 'jit') in res and res[(n, mode)][2] != res[(n, 'jit')][2]:
                fails.append(f'{n} {mode}: not the same image as the JIT')
        for mode, lim in CLOSE.items():
            if mode in imgs and base:
                d = [abs(x - y) for x, y in zip(imgs[mode][2], base[2])]
                mean = sum(d) / len(d)
                if mean > lim:
                    fails.append(f'{n} {mode}: too far from the JIT image (mean difference {mean:.2f} > {lim})')
    if a.update:
        with open(golden_path, 'w', newline='\n') as f:
            for (n, mode), v in sorted(res.items()):
                f.write(f'{n}/{mode} {v[2]}\n')
    else:
        for (n, mode), v in res.items():
            g = golden.get(f'{n}/{mode}')
            if g and g != v[2]:
                fails.append(f'{n} {mode}: the image changed (hash {v[2]}, golden {g})')
    print(f'{"scene":10} {"mode":14} {"best ms":>8} {"median":>8}  hash')
    for (n, mode), (best, med, h, sect) in res.items():
        print(f'{n:10} {mode:14} {best:8.2f} {med:8.2f}  {h}')
        for s in sect:
            print(f'{"":26}{s}')
    for f in fails:
        print('FAIL', f)
    print(f'{len(res)} runs, {len(fails)} failures; images in {sdir}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
