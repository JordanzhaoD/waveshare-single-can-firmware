#!/usr/bin/env python3
# Compact NORMAL vs JERK comparison for reporter's real candump logs.
import re, os
from collections import Counter
D = os.path.dirname(os.path.abspath(__file__))
REAL = os.path.join(D, "real")
LINE = re.compile(r'^\(([0-9.]+)\)\s+can0\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)')
DAS = {0:"off",1:"standby?",2:"avail",3:"standby",4:"eng?",5:"eng?",
       6:"ENGAGED",7:"?",8:"WARN",9:"FAULT",15:"?"}

def load(name):
    r=[]
    with open(os.path.join(REAL,name)) as f:
        for ln in f:
            m=LINE.match(ln.strip())
            if not m: continue
            h=m.group(3); b=[int(h[i:i+2],16) for i in range(0,len(h),2)]
            r.append((float(m.group(1)),int(m.group(2),16),b))
    return r

def s16(hi,lo):
    v=(hi<<8)|lo; return v-65536 if v>=32768 else v

def compact(name):
    rows=load(name); t0=rows[0][0]
    print(f"\n{'='*70}\n  {name}   span={rows[-1][0]-t0:.2f}s  frames={len(rows)}\n{'='*70}")
    # 0x399
    das=[(t,b) for t,c,b in rows if c==0x399]
    prev=None; engage=fault=None; seq=[]
    for t,b in das:
        st=b[0]&0x0F
        if st!=prev:
            seq.append((t-t0,st)); prev=st
            if st==6 and engage is None: engage=t-t0
            if st in (8,9) and fault is None: fault=t-t0
    print("[0x399] status seq: "+" -> ".join(f"{s}({DAS.get(s,'?')})@{t:.2f}s" for t,s in seq))
    # 0x3EE
    ee=[b for c,b in ((c,b) for t,c,b in rows if c==0x3EE) if len(b)>=6]
    ee=[(t,b) for t,c,b in rows if c==0x3EE and len(b)>=6]
    inj=[t-t0 for t,b in ee if b[5]&0x40]
    muxes=Counter(b[0]&7 for t,b in ee)
    print(f"[0x3EE] {len(ee)} frames mux={dict(muxes)}  bit46=1(inject visible): {len(inj)}")
    # 0x488 steering command
    s=[(t,b) for t,c,b in rows if c==0x488 and len(b)>=2]
    if s:
        w=[s16(b[0],b[1]) for t,b in s]; ts=[t-t0 for t,b in s]
        steps=sorted(((abs(w[i]-w[i-1]), ts[i], w[i-1], w[i]) for i in range(1,len(s))), reverse=True)
        print(f"[0x488] steerCmd {len(s)}fr  range=[{min(w)}..{max(w)}]  top steps:")
        for d,t,a,c in steps[:4]:
            print(f"        |Δ|={d:<6} @t={t:.2f}s  {a}->{c}")
    # correlation
    print("[CORR] engage=%s fault=%s Δengage→fault=%s"%(
        f"{engage:.2f}s" if engage else "—",
        f"{fault:.2f}s" if fault else "—",
        f"{fault-engage:+.2f}s" if (engage and fault) else "—"))

for n in ["ids_3EE_399_488_118_145-normal.txt",
          "ids_3EE_399_488_118_145-steerjerkerror-4.txt",
          "ids_3EE_399_488_118_145-steerjerkerror-16.txt"]:
    if os.path.exists(os.path.join(REAL,n)): compact(n)
