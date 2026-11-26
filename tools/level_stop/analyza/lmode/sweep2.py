import sys; sys.path.insert(0,sys.argv[1]); from det import *
import statistics as S
RATE=0.22*0.05  # mm na vzorek toku (0,22 mm/s, 20 vz./s) - jen odhad
data=[]
for fn in sys.argv[2:]:
    dry,cyc,h,st=load(fn)
    for c,rows in sorted(cyc.items()):
        if c in st and c in h:
            kr,_=run2(rows,RAW); data.append((fn.split('/')[-2],c,h[c],rows,dry,kr))
def ev(P,gain=1.0,stepraw=False,show=False):
    hs=[];crit={};miss=0
    for name,c,h,rows,dry,kr in data:
        k,r=run2(rows,P,dry,gain,stepraw)
        if k is None: miss+=1; continue
        crit[r]=crit.get(r,0)+1; he=h+RATE*(kr-k); hs.append(he)
        if show: print('  ',name,c,'h',h,'dk',kr-k,r,round(he,2))
    return miss,hs,crit
for q in [0.50,0.51,0.52,0.53,0.54,0.56]:
  for tpl in [0.0,0.62,0.65,0.68]:
    for sr in [False,True]:
      P=dict(q=q,tpl=tpl,arm=0.85,peak=0.10,step=0.15)
      miss,hs,crit=ev(P,stepraw=sr)
      if miss: print(q,tpl,'skok_hrube' if sr else 'skok_L','nezastavi',miss); continue
      print(q,tpl,'skok_hrube' if sr else 'skok_L',round(S.mean(hs),2),'+-',round(S.stdev(hs),2),round(min(hs),2),round(max(hs),2),crit)
