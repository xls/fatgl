"""Doom 3 BFG Edition asset readers: .resources containers, generated/maps/*.bproc
(the binary processed map), generated/images/*.bimage and the .map entities.
The formats follow the GPL source (neo/renderer/RenderWorld_load.cpp, Model.cpp
LoadBinaryModel, BinaryImageData.h, framework/File.cpp)."""
import os
import struct


class Resources:
    """The .resources containers of a game install; a later file overrides an earlier one."""

    def __init__(self, paths):
        self.files = {}
        self.handles = {}
        for p in paths:
            with open(p, 'rb') as f:
                magic, toff, tlen = struct.unpack('>III', f.read(12))
                f.seek(toff)
                t = f.read(tlen)
            n = struct.unpack('>I', t[:4])[0]
            o = 4
            for _ in range(n):
                ln = struct.unpack('<I', t[o:o + 4])[0]
                o += 4
                name = t[o:o + ln].decode('latin1').lower().replace('\\', '/')
                o += ln
                off, size = struct.unpack('>II', t[o:o + 8])
                o += 8
                self.files[name] = (p, off, size)

    def has(self, name):
        return name.lower() in self.files

    def read(self, name):
        p, off, size = self.files[name.lower()]
        f = self.handles.get(p)
        if f is None:
            f = self.handles[p] = open(p, 'rb')
        f.seek(off)
        return f.read(size)

    def names(self, prefix=''):
        return [n for n in self.files if n.startswith(prefix)]


def game_resources(base, mapname):
    """_common (+ _ordered) and the map's own pack, as the game mounts them."""
    paths = [os.path.join(base, '_common.resources'), os.path.join(base, '_ordered.resources')]
    pack = os.path.join(base, 'maps', mapname.split('/')[-1] + '.resources')
    if not os.path.exists(pack):
        raise SystemExit(f'{pack} not found')
    return Resources([p for p in paths if os.path.exists(p)] + [pack])


# ---------------------------------------------------------------- bproc

class _R:
    def __init__(self, d):
        self.d, self.o = d, 0

    def big(self, fmt):
        v = struct.unpack_from('>' + fmt, self.d, self.o)
        self.o += struct.calcsize('>' + fmt)
        return v if len(v) > 1 else v[0]

    def little(self, fmt):
        v = struct.unpack_from('<' + fmt, self.d, self.o)
        self.o += struct.calcsize('<' + fmt)
        return v if len(v) > 1 else v[0]

    def string(self):
        n = self.little('i')
        s = self.d[self.o:self.o + n].decode('latin1')
        self.o += n
        return s

    def raw(self, n):
        b = self.d[self.o:self.o + n]
        self.o += n
        return b


def _swap_drawverts(raw, n, vec_big):
    """idDrawVert as stored: xyz floats (little endian in the bproc, big endian in
    generated/rendermodels), st big endian halves, 16 bytes of normal / tangent /
    color / color2. Returns the in-memory (little endian) layout."""
    out = bytearray(raw)
    for i in range(n):
        b = i * 32
        if vec_big:
            for k in range(0, 12, 4):
                out[b + k:b + k + 4] = raw[b + k:b + k + 4][::-1]
        o = b + 12
        out[o], out[o + 1], out[o + 2], out[o + 3] = raw[o + 1], raw[o], raw[o + 3], raw[o + 2]
    return bytes(out)


def _sane(v):
    return all(x == x and abs(x) < 1e7 for x in v) and v[0] <= v[3] and v[1] <= v[4] and v[2] <= v[5]


def _read_model(r, named=True):
    m = {'name': r.string() if named else ''}
    magic = r.big('I')
    if magic >> 8 != 0x42524D:
        raise ValueError('bad model magic %x' % magic)
    r.big('q')
    surfs = []
    for _ in range(r.big('i')):
        s = {'id': r.big('i'), 'material': r.string().lower()}
        if r.big('?'):
            o = r.o
            s['bounds'] = r.little('6f')
            vec_big = not _sane(s['bounds'])
            if vec_big:
                r.o = o
                s['bounds'] = r.big('6f')
            r.big('i')
            r.big('????')
            nv = r.big('i')
            s['numVerts'] = nv
            if r.big('i') > 0:
                s['verts'] = _swap_drawverts(r.raw(nv * 32), nv, vec_big)
            nsh = r.big('i')
            if nsh:
                sv = r.raw(nsh * 16)  # idShadowVert xyzw
                if vec_big:
                    sv = struct.pack('<%df' % (nsh * 4), *struct.unpack('>%df' % (nsh * 4), sv))
                s['shadowVerts'] = sv
            ni = r.big('i')
            idx = r.raw(ni * 2)
            s['indexes'] = struct.pack('<%dH' % ni, *struct.unpack('>%dH' % ni, idx))
            s['numIndexes'] = ni
            if r.big('i') > 0:
                # silIndexes: the indexes with duplicate positions merged (silhouette topology)
                si = r.raw(ni * 2)
                s['silIndexes'] = struct.unpack('>%dH' % ni, si)
            r.raw(r.big('i') * 4)
            r.raw(r.big('i') * 8)
            r.raw(r.big('i') * 8)
            if r.big('?'):
                r.raw(nv * 16)
            s['numShadowIndexesNoFrontCaps'], s['numShadowIndexesNoCaps'], s['shadowCapPlaneBits'] = r.big('iii')
            surfs.append(s)
    m['surfaces'] = surfs
    o = r.o
    m['bounds'] = r.little('6f')
    if not _sane(m['bounds']):
        r.o = o
        m['bounds'] = r.big('6f')
    r.big('iii')
    r.string()
    r.big('?????????')
    return m


def read_render_model(d):
    """generated/rendermodels/*.blwo / .base / .bma: one binary model without the name"""
    r = _R(d)
    return _read_model(r, named=False)


def read_bproc(d):
    r = _R(d)
    if r.big('I') >> 8 != 0x50524F:
        raise ValueError('not a bproc')
    n = r.big('i')
    out = {'name': r.string(), 'models': {}, 'portals': [], 'nodes': [], 'numAreas': 0}
    r.big('q')
    for _ in range(n):
        t = r.string().lower()
        if t in ('model', 'shadowmodel'):
            m = _read_model(r)
            out['models'][m['name'].lower()] = m
        elif t == 'interareaportals':
            out['numAreas'] = r.big('i')
            for _ in range(r.big('i')):
                np, a1, a2 = r.big('iii')
                out['portals'].append((a1, a2, [r.big('3f') for _ in range(np)]))
        elif t == 'nodes':
            for _ in range(r.big('i')):
                out['nodes'].append((r.big('4f'), r.big('ii')))
        else:
            raise ValueError('bproc entry ' + t)
    return out


# ---------------------------------------------------------------- bimage

FMT_RGBA8, FMT_XRGB8, FMT_ALPHA, FMT_L8A8, FMT_LUM8, FMT_INT8, FMT_DXT1, FMT_DXT5 = 1, 2, 3, 4, 5, 6, 7, 8
FMT_RGB565 = 12
TT_2D, TT_CUBIC = 1, 2


def read_bimage(d):
    t, magic, ttype, fmt, cfmt, w, h, levels = struct.unpack_from('>qiiiiiii', d, 0)
    o = 36
    sides = 6 if ttype == TT_CUBIC else 1
    images = []
    for _ in range(levels * sides):
        level, destz, iw, ih, size = struct.unpack_from('>iiiii', d, o)
        o += 20
        images.append((level, destz, iw, ih, d[o:o + size]))
        o += size
    return {'type': ttype, 'format': fmt, 'colorFormat': cfmt, 'width': w, 'height': h, 'levels': levels,
            'images': images}


# ---------------------------------------------------------------- .map entities

def read_map_entities(text):
    """Key / value pairs of every entity; brushes and patches are skipped."""
    ents = []
    i, n, depth = 0, len(text), 0
    cur = None
    key = None
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
            continue
        if c == '"':
            j = text.find('"', i + 1)
            s = text[i + 1:j]
            i = j + 1
            if depth == 1:
                if key is None:
                    key = s
                else:
                    cur[key.lower()] = s
                    key = None
            continue
        if c == '{':
            depth += 1
            if depth == 1:
                cur = {}
                key = None
        elif c == '}':
            depth -= 1
            if depth == 0:
                ents.append(cur)
        i += 1
    return ents
