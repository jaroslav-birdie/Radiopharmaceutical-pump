import sys; sys.path.insert(0,sys.argv[1]); from det import *
import statistics as S
out=[]
for fn in sys.argv[2:]:
    dry,cyc,h,st=load(fn)
    for c,rows in sorted(cyc.items()):
        if c not in st: continue
        t,r,d=run(rows,RAW)
        # L track: replay windows in L-domain up to raw stop t
        P=dict(RAW); P['q']=-1; P['tpl']=-1; P['peak']=9; P['step']=9
        dl=Det(P); prev=None; qmin_pre=9; trace=[]
        for tt,ph,c1,c3 in rows:
            fl=ph in 'pw'
            if fl and prev not in ('p','w') and prev is not None: dl.flowbreak()
            prev=ph
            if not fl: continue
            dl.feed(c1-dry[0],c3-dry[1])
            if dl.n==0 and len(dl.b1)==WIN:
                w1,w3=dl.w; trace.append((tt,w1,w3,dl.c3max,dl.armed))
            if tt>=t: break
        w1,w3=trace[-1][1],trace[-1][2]
        pre=[x[2]/x[1] for x in trace if not x[4] and x[1]>0]
        k=[(c1-dry[0],c3-dry[1]) for tt,ph,c1,c3 in rows if ph=='k']
        k1=S.mean(a for a,b in k); k3=S.mean(b for a,b in k)
        out.append((fn.split('/')[-2],c,h.get(c),round(w3/w1,3),round(w3/trace[-1][3],3),round(min(pre),3) if pre else None,round(k3/k1,3),round(k1,2),round(k3,2)))
print('test cyk h  qL@stop tplL@stop qLmin_pred_aktivaci  klid_qL klid_L1 klid_L3')
for o in out: print(*o)
