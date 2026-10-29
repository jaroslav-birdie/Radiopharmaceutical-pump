import statistics as st, json
exec(open('analyza/analyza_profil.py').read().split("F3=2.806")[0])
out={int(k):v for k,v in json.load(open('analyza/summary.json')).items()}
def run(c, ratios=(0.6,0.5,0.45,0.4)):
    rows=R[c]; k=[r for r in rows if r[1]=='k']
    c1R=st.mean(r[3] for r in k); c3R=st.mean(r[5] for r in k)
    b1=[];b3=[];hist=[];nxt=None;c1pk=c3pk=-9;smax=0;fell=False
    res={}
    prev=None
    for t,ph,ml,c1,c2,c3 in rows:
        if ph not in 'pw':
            if prev in ('p','w'): b1=[];b3=[];hist=[];nxt=ml
            prev=ph; continue
        prev=ph
        if nxt is None: nxt=ml
        b1=(b1+[c1])[-10:];b3=(b3+[c3])[-10:]
        if len(b1)<10: continue
        m1=st.median(b1);m3=st.median(b3)
        c1pk=max(c1pk,m1);c3pk=max(c3pk,m3)
        # pojistka (rezim uceni = bez uceni z jinych cyklu)
        g = m1 < 0.9*c1R or (fell and c1pk-c1R>=0.1*c1R and m1-c1R<0.5*(c1pk-c1R))
        if g and 'guard' not in res: res['guard']=ml
        if ml>=nxt:
            nxt=ml+0.25; hist=(hist+[m3])[-5:]
            if len(hist)==5:
                s=(hist[0]-hist[-1])/1.0; smax=max(smax,s)
                rise=c3pk-c3R
                fell = rise>0.1*c3R and (c3pk-m3)>0.5*rise
                if fell and s<0.3*smax and 'plateau' not in res: res['plateau']=ml
        q=m3/m1
        for q0 in ratios:
            if q<=q0 and f'q{q0}' not in res and c1pk-c1R>0.1*c1R: res[f'q{q0}']=ml
        fr=(c3pk-m3)/c3pk
        for f0 in (0.4,0.5):
            if fr>=f0 and f'f{f0}' not in res: res[f'f{f0}']=ml
    return res
allr={}
for c in sorted(R):
    r=run(c); v=out[c]; top=v['c3half']+1; bot=v['c1half']-1
    allr[c]=dict(r,top=top,bot=bot)
    print(c, f"mezera {top:.2f}-{bot:.2f} |", " ".join(f"{k}={x:.2f}" for k,x in r.items()))
json.dump(allr,open('analyza/bez_uceni.json','w'),indent=1)
print("\npoloha vuci mezere (0 = horni okraj mezery, 1 = spodni okraj):")
for key in ['plateau','q0.6','q0.5','q0.45','q0.4','f0.4','f0.5','guard']:
    rel=[(allr[c][key]-allr[c]['top'])/(allr[c]['bot']-allr[c]['top']) for c in allr if key in allr[c]]
    ml=[allr[c][key]-allr[c]['top'] for c in allr if key in allr[c]]
    print(f"{key:8s} rel {['%.2f'%x for x in rel]}  ml od horniho okraje {st.mean(ml):+.2f} +- {st.stdev(ml):.2f}")
