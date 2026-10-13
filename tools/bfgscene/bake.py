"""Bakes a static Doom 3 BFG frame from the game's own assets into a scene file
that tools/bfgscene/bfgscene.exe replays through OpenGL.

    python tools/bfgscene/bake.py --game "<DOOM 3 BFG Edition dir>" --map alphalabs1
        --pos x y z --yaw deg [--pitch deg] [--time sec] --out scenes/name.bfgs

What the game's frontend does per view is done here, once: the view and
projection matrices (infinite far plane, r_znear 3, g_fov 80 widened to 16:9),
portal visibility from the camera's area, the lights (light_radius /
light_target, light materials), their scissor rects, the interactions and the
prelight shadow volumes, material expressions at --time. The GL work is
recorded the way the backend issues it (tr_backend_draw.cpp, GL_State):
depth fill (opaque, then perforated through texture_color with alpha test),
per light a stencil clear to 128, the shadow volumes (two sided stencil,
z-fail drawn as preload + z-pass), the interactions (depth EQUAL, blend ONE
ONE, stencil EQUAL 128, the general path: r_skipInteractionFastPath is 1),
then the ambient stages of every surface in sort order. Shaders are the
game's renderprogs/glsl. The scene file holds id's textures, keep it local.
"""
import argparse
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bfg_assets as A  # noqa: E402
import bfg_material as M  # noqa: E402

W, H = 1280, 720
ZNEAR = 3.0
LIGHT_SCALE = 3.0  # r_lightScale
STENCIL_SHADOW_TEST_VALUE = 128
STENCIL_SHADOW_MASK_VALUE = 255

# GL state bits (GLState.h)
GLS_SRCBLEND = {M.GL_ONE: 0, M.GL_ZERO: 1, M.GL_DST_COLOR: 2, M.GL_ONE_MINUS_DST_COLOR: 3, M.GL_SRC_ALPHA: 4,
                M.GL_ONE_MINUS_SRC_ALPHA: 5, M.GL_DST_ALPHA: 6, M.GL_ONE_MINUS_DST_ALPHA: 7}
GLS_DSTBLEND = {M.GL_ZERO: 0, M.GL_ONE: 1 << 3, M.GL_SRC_COLOR: 2 << 3, M.GL_ONE_MINUS_SRC_COLOR: 3 << 3,
                M.GL_SRC_ALPHA: 4 << 3, M.GL_ONE_MINUS_SRC_ALPHA: 5 << 3, M.GL_DST_ALPHA: 6 << 3,
                M.GL_ONE_MINUS_DST_ALPHA: 7 << 3}
GLS_SRCBLEND_ONE, GLS_SRCBLEND_ZERO, GLS_SRCBLEND_DST_COLOR, GLS_SRCBLEND_SRC_ALPHA = 0, 1, 2, 4
GLS_DSTBLEND_ZERO, GLS_DSTBLEND_ONE, GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA = 0, 1 << 3, 5 << 3
GLS_SRCBLEND_BITS, GLS_DSTBLEND_BITS = 7, 7 << 3
GLS_DEPTHMASK = 1 << 6
GLS_REDMASK, GLS_GREENMASK, GLS_BLUEMASK, GLS_ALPHAMASK = 1 << 7, 1 << 8, 1 << 9, 1 << 10
GLS_COLORMASK = GLS_REDMASK | GLS_GREENMASK | GLS_BLUEMASK
GLS_POLYGON_OFFSET = 1 << 12
GLS_DEPTHFUNC_LESS, GLS_DEPTHFUNC_ALWAYS, GLS_DEPTHFUNC_EQUAL = 0, 1 << 13, 3 << 13
GLS_DEPTHFUNC_BITS = 3 << 13


def stencil_ref(x):
    return (x & 0xFF) << 20


def stencil_mask(x):
    return (x & 0xFF) << 28


GLS_STENCIL_FUNC_ALWAYS, GLS_STENCIL_FUNC_EQUAL = 0, 5 << 36
GLS_STENCIL_OP_PASS_INCR = 3 << 45

GL_FRONT, GL_BACK = 0x0404, 0x0405
GL_KEEP, GL_INCR, GL_DECR = 0x1E00, 0x1E02, 0x1E03

CT_FRONT_SIDED, CT_BACK_SIDED, CT_TWO_SIDED = 0, 1, 2

# ---------------------------------------------------------------- small linear algebra
# 4x4 as row major nested lists, column vectors (idRenderMatrix convention)


def mmul(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def mvec(m, v):
    return [sum(m[r][k] * v[k] for k in range(4)) for r in range(4)]


def ident():
    return [[float(r == c) for c in range(4)] for r in range(4)]


def inverse(m):
    a = [row[:] + [float(r == c) for c in range(4)] for r, row in enumerate(m)]
    for c in range(4):
        p = max(range(c, 4), key=lambda r: abs(a[r][c]))
        if abs(a[p][c]) < 1e-20:
            raise ValueError('singular')
        a[c], a[p] = a[p], a[c]
        pv = a[c][c]
        a[c] = [x / pv for x in a[c]]
        for r in range(4):
            if r != c and a[r][c] != 0:
                f = a[r][c]
                a[r] = [x - f * y for x, y in zip(a[r], a[c])]
    return [row[4:] for row in a]


def dot3(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def sub3(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm3(a):
    l = math.sqrt(dot3(a, a))
    return [x / l for x in a] if l else [0.0, 0.0, 0.0]


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Space:
    """viewEntity_t: a model matrix (axis rows + origin) and its MVP"""

    def __init__(self, axis, origin, view):
        self.axis, self.origin = axis, origin
        # model to world, column vectors: world = R local + origin, R columns = axis rows
        self.model = [[axis[0][0], axis[1][0], axis[2][0], origin[0]],
                      [axis[0][1], axis[1][1], axis[2][1], origin[1]],
                      [axis[0][2], axis[1][2], axis[2][2], origin[2]],
                      [0, 0, 0, 1]]
        self.mvp = mmul(view.mvp_world, self.model)
        self.model_view = mmul(view.view_world, self.model)
        self.identity = axis == [[1, 0, 0], [0, 1, 0], [0, 0, 1]] and origin == [0, 0, 0]

    def to_local_point(self, p):
        d = sub3(p, self.origin)
        return [dot3(d, self.axis[0]), dot3(d, self.axis[1]), dot3(d, self.axis[2])]

    def to_local_plane(self, pl):
        """R_GlobalPlaneToLocal"""
        n = pl[:3]
        return [dot3(n, self.axis[0]), dot3(n, self.axis[1]), dot3(n, self.axis[2]), pl[3] + dot3(self.origin, n)]

    def bounds_to_world(self, b):
        pts = [[(b[3] if i & 1 else b[0]), (b[4] if i & 2 else b[1]), (b[5] if i & 4 else b[2])] for i in range(8)]
        wp = [mvec(self.model, p + [1.0])[:3] for p in pts]
        return [min(p[0] for p in wp), min(p[1] for p in wp), min(p[2] for p in wp),
                max(p[0] for p in wp), max(p[1] for p in wp), max(p[2] for p in wp)]


class View:
    def __init__(self, pos, yaw, pitch, fov_x_43=80.0):
        self.origin = pos
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
        fwd = [cp * cy, cp * sy, -sp]
        left = [-sy, cy, 0.0]
        up = cross(fwd, left)
        self.axis = [fwd, left, up]
        # idPlayerView / CalcFov: g_fov is for 4:3 (640 x 480), the vertical fov follows, then 16:9 widens x
        x = 640.0 / math.tan(math.radians(fov_x_43) / 2)
        fov_y = math.degrees(math.atan2(480.0, x)) * 2
        fov_x = math.degrees(math.atan(math.tan(math.radians(fov_y) / 2) * W / H)) * 2
        self.fov_x, self.fov_y = fov_x, fov_y
        # world -> GL eye (R_SetupViewMatrix with s_flipMatrix)
        o = pos
        rows = [[-left[0], -left[1], -left[2], dot3(o, left)],
                [up[0], up[1], up[2], -dot3(o, up)],
                [-fwd[0], -fwd[1], -fwd[2], dot3(o, fwd)],
                [0, 0, 0, 1]]
        self.view_world = rows
        ymax = ZNEAR * math.tan(math.radians(fov_y) / 2)
        xmax = ZNEAR * math.tan(math.radians(fov_x) / 2)
        self.proj = [[2 * ZNEAR / (2 * xmax), 0, 0, 0],
                     [0, 2 * ZNEAR / (2 * ymax), 0, 0],
                     [0, 0, -0.999, -2 * ZNEAR],
                     [0, 0, -1, 0]]
        self.mvp_world = mmul(self.proj, self.view_world)
        # view frustum planes (world space, inside positive) from the MVP rows (Gribb / Hartmann):
        # left, right, bottom, top, near
        self.planes = []
        m = self.mvp_world
        for a, s in ((0, 1), (0, -1), (1, 1), (1, -1), (2, 1)):
            pl = [m[3][k] + s * m[a][k] for k in range(4)]
            l = math.sqrt(dot3(pl, pl))
            self.planes.append([x / l for x in pl])

    def cull_bounds(self, b):
        """true if the world bounds are outside the frustum"""
        for pl in self.planes:
            px = b[3] if pl[0] > 0 else b[0]
            py = b[4] if pl[1] > 0 else b[1]
            pz = b[5] if pl[2] > 0 else b[2]
            if pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0:
                return True
        return False

    def project_points(self, pts, mvp=None):
        """screen rect [x1,y1,x2,y2] (GL window coords) of a convex point set, clipped to near; None if
        entirely behind"""
        m = mvp or self.mvp_world
        clip = [mvec(m, list(p) + [1.0]) for p in pts]
        return clip_rect(clip)


def clip_rect(clip):
    """screen rect of the clip space points (their hull), clipping edges to the near plane z > -w"""
    inside = [c for c in clip if c[2] > -c[3] + 1e-6]
    if not inside:
        return None
    pts = list(inside)
    n = len(clip)
    if len(inside) != n:
        # add the edge intersections with the near plane between every pair (the hull handles the rest)
        for i in range(n):
            for j in range(i + 1, n):
                a, b = clip[i], clip[j]
                da, db = a[2] + a[3], b[2] + b[3]
                if (da > 0) != (db > 0):
                    t = da / (da - db)
                    pts.append([a[k] + (b[k] - a[k]) * t for k in range(4)])
    xs, ys = [], []
    for c in pts:
        w = c[3] if c[3] > 1e-6 else 1e-6
        xs.append((c[0] / w * 0.5 + 0.5) * W)
        ys.append((c[1] / w * 0.5 + 0.5) * H)
    r = [max(0, int(math.floor(min(xs)))), max(0, int(math.floor(min(ys)))),
         min(W - 1, int(math.ceil(max(xs)))), min(H - 1, int(math.ceil(max(ys))))]
    if r[0] > r[2] or r[1] > r[3]:
        return None
    return r


def clip_zrange(clip):
    """window depth range [zmin, zmax] of the clip space points' hull clipped to the near plane
    (ProjectedNearClippedBounds' z: the near plane is depth 0)"""
    zs = [(c[2] / c[3]) * 0.5 + 0.5 for c in clip if c[2] > -c[3] + 1e-6]
    if not zs:
        return None
    if len(zs) != len(clip):
        zs.append(0.0)
    return max(0.0, min(zs)), min(1.0, max(zs))


def rect_and(a, b):
    if a is None or b is None:
        return None
    r = [max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])]
    return r if r[0] <= r[2] and r[1] <= r[3] else None


def rect_or(a, b):
    if a is None:
        return b
    if b is None:
        return a
    return [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]


def bounds_corners(b):
    return [[(b[3] if i & 1 else b[0]), (b[4] if i & 2 else b[1]), (b[5] if i & 4 else b[2])] for i in range(8)]


def bounds_overlap(a, b):
    return a[0] <= b[3] and b[0] <= a[3] and a[1] <= b[4] and b[1] <= a[4] and a[2] <= b[5] and b[2] <= a[5]


def segment_hits_bounds(p0, p1, b):
    """slab test: does the segment p0 -> p1 touch the box"""
    t0, t1 = 0.0, 1.0
    for k in range(3):
        d = p1[k] - p0[k]
        if abs(d) < 1e-9:
            if p0[k] < b[k] or p0[k] > b[k + 3]:
                return False
            continue
        a, c = (b[k] - p0[k]) / d, (b[k + 3] - p0[k]) / d
        if a > c:
            a, c = c, a
        t0, t1 = max(t0, a), min(t1, c)
        if t0 > t1:
            return False
    return True


def parse_vec(s, n=3):
    v = [float(x) for x in s.split()]
    return (v + [0.0] * n)[:n]


def axis_from_entity(e):
    if 'rotation' in e:
        v = parse_vec(e['rotation'], 9)
        return [v[0:3], v[3:6], v[6:9]]
    if 'angle' in e:
        a = math.radians(float(e['angle']))
        c, s = math.cos(a), math.sin(a)
        return [[c, s, 0.0], [-s, c, 0.0], [0.0, 0.0, 1.0]]
    return [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]


# ---------------------------------------------------------------- world


class World:
    def __init__(self, res, mapname):
        self.res = res
        self.proc = A.read_bproc(res.read('generated/maps/game/%s.bproc' % mapname if '/' not in mapname
                                          else 'generated/maps/%s.bproc' % mapname))
        mp = 'maps/game/%s.map' % mapname if '/' not in mapname else 'maps/%s.map' % mapname
        self.entities = A.read_map_entities(res.read(mp).decode('latin1'))
        self.nodes = self.proc['nodes']
        self.num_areas = self.proc['numAreas']
        self.area_portals = [[] for _ in range(self.num_areas)]
        for a1, a2, w in self.proc['portals']:
            # plane from the winding
            n = norm3(cross(sub3(w[1], w[0]), sub3(w[2], w[0])))
            d = -dot3(n, w[0])
            c = [sum(p[k] for p in w) / len(w) for k in range(3)]
            front = self.point_in_area([c[k] + n[k] * 2.0 for k in range(3)])
            # the side of a1: front if the point in front is in a1 (or the back is in a2)
            a1_front = front == a1 or (front != a2 and self.point_in_area([c[k] - n[k] * 2.0 for k in range(3)]) == a2)
            self.area_portals[a1].append({'into': a2, 'w': w, 'plane': n + [d], 'from_front': a1_front})
            self.area_portals[a2].append({'into': a1, 'w': w, 'plane': n + [d], 'from_front': not a1_front})

    def point_in_area(self, p):
        if not self.nodes:
            return 0
        n = 0
        while True:
            pl, ch = self.nodes[n]
            d = pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3]
            c = ch[0] if d > 0 else ch[1]
            if c == 0:
                return -1
            if c < 0:
                return -1 - c
            n = c

    def bounds_areas(self, b):
        """areas touched by the bounds (R_PushVolumeIntoTree with a box)"""
        out = set()
        if not self.nodes:
            return {0}
        stack = [0]
        while stack:
            n = stack.pop()
            pl, ch = self.nodes[n]
            # box vs plane
            px = b[3] if pl[0] > 0 else b[0]
            py = b[4] if pl[1] > 0 else b[1]
            pz = b[5] if pl[2] > 0 else b[2]
            nx = b[0] if pl[0] > 0 else b[3]
            ny = b[1] if pl[1] > 0 else b[4]
            nz = b[2] if pl[2] > 0 else b[5]
            dmax = pl[0] * px + pl[1] * py + pl[2] * pz + pl[3]
            dmin = pl[0] * nx + pl[1] * ny + pl[2] * nz + pl[3]
            sides = []
            if dmax > 0:
                sides.append(ch[0])
            if dmin <= 0:
                sides.append(ch[1])
            for c in sides:
                if c == 0:
                    continue
                if c < 0:
                    out.add(-1 - c)
                else:
                    stack.append(c)
        return out

    def flow_view(self, view):
        """R_FlowViewThroughPortals: the screen rect each visible area is seen through"""
        start = self.point_in_area(view.origin)
        rects = {}
        if start < 0:
            # outside the world: everything
            for a in range(self.num_areas):
                rects[a] = [0, 0, W - 1, H - 1]
            return rects, start
        full = [0, 0, W - 1, H - 1]

        def flood(area, rect, stack, depth):
            rects[area] = rect_or(rects.get(area), rect)
            if depth > 64:
                return
            for p in self.area_portals[area]:
                if id(p) in stack:
                    continue
                pl = p['plane']
                d = dot3(pl, view.origin) + pl[3]
                if p['from_front'] and d < -0.1 or not p['from_front'] and d > 0.1:
                    continue  # facing away
                if abs(d) < ZNEAR * 1.5:
                    r = rect  # the view is in the portal: no narrowing
                else:
                    r = rect_and(rect, view.project_points(p['w']))
                if r is None:
                    continue
                stack.add(id(p))
                flood(p['into'], r, stack, depth + 1)
                stack.discard(id(p))

        flood(start, full, set(), 0)
        return rects, start


# ---------------------------------------------------------------- images


class Images:
    USAGE = {'specular': 0, 'diffuse': 1, 'default': 2, 'bump': 3, 'light': 5, 'coverage': 10}

    def __init__(self, res):
        self.res = res
        self.index = {}
        for n in res.names('generated/images/'):
            base, _, tail = n[len('generated/images/'):].rpartition('#__')
            if not base:
                continue
            code = tail.split('.')[0]
            self.index.setdefault(base, {})[code] = n
        self.textures = []      # dicts for the scene file
        self.by_key = {}

    def find(self, key, usage='default', cube=False, repeat=M.TR_REPEAT, filt=M.TF_DEFAULT):
        k = (key, usage, cube, repeat, filt)
        if k in self.by_key:
            return self.by_key[k]
        t = self._load(key, usage, cube, repeat, filt)
        self.by_key[k] = t
        return t

    def _load(self, key, usage, cube, repeat, filt):
        if key is None:
            return -1
        if key.startswith('_'):
            return self._intrinsic(key, repeat, filt)
        codes = self.index.get(key)
        if not codes:
            return self._intrinsic('_default', repeat, filt)
        want = '%02d%02d' % (self.USAGE.get(usage, 2), 1 if cube else 0)
        name = codes.get(want)
        if name is None:
            same_cube = [c for c in codes if (c[2:] != '00') == cube]
            name = codes[sorted(same_cube or codes)[0]]
        bi = A.read_bimage(self.res.read(name))
        tex = {'name': name, 'bimage': bi, 'repeat': repeat, 'filter': filt}
        self.textures.append(tex)
        return len(self.textures) - 1

    def _intrinsic(self, key, repeat, filt):
        size = 8
        px = None
        if key == '_white':
            px = (255, 255, 255, 255)
        elif key == '_black':
            px = (0, 0, 0, 0)
        elif key == '_flat':
            px = (0, 128, 0, 128)  # CFM_NORMAL_DXT5 layout: x in alpha, y in green
            size = 2
        elif key in ('_nofalloff',):
            px = (255, 255, 255, 255)
        elif key in ('_currentrender', '_scratch', '_accum', '_currentdepth'):
            return self._screen_texture(key)
        if px is None:
            # _default: R_DefaultImage, a box with a dark outline
            size = 16
            data = bytearray()
            for y in range(size):
                for x in range(size):
                    edge = x == 0 or y == 0 or x == size - 1 or y == size - 1
                    data += bytes((32, 32, 32, 255) if edge else (255, 255, 255, 255))
        else:
            data = bytes(px) * (size * size)
        bi = {'type': A.TT_2D, 'format': A.FMT_RGBA8, 'colorFormat': 0, 'width': size, 'height': size, 'levels': 1,
              'images': [(0, 0, size, size, bytes(data))]}
        self.textures.append({'name': key, 'bimage': bi, 'repeat': repeat, 'filter': filt})
        return len(self.textures) - 1

    def _screen_texture(self, key):
        bi = {'type': A.TT_2D, 'format': A.FMT_RGBA8, 'colorFormat': 0, 'width': W, 'height': H, 'levels': 1,
              'images': [(0, 0, W, H, bytes(W * H * 4))]}
        self.textures.append({'name': key, 'bimage': bi, 'repeat': M.TR_CLAMP, 'filter': M.TF_LINEAR})
        return len(self.textures) - 1


# ---------------------------------------------------------------- programs


class Programs:
    def __init__(self, res):
        self.res = res
        self.list = []
        self.by_name = {}

    def get(self, name, has_fragment=True):
        if name in self.by_name:
            return self.by_name[name]

        def read(n):
            return self.res.read('renderprogs/glsl/' + n).decode('latin1')

        vs = read(name + '_vertex.glsl')
        vu = [u for u in read(name + '_vertex.uniforms').split() if u]
        fs, fu = '', []
        if has_fragment:
            fs = read(name + '_fragment.glsl')
            fu = [u for u in read(name + '_fragment.uniforms').split() if u]
        self.list.append({'name': name, 'vs': vs, 'fs': fs, 'vu': vu, 'fu': fu})
        self.by_name[name] = len(self.list) - 1
        return self.by_name[name]


# ---------------------------------------------------------------- the command stream

OP_STATE, OP_CULL, OP_POLYOFS, OP_SCISSOR, OP_CLEAR, OP_PROGRAM, OP_VA, OP_FA, OP_TEX, OP_STENCILSEP, OP_DRAW, \
    OP_COPYFB, OP_VIEWPORT, OP_MARK, OP_DBOUNDS = range(1, 16)
LAYOUT_DRAWVERT, LAYOUT_SHADOWVERT = 0, 1


class Backend:
    """records GL work like BFG's backend (GL_State caching included)"""

    def __init__(self, programs):
        self.cmds = bytearray()
        self.progs = programs
        self.state = None
        self.cull = None
        self.prog = None
        self.parms = {}
        self.uploaded = {}     # program -> (va tuple, fa tuple)
        self.tex = {}
        self.scissor = None
        self.ofs = (0.0, 0.0)
        self.dbounds = (0.0, 0.0)
        self.counts = {'draws': 0, 'tris': 0}

    def op(self, code, fmt='', *vals):
        self.cmds += struct.pack('<I', code)
        if fmt:
            self.cmds += struct.pack('<' + fmt, *vals)

    def mark(self, name):
        b = name.encode('ascii', 'replace')[:63]
        self.op(OP_MARK, 'I', len(b))
        self.cmds += b

    def gl_state(self, bits):
        if bits != self.state:
            self.op(OP_STATE, 'Q', bits)
            self.state = bits

    def gl_cull(self, ct):
        if ct != self.cull:
            self.op(OP_CULL, 'i', ct)
            self.cull = ct

    def polygon_offset(self, scale, bias):
        if (scale, bias) != self.ofs:
            self.op(OP_POLYOFS, 'ff', scale, bias)
            self.ofs = (scale, bias)

    def depth_bounds(self, zmin, zmax):
        """GL_DepthBoundsTest: 0, 0 disables (r_useLightDepthBounds 1)"""
        if (zmin, zmax) != self.dbounds and zmin <= zmax:
            self.op(OP_DBOUNDS, 'ff', zmin, zmax)
            self.dbounds = (zmin, zmax)

    def gl_scissor(self, r):
        if r != self.scissor:
            self.op(OP_SCISSOR, 'iiii', r[0], r[1], r[2] + 1 - r[0], r[3] + 1 - r[1])
            self.scissor = r

    def clear(self, color, depth, stencil, sval=0, rgba=(0, 0, 0, 0)):
        self.op(OP_CLEAR, 'IIffff', (1 if color else 0) | (2 if depth else 0) | (4 if stencil else 0), sval, *rgba)

    def stencil_separate(self, face, sfail, zfail, zpass):
        self.op(OP_STENCILSEP, 'IIII', face, sfail, zfail, zpass)

    def bind(self, unit, tex):
        if self.tex.get(unit) != tex:
            self.op(OP_TEX, 'Ii', unit, tex)
            self.tex[unit] = tex

    def set_parm(self, name, v):
        self.parms[name] = tuple(f32(x) for x in v)

    def set_mvp(self, mvp):
        for i, n in enumerate(('rpMVPmatrixX', 'rpMVPmatrixY', 'rpMVPmatrixZ', 'rpMVPmatrixW')):
            self.set_parm(n, mvp[i])

    def use(self, prog):
        if prog != self.prog:
            self.op(OP_PROGRAM, 'I', prog)
            self.prog = prog

    def draw(self, layout, vb_off, ib_off, count):
        p = self.progs.list[self.prog]
        va = tuple(self.parms.get(u, (0.0, 0.0, 0.0, 0.0)) for u in p['vu'])
        fa = tuple(self.parms.get(u, (0.0, 0.0, 0.0, 0.0)) for u in p['fu'])
        last = self.uploaded.get(self.prog)
        if last is None or last[0] != va:
            self.op(OP_VA, 'I', len(va))
            self.cmds += struct.pack('<%df' % (4 * len(va)), *[x for v in va for x in v])
        if fa and (last is None or last[1] != fa):
            self.op(OP_FA, 'I', len(fa))
            self.cmds += struct.pack('<%df' % (4 * len(fa)), *[x for v in fa for x in v])
        self.uploaded[self.prog] = (va, fa)
        self.op(OP_DRAW, 'IIII', layout, vb_off, ib_off, count)
        self.counts['draws'] += 1
        self.counts['tris'] += count // 3


# ---------------------------------------------------------------- scene assembly


def eval_parms(e, default_color=(1, 1, 1)):
    parms = [0.0] * 12
    c = parse_vec(e.get('_color', '%g %g %g' % default_color)) if e is not None else list(default_color)
    parms[0], parms[1], parms[2] = c
    parms[3] = 1.0
    if e is not None:
        for i in range(3, 12):
            k = 'shaderparm%d' % i
            if k in e:
                parms[i] = float(e[k])
    return parms


class Bake:
    def __init__(self, args):
        self.args = args
        base = os.path.join(args.game, 'base')
        self.res = A.game_resources(base, args.map)
        texts = [(n, self.res.read(n).decode('latin1')) for n in sorted(self.res.names('materials/'))]
        self.decls = M.Decls(texts)
        self.mat_cache = {}
        self.model_cache = {}
        self.world = World(self.res, args.map)
        self.images = Images(self.res)
        self.progs = Programs(self.res)
        self.view = View(args.pos, args.yaw, args.pitch)
        self.time = args.time
        self.vb = bytearray()
        self.ib = bytearray()
        self.geo_cache = {}
        self.warnings = set()

    def entity_model(self, mname):
        key = mname.lower().replace(chr(92), '/')
        if key in self.model_cache:
            return self.model_cache[key]
        m = self.world.proc['models'].get(key)
        if m is None:
            base, _, ext = key.rpartition('.')
            gen = {'lwo': 'blwo', 'ase': 'base', 'ma': 'bma', 'flt': 'bflt'}.get(ext)
            path = 'generated/rendermodels/%s.%s' % (base, gen) if gen else None
            if path and self.res.has(path):
                m = A.read_render_model(self.res.read(path))
            elif ext not in ('prt', 'md5mesh', ''):
                self.warnings.add('model %s not found' % key)
        self.model_cache[key] = m
        return m

    def material(self, name):
        name = name.lower()
        if name in self.mat_cache:
            return self.mat_cache[name]
        body = self.decls.materials.get(name)
        m = None
        if body is not None:
            try:
                m = M.parse_material(name, body, self.decls)
            except Exception as ex:  # noqa: BLE001  the game defaults these too
                self.warnings.add('material %s: %s' % (name, ex))
        elif name and name != '_default':
            # idMaterial::SetDefaultText: a blended stage of the image with the same name
            self.warnings.add('implicit material ' + name)
            text = '{ { blend blend colored map "%s" clamp } }' % name
            m = M.parse_material(name, M.tokenize(text), self.decls)
        self.mat_cache[name] = m
        return m

    # geometry into the static buffers, like the vertex cache
    def geo(self, surf):
        key = id(surf)
        if key in self.geo_cache:
            return self.geo_cache[key]
        while len(self.vb) % 32:
            self.vb += b'\0'
        vo = len(self.vb)
        self.vb += surf['verts']
        io = len(self.ib)
        self.ib += surf['indexes']
        while len(self.ib) % 4:
            self.ib += b'\0'
        self.geo_cache[key] = (vo, io)
        return vo, io

    def dynamic_shadow(self, surf, light, caps):
        """a shadow volume like the static shadow volume job builds: silhouette quads
        extruded to infinity (w = 0), plus the front and back caps for z-fail"""
        key = ('dv', id(surf))
        if key not in self.geo_cache:
            nv = surf['numVerts']
            if nv * 2 > 65535:
                self.warnings.add('shadow: %s has too many vertices' % surf['material'])
                self.geo_cache[key] = None
            else:
                pos = [struct.unpack_from('<3f', surf['verts'], i * 32) for i in range(nv)]
                nrm = [surf['verts'][i * 32 + 16:i * 32 + 19] for i in range(nv)]
                sv = bytearray()
                for p in pos:
                    sv += struct.pack('<8f', p[0], p[1], p[2], 1.0, p[0], p[1], p[2], 0.0)
                while len(self.vb) % 32:
                    self.vb += bytes(1)
                vo = len(self.vb)
                self.vb += sv
                tri = surf.get('silIndexes') or struct.unpack('<%dH' % surf['numIndexes'], surf['indexes'])
                # the winding convention: does the right handed normal point to the visible side?
                acc = 0.0
                for t in range(0, len(tri), 3):
                    a, b, c = tri[t], tri[t + 1], tri[t + 2]
                    g = cross(sub3(pos[b], pos[a]), sub3(pos[c], pos[a]))
                    n = [x / 127.5 - 1.0 for x in nrm[a]]
                    acc += dot3(g, n)
                edges = {}
                for t in range(0, len(tri), 3):
                    for k in range(3):
                        a, b = tri[t + k], tri[t + (k + 1) % 3]
                        edges.setdefault((min(a, b), max(a, b)), []).append(t)
                self.geo_cache[key] = (vo, pos, tri, 1.0 if acc >= 0 else -1.0, edges)
        g = self.geo_cache[key]
        if g is None:
            return None
        vo, pos, tri, sign, edges = g
        facing = {}
        for t in range(0, len(tri), 3):
            a, b, c = tri[t], tri[t + 1], tri[t + 2]
            n = cross(sub3(pos[b], pos[a]), sub3(pos[c], pos[a]))
            facing[t] = sign * dot3(n, sub3(light, pos[a])) > 0
        out = []

        def emit(i0, i1, i2):
            if sign > 0:
                out.extend((i0, i1, i2))
            else:
                out.extend((i0, i2, i1))

        for t in range(0, len(tri), 3):
            if not facing[t]:
                continue
            for k in range(3):
                a, b = tri[t + k], tri[t + (k + 1) % 3]
                others = [u for u in edges[(min(a, b), max(a, b))] if u != t]
                if others and all(facing[u] for u in others):
                    continue
                emit(2 * b, 2 * a, 2 * a + 1)
                emit(2 * b, 2 * a + 1, 2 * b + 1)
            if caps:
                a, b, c = tri[t], tri[t + 1], tri[t + 2]
                emit(2 * a, 2 * b, 2 * c)
                emit(2 * a + 1, 2 * c + 1, 2 * b + 1)
        if not out:
            return None
        io = len(self.ib)
        self.ib += struct.pack('<%dH' % len(out), *out)
        while len(self.ib) % 4:
            self.ib += bytes(1)
        return vo, io, len(out)

    def shadow_geo(self, surf):
        key = ('s', id(surf))
        if key in self.geo_cache:
            return self.geo_cache[key]
        while len(self.vb) % 32:
            self.vb += b'\0'
        vo = len(self.vb)
        self.vb += surf['shadowVerts']
        io = len(self.ib)
        self.ib += surf['indexes']
        while len(self.ib) % 4:
            self.ib += b'\0'
        self.geo_cache[key] = (vo, io)
        return vo, io

    def build(self):
        w, v = self.world, self.view
        rects, cam_area = w.flow_view(v)
        print(f'camera area {cam_area}, {len(rects)} visible areas, fov {v.fov_x:.1f} x {v.fov_y:.1f}')
        world_space = Space([[1, 0, 0], [0, 1, 0], [0, 0, 1]], [0, 0, 0], v)
        self.world_space = world_space

        # ---- draw surfaces: area models and inline entity models
        surfs = []   # dict: surf, mat, space, rect, parms, areas, wbounds

        def add_model(model, space, parms, areas_rect, ent=None):
            for s in model['surfaces']:
                if 'verts' not in s or s['numIndexes'] == 0:
                    continue
                mat = self.material(s['material'])
                if mat is None or not mat.is_drawn() or mat.deform is not None or mat.gui:
                    continue
                if self.args.only and not any(x in mat.name for x in self.args.only):
                    continue
                if self.args.exclude and any(x in mat.name for x in self.args.exclude):
                    continue
                wb = space.bounds_to_world(s['bounds'])
                if v.cull_bounds(wb):
                    continue
                r = rect_and(areas_rect, v.project_points(bounds_corners(wb)))
                if r is None:
                    continue
                surfs.append({'surf': s, 'mat': mat, 'space': space, 'rect': r, 'parms': parms, 'wb': wb,
                              'ent': ent})

        for a, r in rects.items():
            m = w.proc['models'].get('_area%d' % a)
            if m is not None:
                add_model(m, world_space, eval_parms(None), r)

        # entities with a model: an inline one ("model" == their own name in the bproc) or
        # a generated/rendermodels one (.lwo / .ase / .ma); md5 meshes (animated) and
        # particles are not drawn
        self.lights = []
        self.ent_models = []
        for e in w.entities:
            cls = e.get('classname', '')
            if cls == 'light' or cls.startswith('light_'):
                self.lights.append(e)
                if e.get('start_off') == '1':
                    continue
            mname = e.get('model', '')
            if not mname or cls.startswith('trigger') or cls == 'worldspawn' or cls.startswith('func_emitter'):
                continue
            if e.get('hide') == '1' or self.args.no_entities:
                continue
            model = self.entity_model(mname)
            if model is None:
                continue
            origin = parse_vec(e.get('origin', '0 0 0'))
            space = Space(axis_from_entity(e), origin, v)
            wb = space.bounds_to_world(model['bounds'])
            areas = w.bounds_areas(wb)
            self.ent_models.append({'model': model, 'space': space, 'wb': wb, 'areas': areas, 'ent': e})
            ar = None
            for a in areas:
                ar = rect_or(ar, rects.get(a))
            if ar is None:
                continue
            add_model(model, space, eval_parms(e), ar, e)

        self.surfs = surfs
        print(f'{len(surfs)} surfaces in view')
        self.view_lights = self.build_lights(rects, surfs, world_space)
        print(f'{len(self.view_lights)} lights in view, '
              f'{sum(len(l["inter"]) for l in self.view_lights)} interactions, '
              f'{sum(len(l["shadows"]) for l in self.view_lights)} shadow volumes')
        self.emit(world_space)

    # ------------------------------------------------ lights
    def build_lights(self, rects, surfs, world_space):
        w, v = self.world, self.view
        out = []
        for e in self.lights:
            if e.get('start_off') == '1':
                continue
            origin = parse_vec(e.get('light_origin', e.get('origin', '0 0 0')))
            axis = axis_from_entity({'rotation': e['light_rotation']} if 'light_rotation' in e else e)
            point = 'light_target' not in e
            parallel = e.get('parallel') == '1'
            if point:
                rad = parse_vec(e['light_radius']) if 'light_radius' in e else [float(e.get('light', 300))] * 3
                center = parse_vec(e.get('light_center', '0 0 0'))
                lp = [[0.5 / rad[0], 0, 0, 0.5], [0, 0.5 / rad[1], 0, 0.5], [0, 0, 0.5 / rad[2], 0.5], [0, 0, 0, 1]]
                zscale = 1.0
            else:
                target = parse_vec(e['light_target'])
                up = parse_vec(e['light_up'])
                right = parse_vec(e['light_right'])
                start = parse_vec(e.get('light_start', '0 0 0'))
                end = parse_vec(e['light_end']) if 'light_end' in e else target
                center = [0, 0, 0]
                lp, zscale = spot_projection(target, up, right, start, end)
            shader_name = e.get('texture', 'lights/squarelight1').lower()
            lmat = self.material(shader_name)
            if lmat is None:
                lmat = self.material('lights/defaultpointlight' if point else 'lights/defaultprojectedlight')
            if lmat is None or lmat.fog_light or lmat.blend_light:
                continue  # fog and blend lights: not yet
            parms = eval_parms(e)
            falloff_key = lmat.light_falloff
            if falloff_key is None:
                dm = self.material('lights/defaultpointlight' if point else 'lights/defaultprojectedlight')
                falloff_key = dm.light_falloff if dm else '_nofalloff'
            # old style lightProject: rows S, T, Q (= localProject row 3), falloff (row 2 * zScale)
            lproj_local = [lp[0], lp[1], lp[3], [x * zscale for x in lp[2]]]
            R = [[axis[0][0], axis[1][0], axis[2][0]], [axis[0][1], axis[1][1], axis[2][1]],
                 [axis[0][2], axis[1][2], axis[2][2]]]
            lproj = []
            for pl in lproj_local:
                n = [sum(R[r][k] * pl[k] for k in range(3)) for r in range(3)]
                lproj.append(n + [pl[3] - dot3(origin, n)])
            if parallel:
                d = norm3(center) if any(center) else [0, 0, 1]
                gorigin = [origin[k] + d[k] * 100000.0 for k in range(3)]
            else:
                gorigin = [origin[k] + sum(axis[j][k] * center[j] for j in range(3)) for k in range(3)]
            light_matrix = [[R[0][0], R[0][1], R[0][2], origin[0]], [R[1][0], R[1][1], R[1][2], origin[1]],
                            [R[2][0], R[2][1], R[2][2], origin[2]], [0, 0, 0, 1]]
            base_project = mmul(lp, inverse(light_matrix))
            inv_base = inverse(base_project)
            corners = []
            for i in range(8):
                c = mvec(inv_base, [float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1), 1.0])
                ww = c[3] if abs(c[3]) > 1e-9 else 1e-9
                corners.append([c[0] / ww, c[1] / ww, c[2] / ww])
            lb = [min(c[0] for c in corners), min(c[1] for c in corners), min(c[2] for c in corners),
                  max(c[0] for c in corners), max(c[1] for c in corners), max(c[2] for c in corners)]
            if v.cull_bounds(lb):
                continue
            areas = w.bounds_areas(lb)
            arect = None
            for a in areas:
                arect = rect_or(arect, rects.get(a))
            if arect is None:
                continue
            inside = all(lb[k] <= v.origin[k] <= lb[k + 3] for k in range(3))
            lrect = [0, 0, W - 1, H - 1] if inside else v.project_points(corners)
            scissor = rect_and(arect, lrect)
            if scissor is None:
                continue
            zrange = clip_zrange([mvec(v.mvp_world, list(c) + [1.0]) for c in corners])
            if zrange is None or zrange[0] >= zrange[1]:
                continue
            spectrum = lmat.spectrum
            inter, trans = [], []
            for s in surfs:
                mat = s['mat']
                if mat.spectrum != spectrum or not mat.receives_lighting():
                    continue
                if not bounds_overlap(s['wb'], lb):
                    continue
                if rect_and(s['rect'], scissor) is None:
                    continue
                (trans if mat.coverage == M.MC_TRANSLUCENT else inter).append(s)
            if not inter and not trans:
                continue
            shadows = []
            if e.get('noshadows', '0') != '1' and not lmat.no_shadows and not self.args.no_shadows:
                world_space = self.world_space
                pm = w.proc['models'].get('_prelight_' + e.get('name', '').lower())
                prelight = pm is not None and pm['surfaces'] and 'shadowVerts' in pm['surfaces'][0]
                if prelight:
                    ps = pm['surfaces'][0]
                    sb = ps['bounds']
                    if not v.cull_bounds(sb):
                        ins = all(sb[k] - ZNEAR <= v.origin[k] <= sb[k + 3] + ZNEAR for k in range(3)) and inside
                        vo, io = self.shadow_geo(ps)
                        shadows.append({'space': world_space, 'vo': vo, 'io': io, 'n': ps['numIndexes'], 'zfail': ins})
                # dynamic shadows: entity models, and the world when there is no prelight volume
                casters = [(m['model'], m['space'], m['ent']) for m in self.ent_models
                           if bounds_overlap(m['wb'], lb) and m['ent'].get('noshadows', '0') != '1']
                if not prelight:
                    for a in areas:
                        am = w.proc['models'].get('_area%d' % a)
                        if am is not None:
                            casters.append((am, world_space, None))
                for model, sp, ent in casters:
                    if ent is not None and ent is e:
                        continue  # a light's own fixture model does not shadow it
                    local_light = sp.to_local_point(gorigin)
                    for surf in model['surfaces']:
                        mat = self.material(surf['material'])
                        if mat is None or not mat.casts_shadows() or 'verts' not in surf:
                            continue
                        swb = sp.bounds_to_world(surf['bounds'])
                        if not bounds_overlap(swb, lb):
                            continue
                        zfail = self.args.force_zfail or segment_hits_bounds(
                            gorigin, v.origin, [swb[k] - ZNEAR * 2 for k in range(3)] + [swb[k + 3] + ZNEAR * 2 for k in range(3)])
                        sh = self.dynamic_shadow(surf, local_light, zfail)
                        if sh is not None:
                            vo, io, n = sh
                            shadows.append({'space': sp, 'vo': vo, 'io': io, 'n': n, 'zfail': zfail})
            out.append({'ent': e, 'mat': lmat, 'parms': parms, 'lproj': lproj, 'origin': gorigin, 'scissor': scissor,
                        'zrange': zrange,
                        'falloff': falloff_key, 'inter': inter, 'trans': trans, 'shadows': shadows, 'point': point,
                        'inv_base': inv_base})
        return out

    # ------------------------------------------------ the backend passes
    def env(self, parms):
        return {'time': self.time, 'parms': parms}

    def stage_image(self, st, usage):
        return self.images.find(st.image or '_default', usage, st.cube, st.repeat, st.filter)

    def emit(self, world_space):
        be = self.backend = Backend(self.progs)
        v = self.view
        full = [0, 0, W - 1, H - 1]
        p_depth = self.progs.get('depth')
        p_texcolor = self.progs.get('texture_color')
        p_texgen = self.progs.get('texture_color_texgen')
        p_inter = self.progs.get('interaction')
        p_inter_amb = self.progs.get('interactionambient')
        p_shadow = self.progs.get('shadow', has_fragment=False)

        # RB_DrawViewInternal: viewport, scissor, GLS_DEFAULT, clear depth + stencil 128, cull front
        be.mark('view')
        be.gl_scissor(full)
        be.gl_state(0)
        be.clear(False, True, True, STENCIL_SHADOW_TEST_VALUE)
        be.gl_cull(CT_FRONT_SIDED)
        be.set_parm('rpGlobalEyePos', v.origin + [1.0])
        ob = LIGHT_SCALE * 0.5
        be.set_parm('rpOverbright', [ob] * 4)
        for i, n in enumerate(('rpProjectionMatrixX', 'rpProjectionMatrixY', 'rpProjectionMatrixZ',
                               'rpProjectionMatrixW')):
            be.set_parm(n, v.proj[i])

        # sort like R_SortDrawSurfs: by material sort, then in list order
        order = sorted(range(len(self.surfs)), key=lambda i: (self.surfs[i]['mat'].sort, i))
        surfs = [self.surfs[i] for i in order]
        for s in surfs:
            s['regs'] = self.env(s['parms'])

        # ---- RB_FillDepthBufferFast
        be.mark('depth')
        be.gl_state(0)
        cur_space = None
        perforated = []
        subviews = [s for s in surfs if s['mat'].sort == M.SS_SUBVIEW]
        for s in surfs:
            mat = s['mat']
            if mat.sort == M.SS_SUBVIEW:
                continue
            if mat.coverage == M.MC_TRANSLUCENT:
                continue
            if mat.coverage == M.MC_PERFORATED:
                perforated.append(s)
                continue
            if s['space'] is not cur_space:
                be.set_mvp(s['space'].mvp)
                cur_space = s['space']
            be.use(p_depth)
            self.draw_surf(s)
        for s in subviews + perforated:
            self.fill_depth_generic(s, p_depth, p_texcolor)
        be.set_parm('rpAlphaTest', [0, 0, 0, 0])

        # ---- RB_DrawInteractions
        be.mark('interactions')
        for L in self.view_lights:
            self.draw_light(L, p_inter, p_inter_amb, p_shadow)
        be.gl_state(0)
        for i in range(5):
            be.bind(i, -1)
        be.depth_bounds(0.0, 0.0)

        # ---- RB_DrawShaderPasses (ambient stages), up to post process
        be.mark('shader passes')
        for s in surfs:
            mat = s['mat']
            if not mat.has_ambient() or mat.portal_sky:
                continue
            if mat.sort >= M.SS_POST_PROCESS:
                continue  # needs _currentRender: not yet
            self.shader_passes(s, p_texcolor, p_texgen)
        be.gl_cull(CT_FRONT_SIDED)
        be.mark('end')

    def draw_surf(self, s):
        vo, io = self.geo(s['surf'])
        self.backend.draw(LAYOUT_DRAWVERT, vo, io, s['surf']['numIndexes'])

    def fill_depth_generic(self, s, p_depth, p_texcolor):
        be = self.backend
        mat = s['mat']
        env = s['regs']
        be.set_mvp(s['space'].mvp)
        surf_state = 0
        if mat.has_polygon_offset:
            surf_state |= GLS_POLYGON_OFFSET
            be.polygon_offset(0.0, -600.0 * mat.polygon_offset)
        color = [0.0, 0.0, 0.0, 1.0]
        if mat.sort == M.SS_SUBVIEW:
            surf_state |= GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO
            color = [1.0, 1.0, 1.0, 1.0]
        draw_solid = mat.coverage == M.MC_OPAQUE
        if mat.coverage == M.MC_PERFORATED:
            did = False
            for st in mat.stages:
                if st.alpha_test is None or st.condition.ev(env) == 0:
                    continue
                did = True
                color[3] = st.color[3].ev(env)
                if color[3] <= 0:
                    continue
                state = surf_state
                if st.private_offset:
                    be.polygon_offset(0.0, -600.0 * st.private_offset)
                    state |= GLS_POLYGON_OFFSET
                be.set_parm('rpColor', [min(1, max(0, c)) for c in color])
                be.gl_state(state)
                be.set_parm('rpAlphaTest', [st.alpha_test.ev(env)] * 4)
                be.use(p_texcolor)
                set_vertex_color(be, M.SVC_IGNORE)
                be.bind(0, self.stage_image(st, 'coverage' if st.lighting == M.SL_COVERAGE else 'default'))
                self.texture_matrix(st, env)
                be.set_parm('rpTexGen0Enabled', [0, 0, 0, 0])
                self.draw_surf(s)
                if st.private_offset:
                    be.polygon_offset(0.0, -600.0 * mat.polygon_offset)
            if not did:
                draw_solid = True
        if draw_solid:
            if mat.sort == M.SS_SUBVIEW:
                return  # mirrors and remote views: not yet
            be.use(p_depth)
            be.gl_state(surf_state | GLS_ALPHAMASK)
            self.draw_surf(s)

    def texture_matrix(self, st, env):
        be = self.backend
        s_row, t_row = [1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]
        if st.matrix is not None:
            m = [[e.ev(env) for e in row] for row in st.matrix]
            tx, ty = m[0][2], m[1][2]
            if tx < -40 or tx > 40:
                tx -= int(tx)
            if ty < -40 or ty > 40:
                ty -= int(ty)
            s_row = [m[0][0], m[0][1], 0.0, tx]
            t_row = [m[1][0], m[1][1], 0.0, ty]
        be.set_parm('rpTextureMatrixS', s_row)
        be.set_parm('rpTextureMatrixT', t_row)

    def draw_light(self, L, p_inter, p_inter_amb, p_shadow):
        be = self.backend
        v = self.view
        be.mark('light ' + L['ent'].get('name', '?'))
        lmat = L['mat']
        lenv = self.env(L['parms'])
        sc = L['scissor']
        if not self.args.no_depth_bounds:
            be.depth_bounds(*L['zrange'])  # the light's depth range for its shadows and interactions
        perform_stencil = bool(L['shadows'])
        if perform_stencil:
            # RB_DrawInteractions without stencil select: clear whole 16 pixel tiles to 128
            r = [sc[0] & ~15, sc[1] & ~15, (sc[2] + 15) & ~15, (sc[3] + 15) & ~15]
            be.gl_scissor(r)
            be.gl_state(0)
            be.clear(False, False, True, STENCIL_SHADOW_TEST_VALUE)
            # RB_StencilShadowPass
            be.use(p_shadow)
            be.bind(0, -1)
            be.polygon_offset(0.0, 1.0)  # r_shadowPolygonFactor 0, -r_shadowPolygonOffset (-1)
            be.gl_state(GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS | GLS_STENCIL_OP_PASS_INCR |
                        stencil_ref(STENCIL_SHADOW_TEST_VALUE) | stencil_mask(STENCIL_SHADOW_MASK_VALUE) |
                        GLS_POLYGON_OFFSET)
            be.gl_cull(CT_TWO_SIDED)
            be.gl_scissor(sc)
            cur = None
            for sh in L['shadows']:
                if sh['space'] is not cur:
                    cur = sh['space']
                    be.set_mvp(cur.mvp)
                    be.set_parm('rpLocalLightOrigin', cur.to_local_point(L['origin']) + [0.0])
                if not sh['zfail']:
                    be.stencil_separate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR)
                    be.stencil_separate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR)
                    be.draw(LAYOUT_SHADOWVERT, sh['vo'], sh['io'], sh['n'])
                else:
                    # preload + z-pass (r_useStencilShadowPreload 1)
                    be.stencil_separate(GL_FRONT, GL_KEEP, GL_DECR, GL_DECR)
                    be.stencil_separate(GL_BACK, GL_KEEP, GL_INCR, GL_INCR)
                    be.draw(LAYOUT_SHADOWVERT, sh['vo'], sh['io'], sh['n'])
                    be.stencil_separate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR)
                    be.stencil_separate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR)
                    be.draw(LAYOUT_SHADOWVERT, sh['vo'], sh['io'], sh['n'])
            be.gl_cull(CT_FRONT_SIDED)
        self.render_interactions(L, L['inter'], GLS_DEPTHFUNC_EQUAL, perform_stencil, p_inter, p_inter_amb, lenv)
        if L['trans']:
            be.depth_bounds(0.0, 0.0)  # translucent surfaces did not write depth
            self.render_interactions(L, L['trans'], GLS_DEPTHFUNC_LESS, False, p_inter, p_inter_amb, lenv)

    def render_interactions(self, L, slist, depth_func, stencil, p_inter, p_inter_amb, lenv):
        be = self.backend
        v = self.view
        lmat = L['mat']
        be.gl_scissor(L['scissor'])
        if stencil:
            be.gl_state(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depth_func | GLS_STENCIL_FUNC_EQUAL |
                        stencil_ref(STENCIL_SHADOW_TEST_VALUE) | stencil_mask(STENCIL_SHADOW_MASK_VALUE))
        else:
            be.gl_state(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | depth_func | GLS_STENCIL_FUNC_ALWAYS)
        for lst in lmat.stages:
            if lst.condition.ev(lenv) == 0:
                continue
            lc = [LIGHT_SCALE * lst.color[i].ev(lenv) for i in range(3)] + [lst.color[3].ev(lenv)]
            diffuse_color = lc
            specular_color = [c * 2.0 for c in lc]
            ltm = None
            if lst.matrix is not None:
                m = [[e.ev(lenv) for e in row] for row in lst.matrix]
                tx, ty = m[0][2], m[1][2]
                if tx < -40 or tx > 40:
                    tx -= int(tx)
                if ty < -40 or ty > 40:
                    ty -= int(ty)
                ltm = [[m[0][0], m[0][1], 0, tx], [m[1][0], m[1][1], 0, ty], [0, 0, 1, 0], [0, 0, 0, 1]]
            wl = self.args.white_lights
            be.bind(1, self.images.find('_white') if wl & 1 else
                    self.images.find(L['falloff'], 'light', False, M.TR_CLAMP_TO_ZERO, M.TF_DEFAULT))
            be.bind(2, self.images.find('_white') if wl & 2 else
                    self.images.find(lst.image or '_white', 'light', lst.cube, lst.repeat, lst.filter))
            # RB_SetupForFastPathInteractions
            for n in ('rpBumpMatrixS', 'rpDiffuseMatrixS', 'rpSpecularMatrixS'):
                be.set_parm(n, [1, 0, 0, 0])
            for n in ('rpBumpMatrixT', 'rpDiffuseMatrixT', 'rpSpecularMatrixT'):
                be.set_parm(n, [0, 1, 0, 0])
            set_vertex_color(be, M.SVC_IGNORE)
            be.set_parm('rpDiffuseModifier', diffuse_color)
            be.set_parm('rpSpecularModifier', specular_color)
            cur_space = None
            for s in slist:
                be.use(p_inter_amb if lmat.ambient_light else p_inter)
                mat = s['mat']
                env = s['regs']
                sp = s['space']
                if sp is not cur_space:
                    cur_space = sp
                    be.set_mvp(sp.mvp)
                    be.set_parm('rpLocalLightOrigin', sp.to_local_point(L['origin']) + [0.0])
                    be.set_parm('rpLocalViewOrigin', sp.to_local_point(v.origin) + [1.0])
                    lp = [sp.to_local_plane(pl) for pl in L['lproj']]
                    if ltm is not None:
                        lp = bake_texture_matrix(lp, ltm)
                    be.set_parm('rpLightProjectionS', lp[0])
                    be.set_parm('rpLightProjectionT', lp[1])
                    be.set_parm('rpLightProjectionQ', lp[2])
                    be.set_parm('rpLightFalloffS', lp[3])
                # the general path (r_skipInteractionFastPath 1)
                inter = {'bump': None, 'diffuse': None, 'specular': None, 'dc': [0] * 4, 'sc': [0] * 4,
                         'bm': None, 'dm': None, 'sm': None, 'vc': M.SVC_IGNORE}
                for st in mat.stages:
                    if st.lighting in (M.SL_COVERAGE, M.SL_AMBIENT):
                        continue
                    if st.condition.ev(env) == 0:
                        continue
                    if st.lighting == M.SL_BUMP:
                        if inter['bump'] is not None:
                            self.single_interaction(inter, s, L)
                        inter['bump'] = self.stage_image(st, 'bump')
                        inter['diffuse'] = inter['specular'] = None
                        inter['bm'] = stage_matrix(st, env)
                    elif st.lighting == M.SL_DIFFUSE:
                        if inter['diffuse'] is not None:
                            self.single_interaction(inter, s, L)
                        inter['diffuse'] = self.stage_image(st, 'diffuse')
                        inter['vc'] = st.vertex_color
                        inter['dm'] = stage_matrix(st, env)
                        inter['dc'] = [min(1, max(0, st.color[i].ev(env))) * diffuse_color[i] for i in range(4)]
                    elif st.lighting == M.SL_SPECULAR:
                        if inter['specular'] is not None:
                            self.single_interaction(inter, s, L)
                        inter['specular'] = self.stage_image(st, 'specular')
                        inter['vc'] = st.vertex_color
                        inter['sm'] = stage_matrix(st, env)
                        inter['sc'] = [min(1, max(0, st.color[i].ev(env))) * specular_color[i] for i in range(4)]
                self.single_interaction(inter, s, L)

    def single_interaction(self, inter, s, L):
        """RB_DrawSingleInteraction"""
        be = self.backend
        if inter['bump'] is None:
            return
        black = self.images.find('_black')
        diffuse = inter['diffuse'] if inter['diffuse'] is not None else black
        specular = inter['specular'] if inter['specular'] is not None and not L['mat'].ambient_light else black
        dblack = diffuse == black or all(c <= 0 for c in inter['dc'][:3])
        sblack = specular == black or all(c <= 0 for c in inter['sc'][:3])
        if dblack and sblack:
            return
        ident_s, ident_t = [1, 0, 0, 0], [0, 1, 0, 0]
        for key, ns, nt in (('bm', 'rpBumpMatrixS', 'rpBumpMatrixT'), ('dm', 'rpDiffuseMatrixS', 'rpDiffuseMatrixT'),
                            ('sm', 'rpSpecularMatrixS', 'rpSpecularMatrixT')):
            m = inter[key]
            be.set_parm(ns, m[0] if m else ident_s)
            be.set_parm(nt, m[1] if m else ident_t)
        set_vertex_color(be, inter['vc'])
        be.set_parm('rpDiffuseModifier', inter['dc'])
        be.set_parm('rpSpecularModifier', inter['sc'])
        be.bind(0, inter['bump'])
        be.bind(3, diffuse)
        be.bind(4, specular)
        self.draw_surf(s)

    def shader_passes(self, s, p_texcolor, p_texgen):
        be = self.backend
        v = self.view
        mat = s['mat']
        env = s['regs']
        sp = s['space']
        be.set_mvp(sp.mvp)
        be.set_parm('rpLocalViewOrigin', sp.to_local_point(v.origin) + [1.0])
        be.gl_scissor(s['rect'])
        be.gl_cull({M.CT_FRONT: CT_FRONT_SIDED, M.CT_BACK: CT_BACK_SIDED, M.CT_TWO: CT_TWO_SIDED}[mat.cull])
        surf_state = 0
        if mat.has_polygon_offset:
            be.polygon_offset(0.0, -600.0 * mat.polygon_offset)
            surf_state = GLS_POLYGON_OFFSET
        for st in mat.stages:
            if st.condition.ev(env) == 0 or st.lighting != M.SL_AMBIENT:
                continue
            state = surf_state | stage_draw_state(st, mat)
            if (state & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE):
                continue
            if st.program is not None or st.cinematic or st.dynamic:
                self.warnings.add('skipped a %s stage in %s' % ('program' if st.program else 'dynamic', mat.name))
                continue
            if st.texgen not in (M.TG_EXPLICIT, M.TG_SCREEN, M.TG_SCREEN2):
                self.warnings.add('skipped texgen %d in %s' % (st.texgen, mat.name))
                continue
            color = [st.color[i].ev(env) for i in range(4)]
            if (state & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE) and \
                    color[0] <= 0 and color[1] <= 0 and color[2] <= 0:
                continue
            if (state & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA |
                                                                       GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA) and color[3] <= 0:
                continue
            be.set_parm('rpColor', [min(1, max(0, c)) for c in color])
            texgen = st.texgen in (M.TG_SCREEN, M.TG_SCREEN2)
            be.use(p_texgen if texgen else p_texcolor)
            set_vertex_color(be, st.vertex_color)
            be.bind(0, self.stage_image(st, 'default'))
            if st.private_offset:
                be.polygon_offset(0.0, -600.0 * st.private_offset)
                state |= GLS_POLYGON_OFFSET
            be.gl_state(state)
            self.texture_matrix(st, env)
            if texgen:
                mvp = mmul(v.proj, sp.model_view)
                be.set_parm('rpTexGen0S', mvp[0])
                be.set_parm('rpTexGen0T', mvp[1])
                be.set_parm('rpTexGen0Q', mvp[3])
                be.set_parm('rpTexGen0Enabled', [1, 1, 1, 1])
            else:
                be.set_parm('rpTexGen0Enabled', [0, 0, 0, 0])
            self.draw_surf(s)
            if st.private_offset:
                be.polygon_offset(0.0, -600.0 * mat.polygon_offset)

    # ------------------------------------------------ output
    def write(self, path):
        be = self.backend
        out = bytearray(b'BFGS')
        out += struct.pack('<IIIIII', 2, W, H, len(self.images.textures), len(self.progs.list), len(be.cmds))
        for t in self.images.textures:
            out += texture_record(t)
        for p in self.progs.list:
            for s in (p['vs'], p['fs']):
                b = s.encode('latin1')
                out += struct.pack('<I', len(b)) + b
        out += struct.pack('<II', len(self.vb), len(self.ib))
        out += self.vb + self.ib
        out += be.cmds
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(out)
        print(f'{path}: {len(out) / 1e6:.1f} MB, {len(self.images.textures)} textures, {len(self.progs.list)} programs, '
              f'{be.counts["draws"]} draws, {be.counts["tris"]} triangles')


def stage_matrix(st, env):
    if st.matrix is None:
        return None
    m = [[e.ev(env) for e in row] for row in st.matrix]
    tx, ty = m[0][2], m[1][2]
    if tx < -40 or tx > 40:
        tx -= int(tx)
    if ty < -40 or ty > 40:
        ty -= int(ty)
    return [[m[0][0], m[0][1], 0.0, tx], [m[1][0], m[1][1], 0.0, ty]]


def stage_draw_state(st, mat):
    bits = GLS_SRCBLEND.get(st.src_blend, 0) | GLS_DSTBLEND.get(st.dst_blend, 0)
    if st.mask & 1:
        bits |= GLS_REDMASK
    if st.mask & 2:
        bits |= GLS_GREENMASK
    if st.mask & 4:
        bits |= GLS_BLUEMASK
    if st.mask & 8:
        bits |= GLS_ALPHAMASK
    if st.mask & 16:
        bits |= GLS_DEPTHMASK
    if mat.sort == M.SS_POST_PROCESS:
        bits |= GLS_DEPTHFUNC_LESS
    elif mat.coverage == M.MC_TRANSLUCENT or st.ignore_alpha_test:
        bits |= GLS_DEPTHFUNC_LESS | GLS_DEPTHMASK
    else:
        bits |= GLS_DEPTHFUNC_EQUAL | GLS_DEPTHMASK
    return bits


def set_vertex_color(be, svc):
    if svc == M.SVC_IGNORE:
        be.set_parm('rpVertexColorModulate', [0, 0, 0, 0])
        be.set_parm('rpVertexColorAdd', [1, 1, 1, 1])
    elif svc == M.SVC_MODULATE:
        be.set_parm('rpVertexColorModulate', [1, 1, 1, 1])
        be.set_parm('rpVertexColorAdd', [0, 0, 0, 0])
    else:
        be.set_parm('rpVertexColorModulate', [-1, -1, -1, -1])
        be.set_parm('rpVertexColorAdd', [1, 1, 1, 1])


def bake_texture_matrix(lp, tm):
    """RB_BakeTextureMatrixIntoTexgen: S, T rows of (texture matrix * [S; T; 0; Q])"""
    gen = [lp[0], lp[1], [0, 0, 0, 0], lp[2]]
    s = [sum(tm[0][k] * gen[k][c] for k in range(4)) for c in range(4)]
    t = [sum(tm[1][k] * gen[k][c] for k in range(4)) for c in range(4)]
    return [s, t, lp[2], lp[3]]


def spot_projection(target, up, right, start, end):
    """R_ComputeSpotLightProjectionMatrix: rows of localProject and zScale"""
    tl = math.sqrt(dot3(target, target))
    nt = [x / tl for x in target]
    nr = [x * (0.5 * tl / dot3(right, right)) for x in right]
    nu = [x * (-0.5 * tl / dot3(up, up)) for x in up]
    lp = [nr + [0.0], nu + [0.0], [0, 0, 0, 0], nt + [0.0]]
    znear = max(dot3(start, nt), 8.0)
    zfar = max(dot3(end, nt), 16.0)
    zs = (znear + zfar) / zfar
    lp[2] = [nt[0] * zs, nt[1] * zs, nt[2] * zs, -znear * zs]
    pt = mvec(lp, target + [1.0])
    ofs0 = 0.5 - pt[0] / pt[3]
    ofs1 = 0.5 - pt[1] / pt[3]
    lp[0] = [lp[0][k] + ofs0 * lp[3][k] for k in range(4)]
    lp[1] = [lp[1][k] + ofs1 * lp[3][k] for k in range(4)]
    return lp, 1.0 / (znear + zfar)


# texture records: what glTexImage / glCompressedTexImage get
GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP = 0x0DE1, 0x8513
GL_RGBA8, GL_RGB, GL_RGBA, GL_R8, GL_RED, GL_RG8, GL_RG = 0x8058, 0x1907, 0x1908, 0x8229, 0x1903, 0x822B, 0x8227
GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT_5_6_5 = 0x1401, 0x8363
GL_DXT1, GL_DXT5 = 0x83F1, 0x83F3
GL_ZERO_, GL_ONE_, GL_RED_, GL_GREEN_, GL_BLUE_, GL_ALPHA_ = 0, 1, 0x1903, 0x1904, 0x1905, 0x1906
GL_LINEAR, GL_NEAREST, GL_LINEAR_MIPMAP_LINEAR = 0x2601, 0x2600, 0x2703
GL_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_BORDER = 0x2901, 0x812F, 0x812D


def texture_record(t):
    bi = t['bimage']
    fmt = bi['format']
    swz = (GL_RED_, GL_GREEN_, GL_BLUE_, GL_ALPHA_)
    swap = 0
    compressed = 0
    if fmt == A.FMT_RGBA8:
        ifmt, dfmt, dtype = GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE
    elif fmt == A.FMT_XRGB8:
        ifmt, dfmt, dtype = GL_RGB, GL_RGBA, GL_UNSIGNED_BYTE
    elif fmt == A.FMT_RGB565:
        ifmt, dfmt, dtype, swap = GL_RGB, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 1
    elif fmt == A.FMT_ALPHA:
        ifmt, dfmt, dtype = GL_R8, GL_RED, GL_UNSIGNED_BYTE
        swz = (GL_ONE_, GL_ONE_, GL_ONE_, GL_RED_)
    elif fmt == A.FMT_L8A8:
        ifmt, dfmt, dtype = GL_RG8, GL_RG, GL_UNSIGNED_BYTE
        swz = (GL_RED_, GL_RED_, GL_RED_, GL_GREEN_)
    elif fmt == A.FMT_LUM8:
        ifmt, dfmt, dtype = GL_R8, GL_RED, GL_UNSIGNED_BYTE
        swz = (GL_RED_, GL_RED_, GL_RED_, GL_ONE_)
    elif fmt == A.FMT_INT8:
        ifmt, dfmt, dtype = GL_R8, GL_RED, GL_UNSIGNED_BYTE
        swz = (GL_RED_, GL_RED_, GL_RED_, GL_RED_)
    elif fmt == A.FMT_DXT1:
        ifmt, dfmt, dtype, compressed = GL_DXT1, GL_RGBA, GL_UNSIGNED_BYTE, 1
    elif fmt == A.FMT_DXT5:
        ifmt, dfmt, dtype, compressed = GL_DXT5, GL_RGBA, GL_UNSIGNED_BYTE, 1
    else:
        raise ValueError('image format %d (%s)' % (fmt, t['name']))
    if bi['colorFormat'] == 3:  # CFM_GREEN_ALPHA
        swz = (GL_ONE_, GL_ONE_, GL_ONE_, GL_GREEN_)
    filt = t['filter']
    levels = bi['levels']
    if filt == M.TF_DEFAULT:
        minf, magf = GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR
    elif filt == M.TF_LINEAR:
        minf, magf = GL_LINEAR, GL_LINEAR
    else:
        minf, magf = GL_NEAREST, GL_NEAREST
    rep = t['repeat']
    border = (0.0, 0.0, 0.0, 0.0)
    if rep == M.TR_REPEAT:
        wrap = GL_REPEAT
    elif rep == M.TR_CLAMP:
        wrap = GL_CLAMP_TO_EDGE
    elif rep == M.TR_CLAMP_TO_ZERO:
        wrap = GL_CLAMP_TO_BORDER
    else:
        wrap, border = GL_CLAMP_TO_BORDER, (1.0, 1.0, 1.0, 0.0)
    aniso = 8.0 if filt == M.TF_DEFAULT else 1.0
    target = GL_TEXTURE_CUBE_MAP if bi['type'] == A.TT_CUBIC else GL_TEXTURE_2D
    out = struct.pack('<IIIIIIIIIIiiiiIIIffffff', target, ifmt, dfmt, dtype, compressed, swap, bi['width'],
                      bi['height'], levels, len(bi['images']), *swz, minf, magf, wrap, *border, 0.5, aniso)
    for level, z, w, h, data in bi['images']:
        out += struct.pack('<IIIII', level, z, w, h, len(data)) + data
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', required=True, help='the DOOM 3 BFG Edition install directory')
    ap.add_argument('--map', required=True, help='map name, e.g. alphalabs1 (base/maps/<name>.resources)')
    ap.add_argument('--pos', type=float, nargs=3, required=True, help='camera position (game units)')
    ap.add_argument('--yaw', type=float, default=0.0, help='camera yaw, degrees (getviewpos order)')
    ap.add_argument('--pitch', type=float, default=0.0)
    ap.add_argument('--time', type=float, default=10.0, help='material time, seconds')
    ap.add_argument('--out', required=True)
    ap.add_argument('--no-shadows', action='store_true', help='debug: leave out the shadow volumes')
    ap.add_argument('--no-depth-bounds', action='store_true', help='r_useLightDepthBounds 0 (GL_EXT_depth_bounds_test off)')
    ap.add_argument('--white-lights', type=int, default=0, help='debug: 1 white falloff, 2 white projection images')
    ap.add_argument('--only', nargs='*', default=[], help='debug: only materials containing one of these')
    ap.add_argument('--exclude', nargs='*', default=[], help='debug: leave out materials containing one of these')
    ap.add_argument('--no-entities', action='store_true', help='debug: world geometry only')
    ap.add_argument('--force-zfail', action='store_true', help='debug: z-fail (capped) dynamic shadow volumes')
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    b = Bake(a)
    b.build()
    b.write(a.out)
    if a.verbose:
        for w in sorted(b.warnings):
            print('warning:', w)
    elif b.warnings:
        print(f'{len(b.warnings)} warnings (--verbose lists them)')


if __name__ == '__main__':
    main()
