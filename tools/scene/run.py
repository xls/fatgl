"""Runs tools/scene (a Doom 3 BFG style frame) in every fatgl mode and checks the images.

    python tools/scene/run.py [--build build-dev] [--frames 10] [--update] [--out <dir>]

Modes that must render the same bits: the JIT, the interpreter, AVX2, SSE2, one
thread. MSAA (4x / 8x) and fast textures may differ from them, but only a little
(edges, filtering): a pass that paints the frame (a mask ignored, a broken light)
fails. Every mode's image hash is also checked against tools/scene/golden.txt
(--update rewrites it after an intended change). Prints the frame times; writes
PNGs of every mode (and difference images of the failures) to --out.
"""
import argparse
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# mode: environment
MODES = [
    ('jit', {}),
    ('interpreter', {'FATGL_SHADERS': 'interp'}),
    ('avx2', {'FM_SIMD': 'avx2'}),
    ('sse2', {'FM_SIMD': 'sse2'}),
    ('1thread', {'FATGL_THREADS': '1'}),
    ('msaa4', {'FATGL_MSAA': '4'}),
    ('msaa8', {'FATGL_MSAA': '8'}),
    ('fast_textures', {'FATGL_TEXTURES': 'fast'}),
]
SAME = ['jit', 'interpreter', 'avx2', 'sse2', '1thread']  # bit identical
CLOSE = {'msaa4': 1.5, 'msaa8': 1.5, 'fast_textures': 2.0}  # mean |difference| per channel against jit


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
    ap.add_argument('--build', default=os.path.join(ROOT, 'build-dev'))
    ap.add_argument('--frames', type=int, default=10)
    ap.add_argument('--update', action='store_true', help='rewrite golden.txt from this run')
    ap.add_argument('--out', default=None, help='PNG output directory (default <build>/scene_out)')
    a = ap.parse_args()
    exe = os.path.join(a.build, 'scene.exe')
    out = a.out or os.path.join(a.build, 'scene_out')
    os.makedirs(out, exist_ok=True)
    golden_path = os.path.join(HERE, 'golden.txt')
    golden = {}
    if os.path.exists(golden_path):
        for line in open(golden_path):
            if line.strip() and not line.startswith('#'):
                k, v = line.split()
                golden[k] = v
    res, imgs, fails = {}, {}, []
    for name, env in MODES:
        e = dict(os.environ)
        for k in ('FATGL_SHADERS', 'FM_SIMD', 'FATGL_THREADS', 'FATGL_MSAA', 'FATGL_TEXTURES', 'FM_JIT'):
            e.pop(k, None)
        e.update(env)
        e['FATGL_LOG'] = '0'
        ppm = os.path.join(out, name + '.ppm')
        r = subprocess.run([exe, os.path.join(ROOT, 'tools', 'bench'), str(a.frames), ppm], env=e, capture_output=True, text=True,
                           timeout=600)
        ms = h = None
        for line in r.stdout.splitlines():
            if line.startswith('scene '):
                ms = float(line.split(':')[1].split('ms')[0])
            if line.startswith('hash '):
                h = line.split()[1]
        if r.returncode or h is None:
            fails.append(f'{name}: the scene failed (exit {r.returncode})' + (f': {r.stdout.strip()[-300:]}' if r.stdout else ''))
            continue
        w, hh, rgb = read_ppm(ppm)
        write_png(os.path.join(out, name + '.png'), w, hh, rgb)
        os.remove(ppm)
        res[name] = (ms, h)
        imgs[name] = (w, hh, rgb)
    base = imgs.get('jit')
    for name in SAME:
        if name in res and 'jit' in res and res[name][1] != res['jit'][1]:
            fails.append(f'{name}: not the same image as the JIT ({res[name][1]} vs {res["jit"][1]})')
    for name, lim in CLOSE.items():
        if name not in imgs or not base:
            continue
        w, h, rgb = imgs[name]
        d = [abs(x - y) for x, y in zip(rgb, base[2])]
        mean = sum(d) / len(d)
        if mean > lim:
            fails.append(f'{name}: too far from the JIT image (mean difference {mean:.2f} > {lim})')
            write_png(os.path.join(out, name + '_diff.png'), w, h, bytes(min(255, 8 * x) for x in d))
    if a.update:
        with open(golden_path, 'w', newline='\n') as f:
            f.write('# tools/scene image hashes per mode (python tools/scene/run.py --update)\n')
            for name, _ in MODES:
                if name in res:
                    f.write(f'{name} {res[name][1]}\n')
    else:
        for name, (ms, h) in res.items():
            if name in golden and golden[name] != h:
                fails.append(f'{name}: the image changed (hash {h}, golden {golden[name]}; {out}/{name}.png)')
    print(f'{"mode":16} {"ms":>8}  hash')
    for name, _ in MODES:
        if name in res:
            print(f'{name:16} {res[name][0]:8.2f}  {res[name][1]}')
    for f in fails:
        print('FAIL', f)
    print(f'{len(res)} modes, {len(fails)} failures; images in {out}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
