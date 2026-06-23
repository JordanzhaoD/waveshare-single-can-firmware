#!/usr/bin/env python3
# candump (.txt) analyzer for reporter's real steer-jerk logs.
# Format: (secs) can0 HEXID#HEXDATA
# Decodes 0x399 (DAS), 0x3EE (FSD activation / bit46), 0x488 (steer, 4B), 0x145 (8B).
import re, os, sys
D = os.path.dirname(os.path.abspath(__file__))
REAL = os.path.join(D, "real")
LINE = re.compile(r'^\(([0-9.]+)\)\s+can0\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)')
DAS = {0:"off",1:"standby?",2:"avail?",3:"standby",4:"eng?",5:"eng?",
       6:"ENGAGED",7:"?",8:"WARN/handover",9:"FAULT/abort",15:"?"}

def load(name):
    rows=[]
    with open(os.path.join(REAL,name)) as f:
        for ln in f:
            m=LINE.match(ln.strip())
            if not m: continue
            t=float(m.group(1)); cid=int(m.group(2),16)
            h=m.group(3); b=[int(h[i:i+2],16) for i in range(0,len(h),2)]
            rows.append((t,cid,b))
    return rows

def s16(hi,lo):
    v=(hi<<8)|lo
    return v-65536 if v>=32768 else v

def analyze(name):
    rows=load(name)
    if not rows: print(f"{name}: EMPTY"); return None
    t0=rows[0][0]; tend=rows[-1][0]-t0
    print("="*82)
    print(f"  {name}   span={tend:.2f}s  frames={len(rows)}")
    # ID histogram
    from collections import Counter
    hist=Counter(cid for _,cid,_ in rows)
    print(f"  IDs: " + " ".join(f"0x{k:X}({v})" for k,v in sorted(hist.items())))
    print("="*82)

    # 0x399 status transitions
    das=[(t,b) for t,cid,b in rows if cid==0x399]
    print("\n[0x399] DAS status (b0&0x0F) transitions:")
    prev=None; engage=None; fault=None
    for t,b in das:
        st=b[0]&0x0F
        if st!=prev:
            rel=t-t0
            if st==6 and engage is None: engage=rel
            if st in (8,9) and fault is None: fault=rel
            print(f"  t={rel:7.2f}s  status={st:<2} ({DAS.get(st,'?'):<13})")
            prev=st

    # 0x3EE injection
    ee=[(t,b) for t,cid,b in rows if cid==0x3EE]
    print(f"\n[0x3EE] FSD activation ({len(ee)} frames):")
    inj=[]
    for t,b in ee:
        if len(b)<8: continue
        mux=b[0]&0x07; bit46=bool(b[5]&0x40); ui=(b[4]>>6)&1
        mark="*INJ" if bit46 else "    "
        tag=""
        if bit46: inj.append(t-t0)
        print(f"  t={t-t0:7.2f}s {mark} mux={mux} bit46={int(bit46)} uiSel={ui} {[f'{x:02X}' for x in b]}")
    print(f"  -> bit46=1 injection count: {len(inj)}" + (f"  first @ {inj[0]:.2f}s" if inj else ""))

    # 0x488 steering (4 bytes) — track raw 16-bit words + spike detection
    s=[(t,b) for t,cid,b in rows if cid==0x488]
    if s:
        w0=[s16(b[0],b[1]) for t,b in s]
        w1=[s16(b[2],b[3]) if len(b)>=4 else 0 for t,b in s]
        ts=[t-t0 for t,b in s]
        # max frame-to-frame delta
        dmax=0; dmax_t=None
        for i in range(1,len(s)):
            d=abs(w0[i]-w0[i-1])
            if d>dmax: dmax=d; dmax_t=ts[i]
        print(f"\n[0x488] steering ({len(s)} frames): w0[b0b1] range=[{min(w0)}..{max(w0)}] maxStep={dmax}@{dmax_t:.2f}s  w1[b2b3] range=[{min(w1)}..{max(w1)}]")
        # show largest steps
        steps=sorted(((abs(w0[i]-w0[i-1]), ts[i], w0[i-1], w0[i]) for i in range(1,len(s))), reverse=True)[:5]
        for d,t,a,c in steps:
            print(f"    step |Δ|={d:<6} @ t={t:.2f}s  {a} -> {c}")

    # 0x145 (8 bytes) — track changes in key bytes
    p=[(t,b) for t,cid,b in rows if cid==0x145]
    if p:
        print(f"\n[0x145] ({len(p)} frames) byte-change scan (b0..b3):")
        prev=None
        for t,b in p:
            key=tuple(b[:4])
            if key!=prev:
                print(f"  t={t-t0:7.2f}s  {[f'{x:02X}' for x in b]}")
                prev=key

    # correlation
    print("\n[CORRELATION]:")
    print(f"  AP ENGAGE (->6):  {f'{engage:.2f}s' if engage else '—'}")
    if inj and engage: print(f"  first bit46 INJ:  {inj[0]:.2f}s   (Δfrom engage = {inj[0]-engage:+.2f}s)")
    elif inj: print(f"  first bit46 INJ:  {inj[0]:.2f}s (no engage seen)")
    else: print(f"  bit46 INJ:        NEVER (no FSD activation injection in log)")
    if fault and engage: print(f"  AP FAULT (->8/9): {fault:.2f}s   (Δfrom engage = {fault-engage:+.2f}s)")
    print()
    return {"engage":engage,"fault":fault,"inj":inj}

for n in ["ids_3EE_399_488_118_145-normal.txt",
          "ids_3EE_399_488_118_145-steerjerkerror-4.txt",
          "ids_3EE_399_488_118_145-steerjerkerror-16.txt"]:
    if os.path.exists(os.path.join(REAL,n)):
        analyze(n)
