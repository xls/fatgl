"""Doom 3 materials (.mtr) and tables, the subset the renderer's draw passes use,
following neo/renderer/Material.cpp: stages (bump / diffuse / specular /
ambient / coverage), blend, alpha test, colors, texture matrices, coverage,
sort, cull, polygon offset, shadows. Expressions are evaluated once, at the
scene's time and with the entity's shader parms."""
import math
import re

SL_AMBIENT, SL_BUMP, SL_DIFFUSE, SL_SPECULAR, SL_COVERAGE = range(5)
SVC_IGNORE, SVC_MODULATE, SVC_INVERSE_MODULATE = range(3)
MC_BAD, MC_OPAQUE, MC_PERFORATED, MC_TRANSLUCENT = range(4)
CT_FRONT, CT_BACK, CT_TWO = range(3)
TR_REPEAT, TR_CLAMP, TR_CLAMP_TO_ZERO, TR_CLAMP_TO_ZERO_ALPHA = range(4)
TF_DEFAULT, TF_LINEAR, TF_NEAREST = range(3)
TG_EXPLICIT, TG_DIFFUSE_CUBE, TG_REFLECT_CUBE, TG_SKYBOX_CUBE, TG_WOBBLESKY_CUBE, TG_SCREEN, TG_SCREEN2, TG_GLASSWARP = range(8)

SS_SUBVIEW, SS_GUI, SS_BAD, SS_OPAQUE, SS_PORTAL_SKY, SS_DECAL, SS_FAR, SS_MEDIUM, SS_CLOSE = -3, -2, -1, 0, 1, 2, 3, 4, 5
SS_ALMOST_NEAREST, SS_NEAREST, SS_POST_PROCESS = 6, 7, 100
SORTS = {'subview': SS_SUBVIEW, 'opaque': SS_OPAQUE, 'decal': SS_DECAL, 'far': SS_FAR, 'medium': SS_MEDIUM,
         'close': SS_CLOSE, 'almostnearest': SS_ALMOST_NEAREST, 'nearest': SS_NEAREST, 'postprocess': SS_POST_PROCESS,
         'portalsky': SS_PORTAL_SKY}

GL_ZERO, GL_ONE = 0, 1
GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA = 0x300, 0x301, 0x302, 0x303
GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE = 0x304, 0x305, 0x306, 0x307, 0x308
BLEND_NAMES = {'gl_zero': GL_ZERO, 'gl_one': GL_ONE, 'gl_src_color': GL_SRC_COLOR,
               'gl_one_minus_src_color': GL_ONE_MINUS_SRC_COLOR, 'gl_src_alpha': GL_SRC_ALPHA,
               'gl_one_minus_src_alpha': GL_ONE_MINUS_SRC_ALPHA, 'gl_dst_alpha': GL_DST_ALPHA,
               'gl_one_minus_dst_alpha': GL_ONE_MINUS_DST_ALPHA, 'gl_dst_color': GL_DST_COLOR,
               'gl_one_minus_dst_color': GL_ONE_MINUS_DST_COLOR, 'gl_src_alpha_saturate': GL_SRC_ALPHA_SATURATE}

IMAGE_FUNCS = {'heightmap', 'addnormals', 'smoothnormals', 'add', 'scale', 'invertalpha', 'invertcolor',
               'makeintensity', 'makealpha', 'downsize'}

# ---------------------------------------------------------------- lexer

_TOK = re.compile(r'''
    (?P<ws>[ \t\r]+) | (?P<nl>\n) | (?P<lc>//[^\n]*) | (?P<bc>/\*.*?\*/) |
    (?P<str>"[^"\n]*") |
    (?P<num>(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?) |
    (?P<name>[A-Za-z_/\\:.$][A-Za-z_0-9/\\:.$]*) |
    (?P<punc>&&|\|\||==|!=|>=|<=|[{}()\[\],+\-*/%<>=!&|;#^~?@]) |
    (?P<bad>.)''', re.S | re.X)


class Tok:
    __slots__ = ('s', 'kind', 'line')

    def __init__(self, s, kind, line):
        self.s, self.kind, self.line = s, kind, line

    def __repr__(self):
        return self.s


def tokenize(text):
    out, line = [], 1
    for m in _TOK.finditer(text):
        k = m.lastgroup
        s = m.group()
        if k == 'nl':
            line += 1
        elif k == 'bc':
            line += s.count('\n')
        elif k in ('ws', 'lc', 'bad'):
            pass
        elif k == 'str':
            out.append(Tok(s[1:-1], 'str', line))
        else:
            out.append(Tok(s, 'num' if k == 'num' else ('punc' if k == 'punc' else 'name'), line))
    return out


class Src:
    def __init__(self, toks, name=''):
        self.t, self.i, self.name = toks, 0, name

    def peek(self, k=0):
        j = self.i + k
        return self.t[j] if j < len(self.t) else None

    def next(self):
        if self.i >= len(self.t):
            raise EOFError(self.name)
        t = self.t[self.i]
        self.i += 1
        return t

    def lower(self):
        return self.next().s.lower()

    def expect(self, s):
        t = self.next()
        if t.s != s:
            raise SyntaxError(f'{self.name}: expected {s!r} got {t.s!r} (line {t.line})')

    def accept(self, s):
        t = self.peek()
        if t is not None and t.s == s:
            self.i += 1
            return True
        return False

    def on_line(self, line):
        t = self.peek()
        return t is not None and t.line == line

    def skip_line(self, line):
        while self.on_line(line):
            self.i += 1

    def skip_braced(self):
        """after a '{' was read"""
        depth = 1
        while depth:
            s = self.next().s
            if s == '{':
                depth += 1
            elif s == '}':
                depth -= 1


# ---------------------------------------------------------------- decls

class Decls:
    """Every material and table text of the given .mtr sources."""

    def __init__(self, texts):
        self.materials, self.tables = {}, {}
        for fname, text in texts:
            src = Src(tokenize(text), fname)
            while src.peek() is not None:
                t = src.next()
                if t.s == '{':
                    src.skip_braced()
                    continue
                kind = t.s.lower()
                if kind in ('table', 'material', 'skin', 'particle', 'entitydef', 'sound', 'fx', 'guide'):
                    if src.peek() is None:
                        break
                    name = src.next().s.lower()
                else:
                    name, kind = kind, 'material'
                if src.peek() is None or src.peek().s != '{':
                    continue
                start = src.i
                src.next()
                src.skip_braced()
                body = src.t[start:src.i]
                if kind == 'table':
                    self.tables.setdefault(name, Table(name, body))
                elif kind == 'material':
                    self.materials.setdefault(name, body)


def _float(s):
    """idLexer reads typos like '..5' as the number they end with"""
    m = re.search(r'\d*\.?\d+(?:[eE][-+]?\d+)?', s)
    return float(m.group()) if m else 0.0


class Table:
    def __init__(self, name, body):
        self.snap = self.clamp = False
        self.values = []
        s = Src(body[1:-1], name)
        while s.peek() is not None:
            t = s.next()
            low = t.s.lower()
            if low == 'snap':
                self.snap = True
            elif low == 'clamp':
                self.clamp = True
            elif t.s == '{':
                neg = False
                while True:
                    u = s.next()
                    if u.s == '}':
                        break
                    if u.s == ',':
                        continue
                    if u.s == '-':
                        neg = True
                        continue
                    v = _float(u.s)
                    self.values.append(-v if neg else v)
                    neg = False
        # idDeclTable::Parse copies the first value to the end, so lerping need not wrap
        if self.values:
            self.values.append(self.values[0])

    def lookup(self, index):
        """idDeclTable::TableLookup"""
        vals = self.values
        domain = len(vals) - 1
        if domain <= 1:
            return 1.0
        if self.clamp:
            index *= (domain - 1)
            if index >= domain - 1:
                return vals[domain - 1]
            if index <= 0:
                return vals[0]
            i = int(index)
            frac = index - i
        else:
            index *= domain
            if index < 0:
                index += domain * math.ceil(-index / domain)
            i = int(math.floor(index))
            frac = index - i
            i = i % domain
        if not self.snap:
            return vals[i] * (1.0 - frac) + vals[i + 1] * frac
        return vals[i]


# ---------------------------------------------------------------- expressions
# parsed into small trees, evaluated with (time, parms, globals)

class Expr:
    __slots__ = ('op', 'a', 'b')

    def __init__(self, op, a=None, b=None):
        self.op, self.a, self.b = op, a, b

    def is_const(self):
        if self.op == 'num':
            return True
        if self.op in ('time', 'parm', 'global', 'sound'):
            return False
        if self.op == 'table':
            return self.b.is_const()
        if self.op == 'neg':
            return self.a.is_const()
        return self.a.is_const() and self.b.is_const()

    def ev(self, env):
        op = self.op
        if op == 'num':
            return self.a
        if op == 'time':
            return env['time']
        if op == 'parm':
            return env['parms'][self.a]
        if op == 'global':
            return 0.0
        if op == 'sound':
            return 0.0
        if op == 'neg':
            return -self.a.ev(env)
        if op == 'table':
            return self.a.lookup(self.b.ev(env))
        x, y = self.a.ev(env), self.b.ev(env)
        if op == '+':
            return x + y
        if op == '-':
            return x - y
        if op == '*':
            return x * y
        if op == '/':
            return x / y if y != 0 else 0.0
        if op == '%':
            return float(int(x) % int(y)) if int(y) != 0 else 0.0
        if op == '>':
            return float(x > y)
        if op == '>=':
            return float(x >= y)
        if op == '<':
            return float(x < y)
        if op == '<=':
            return float(x <= y)
        if op == '==':
            return float(x == y)
        if op == '!=':
            return float(x != y)
        if op == '&&':
            return float(x != 0 and y != 0)
        if op == '||':
            return float(x != 0 or y != 0)
        raise ValueError(op)


def C(v):
    return Expr('num', float(v))


PRI = {'*': 4, '/': 4, '%': 4, '+': 3, '-': 3, '>': 2, '>=': 2, '<': 2, '<=': 2, '==': 2, '!=': 2, '&&': 1, '||': 1}


def parse_expr(src, decls, pri=4):
    if pri == 0:
        return _term(src, decls)
    a = parse_expr(src, decls, pri - 1)
    while True:
        t = src.peek()
        if t is None or PRI.get(t.s) != pri:  # idLexer may read '/' as a path name; tokens compare as strings
            return a
        src.next()
        b = parse_expr(src, decls, pri - 1)
        a = Expr(t.s, a, b)


def _term(src, decls):
    t = src.next()
    s = t.s.lower()
    if t.s == '-':
        u = src.peek()
        if u is not None and u.kind == 'num':
            src.next()
            return C(-float(u.s))
        return Expr('neg', _term(src, decls))
    if t.kind == 'num':
        return C(float(t.s))
    if t.s == '(':
        e = parse_expr(src, decls)
        src.expect(')')
        return e
    if s == 'time':
        return Expr('time')
    if s.startswith('parm') and s[4:].isdigit():
        return Expr('parm', int(s[4:]))
    if s.startswith('global') and s[6:].isdigit():
        return Expr('global', int(s[6:]))
    if s == 'fragmentprograms':
        return C(1)
    if s == 'sound':
        return Expr('sound')
    tab = decls.tables.get(s)
    if tab is not None:
        src.expect('[')
        e = parse_expr(src, decls)
        src.expect(']')
        return Expr('table', tab, e)
    raise SyntaxError(f'{src.name}: bad term {t.s!r} line {t.line}')


# ---------------------------------------------------------------- image programs

def parse_image_program(src):
    """R_ParsePastImageProgram: the tokens of one image program. Returns (key, text)
    where key is the generated/images/ base name ("addnormals/a/heightmap/b/3")."""
    parts = []

    def one():
        t = src.next()
        low = t.s.lower()
        if low in IMAGE_FUNCS and src.peek() is not None and src.peek().s == '(':
            src.next()
            parts.append(low)
            while True:
                one()
                if src.accept(','):
                    continue
                if src.peek() is not None and src.peek().kind == 'num':
                    continue
                src.expect(')')
                break
        elif t.kind == 'num':
            parts.append(t.s)
            # numeric arguments may follow a comma
        else:
            nm = t.s.replace('\\', '/').lower()
            nm = re.sub(r'\.(tga|jpg|png|dds|bmp|pcx)$', '', nm)
            parts.append(nm)

    one()
    return '/'.join(parts)


# ---------------------------------------------------------------- materials

class Stage:
    def __init__(self):
        self.lighting = SL_AMBIENT
        self.image = None             # generated image key
        self.cube = False
        self.src_blend, self.dst_blend = GL_ONE, GL_ZERO
        self.has_blend = False
        self.color = [C(1), C(1), C(1), C(1)]
        self.vertex_color = SVC_IGNORE
        self.alpha_test = None
        self.condition = C(1)
        self.matrix = None            # 2x3 of Expr
        self.repeat = TR_REPEAT
        self.filter = TF_DEFAULT
        self.texgen = TG_EXPLICIT
        self.mask = 0                 # 1 r 2 g 4 b 8 a 16 depth
        self.private_offset = 0.0
        self.ignore_alpha_test = False
        self.program = None           # new style stage (vertex / fragment program names)
        self.fragment_maps = []
        self.vertex_parms = []
        self.cinematic = False
        self.dynamic = False
        self.td = None                # texture usage hint


def _mat_mul(old, reg):
    """idMaterial::MultiplyTextureMatrix: old * reg (2x3 affine)"""
    if old is None:
        return reg

    def add(x, y):
        return Expr('+', x, y)

    def mul(x, y):
        return Expr('*', x, y)

    m = [[None] * 3 for _ in range(2)]
    for r in range(2):
        m[r][0] = add(mul(old[r][0], reg[0][0]), mul(old[r][1], reg[1][0]))
        m[r][1] = add(mul(old[r][0], reg[0][1]), mul(old[r][1], reg[1][1]))
        m[r][2] = add(add(mul(old[r][0], reg[0][2]), mul(old[r][1], reg[1][2])), old[r][2])
    return m


class Material:
    def __init__(self, name):
        self.name = name
        self.stages = []
        self.coverage = MC_BAD
        self.sort = SS_BAD
        self.cull = CT_FRONT
        self.polygon_offset = 0.0
        self.has_polygon_offset = False
        self.no_shadows = False
        self.no_self_shadow = False
        self.force_shadows = False
        self.translucent_kw = False
        self.light_falloff = None
        self.fog_light = self.blend_light = self.ambient_light = False
        self.spectrum = 0
        self.deform = None
        self.gui = False
        self.defaulted = False
        self.repeat_default = TR_REPEAT
        self.no_fog = False
        self.portal_sky = False

    # what the renderer asks
    def interaction_stages(self):
        return [s for s in self.stages if s.lighting in (SL_BUMP, SL_DIFFUSE, SL_SPECULAR)]

    def receives_lighting(self):
        return any(s.lighting != SL_AMBIENT and s.lighting != SL_COVERAGE for s in self.stages)

    def has_ambient(self):
        return any(s.lighting == SL_AMBIENT for s in self.stages)

    def is_drawn(self):
        return bool(self.stages) or self.gui

    def casts_shadows(self):
        return (not self.no_shadows and self.coverage != MC_TRANSLUCENT) or self.force_shadows


def parse_material(name, body, decls):
    m = Material(name)
    src = Src(body[1:-1] + [Tok('}', 'punc', 0)], name)
    pd_ambient = 0
    while True:
        t = src.next()
        if t.s == '}':
            break
        low = t.s.lower()
        if t.s == '{':
            st = _parse_stage(src, decls, m)
            if st is not None:
                m.stages.append(st)
                if st.lighting == SL_DIFFUSE and st.alpha_test is not None:
                    cov = _copy_stage(st)
                    st.alpha_test = None
                    cov.lighting = SL_COVERAGE
                    m.stages.append(cov)
                if st.lighting == SL_AMBIENT:
                    pd_ambient += 1
            continue
        line = t.line
        if low.startswith('qer_') or low in ('description', 'materialtype', 'editorimage', 'guide'):
            src.skip_line(line)
        elif low == 'renderbump' or low == 'renderbumpflat':
            src.skip_line(line)
        elif low in ('diffusemap', 'bumpmap', 'specularmap'):
            st = Stage()
            st.lighting = {'diffusemap': SL_DIFFUSE, 'bumpmap': SL_BUMP, 'specularmap': SL_SPECULAR}[low]
            st.image = parse_image_program(src)
            st.repeat = m.repeat_default
            m.stages.append(st)
        elif low == 'lightfalloffimage':
            m.light_falloff = parse_image_program(src)
        elif low == 'translucent':
            m.coverage = MC_TRANSLUCENT
        elif low == 'forceopaque':
            m.coverage = MC_OPAQUE
        elif low == 'twosided':
            m.cull = CT_TWO
            m.no_shadows = True
        elif low == 'backsided':
            m.cull = CT_BACK
            m.no_shadows = True
        elif low == 'noshadows':
            m.no_shadows = True
        elif low == 'noselfshadow':
            m.no_self_shadow = True
        elif low == 'forceshadows':
            m.force_shadows = True
        elif low == 'polygonoffset':
            m.has_polygon_offset = True
            m.polygon_offset = 1.0
            if src.on_line(line):
                m.polygon_offset = _number(src)
        elif low == 'decal_macro':
            m.has_polygon_offset = True
            m.polygon_offset = 1.0
            m.sort = SS_DECAL
            m.no_shadows = True
        elif low == 'sort':
            u = src.next()
            m.sort = SORTS.get(u.s.lower(), None)
            if m.sort is None:
                m.sort = _float(u.s)  # atof: unknown words sort as 0
        elif low == 'spectrum':
            m.spectrum = int(_number(src))
        elif low == 'foglight':
            m.fog_light = True
        elif low == 'blendlight':
            m.blend_light = True
        elif low == 'ambientlight':
            m.ambient_light = True
        elif low == 'mirror':
            m.sort = SS_SUBVIEW
            m.coverage = MC_OPAQUE
        elif low == 'portalsky':
            m.portal_sky = True
        elif low == 'nofog':
            m.no_fog = True
        elif low == 'clamp':
            m.repeat_default = TR_CLAMP
        elif low in ('zeroclamp', 'alphazeroclamp'):
            m.repeat_default = TR_CLAMP_TO_ZERO
        elif low == 'deform':
            d = src.lower()
            m.deform = d
            if d in ('sprite', 'tube', 'flare'):
                m.cull = CT_TWO
                m.no_shadows = True
            if d in ('flare', 'expand', 'move'):
                parse_expr(src, decls)
            elif d == 'turbulent':
                src.next()
                parse_expr(src, decls)
                parse_expr(src, decls)
                parse_expr(src, decls)
            elif d in ('particle', 'particle2'):
                src.next()
            elif d == 'eyeball':
                pass
        elif low == 'guisurf':
            m.gui = True
            u = src.lower()
            if u in ('entity', 'entity2', 'entity3'):
                pass
        elif low == 'decalinfo':
            src.next()
            src.next()
            for _ in range(2):
                src.expect('(')
                while src.next().s != ')':
                    pass
        elif low == 'surfacetype':
            src.next()
        else:
            # surface / content flags and other single keywords
            pass

    _add_implicit_stages(m)
    _sort_interaction_stages(m.stages)
    # lit two sided surfaces got back sides in the geometry; they draw front sided
    if m.cull == CT_TWO and any(s.lighting != SL_AMBIENT or s.texgen != TG_EXPLICIT for s in m.stages):
        m.cull = CT_FRONT

    # coverage
    num_ambient = sum(1 for s in m.stages if s.lighting == SL_AMBIENT)
    for s in m.stages:
        if s.alpha_test is not None and m.coverage == MC_BAD:
            m.coverage = MC_PERFORATED
    if m.coverage == MC_BAD:
        if not m.stages:
            m.coverage = MC_TRANSLUCENT
        elif len(m.stages) != num_ambient:
            m.coverage = MC_OPAQUE
        else:
            s0 = m.stages[0]
            if s0.dst_blend != GL_ZERO or s0.src_blend in (GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_DST_ALPHA,
                                                             GL_ONE_MINUS_DST_ALPHA):
                m.coverage = MC_TRANSLUCENT
            else:
                m.coverage = MC_OPAQUE
    if m.coverage == MC_TRANSLUCENT:
        m.no_shadows = True
    if m.sort == SS_BAD:
        if m.has_polygon_offset:
            m.sort = SS_DECAL
        elif m.coverage == MC_TRANSLUCENT:
            m.sort = SS_MEDIUM
        else:
            m.sort = SS_OPAQUE
    for s in m.stages:
        if s.image == '_currentrender' or any(fm == '_currentrender' for fm in s.fragment_maps):
            if m.sort != SS_PORTAL_SKY:
                m.sort = SS_POST_PROCESS
                m.coverage = MC_TRANSLUCENT
            break
    return m


def _add_implicit_stages(m):
    """idMaterial::AddImplicitStages: a _flat bump map and a _white diffuse map when missing"""
    lit = {s.lighting for s in m.stages}
    if not lit & {SL_BUMP, SL_DIFFUSE, SL_SPECULAR}:
        return
    if SL_BUMP not in lit:
        st = Stage()
        st.lighting, st.image, st.repeat = SL_BUMP, '_flat', m.repeat_default
        m.stages.append(st)
    if SL_DIFFUSE not in lit and SL_SPECULAR not in lit and not any(s.texgen == TG_REFLECT_CUBE for s in m.stages):
        st = Stage()
        st.lighting, st.image, st.repeat = SL_DIFFUSE, '_white', m.repeat_default
        m.stages.append(st)


def _sort_interaction_stages(st):
    """idMaterial::SortInteractionStages: bump, then diffuse, then specular within each bump group"""
    n = len(st)
    i = 0
    while i < n:
        j = i + 1
        while j < n:
            if st[j].lighting == SL_BUMP and st[i].lighting == SL_BUMP:
                break
            j += 1
        for l in range(1, j - i):
            for k in range(i, j - l):
                if st[k].lighting > st[k + 1].lighting:
                    st[k], st[k + 1] = st[k + 1], st[k]
        i = j


def _number(src):
    t = src.next()
    if t.s == '-':
        return -float(src.next().s)
    return float(t.s)


def _copy_stage(st):
    c = Stage()
    c.__dict__.update(st.__dict__)
    c.color = list(st.color)
    c.fragment_maps = list(st.fragment_maps)
    return c


def _parse_stage(src, decls, m):
    ss = Stage()
    ss.repeat = m.repeat_default
    while True:
        t = src.next()
        if t.s == '}':
            break
        low = t.s.lower()
        line = t.line
        if low == 'name':
            src.skip_line(line)
        elif low == 'blend':
            u = src.lower()
            if u == 'blend':
                ss.src_blend, ss.dst_blend = GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA
            elif u == 'add':
                ss.src_blend, ss.dst_blend = GL_ONE, GL_ONE
            elif u in ('filter', 'modulate'):
                ss.src_blend, ss.dst_blend = GL_DST_COLOR, GL_ZERO
            elif u == 'none':
                ss.src_blend, ss.dst_blend = GL_ZERO, GL_ONE
            elif u == 'bumpmap':
                ss.lighting = SL_BUMP
            elif u == 'diffusemap':
                ss.lighting = SL_DIFFUSE
            elif u == 'specularmap':
                ss.lighting = SL_SPECULAR
            else:
                ss.src_blend = BLEND_NAMES.get(u, GL_ONE)
                if src.accept(','):
                    ss.dst_blend = BLEND_NAMES.get(src.lower(), GL_ZERO)
            ss.has_blend = True
        elif low == 'map':
            ss.image = parse_image_program(src)
        elif low in ('cubemap', 'cameracubemap'):
            ss.image = parse_image_program(src)
            ss.cube = True
        elif low in ('remoterendermap', 'mirrorrendermap', 'xrayrendermap'):
            src.next()
            src.next()
            ss.dynamic = True
            if low != 'remoterendermap':
                ss.texgen = TG_SCREEN
        elif low == 'screen':
            ss.texgen = TG_SCREEN
        elif low == 'screen2':
            ss.texgen = TG_SCREEN2
        elif low == 'glasswarp':
            ss.texgen = TG_GLASSWARP
        elif low == 'videomap':
            u = src.lower()
            if u == 'loop':
                src.next()
            ss.cinematic = True
        elif low == 'soundmap':
            src.next()
            ss.cinematic = True
        elif low == 'ignorealphatest':
            ss.ignore_alpha_test = True
        elif low == 'nearest':
            ss.filter = TF_NEAREST
        elif low == 'linear':
            ss.filter = TF_LINEAR
        elif low == 'clamp':
            ss.repeat = TR_CLAMP
        elif low == 'noclamp':
            ss.repeat = TR_REPEAT
        elif low == 'zeroclamp':
            ss.repeat = TR_CLAMP_TO_ZERO
        elif low == 'alphazeroclamp':
            ss.repeat = TR_CLAMP_TO_ZERO_ALPHA
        elif low in ('forcehighquality', 'highquality', 'uncompressed', 'nopicmip'):
            pass
        elif low == 'vertexcolor':
            ss.vertex_color = SVC_MODULATE
        elif low == 'inversevertexcolor':
            ss.vertex_color = SVC_INVERSE_MODULATE
        elif low == 'privatepolygonoffset':
            ss.private_offset = 1.0
            if src.on_line(line):
                ss.private_offset = _number(src)
        elif low == 'texgen':
            u = src.lower()
            ss.texgen = {'normal': TG_DIFFUSE_CUBE, 'reflect': TG_REFLECT_CUBE, 'skybox': TG_SKYBOX_CUBE,
                         'wobblesky': TG_WOBBLESKY_CUBE}.get(u, TG_EXPLICIT)
            if u == 'wobblesky':
                parse_expr(src, decls)
                parse_expr(src, decls)
                parse_expr(src, decls)
        elif low in ('scroll', 'translate'):
            a = parse_expr(src, decls)
            src.expect(',')
            b = parse_expr(src, decls)
            ss.matrix = _mat_mul(ss.matrix, [[C(1), C(0), a], [C(0), C(1), b]])
        elif low == 'scale':
            a = parse_expr(src, decls)
            src.expect(',')
            b = parse_expr(src, decls)
            ss.matrix = _mat_mul(ss.matrix, [[a, C(0), C(0)], [C(0), b, C(0)]])
        elif low == 'centerscale':
            a = parse_expr(src, decls)
            src.expect(',')
            b = parse_expr(src, decls)
            ss.matrix = _mat_mul(ss.matrix, [[a, C(0), Expr('-', C(0.5), Expr('*', C(0.5), a))],
                                             [C(0), b, Expr('-', C(0.5), Expr('*', C(0.5), b))]])
        elif low == 'shear':
            a = parse_expr(src, decls)
            src.expect(',')
            b = parse_expr(src, decls)
            ss.matrix = _mat_mul(ss.matrix, [[C(1), a, Expr('*', C(-0.5), a)], [b, C(1), Expr('*', C(-0.5), b)]])
        elif low == 'rotate':
            a = parse_expr(src, decls)
            sn = Expr('table', decls.tables['sintable'], a)
            cs = Expr('table', decls.tables['costable'], a)
            ss.matrix = _mat_mul(ss.matrix, [
                [cs, Expr('-', C(0), sn), Expr('+', Expr('+', Expr('*', C(-0.5), cs), Expr('*', C(0.5), sn)), C(0.5))],
                [sn, cs, Expr('+', Expr('+', Expr('*', C(-0.5), sn), Expr('*', C(-0.5), cs)), C(0.5))]])
        elif low in ('maskred', 'maskgreen', 'maskblue', 'maskalpha', 'maskcolor', 'maskdepth'):
            ss.mask |= {'maskred': 1, 'maskgreen': 2, 'maskblue': 4, 'maskalpha': 8, 'maskcolor': 7,
                        'maskdepth': 16}[low]
        elif low == 'alphatest':
            ss.alpha_test = parse_expr(src, decls)
        elif low == 'colored':
            ss.color = [Expr('parm', 0), Expr('parm', 1), Expr('parm', 2), Expr('parm', 3)]
        elif low == 'color':
            ss.color = [parse_expr(src, decls)]
            for _ in range(3):
                src.expect(',')
                ss.color.append(parse_expr(src, decls))
        elif low in ('red', 'green', 'blue', 'alpha'):
            ss.color[('red', 'green', 'blue', 'alpha').index(low)] = parse_expr(src, decls)
        elif low == 'rgb':
            e = parse_expr(src, decls)
            ss.color[0] = ss.color[1] = ss.color[2] = e
        elif low == 'rgba':
            e = parse_expr(src, decls)
            ss.color = [e, e, e, e]
        elif low == 'if':
            ss.condition = parse_expr(src, decls)
        elif low in ('program', 'fragmentprogram', 'vertexprogram'):
            if src.on_line(line):
                ss.program = src.next().s
        elif low in ('vertexparm', 'vertexparm2'):
            idx = int(_number(src))
            es = [parse_expr(src, decls)]
            while src.accept(','):
                es.append(parse_expr(src, decls))
            ss.vertex_parms.append((idx, es))
        elif low == 'fragmentmap':
            idx = int(_number(src))
            while src.peek().s.lower() in ('cubemap', 'cameracubemap', 'nearest', 'linear', 'clamp', 'noclamp',
                                           'zeroclamp', 'alphazeroclamp', 'forcehighquality', 'uncompressed',
                                           'highquality', 'nopicmip'):
                src.next()
            key = parse_image_program(src)
            while len(ss.fragment_maps) <= idx:
                ss.fragment_maps.append(None)
            ss.fragment_maps[idx] = key
        else:
            raise SyntaxError(f'{m.name}: unknown stage token {t.s!r} line {line}')
    return ss
