import sys
import statistics as st, re, json
R={}; ev={}
cyc=None
for l in open(sys.argv[1] if len(sys.argv) > 1 else 'test5_profil.txt'):
    l=l.strip()
    m=re.match(r'# ===== CYKLUS (\d+)',l)
    if m: cyc=int(m.group(1))
    m=re.match(r'# STIN (STOP|POJISTKA C1) @ ([\d.]+) ml\s+C1=([\d.]+) C3=([\d.]+) delta=([\d.]+)',l)
    if m: ev.setdefault(cyc,{})[m.group(1)]=dict(ml=float(m.group(2)),c1=float(m.group(3)),c3=float(m.group(4)),d=float(m.group(5)))
    p=l.split(';')
    if len(p)==7 and p[0].isdigit():
        try: R.setdefault(int(p[0]),[]).append((int(p[1]),p[2],float(p[3]),float(p[4]),float(p[5]),float(p[6])))
        except: pass
F3=2.806
def medbin(rows,i,b,w=0.25): 
    s=[r[i] for r in rows if b<=r[2]<b+w]; return st.median(s) if s else None
out={}
for c in sorted(R):
    rows=R[c]; w=[r for r in rows if r[1]=='w']
    bins=[x/4 for x in range(0,80)]
    c1=[(b,medbin(w,3,b)) for b in bins]; c1=[x for x in c1 if x[1]]
    c3=[(b,medbin(w,5,b)) for b in bins]; c3=[x for x in c3 if x[1]]
    def half(series,after):
        pk=max((v for b,v in series if b>=after),default=None)
        ib=[b for b,v in series if v==pk][0]
        fl=min(v for b,v in series if b>ib)
        h=(pk+fl)/2
        for b,v in series:
            if b>ib and v<h: return b+0.125,pk,fl
    h3=half(c3,3); h1=half(c1,h3[0])
    o=[r for r in rows if r[1]=='o']; k=[r for r in rows if r[1]=='k']
    out[c]=dict(stop=ev[c]['STOP'],guard=ev[c]['POJISTKA C1'],c3half=h3[0],c1half=h1[0],
               c3pk=h3[1],c3fl=h3[2],c1pk=h1[1],c1fl=h1[2],
               k=[st.median(r[i] for r in k) for i in (3,4,5)],o=[st.median(r[i] for r in o) for i in (3,4,5)])
for c,v in out.items():
    print(c, f"STOP {v['stop']['ml']:.2f} (d={v['stop']['d']:.2f})  POJ {v['guard']['ml']:.2f}  rezerva {v['guard']['ml']-v['stop']['ml']:.2f} | C3 50% {v['c3half']:.2f} C1 50% {v['c1half']:.2f} rozestup {v['c1half']-v['c3half']:.2f} | STOP->C1 50% {v['c1half']-v['stop']['ml']:.2f} | C3 {v['c3pk']:.2f}->{v['c3fl']:.2f} C1 {v['c1pk']:.2f}->{v['c1fl']:.2f}")
json.dump(out,open('analyza/summary.json','w'),indent=1)
m=[out[c]['guard']['ml']-out[c]['stop']['ml'] for c in (2,3,4,5)]
print("rezerva c2-5: mean %.2f sd %.2f"%(st.mean(m),st.stdev(m)))
m=[out[c]['c1half']-out[c]['stop']['ml'] for c in (2,3,4,5)]
print("STOP->C1 50%% c2-5: mean %.2f sd %.2f"%(st.mean(m),st.stdev(m)))
m=[out[c]['c1half']-out[c]['c3half'] for c in (1,2,3,4,5)]
print("rozestup C3->C1: mean %.2f sd %.2f"%(st.mean(m),st.stdev(m)))

# delta prechody pro ruzne alfa (median 10 vzorku, F3 a D1 z uceni c1)
D1=3.533
alphas=[0.5,0.6,0.7,0.8,0.9,0.95]
cross={}
for c in (2,3,4,5):
    rows=[r for r in R[c] if r[1] in 'pw']
    b1=[];b3=[];res={}
    for t,ph,ml,c1,c2,c3 in rows:
        b1=(b1+[c1])[-10:]; b3=(b3+[c3])[-10:]
        if len(b1)<10: continue
        m1=st.median(b1); m3=st.median(b3); den=m1-F3
        if den<0.3*D1: continue
        d=(m1-m3)/den
        for a in alphas:
            if a not in res and d>=a: res[a]=ml
    cross[c]=res
for a in alphas:
    v=[cross[c].get(a) for c in (2,3,4,5)]
    rel=[out[c]['guard']['ml']-cross[c][a] for c in (2,3,4,5) if a in cross[c]]
    print(f"alfa {a}: ml {v}  rezerva do pojistky {st.mean(rel):.2f} +- {st.stdev(rel):.2f}")
json.dump({str(c):{str(a):m for a,m in cross[c].items()} for c in cross},open('analyza/cross.json','w'))
