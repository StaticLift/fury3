#!/usr/bin/env python3
"""Convert a .glb or .gltf model into a Fury3 ship mod.

    glb2fury.py model.glb out_dir --ref <SHIP.BIN> --art <ART folder>
                [--replace SHIP.BIN] [--tris 700] [--min-part 0.04] [--name MYSHIP]
                [--wings DEGREES --wing-y Y --wing-root X --wing-body X]

Writes out_dir/mods/<name>.pod (the model and one 64x64 colour texture),
out_dir/pod.ini, and out_dir/preview.png. Copy pod.ini and mods/ into the game
folder (FURY3/). The game mounts the archives pod.ini lists, in order, and the
first match wins, so the CD's archives stay untouched.

The model is fitted to the box of the --ref model (Y up, nose along +Z) and reuses
its scale header, so it flies at the same size. Needs numpy, pillow. Model format (little-endian):
  u32 0x14, scale, 2, 0, nverts; nverts * i32 x,y,z
  chunks: 0x17 first,count | 0x0D index, char name[16] | 0x18 n, i32 nx,ny,nz (16.16
  unit normal), i32 d (16.16 n.v), n * (u32 vertex, i32 u, i32 v) (16.16, 0..255
  across the texture) | 0 end

--lod adds a cheap face list after the model, for far away and for shadows, using extra
vertices at the end of the vertex table so that poses move it too. A skipped chunk (0x1B)
after the first 0x17 marks it (animated models must have 0x17 straight after the vertices): u32 0x1B, 0, 0, 'LOD1', offset of the list from the chunk,
world distance. The runtime's model thunk (gpu_capture.c) turns that chunk into a call to
the list followed by an end marker when the list is wanted.

--glow adds a disc behind each wing's engine, unlit and blended like a laser bolt: a
0x1F chunk of palette indices, then 0x06 faces, last in the main face list. The runtime
sets the indices from the throttle (recomp_runtime.c).

--wings adds a second pose after the end marker, which the game never reads: 'WING',
u32 nverts, nverts * i32 x,y,z. The runtime slides the model between the two on the
d-pad (recomp_runtime.c). The model must already have upper and lower wings: pieces
further than --wing-body from the centreline hinge along the nose axis at x = +-wing-root,
y = wing-y, those above wing-y up and those below down.
"""
import argparse, glob, json, os, struct
import numpy as np
from PIL import Image

CELL = 8                # texture cells: one colour at one brightness each
LEVELS = (0.58, 0.8, 1.0)
LIGHT = np.array((0.35, 0.85, 0.4)) / np.linalg.norm((0.35, 0.85, 0.4))


def node_matrix(n):
    if 'matrix' in n:
        return np.array(n['matrix'], float).reshape(4, 4).T
    t = np.eye(4); t[:3, 3] = n.get('translation', (0, 0, 0))
    x, y, z, w = n.get('rotation', (0, 0, 0, 1))
    r = np.eye(4)
    r[:3, :3] = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                 [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                 [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    s = np.diag(list(n.get('scale', (1, 1, 1))) + [1])
    return t @ r @ s


def load_glb(path, textured=None):
    """{material index: (vertices, triangles)} in model space, and base colours. With
    textured, a dict, also fills {material: (a uv per vertex, image array or None)}."""
    if path.lower().endswith('.gltf'):
        j = json.load(open(path))
        bin0 = open(os.path.join(os.path.dirname(path), j['buffers'][0]['uri']), 'rb').read()
    else:
        d = open(path, 'rb').read()
        jl = struct.unpack_from('<I', d, 12)[0]
        j = json.loads(d[20:20 + jl])
        bin0 = d[20 + jl + 8:]

    def acc(i):
        a = j['accessors'][i]; v = j['bufferViews'][a['bufferView']]
        n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}[a['type']]
        dt = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[a['componentType']]
        off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = v.get('byteStride', 0) or n * np.dtype(dt).itemsize
        raw = np.frombuffer(bin0, np.uint8, stride * a['count'], off).reshape(a['count'], stride)
        return np.frombuffer(raw[:, :n * np.dtype(dt).itemsize].tobytes(), dt).reshape(a['count'], n).copy()

    parts = {}

    def walk(i, parent):
        n = j['nodes'][i]; m = parent @ node_matrix(n)
        if 'mesh' in n:
            for pr in j['meshes'][n['mesh']]['primitives']:
                p = acc(pr['attributes']['POSITION']).astype(float)
                idx = acc(pr['indices']).ravel().astype(int) if 'indices' in pr else np.arange(len(p))
                p = (m[:3, :3] @ p.T).T + m[:3, 3]
                uv = acc(pr['attributes']['TEXCOORD_0']).astype(float) if 'TEXCOORD_0' in pr['attributes'] else np.zeros((len(p), 2))
                parts.setdefault(pr.get('material', 0), []).append((p, idx.reshape(-1, 3), uv))
        for c in n.get('children', []):
            walk(c, m)
    for r in j['scenes'][j.get('scene', 0)]['nodes']:
        walk(r, np.eye(4))
    mats = {}
    for k, ps in parts.items():
        off = 0; vs = []; ts = []
        for p, t, _ in ps:
            vs.append(p); ts.append(t + off); off += len(p)
        mats[k] = (np.vstack(vs), np.vstack(ts))
        if textured is not None:
            pb = j['materials'][k].get('pbrMetallicRoughness', {})
            img = None
            if 'baseColorTexture' in pb:
                uri = j['images'][j['textures'][pb['baseColorTexture']['index']]['source']]['uri']
                im = Image.open(os.path.join(os.path.dirname(path), uri)).convert('RGB')
                im.thumbnail((1024, 1024))
                img = np.asarray(im, float)
            textured[k] = (np.vstack([q[2] for q in ps]), img)
    cols = [np.array(m.get('pbrMetallicRoughness', {}).get('baseColorFactor', (1, 1, 1, 1))[:3]) for m in j['materials']]
    return mats, cols


def weld(v, t):
    q = np.round(v / (np.ptp(v, axis=0).max() * 1e-4 + 1e-12)).astype(np.int64)
    _, first, inv = np.unique(q, axis=0, return_index=True, return_inverse=True)
    t = inv.ravel()[t]
    return v[first], t[(t[:, 0] != t[:, 1]) & (t[:, 1] != t[:, 2]) & (t[:, 0] != t[:, 2])]


def square_up(mats):
    """Turns the model so that its largest flat surfaces face along X, as a flat-winged fighter's do."""
    cov = np.zeros((3, 3))
    for v, t in mats.values():
        q = v[t]; n = np.cross(q[:, 1] - q[:, 0], q[:, 2] - q[:, 0])
        cov += n.T @ (n / (np.linalg.norm(n, axis=1, keepdims=True) + 1e-12))
    e = np.linalg.eigh(cov)[1][:, -1]
    e = e if e[0] > 0 else -e
    axis = np.cross(e, (1, 0, 0)); sn = np.linalg.norm(axis); cs = e[0]
    k = np.array([[0, -axis[2], axis[1]], [axis[2], 0, -axis[0]], [-axis[1], axis[0], 0]]) / (sn + 1e-12)
    r = np.eye(3) + sn * k + (1 - cs) * k @ k
    print('squared up by %.1f degrees' % np.degrees(np.arctan2(sn, cs)))
    return {key: ((r @ v.T).T, t) for key, (v, t) in mats.items()}


def orient(mats, ref_lo, ref_hi, forward=None, fit=0, floor=False):
    """Y up, longest horizontal axis along Z with the nose at +Z, fitted to the ref box.
    forward (+1 or -1) says which way Z the nose already points instead of working it out.
    fit makes the longest side that fraction of the ref's length, for models that are not long."""
    pts = np.vstack([v for v, _ in mats.values()])
    lo, hi = pts.min(0), pts.max(0)
    fwd = 2 if forward or hi[2] - lo[2] >= hi[0] - lo[0] else 0
    side = 0 if fwd == 2 else 2
    # engines and wings sit at the back, so the bulk of the surface is behind the middle
    tri = np.vstack([v[t].mean(1) for v, t in mats.values()])
    sign = forward or (-1.0 if tri[:, fwd].mean() > (lo[fwd] + hi[fwd]) / 2 else 1.0)
    r = np.zeros((3, 3)); r[2, fwd] = sign; r[1, 1] = 1
    r[0, side] = 1
    if np.linalg.det(r) < 0:
        r[0, side] = -1
    out = {}
    p2 = (r @ pts.T).T
    s = (ref_hi[2] - ref_lo[2]) / np.ptp(p2[:, 2])
    if fit:
        s = (ref_hi[2] - ref_lo[2]) * fit / np.ptp(p2, axis=0).max()
    c = (p2.min(0) + p2.max(0)) / 2
    zc = (ref_lo[2] + ref_hi[2]) / 2
    for k, (v, t) in mats.items():
        q = (r @ v.T).T
        q = (q - c) * s + (0, 0, zc)
        if floor:               # ground units stand on y = 0
            q[:, 1] += np.ptp(p2[:, 1]) * s / 2
        out[k] = (q, t)
    return out


def components(v, t):
    """The connected piece each vertex belongs to."""
    p = list(range(len(v)))

    def find(a):
        while p[a] != a:
            p[a] = p[p[a]]; a = p[a]
        return a
    for a, b, c in t:
        p[find(int(a))] = find(int(b)); p[find(int(a))] = find(int(c))
    return np.array([find(i) for i in range(len(v))])


def split_panels(mats, side_x):
    """Like split_wings, for models with one flat panel a side: groups 5 and 6."""
    out = {}
    for k, (v, t) in mats.items():
        v, t = weld(v, t)
        root = components(v, t)
        for r in np.unique(root[t[:, 0]]):
            x = v[root == r, 0]; x = (x.min() + x.max()) / 2
            g = 0 if abs(x) < side_x else 5 if x > 0 else 6
            out.setdefault((k, g), (v, []))[1].append(t[root[t[:, 0]] == r])
    res = {}
    for key, (v, ts) in out.items():
        t = np.vstack(ts)
        used, inv = np.unique(t, return_inverse=True)
        res[key] = (v[used], inv.reshape(t.shape))
    return res


def plate(v, corners=6, half=170):
    """One solid plate: the outline of a panel made of many pieces, facing along X."""
    fit = np.linalg.lstsq(np.c_[v[:, 1:], np.ones(len(v))], v[:, 0], rcond=None)[0]
    pts = sorted(map(tuple, v[:, 1:]))

    def half_hull(ps):
        h = []
        for p in ps:
            while len(h) > 1 and (h[-1][0] - h[-2][0]) * (p[1] - h[-2][1]) - (h[-1][1] - h[-2][1]) * (p[0] - h[-2][0]) <= 0:
                h.pop()
            h.append(p)
        return h[:-1]
    hull = np.array(half_hull(pts) + half_hull(pts[::-1]))
    while len(hull) > corners:          # drop the corner that adds the least area
        prv, nxt = np.roll(hull, 1, 0), np.roll(hull, -1, 0)
        u, w = hull - prv, nxt - prv
        hull = np.delete(hull, np.abs(u[:, 0] * w[:, 1] - u[:, 1] * w[:, 0]).argmin(), 0)
    n = len(hull)
    x = np.c_[hull, np.ones(n)] @ fit
    pv = np.vstack([np.c_[x + half, hull], np.c_[x - half, hull]])
    pt = []
    for i in range(1, n - 1):
        pt += [(0, i, i + 1), (n, n + i, n + i + 1)]
    for i in range(n):
        j = (i + 1) % n
        pt += [(i, j, n + j), (i, n + j, n + i)]
    pt = np.array(pt, np.int64)
    q = pv[pt]; c = pv.mean(0)
    flip = (np.cross(q[:, 1] - q[:, 0], q[:, 2] - q[:, 0]) * (q.mean(1) - c)).sum(1) < 0
    pt[flip] = pt[flip][:, ::-1]
    return pv, pt


def split_wings(mats, split_y, body_x):
    """{(material, group): mesh}. Group 0 is the body, 1 to 4 the wings. Each is simplified
    alone: the stacked wings lie closer together than any grid that would merge them."""
    out = {}
    for k, (v, t) in mats.items():
        v, t = weld(v, t)
        root = components(v, t)
        for r in np.unique(root[t[:, 0]]):
            q = v[root == r]
            x, y = (q[:, :2].min(0) + q[:, :2].max(0)) / 2
            g = 0 if abs(x) < body_x else (1 if x > 0 else 2) + (0 if y > split_y else 2)
            out.setdefault((k, g), (v, []))[1].append(t[root[t[:, 0]] == r])
    res = {}
    for key, (v, ts) in out.items():
        t = np.vstack(ts)
        used, inv = np.unique(t, return_inverse=True)
        res[key] = (v[used], inv.reshape(t.shape))
    return res


def drop_reaching(v, t, y):
    """Removes connected pieces that reach below y."""
    v, t = weld(v, t)
    root = components(v, t)
    low = np.unique(root[v[:, 1] < y])
    return v, t[~np.isin(root[t[:, 0]], low)]


def open_pose(vs, groups, degrees, root, split_y):
    """The vertices with each wing swung about its hinge."""
    a = np.radians(degrees); out = []
    for (x, y, z), g in zip(vs, groups):
        if g:
            side = 1 if g in (1, 3) else -1
            up = 1 if g < 3 else -1
            dx, dy = x - side * root, y - split_y
            c, s = np.cos(a), np.sin(a) * side * up
            x, y = side * root + c * dx - s * dy, split_y + s * dx + c * dy
        out.append((int(round(x)), int(round(y)), int(round(z))))
    return out


def drop_small_parts(v, t, min_size):
    """Removes connected pieces whose longest side is under min_size (surface detail)."""
    root = components(v, t)
    keep = np.zeros(len(v), bool)
    for r in np.unique(root):
        m = root == r
        keep[m] = np.ptp(v[m], axis=0).max() >= min_size
    return v, t[keep[t[:, 0]]]


def ear_clip(pts, nrm):
    """Triangulates a simple polygon (3D points in order, CCW about nrm); index triples."""
    ax = int(np.argmax(np.abs(nrm)))
    p = np.delete(pts, ax, axis=1)
    # dropping an axis keeps winding only for positive normal components on x and z
    flip = (nrm[ax] < 0) != (ax == 1)
    if flip:
        p = p * (1, -1)
    idx = list(range(len(p))); out = []

    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    guard = 0
    while len(idx) > 3 and guard < 20000:
        guard += 1
        for i in range(len(idx)):
            a, b, c = idx[i - 1], idx[i], idx[(i + 1) % len(idx)]
            if cross(p[a], p[b], p[c]) <= 0:
                continue
            inside = any(j not in (a, b, c) and cross(p[a], p[b], p[j]) >= 0 and cross(p[b], p[c], p[j]) >= 0
                         and cross(p[c], p[a], p[j]) >= 0 for j in idx)
            if not inside:
                out.append((a, b, c)); del idx[i]
                break
        else:
            return None
    if len(idx) == 3:
        out.append(tuple(idx))
    return out


def merge_coplanar(v, t):
    """Re-triangulates each flat region from its outline: same shape, far fewer triangles."""
    a, b, c = v[t[:, 0]], v[t[:, 1]], v[t[:, 2]]
    n = np.cross(b - a, c - a); ln = np.linalg.norm(n, axis=1)
    ok = ln > 1e-9
    v_t = t[ok]; n = n[ok] / ln[ok, None]; d = (n * v[v_t[:, 0]]).sum(1)
    span = np.ptp(v, axis=0).max()
    edge = {}
    for i, tri in enumerate(v_t):
        for e in range(3):
            x, y = int(tri[e]), int(tri[(e + 1) % 3])
            edge.setdefault((min(x, y), max(x, y)), []).append(i)
    p = list(range(len(v_t)))

    def find(i):
        while p[i] != i:
            p[i] = p[p[i]]; i = p[i]
        return i
    for l in edge.values():
        for i in l[1:]:
            j = l[0]
            if n[i] @ n[j] > 0.9995 and abs(d[i] - d[j]) < span * 2e-3:
                p[find(i)] = find(j)
    regions = {}
    for i in range(len(v_t)):
        regions.setdefault(find(i), []).append(i)
    out = []
    for ids in regions.values():
        if len(ids) < 3:
            out += [tuple(v_t[i]) for i in ids]; continue
        directed = {(int(v_t[i][e]), int(v_t[i][(e + 1) % 3])) for i in ids for e in range(3)}
        nxt = {}; simple = True
        for x, y in directed:
            if (y, x) not in directed:
                simple = simple and x not in nxt
                nxt[x] = y
        loop = []
        if simple and nxt:
            s = next(iter(nxt)); cur = s
            while True:
                loop.append(cur); cur = nxt.get(cur)
                if cur is None or cur == s or len(loop) > len(nxt):
                    break
            simple = cur == s and len(loop) == len(nxt)     # one outline, no holes
        if not simple:
            out += [tuple(v_t[i]) for i in ids]; continue
        keep = []
        for i, q in enumerate(loop):
            u = v[q] - v[loop[i - 1]]; w = v[loop[(i + 1) % len(loop)]] - v[q]
            if np.linalg.norm(np.cross(u, w)) > 1e-3 * np.linalg.norm(u) * np.linalg.norm(w):
                keep.append(q)
        tris = ear_clip(v[keep], n[ids[0]]) if len(keep) >= 3 else None
        out += [(keep[x], keep[y], keep[z]) for x, y, z in tris] if tris else [tuple(v_t[i]) for i in ids]
    return v, np.array(out, np.int64)


def cluster(v, t, cell):
    """Snaps vertices to a grid (each to the mean of its cell). Thin parts stay put."""
    q = np.floor(v / cell).astype(np.int64)
    _, inv = np.unique(q, axis=0, return_inverse=True)
    inv = inv.ravel(); nv = np.zeros((inv.max() + 1, 3)); c = np.zeros(inv.max() + 1)
    np.add.at(nv, inv, v); np.add.at(c, inv, 1)
    nv /= c[:, None]
    nt = inv[t]
    nt = nt[(nt[:, 0] != nt[:, 1]) & (nt[:, 1] != nt[:, 2]) & (nt[:, 0] != nt[:, 2])]
    # the same triangle can arrive twice: keep one per winding
    key = np.sort(nt, axis=1) * (1, 1, 1)
    _, first = np.unique(np.c_[key, np.sign(np.cross(nv[nt[:, 1]] - nv[nt[:, 0]], nv[nt[:, 2]] - nv[nt[:, 0]]) @ (0.3, 0.7, 0.6))], axis=0, return_index=True)
    return nv, nt[first]


def simplify(mats, total, min_part):
    length = np.ptp(np.vstack([v for v, _ in mats.values()])[:, 2])
    cleaned = {}
    for k, (v, t) in mats.items():
        v, t = weld(v, t)
        v, t = drop_small_parts(v, t, min_part * length)
        if len(t):
            cleaned[k] = merge_coplanar(v, t)
    n0 = sum(len(t) for _, t in cleaned.values())
    print('after merging flat regions:', n0, 'triangles', {str(k): len(t) for k, (_, t) in cleaned.items()})
    if n0 <= total:
        return cleaned
    lo, hi = length * 0.002, length * 0.2          # search the grid size for the target
    best = None
    for _ in range(12):
        cell = (lo * hi) ** 0.5
        trial = {k: merge_coplanar(*cluster(v, t, cell)) for k, (v, t) in cleaned.items()}
        n = sum(len(t) for _, t in trial.values())
        if n > total:
            lo = cell
        else:
            hi = cell; best = trial
    print('after clustering:', sum(len(t) for _, t in best.values()), 'triangles, grid', int(hi))
    return best

def reduce_edges(v, t, target, border=True):
    """Edge-collapse reduction: keeps outlines and thin plates, which grid snapping loses."""
    import pyfqmr
    size = np.ptp(v, axis=0).max()     # its stopping threshold is an absolute error
    m = pyfqmr.Simplify(); m.setMesh(v / size, t.astype(np.int32))
    m.simplify_mesh(target_count=target, aggressiveness=5, preserve_border=border, verbose=False)
    rv, rt, _ = m.getMesh()
    if not border and len(rt) > target * 1.5:     # it stalls on thin frames: this one doesn't
        import fast_simplification
        rv, rt = fast_simplification.simplify((v / size).astype(np.float32), t.astype(np.int32), target_count=target, agg=9)
    return np.array(rv, float) * size, np.array(rt, np.int64)


def prism(v, t, sides, stations):
    """A low-poly stand-in for a long thin part: rings fitted to it along its long axis."""
    w = np.array([(i, j, 8 - i - j) for i in range(9) for j in range(9 - i)]) / 8
    v = (v[t].transpose(0, 2, 1) @ w.T).transpose(0, 2, 1).reshape(-1, 3)    # points over its surface
    ax = int(np.argmax(np.ptp(v, axis=0))); a1, a2 = [i for i in range(3) if i != ax]
    lo, hi = v[:, ax].min(), v[:, ax].max()
    band = (hi - lo) / (stations - 1) * 0.6
    ang = (np.arange(sides) + 0.5) * 2 * np.pi / sides
    dirs = np.c_[np.cos(ang), np.sin(ang)]
    rings = []
    for at in np.linspace(lo, hi, stations):
        near = np.abs(v[:, ax] - at)
        p = v[near <= max(band, near.min())][:, (a1, a2)]
        c = (p.min(0) + p.max(0)) / 2
        r = ((p - c) @ dirs.T).max(0)
        ring = np.zeros((sides, 3)); ring[:, ax] = at
        ring[:, (a1, a2)] = c + dirs * np.maximum(r, 1)[:, None]
        rings.append(ring)
    pv = np.vstack(rings); pt = []
    for i in range(stations - 1):
        for j in range(sides):
            a_, b_ = i * sides + j, i * sides + (j + 1) % sides
            c_, d_ = a_ + sides, b_ + sides
            pt += [(a_, b_, d_), (a_, d_, c_)]
    for j in range(1, sides - 1):
        pt.append((0, j + 1, j))
        e = (stations - 1) * sides
        pt.append((e, e + j, e + j + 1))
    pt = np.array(pt, np.int64)
    # the winding above depends on which way the axes run: make faces point outward
    q = pv[pt]; n = np.cross(q[:, 1] - q[:, 0], q[:, 2] - q[:, 0])
    if ((q.mean(1) - pv.mean(0)) * n).sum() < 0:
        pt = pt[:, ::-1]
    return pv, pt


def low_poly_parts(v, t, part_faces=20, stretch=2.2, thin=3500, keep=9000, rings=None, round_only=False):
    """Parts that are already simple stay. Long thin ones become prisms, and the rest are
    reduced; the source is open shells, which reduction alone tears apart. The texture
    is painted from the full model, so the detail removed here still shows."""
    root = components(v, t)
    vs = []; ts = []; base = 0
    for r in np.unique(root[t[:, 0]]):
        p = t[root[t[:, 0]] == r]
        used, inv = np.unique(p, return_inverse=True)
        pv, pt = v[used], inv.reshape(p.shape)
        size = np.sort(np.ptp(pv, axis=0))
        if rings and len(pt) > 16:
            pv, pt = prism(pv, pt, *rings)
        elif len(pt) > 30 and size[2] >= stretch * size[1] and size[1] < thin and (not round_only or size[1] < 1.6 * size[0]):
            pv, pt = prism(pv, pt, (8 if round_only and size[1] > 5000 else 6) if size[1] > 1200 else 4, 3 + (size[2] > 8000) + (round_only and size[1] > 5000))
        elif len(pt) > 36:
            pv, pt = reduce_edges(pv, pt, part_faces if size[2] < keep else len(pt), not round_only)
        vs.append(pv); ts.append(pt + base); base += len(pv)
    return np.vstack(vs), np.vstack(ts)


def core_bar(v, t, half):
    """A solid bar through the triangles within half of the centreline."""
    keep = np.abs(v[t].mean(1)[:, 0]) < half
    return prism(v, t[keep], 4, 2)


def low_detail(v, t, least):
    """The far-away version of a mesh: its large parts only, as four-sided prisms."""
    root = components(v, t)
    vs = []; ts = []; base = 0
    for r in np.unique(root[t[:, 0]]):
        p = t[root[t[:, 0]] == r]
        used, inv = np.unique(p, return_inverse=True)
        pv, pt = v[used], inv.reshape(p.shape)
        if np.ptp(pv, axis=0).max() < least:
            continue
        if len(pt) > 20:
            pv, pt = prism(pv, pt, 4, 2)
        vs.append(pv); ts.append(pt + base); base += len(pv)
    return (np.vstack(vs), np.vstack(ts)) if vs else (v[:0], t[:0])


def engine_ends(lo):
    """(group, centre, radius) of the rear end of each wing's rearmost part."""
    out = []
    for (_, g), (v, t) in lo.items():
        if not g:
            continue
        root = components(v, t)
        back = root[np.unique(t)[v[np.unique(t), 2].argmin()]]
        q = v[root == back]
        ring = q[q[:, 2] <= q[:, 2].min() + 0.03 * np.ptp(q[:, 2])]
        c = (ring.min(0) + ring.max(0)) / 2
        out.append((g, c, 0.8 * np.ptp(ring[:, :2], axis=0).min() / 2))
    return out


def ship_colours(art):
    """The palette indices the stock ship's textures use, with their VGA.ACT colours.
    Texture bytes are palette indices. These are the ones known to look right on every
    planet (each planet loads its own palette, and no index is identical across them)."""
    used = set()
    for f in glob.glob(os.path.join(art, 'FURY*.RAW')):
        used.update(open(f, 'rb').read())
    used.discard(0)
    idx = np.array(sorted(used))
    pal = np.frombuffer(open(os.path.join(art, 'VGA.ACT'), 'rb').read()[:768], np.uint8).reshape(256, 3).astype(float)
    return idx, pal[idx]


def canon(p, g, split_y):
    """A vertex array in the upper right wing's space, so all four wings share a texture."""
    q = np.array(p, float)
    if g in (2, 4, 6):
        q[..., 0] *= -1
    if g in (3, 4):
        q[..., 1] = 2 * split_y - q[..., 1]
    return q


def chart_class(g):
    """Groups in a class share texture charts: the body; wings; a walker's right and left legs."""
    return 'b' if g == 0 else 'w' if g < 7 else 'r' if g < 10 else 'l'


def tri_chart(q, g):
    """(chart key, triangle) for a canonical triangle: charts are flat projections along an
    axis, one per direction, and the body's two sides share one."""
    n = np.cross(q[1] - q[0], q[2] - q[0])
    if (g in (2, 4, 6)) != (g in (3, 4)):
        n = -n
    ax = int(np.argmax(np.abs(n))); pos = bool(n[ax] > 0)
    if not g and ax == 0 and not pos:
        q = q * (-1, 1, 1); pos = True
    return (chart_class(g), ax, pos), q


PLANE = {0: (2, 1), 1: (0, 2), 2: (0, 1)}       # chart axis: the two axes in its plane


def pack(sizes, scale):
    """Skyline packing into 256x256 with a 2 texel gap: {key: (x, y)}, or None if too big."""
    sky = np.zeros(256, int); out = {}
    for key, (w, h) in sorted(sizes.items(), key=lambda s: -s[1][0] * s[1][1]):
        w = int(np.ceil(w / scale)) + 5; h = int(np.ceil(h / scale)) + 5
        if w > 256:
            return None
        tops = [sky[x:x + w].max() for x in range(257 - w)]
        x = int(np.argmin(tops)); y = int(tops[x])
        if y + h > 256:
            return None
        out[key] = (x + 1, y + 1); sky[x:x + w] = y + h
    return out


def paint(rgb, mask, p, c, uv=None, img=None):
    """Fills a triangle on the canvas, with the colour c, multiplied by the image at uv."""
    x0, y0 = np.floor(p.min(0)).astype(int); x1, y1 = np.ceil(p.max(0)).astype(int) + 1
    x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, rgb.shape[1]); y1 = min(y1, rgb.shape[0])
    if x1 <= x0 or y1 <= y0:
        return
    y, x = np.mgrid[y0:y1, x0:x1] + 0.5
    a, b, d = p
    den = (b[1] - d[1]) * (a[0] - d[0]) + (d[0] - b[0]) * (a[1] - d[1])
    if abs(den) < 1e-9:
        return
    w0 = ((b[1] - d[1]) * (x - d[0]) + (d[0] - b[0]) * (y - d[1])) / den
    w1 = ((d[1] - a[1]) * (x - d[0]) + (a[0] - d[0]) * (y - d[1])) / den
    w2 = 1 - w0 - w1
    m = (w0 >= -0.02) & (w1 >= -0.02) & (w2 >= -0.02)
    if not m.any():
        return
    px = np.empty(m.sum() * 3).reshape(-1, 3)
    if img is None:
        px[:] = c
    else:
        u = w0[m] * uv[0, 0] + w1[m] * uv[1, 0] + w2[m] * uv[2, 0]
        v = w0[m] * uv[0, 1] + w1[m] * uv[1, 1] + w2[m] * uv[2, 1]
        h, wd = img.shape[:2]
        px = img[(np.mod(v, 1) * h).astype(int) % h, (np.mod(u, 1) * wd).astype(int) % wd] / 255 * c
    rgb[y0:y1, x0:x1][m] = px
    mask[y0:y1, x0:x1][m] = 255


def bake(hi, lo, cols, colours, split_y, src=None, grid=True):
    """Paints the full model into one 256x256 palette texture, from which the reduced mesh
    takes its detail. Returns the texture, an RGB copy and uv(group, triangle). src lists
    the model's own triangles with their images, when it has any: {material: (triangles,
    their uvs, image, their groups)}."""
    from PIL import ImageDraw
    charts = {}
    for (k, g), (v, t) in lo.items():
        q = canon(v, g, split_y)
        for tri in t:
            key, c = tri_chart(q[tri], g)
            lo_hi = charts.setdefault(key, [np.full(2, 1e18), np.full(2, -1e18)])
            uv = c[:, PLANE[key[1]]]
            lo_hi[0] = np.minimum(lo_hi[0], uv.min(0)); lo_hi[1] = np.maximum(lo_hi[1], uv.max(0))
    sizes = {key: tuple(b - a) for key, (a, b) in charts.items()}
    scale = 20.0
    while pack(sizes, scale) is None:
        scale *= 1.03
    place = pack(sizes, scale)
    print('atlas: %d charts, %.0f model units per texel' % (len(charts), scale))
    S = 4
    first = min([g for _, g in hi if 0 < g < 7] or [0])   # wings share what this one looks like
    rgb = np.zeros((256, 256, 3)); mask = np.zeros((256, 256), bool)
    rng = np.random.default_rng(1)
    for key, (a, b) in charts.items():
        cls, ax, pos = key
        ua, va = PLANE[ax]; x0, y0 = place[key]
        w = int(np.ceil((b - a)[0] / scale)) + 3; h = int(np.ceil((b - a)[1] / scale)) + 3
        view = np.zeros(3); view[ax] = 1 if pos else -1
        tris = []
        for (k, g), (v, t) in hi.items():
            if chart_class(g) != cls or (cls == 'w' and g != first):
                continue
            for tri in t:
                q = v[tri]
                n = np.cross(q[1] - q[0], q[2] - q[0]); ln = np.linalg.norm(n)
                if ln < 1e-6 or (n / ln) @ view <= 0.02:
                    continue
                shade = 0.72 + 0.28 * ((n / ln) @ view)
                tris.append((q[:, ax].mean() * view[ax], q[:, (ua, va)], k, shade))
        im = Image.new('RGB', (w * S, h * S)); mk = Image.new('L', (w * S, h * S))
        dr = ImageDraw.Draw(im); dm = ImageDraw.Draw(mk)
        if src:
            canvas = np.zeros((h * S, w * S, 3)); cover = np.zeros((h * S, w * S), np.uint8)
            flat = []
            for k, (q3, uvs, img, grp) in src.items():
                n3 = np.cross(q3[:, 1] - q3[:, 0], q3[:, 2] - q3[:, 0])
                n3 = n3 / (np.linalg.norm(n3, axis=1, keepdims=True) + 1e-9)
                dots = n3 @ view
                use = (dots > 0.02) & np.array([chart_class(g) == cls and (cls != 'w' or g == first) for g in grp])
                for i in np.nonzero(use)[0]:
                    flat.append((q3[i, :, ax].mean() * view[ax], k, i, 0.72 + 0.28 * dots[i]))
            for _, k, i, shade in sorted(flat, key=lambda r: r[0]):
                q3, uvs, img, _ = src[k]
                pts = ((q3[i][:, (ua, va)] - a) / scale + 1) * S
                c = 255 * np.clip(cols[k], 0, 1) ** (1 / 2.2) * shade
                paint(canvas, cover, pts, c, uvs[i], img)
            im = Image.fromarray(np.clip(canvas, 0, 255).astype(np.uint8)); mk = Image.fromarray(cover)
        for _, uv, k, shade in sorted(tris, key=lambda r: r[0]) if not src else ():
            pts = [tuple(((p - a) / scale + 1) * S) for p in uv]
            c = 255 * np.clip(cols[k], 0, 1) ** (1 / 2.2) * shade
            dr.polygon(pts, fill=tuple(int(x) for x in c), outline=tuple(int(x) for x in c)); dm.polygon(pts, fill=255, outline=255)
        px = np.asarray(im.resize((w, h), Image.BOX), float)
        alpha = np.asarray(mk.resize((w, h), Image.BOX), float)
        px = px * (255 / np.maximum(alpha, 1))[..., None]      # no dark rim from the background
        px = np.minimum(px, 255)
        cov = alpha > 96
        # plates and lines: world-sized, so they match across charts
        U = a[0] + (np.arange(w) - 1) * scale; V = a[1] + (np.arange(h) - 1) * scale
        pu = np.floor(U / 2300).astype(int); pv = np.floor(V / 3400).astype(int)
        tint = rng.uniform(0.97, 1.03, (pu.max() - pu.min() + 1, pv.max() - pv.min() + 1))
        f = tint[pu - pu.min()][:, pv - pv.min()].T
        f[:, np.mod(U, 2300) < scale] *= 0.93
        f[np.mod(V, 3400) < scale, :] *= 0.93
        grey = (px.max(2) - px.min(2)) < 48
        if grid:
            px = np.where(grey[..., None], px * f[..., None], px)
        rgb[y0:y0 + h, x0:x0 + w] = px; mask[y0:y0 + h, x0:x0 + w] = cov
    # grow the painted areas so that edge texels never show black
    for _ in range(3):
        grown = mask.copy(); out = rgb.copy()
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            r = np.roll(rgb, (dy, dx), (0, 1)); m = np.roll(mask, (dy, dx), (0, 1)) & ~grown
            out[m] = r[m]; grown |= m
        rgb, mask = out, grown
    idx_ok, rgb_ok = colours
    d = np.abs(rgb[:, :, None, :] - rgb_ok[None, None]).sum(3)
    tex = idx_ok[d.argmin(2)].astype(np.uint8); tex[~mask] = idx_ok[0]

    def uv(g, tri):
        key, c = tri_chart(canon(tri, g, split_y), g)
        a = charts[key][0]; x0, y0 = place[key]
        return (c[:, PLANE[key[1]]] - a) / scale + 1 + (x0, y0)
    return tex.tobytes(), rgb_ok[d.argmin(2)].astype(np.uint8), uv


def build(mats, cols, ref, colours, tex_name, uv=None, low=None, lod_dist=0, after=None, glow=None):
    """after(vs, groups) gives bytes to put straight after the end marker."""
    cells = {}
    atlas = np.zeros((64, 64), np.uint8)
    idx_ok, rgb_ok = colours

    def cell(mat, lvl):
        key = (mat, lvl)
        if key not in cells:
            i = len(cells); cx, cy = i % (64 // CELL), i // (64 // CELL)
            cells[key] = (cx * CELL + CELL / 2, cy * CELL + CELL / 2)
            srgb = 255 * np.clip(cols[mat], 0, 1) ** (1 / 2.2) * LEVELS[lvl]
            atlas[cy * CELL:(cy + 1) * CELL, cx * CELL:(cx + 1) * CELL] = idx_ok[np.abs(rgb_ok - srgb).sum(1).argmin()]
        return cells[key]

    vs = []
    groups = []
    faces = []
    all_mats = list(mats.items()) + list((low or {}).items())
    n_hi = None
    for i, (k, (v, t)) in enumerate(all_mats):
        if i == len(mats):
            n_hi = (len(vs), len(faces))
        base = len(vs); vs += [tuple(int(round(c)) for c in p) for p in v]; groups += [k[1]] * len(v)
        for tri in t:
            a, b, c = [np.array(vs[base + int(i)], float) for i in tri]
            n = np.cross(b - a, c - a); ln = np.linalg.norm(n)
            if ln < 1:
                continue
            n /= ln
            lvl = int(np.clip((n @ LIGHT + 1) / 2 * 3, 0, 2))
            where = uv(k[1], np.array([a, b, c])) if uv else cell(k[0], lvl)
            faces.append(([base + int(i) for i in tri], n, float(n @ a), where, lvl, k))
    rd = lambda v: int(round(v * 65536))
    if n_hi is None:
        n_hi = (len(vs), len(faces))
    glow_bytes = b''
    low_verts = len(vs)
    if glow:
        first = len(vs); tris = b''
        for g, c, r in glow:
            base = len(vs)
            vs.append(tuple(int(round(x)) for x in (c[0], c[1], c[2] - 30))); groups.append(g)
            for i in range(6):
                a = i * np.pi / 3
                vs.append(tuple(int(round(x)) for x in (c[0] + r * np.cos(a), c[1] + r * np.sin(a), c[2] - 30))); groups.append(g)
            for i in range(6):
                tris += struct.pack('<2I4i3I', 6, 3, 0, 0, rd(-1), rd(-(c[2] - 30)), base, base + 1 + i, base + 1 + (i + 1) % 6)
        n = len(vs) - first
        glow_bytes = struct.pack('<3I', 0x1F, first, n) + struct.pack('<%dI' % n, *([0x88] * n)) + tris
    out = struct.pack('<5I', 0x14, ref['scale'], 2, 0, len(vs))
    out += b''.join(struct.pack('<3i', *p) for p in vs)
    marker = len(out) + 12

    def face_list(first, count, flist, extra=b''):
        o = struct.pack('<3I', 0x17, first, count)
        o += struct.pack('<2I', 13, 0) + tex_name.encode().ljust(16, b'\0')
        return o + b''.join(face_bytes(f) for f in flist) + extra + struct.pack('<I', 0)

    def face_bytes(f):
        idx, n, d, where, _, _ = f
        o = struct.pack('<2I4i', 0x18, 3, rd(n[0]), rd(n[1]), rd(n[2]), rd(d))
        if uv:                  # texels of a 256x256 texture
            pts = where
        else:                   # 0..255 across a 64x64 one
            pts = [((where[0] + du) * 4, (where[1] + dv) * 4) for du, dv in ((-1.5, -1.5), (1.5, -1.5), (0, 1.5))]
        for i, (u, v) in zip(idx, pts):
            o += struct.pack('<I2i', i, rd(u), rd(v))
        return o

    block = face_list(0, n_hi[0], faces[:n_hi[1]], glow_bytes)
    hi_faces = len(out) + 36 + (24 if low else 0)
    out += block[:12] + (bytes(24) if low else b'') + block[12:]
    if after:
        out += after(vs, groups)
    layout = [(hi_faces, faces[:n_hi[1]])]
    if low:
        layout.append((len(out) + 36, faces[n_hi[1]:]))
        at = len(out)
        out += face_list(n_hi[0], low_verts - n_hi[0], faces[n_hi[1]:])
        out = out[:marker] + struct.pack('<3I4sIi', 0x1B, 0, 0, b'LOD1', at - marker, int(lod_dist)) + out[marker + 24:]
    return out, atlas.tobytes(), vs, groups, faces, layout


def make_pod(entries):
    head = struct.pack('<I', len(entries)) + b'Fury3 mod'.ljust(80, b'\0')
    off = len(head) + 40 * len(entries)
    dirs = b''; blob = b''
    for name, data in entries:
        dirs += name.encode().ljust(32, b'\0') + struct.pack('<II', len(data), off + len(blob))
        blob += data
    return head + dirs + blob


def preview(path, vs, faces, cols):
    V = np.array(vs, float); views = [(0, 2), (0, 1), (2, 1)]
    img = Image.new('RGB', (1200, 400), (24, 40, 44)); from PIL import ImageDraw
    dr = ImageDraw.Draw(img); s = 370 / (np.abs(V).max() * 2.1)
    for w, (a, b) in enumerate(views):
        depth = 3 - a - b
        for idx, _, _, _, lvl, k in sorted(faces, key=lambda f: V[f[0]][:, depth].mean()):
            if np.cross(V[idx[1]] - V[idx[0]], V[idx[2]] - V[idx[0]])[depth] < 0:
                continue
            c = tuple(int(x) for x in 255 * np.clip(cols[k[0] % len(cols)], 0, 1) ** (1 / 2.2) * LEVELS[lvl])
            dr.polygon([(200 + w * 400 + V[i][a] * s, 200 - V[i][b] * s) for i in idx], fill=c)
    img.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('glb'); ap.add_argument('out'); ap.add_argument('--ref', required=True); ap.add_argument('--art', required=True, help='folder with VGA.ACT and FURY*.RAW'); ap.add_argument('--min-part', type=float, default=0.04)
    ap.add_argument('--replace', default='SHIP.BIN'); ap.add_argument('--tris', type=int, default=700); ap.add_argument('--name', default='MYSHIP')
    ap.add_argument('--lod', type=float, default=0, help='how many model lengths away the cheap face list takes over'); ap.add_argument('--low-part', type=float, default=0.1, help='smallest part kept in it, as a fraction of the length')
    ap.add_argument('--rings', default='', help='sides,stations: round every part into a fitted prism'); ap.add_argument('--yaw', type=int, default=0, help='90: turn +x to +z first')
    ap.add_argument('--core', type=float, default=0, help='a solid bar through what lies within this of the centreline')
    ap.add_argument('--strut', default='', help='near,far: a solid strut each side through what lies between')
    ap.add_argument('--bent', action='store_true', help='panels are folded along y = 0')
    ap.add_argument('--round-only', action='store_true', help='make prisms only of parts round in cross-section')
    ap.add_argument('--textured', action='store_true', help="paint the model's own image textures into the bake")
    ap.add_argument('--glow', action='store_true', help='a throttle-driven glow behind each wing engine')
    ap.add_argument('--floor', action='store_true', help='stand on y = 0'); ap.add_argument('--square-up', action='store_true'); ap.add_argument('--forward', type=int, default=0, help='+1 or -1: the nose already points along +Z or -Z'); ap.add_argument('--fit', type=float, default=0, help='longest side as a fraction of the ref length')
    ap.add_argument('--also', default='', help='other mods to list in pod.ini'); ap.add_argument('--panels', type=float, default=0, help='pieces centred further out than this along X become one solid plate a side')
    ap.add_argument('--part-faces', type=int, default=20); ap.add_argument('--keep-size', type=float, default=9000, help='parts longer than this are not reduced'); ap.add_argument('--stretch', type=float, default=2.2); ap.add_argument('--thin', type=float, default=3500)
    ap.add_argument('--bake', action='store_true', help='256x256 texture painted from the full model'); ap.add_argument('--drop-below', type=float, default=None, help='remove pieces reaching under this y (landing gear)')
    ap.add_argument('--wings', type=float, default=0, help='open angle of each wing'); ap.add_argument('--wing-y', type=float, default=0); ap.add_argument('--wing-root', type=float, default=1500); ap.add_argument('--wing-body', type=float, default=2200)
    a = ap.parse_args()
    r = open(a.ref, 'rb').read()
    ver, scale, _, _, nv = struct.unpack_from('<5I', r, 0)
    rv = np.frombuffer(r, '<i4', nv * 3, 20).reshape(nv, 3)
    textured = {}
    mats, cols = load_glb(a.glb, textured)
    print('source triangles', sum(len(t) for _, t in mats.values()), 'materials', len(mats))
    if a.yaw:
        mats = {k: (np.c_[-v[:, 2], v[:, 1], v[:, 0]], t) for k, (v, t) in mats.items()}
    if a.square_up:
        mats = square_up(mats)
    mats = orient(mats, rv.min(0), rv.max(0), a.forward, a.fit, a.floor)
    src = None
    if a.textured:
        side = lambda c: 0 if not a.panels or abs(c) < a.panels else 5 if c > 0 else 6
        src = {}
        for k, (v, t) in mats.items():
            uv, img = textured[k]
            src[k] = (v[t], uv[t], img, [side(c) for c in v[t].mean(1)[:, 0]])
    if a.drop_below is not None:
        mats = {k: drop_reaching(*m, a.drop_below) for k, m in mats.items()}
    hi = split_wings(mats, a.wing_y, a.wing_body) if a.wings else split_panels(mats, a.panels) if a.panels else {(k, 0): (weld(*m)) for k, m in mats.items()}
    colours = ship_colours(a.art)
    tex_name = a.name.upper() + '.RAW'

    def second_pose(vs, groups):
        if not a.wings:
            return b''
        p = open_pose(vs, groups, a.wings, a.wing_root, a.wing_y)
        return b'WING' + struct.pack('<I', len(vs)) + b''.join(struct.pack('<3i', *q) for q in p)
    if a.bake:              # the texture carries the colours, so parts need not stay apart
        joined = {}
        for (k, g), (v, t) in hi.items():
            vs0, ts0, n0 = joined.get(g, ([], [], 0))
            joined[g] = (vs0 + [v], ts0 + [t + n0], n0 + len(v))
        length = np.ptp(np.vstack([v for v, _ in hi.values()])[:, 2])
        lo = {}
        for g, (v, t, _) in joined.items():
            if g >= 5:
                w = np.vstack(v)
                if a.bent:              # folded along the middle: two plates that meet there
                    up = plate(w[w[:, 1] >= -1]); down = plate(w[w[:, 1] <= 1])
                    lo[(0, g)] = (np.vstack((up[0], down[0])), np.vstack((up[1], down[1] + len(up[0]))))
                else:
                    lo[(0, g)] = plate(w)
                continue
            whole = weld(np.vstack(v), np.vstack(t))
            lo[(0, g)] = low_poly_parts(*merge_coplanar(*drop_small_parts(*whole, a.min_part * length)), a.part_faces, a.stretch, a.thin, a.keep_size, tuple(map(int, a.rings.split(","))) if a.rings else None, a.round_only)
            if a.core and g == 0:       # small central pieces, as one solid bar
                wv, wt = whole
                bv, bt = core_bar(wv, wt, a.core)
                lv, lt = lo[(0, g)]
                lo[(0, g)] = (np.vstack((lv, bv)), np.vstack((lt, bt + len(lv))))
            if a.strut and g == 0:      # a solid strut each side, through what lies between these
                near, far = map(float, a.strut.split(','))
                wv, wt = whole
                cx = wv[wt].mean(1)[:, 0]
                for sign in (1, -1):
                    keep = (cx * sign > near) & (cx * sign < far)
                    if keep.any():
                        bv, bt = prism(wv, wt[keep], 6, 2)
                        lv, lt = lo[(0, g)]
                        lo[(0, g)] = (np.vstack((lv, bv)), np.vstack((lt, bt + len(lv))))
        low = None; dist = 0
        if a.lod:
            size = max(np.ptp(np.vstack([v for v, _ in hi.values()]), axis=0))
            low = {(1, g): low_detail(*lo[(0, g)], a.low_part * size) for _, g in lo}
            low = {k: m for k, m in low.items() if len(m[1])}
            dist = a.lod * size * 65536 / scale       # the runtime compares in world units
        tex, atlas_rgb, uv = bake(hi, {**lo, **(low or {})}, cols, colours, a.wing_y, src, not a.textured)
        os.makedirs(a.out, exist_ok=True)
        Image.fromarray(atlas_rgb).resize((768, 768), Image.NEAREST).save(os.path.join(a.out, a.name.lower() + '_texture.png'))
        binf, _, vs, groups, faces, _ = build(lo, cols, {'scale': scale}, colours, tex_name, uv, low, dist, second_pose, engine_ends(lo) if a.glow else None)
    else:
        lo = simplify(hi, a.tris, a.min_part)
        binf, tex, vs, groups, faces, _ = build(lo, cols, {'scale': scale}, colours, tex_name, after=second_pose)
    if a.wings:
        opened = open_pose(vs, groups, a.wings, a.wing_root, a.wing_y)
        preview(os.path.join(a.out, a.name.lower() + '_preview_open.png'), opened, faces, cols)
    os.makedirs(os.path.join(a.out, 'mods'), exist_ok=True)
    pod = make_pod([('MODELS\\' + n.upper(), binf) for n in a.replace.split(',')] + [('ART\\' + tex_name, tex)])
    open(os.path.join(a.out, 'mods', a.name.lower() + '.pod'), 'wb').write(pod)
    with open(os.path.join(a.out, 'pod.ini'), 'w', newline='\r\n') as f:
        mods = [a.name.lower()] + [m for m in a.also.split(',') if m]
        f.write('%d\n' % (len(mods) + 2) + ''.join('.\\mods\\%s.pod\n' % m for m in mods) + '.\\system\\startup.pod\n.\\system\\fury3.pod\n')
    preview(os.path.join(a.out, a.name.lower() + '_preview.png'), vs, faces, cols)
    V = np.array(vs)
    print('result: %d verts, %d faces, model %d bytes; box %s .. %s; ship box %s .. %s' % (len(vs), len(faces), len(binf), V.min(0), V.max(0), rv.min(0), rv.max(0)))


if __name__ == '__main__':
    main()
