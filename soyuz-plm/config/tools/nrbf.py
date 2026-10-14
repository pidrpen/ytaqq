"""Minimal MS-NRBF reader/writer with exact round-trip."""
import struct, io

PRIM_SIZE = {1: 1, 2: 1, 6: 8, 7: 2, 8: 4, 9: 8, 10: 1, 11: 4, 12: 8, 13: 8, 14: 2, 15: 4, 16: 8, 17: 0}
PRIM_FMT = {1: '<?', 2: '<B', 6: '<d', 7: '<h', 8: '<i', 9: '<q', 10: '<b', 11: '<f', 12: '<q', 13: '<Q', 14: '<H', 15: '<I', 16: '<Q'}


class R:
    def __init__(s, b): s.b = b; s.p = 0
    def read(s, n):
        v = s.b[s.p:s.p + n]; assert len(v) == n; s.p += n; return v
    def u8(s): return s.read(1)[0]
    def i32(s): return struct.unpack('<i', s.read(4))[0]
    def lps(s):
        n = 0; sh = 0
        while True:
            c = s.u8(); n |= (c & 0x7f) << sh; sh += 7
            if not c & 0x80: break
        return s.read(n).decode('utf-8')


def w_lps(out, st):
    b = st.encode('utf-8'); n = len(b)
    while True:
        c = n & 0x7f; n >>= 7
        if n: out.write(bytes([c | 0x80]))
        else: out.write(bytes([c])); break
    out.write(b)


def r_prim(r, t):
    if t == 18 or t == 5: return r.lps()
    if t == 3:
        c = r.u8(); bs = bytes([c])
        if c >= 0xc0:
            extra = 1 if c < 0xe0 else 2 if c < 0xf0 else 3
            bs += r.read(extra)
        return bs.decode('utf-8')
    if t == 17: return None
    return struct.unpack(PRIM_FMT[t], r.read(PRIM_SIZE[t]))[0]


def w_prim(out, t, v):
    if t == 18 or t == 5: w_lps(out, v); return
    if t == 3: out.write(v.encode('utf-8')); return
    if t == 17: return
    out.write(struct.pack(PRIM_FMT[t], v))


class Doc:
    """records: list of dicts in stream order (top-level). Nested records live inside values."""

    def __init__(s, data):
        s.meta = {}  # objectId -> (classinfo, typeinfo or None, lib)
        r = R(data)
        s.records = []
        while True:
            rec = s.read_record(r)
            s.records.append(rec)
            if rec['t'] == 11: break
        s.trailer = data[r.p:]

    def read_classinfo(s, r):
        oid = r.i32(); name = r.lps(); n = r.i32()
        return {'id': oid, 'name': name, 'members': [r.lps() for _ in range(n)]}

    def read_typeinfo(s, r, n):
        bts = [r.u8() for _ in range(n)]; add = []
        for bt in bts:
            if bt in (0, 7): add.append(r.u8())
            elif bt == 3: add.append(r.lps())
            elif bt == 4: add.append((r.lps(), r.i32()))
            else: add.append(None)
        return {'bt': bts, 'add': add}

    def read_values(s, r, ci, ti):
        vals = []; pending_null = 0
        for i in range(len(ci['members'])):
            if pending_null:
                vals.append({'t': 'nullcont'}); pending_null -= 1; continue
            if ti is not None and ti['bt'][i] == 0:
                vals.append({'t': 'prim', 'pt': ti['add'][i], 'v': r_prim(r, ti['add'][i])})
            else:
                rec = s.read_record(r); vals.append(rec)
                if rec['t'] in (13, 14): pending_null = rec['n'] - 1
        return vals

    def read_elems(s, r, n, prim=None):
        vals = []; i = 0
        while i < n:
            if prim is not None:
                vals.append({'t': 'prim', 'pt': prim, 'v': r_prim(r, prim)}); i += 1
            else:
                rec = s.read_record(r); vals.append(rec)
                i += rec['n'] if rec['t'] in (13, 14) else 1
        return vals

    def read_record(s, r):
        t = r.u8(); rec = {'t': t}
        if t == 0:
            rec['v'] = struct.unpack('<iiii', r.read(16))
        elif t == 12:
            rec['id'] = r.i32(); rec['name'] = r.lps()
        elif t in (4, 5, 2, 3):
            ci = s.read_classinfo(r)
            ti = s.read_typeinfo(r, len(ci['members'])) if t in (4, 5) else None
            lib = r.i32() if t in (5, 3) else None
            rec.update(ci=ci, ti=ti, lib=lib)
            s.meta[ci['id']] = (ci, ti)
            rec['vals'] = s.read_values(r, ci, ti)
        elif t == 1:
            rec['id'] = r.i32(); rec['mid'] = r.i32()
            ci, ti = s.meta[rec['mid']]
            s.meta[rec['id']] = (ci, ti)
            rec['vals'] = s.read_values(r, ci, ti)
        elif t == 6:
            rec['id'] = r.i32(); rec['s'] = r.lps()
        elif t == 7:
            rec['id'] = r.i32(); rec['at'] = r.u8(); rank = r.i32()
            rec['lens'] = [r.i32() for _ in range(rank)]
            rec['lbs'] = [r.i32() for _ in range(rank)] if rec['at'] in (3, 4, 5) else None
            rec['et'] = r.u8()
            et = rec['et']
            if et in (0, 7): rec['ea'] = r.u8()
            elif et == 3: rec['ea'] = r.lps()
            elif et == 4: rec['ea'] = (r.lps(), r.i32())
            else: rec['ea'] = None
            total = 1
            for l in rec['lens']: total *= l
            rec['vals'] = s.read_elems(r, total, rec['ea'] if et == 0 else None)
        elif t == 8:
            rec['pt'] = r.u8(); rec['v'] = r_prim(r, rec['pt'])
        elif t == 9:
            rec['ref'] = r.i32()
        elif t == 10 or t == 11:
            pass
        elif t == 13:
            rec['n'] = r.u8()
        elif t == 14:
            rec['n'] = r.i32()
        elif t == 15:
            rec['id'] = r.i32(); n = r.i32(); rec['pt'] = r.u8()
            if rec['pt'] == 2:
                rec['raw'] = r.read(n)
            else:
                rec['vals'] = [r_prim(r, rec['pt']) for _ in range(n)]
        elif t in (16, 17):
            rec['id'] = r.i32(); n = r.i32(); rec['len'] = n
            rec['vals'] = s.read_elems(r, n)
        else:
            raise ValueError(f'record type {t} at {r.p}')
        return rec

    # ---- writer ----
    def dump(s):
        out = io.BytesIO()
        for rec in s.records: s.w_record(out, rec)
        out.write(s.trailer)
        return out.getvalue()

    def w_vals(s, out, rec, ti):
        for i, v in enumerate(rec['vals']):
            if v['t'] == 'nullcont': continue
            if v['t'] == 'prim': w_prim(out, v['pt'], v['v'])
            else: s.w_record(out, v)

    def w_record(s, out, rec):
        t = rec['t']; out.write(bytes([t]))
        if t == 0: out.write(struct.pack('<iiii', *rec['v']))
        elif t == 12: out.write(struct.pack('<i', rec['id'])); w_lps(out, rec['name'])
        elif t in (2, 3, 4, 5):
            ci = rec['ci']; out.write(struct.pack('<i', ci['id'])); w_lps(out, ci['name'])
            out.write(struct.pack('<i', len(ci['members'])))
            for m in ci['members']: w_lps(out, m)
            ti = rec['ti']
            if ti is not None:
                out.write(bytes(ti['bt']))
                for bt, a in zip(ti['bt'], ti['add']):
                    if bt in (0, 7): out.write(bytes([a]))
                    elif bt == 3: w_lps(out, a)
                    elif bt == 4: w_lps(out, a[0]); out.write(struct.pack('<i', a[1]))
            if rec['lib'] is not None: out.write(struct.pack('<i', rec['lib']))
            s.w_vals(out, rec, ti)
        elif t == 1:
            out.write(struct.pack('<ii', rec['id'], rec['mid'])); s.w_vals(out, rec, None)
        elif t == 6: out.write(struct.pack('<i', rec['id'])); w_lps(out, rec['s'])
        elif t == 7:
            out.write(struct.pack('<iBi', rec['id'], rec['at'], len(rec['lens'])))
            for l in rec['lens']: out.write(struct.pack('<i', l))
            if rec['lbs'] is not None:
                for l in rec['lbs']: out.write(struct.pack('<i', l))
            out.write(bytes([rec['et']])); et = rec['et']; a = rec['ea']
            if et in (0, 7): out.write(bytes([a]))
            elif et == 3: w_lps(out, a)
            elif et == 4: w_lps(out, a[0]); out.write(struct.pack('<i', a[1]))
            for v in rec['vals']:
                if v['t'] == 'prim': w_prim(out, v['pt'], v['v'])
                else: s.w_record(out, v)
        elif t == 8: out.write(bytes([rec['pt']])); w_prim(out, rec['pt'], rec['v'])
        elif t == 9: out.write(struct.pack('<i', rec['ref']))
        elif t in (10, 11): pass
        elif t == 13: out.write(bytes([rec['n']]))
        elif t == 14: out.write(struct.pack('<i', rec['n']))
        elif t == 15:
            n = len(rec['raw']) if 'raw' in rec else len(rec['vals'])
            out.write(struct.pack('<iiB', rec['id'], n, rec['pt']))
            if 'raw' in rec: out.write(rec['raw'])
            else:
                for v in rec['vals']: w_prim(out, rec['pt'], v)
        elif t in (16, 17):
            out.write(struct.pack('<ii', rec['id'], rec['len']))
            for v in rec['vals']: s.w_record(out, v)
