"""The baked Doom 3 BFG frames on Mesa's llvmpipe (the bar fatgl is measured
against): builds bfgscene.c for Linux in a Debian container (Docker) and
replays every scene with 1 and all threads (LP_NUM_THREADS), timings and
image hashes like run.py's, PNGs as <scene>_llvmpipe.png.

    python tools/bfgscene/llvmpipe.py [--scenes <dir>] [--frames 10] [--only garage,...] [--threads 1,32] [--no-aniso]

--no-aniso: without BFG's 8x anisotropic filtering, which llvmpipe implements and
fatgl does not (the same texture work as fatgl).

The scenes come from run.py (<build>/bfgscenes). Docker Desktop must be running.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import run  # noqa: E402  (read_ppm, write_png, read_scenes)

SCRIPT = r'''
set -e
apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq gcc libc6-dev libegl-dev libgl-dev libegl-mesa0 libgl1-mesa-dri >/dev/null 2>&1
gcc -O2 -o /tmp/bfgscene /tools/bfgscene.c -lEGL -lGL -lm
export EGL_PLATFORM=surfaceless
for s in $SCENES; do
  for t in $THREADS; do
    echo "== $s $t"
    LP_NUM_THREADS=$t /tmp/bfgscene /scenes/$s.bfgs $FRAMES /scenes/${s}_llvmpipe_$t.ppm $EXTRA
  done
done
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--scenes', default=os.path.join(ROOT, 'build-dev', 'bfgscenes'))
    ap.add_argument('--frames', type=int, default=10)
    ap.add_argument('--only', help='comma separated scene names')
    ap.add_argument('--threads', default='1,32', help='LP_NUM_THREADS values')
    ap.add_argument('--no-aniso', action='store_true', help='no anisotropic filtering (like fatgl)')
    a = ap.parse_args()
    names = [s[0] for s in run.read_scenes() if not a.only or s[0] in a.only.split(',')]
    threads = a.threads.split(',')
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    cmd = ['docker', 'run', '--rm', '--platform', 'linux/amd64',
           '-v', HERE.replace('\\', '/') + ':/tools:ro', '-v', a.scenes.replace('\\', '/') + ':/scenes',
           '-e', 'SCENES=' + ' '.join(names), '-e', 'THREADS=' + ' '.join(threads), '-e', 'FRAMES=%d' % a.frames, '-e', 'EXTRA=' + ('--no-aniso' if a.no_aniso else ''),
           'debian:trixie', 'sh', '-c', SCRIPT]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(r.stdout[-3000:] + r.stderr[-3000:])
    cur = None
    res = {}
    for line in r.stdout.splitlines():
        if line.startswith('== '):
            cur = tuple(line[3:].split())
        elif line.startswith('renderer: ') and cur:
            res.setdefault(cur, {})['renderer'] = line[10:]
        elif line.startswith('bfgscene ') and cur:
            res.setdefault(cur, {})['best'] = float(line.split(':')[1].split('ms')[0])
            res[cur]['median'] = float(line.split('best,')[1].split('ms')[0])
        elif line.startswith('hash ') and cur:
            res.setdefault(cur, {})['hash'] = line.split()[1]
    renderer = next((v.get('renderer') for v in res.values() if v.get('renderer')), '?')
    print('llvmpipe:', renderer)
    print(f'{"scene":10} {"threads":>7} {"best ms":>8} {"median":>8}  hash')
    for (s, t), v in res.items():
        print(f'{s:10} {t:>7} {v.get("best", 0):8.2f} {v.get("median", 0):8.2f}  {v.get("hash", "?")}')
        ppm = os.path.join(a.scenes, f'{s}_llvmpipe_{t}.ppm')
        if os.path.exists(ppm):
            w, h, rgb = run.read_ppm(ppm)
            run.write_png(os.path.join(a.scenes, f'{s}_llvmpipe.png'), w, h, rgb)
            os.remove(ppm)
    return 0


if __name__ == '__main__':
    sys.exit(main())
