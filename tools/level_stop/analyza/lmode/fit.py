import sys; sys.path.insert(0,sys.argv[1]); from det import *
P=dict(q=0.52,tpl=0.65,arm=0.85,peak=0.10,step=0.25)
for fn in sys.argv[2:]:
    dry,cyc,h,st=load(fn)
    for c,rows in sorted(cyc.items()):
        if c not in st: continue
        k,r=run2(rows,P,dry)
        d=Det(dict(P,q=-1,tpl=-1,peak=9,step=9)); prev=None; n=0; early=False; chk=None; qmax=None; c3m=-1
        for t,ph,c1,c3 in rows:
            fl=ph in 'pw'
            if fl and prev not in ('p','w') and prev is not None: d.flowbreak()
            prev=ph
            if not fl: continue
            n+=1
            d.feed(c1-dry[0],c3-dry[1])
            if d.n==0 and len(d.b1)==WIN:
                w1,w3=d.w
                if w3>=c3m: c3m=w3; qmax=w3/w1
                if d.armed:
                    if w3>0.65*d.c3max and w1<0.95*d.c1max: early=True
                    if chk is None and w3<=0.80*d.c3max: chk=((w1-w3)-d.d0)/d.c3max
            if n>=k: break
        print(fn.split('/')[-2],c,r,'C1_early',early,'narust',None if chk is None else round(chk,3),'qL_v_max',round(qmax,3))
