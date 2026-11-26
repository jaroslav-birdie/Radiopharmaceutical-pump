import re, sys
BIN=12; WIN=8
def load(fn):
    dry=None; cyc={}; heights={}; stops={}; cur=None; last=None
    for L in open(fn, errors='ignore'):
        m=re.search(r'suche hodnoty: C1=([\d.]+) C3=([\d.]+)',L)
        if m: dry=(float(m[1]),float(m[2]))
        m=re.search(r'===== CYKLUS (\d+)/',L)
        if m: cur=int(m[1])
        m=re.search(r'# STOP \S+.*?@ t=(\d+)',L)
        if m and cur: stops[cur]=int(m[1])
        # produkcni firmware: cyklus = posledni vzorek, zastaveni = 'KRITICKA HLADINA'
        if ('KRITICKA HLADINA' in L or 'SKOK C1' in L) and last and last[0] not in stops:
            stops[last[0]]=last[1]; cur=last[0]
        m=re.search(r'obsluha: ([\d]+[.,]?\d*)\s*$',L)
        if m and cur and cur not in heights:
            try: heights[cur]=float(m[1].replace(',','.'))
            except: pass
        m=re.search(r'-> (\d+);(\d+);(\w);([-\d.]+);([-\d.]+);([-\d.]+);([-\d.]+)',L)
        if m:
            cyc.setdefault(int(m[1]),[]).append((int(m[2]),m[3],float(m[5]),float(m[7])))
            last=(int(m[1]),int(m[2]))
    return dry,cyc,heights,stops

class Det:
    def __init__(s,P):
        s.P=P; s.acc1=s.acc3=0.0; s.n=0; s.b1=[];s.b3=[]; s.p1=s.p2=-1e9; s.armed=False
        s.d0=None; s.dmax=-1e9; s.c1max=-1e9; s.c3max=-1e9; s.done=None
    def flowbreak(s): s.acc1=s.acc3=0.0; s.n=0; s.p1=s.p2=-1e9
    def feed(s,c1,c3):
        if s.done: return None
        s.acc1+=c1; s.acc3+=c3; s.n+=1
        if s.n<BIN: return None
        r=s.bin(s.acc1/s.n,s.acc3/s.n); s.acc1=s.acc3=0.0; s.n=0
        if r: s.done=r
        return r
    def bin(s,c1,c3):
        P=s.P
        ref=max(s.p1,s.p2)
        if s.armed and s.p2>-1e8 and c1<(1-P['step'])*ref: s.w=(c1,c3); return 'skok'
        s.p2=s.p1; s.p1=c1
        s.b1.append(c1); s.b3.append(c3)
        if len(s.b1)>WIN: s.b1.pop(0); s.b3.pop(0)
        if len(s.b1)<WIN: return None
        w1=sum(s.b1)/WIN; w3=sum(s.b3)/WIN; s.w=(w1,w3); d=w1-w3
        if s.d0 is None: s.d0=d
        if w3>s.c3max: s.c3max=w3
        if w1>s.c1max: s.c1max=w1
        if not s.armed and w3<=P['arm']*s.c3max: s.armed=True
        if w3<=P['q']*w1: return 'podil'
        if not s.armed: return None
        if d>s.dmax: s.dmax=d
        if w3<=P['tpl']*s.c3max: return 'sablona'
        if d<s.dmax-P['peak']*(s.dmax-s.d0): return 'vrchol'
        return None

def run(rows, P, dry=(0,0), gain=1.0, off=(0,0)):
    """rows: (t,phase,c1,c3). Mode: subtract dry; gain scales liquid part; off adds to baseline."""
    d=Det(P); prev=None
    for t,ph,c1,c3 in rows:
        fl = ph in 'pw'
        if fl and prev not in ('p','w') and prev is not None: d.flowbreak()
        prev=ph
        if not fl: continue
        l1=(c1-dry[0])*gain+off[0]; l3=(c3-dry[1])*gain+off[1]
        r=d.feed(l1,l3)
        if r: return t,r,d
    return None,None,d
RAW=dict(q=0.68,tpl=0.78,arm=0.85,peak=0.10,step=0.15)

def run2(rows, P, dry=(0,0), gain=1.0, stepraw=False):
    """Vraci (index vzorku toku pri zastaveni, kriterium). stepraw: skok hlida hrube C1."""
    d=Det(P); prev=None; k=0
    ds=Det(dict(q=-1,tpl=-1,arm=P['arm'],peak=9,step=P['step'])) if stepraw else None
    for t,ph,c1,c3 in rows:
        fl = ph in 'pw'
        if fl and prev not in ('p','w') and prev is not None:
            d.flowbreak()
            if ds: ds.flowbreak()
        prev=ph
        if not fl: continue
        k+=1
        if ds:
            ds.armed=d.armed
            rs=ds.feed(c1,c3)
            if rs=='skok': return k,'skok'
            d.P=dict(P,step=9)
        r=d.feed((c1-dry[0])*gain,(c3-dry[1])*gain)
        if r: return k,r
    return None,None
