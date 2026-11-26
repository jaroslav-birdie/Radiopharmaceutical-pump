import sys; sys.path.insert(0,sys.argv[1]); from det import *
import statistics as S
RATE=0.22*0.05
for g in [1.0,0.7,0.5]:
    hs=[];miss=0;crit={}
    for fn in sys.argv[2:]:
        dry,cyc,h,st=load(fn)
        for c,rows in sorted(cyc.items()):
            if c not in st or c not in h: continue
            kr,_=run2(rows,RAW)
            # hruby detektor, kapalina zeslabena g-krat (sucha hodnota zustava)
            rr=[(t,ph,dry[0]+(c1-dry[0])*g,dry[1]+(c3-dry[1])*g) for t,ph,c1,c3 in rows]
            k,r=run2(rr,RAW)
            if k is None: miss+=1; continue
            crit[r]=crit.get(r,0)+1; hs.append(h[c]+RATE*(kr-k))
    print('hruby, kapalina x',g,'nezastavi do konce dat:',miss,'z 20', (round(S.mean(hs),2) if hs else ''), crit)
