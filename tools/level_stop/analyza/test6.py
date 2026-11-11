# Test 6 - analyza profilu ve studni (5 cyklu po 20 ml vzduchu).
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test6.py <log.txt> [vystupni_slozka_obrazku]
# Vypise polohy priznaku vuci skoku a ulozi obr1..obr4 (matplotlib).
import json, statistics as st, sys
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
LOG=sys.argv[1]
OUT=sys.argv[2] if len(sys.argv)>2 else 'img6'
import os; os.makedirs(OUT, exist_ok=True)
import statistics as st, sys
R={}
for l in open(LOG):
    p=l.strip().split(';')
    if len(p)==7 and p[0].isdigit():
        try: R.setdefault(int(p[0]),[]).append((int(p[1]),p[2],float(p[3]),float(p[4]),float(p[5]),float(p[6])))
        except: pass
med=lambda rs,i: st.median(r[i] for r in rs)

def segs(rows):
    out=[];cur=[];b=[]
    for t,ph,ml,c1,c2,c3 in rows:
        if ph not in 'pw':
            if cur: out.append(cur); cur=[]; b=[]
            continue
        b=(b+[(c1,c2,c3)])[-10:]
        if len(b)<10: continue
        cur.append((ml,st.median(x[0] for x in b),st.median(x[1] for x in b),st.median(x[2] for x in b)))
    if cur: out.append(cur)
    return out
res={}
for c in range(1,6):
    S=[p for s in segs(R[c]) for p in s]
    # skok: nejvetsi pokles C1+C2 behem 0.3 ml
    best=(0,None)
    for i in range(len(S)):
        j=i
        while j<len(S) and S[j][0]-S[i][0]<0.3: j+=1
        if j<len(S):
            d=(S[i][1]+S[i][2])-(S[j][1]+S[j][2])
            if d>best[0]: best=(d,S[j][0])
    step=best[1]
    pre=[p for p in S if p[0]<step-0.3]
    # C3: vysoka hodnota = median v 1 ml pred zacatkem poklesu; floor = median C3 v [step-1.5, step-0.3]
    c3hi=max(st.median(p[3] for p in pre if b<=p[0]<b+1) for b in [x/2 for x in range(6,30)] if any(b<=p[0]<b+1 for p in pre))
    c3lo=st.median(p[3] for p in pre if step-1.5<=p[0])
    bm=[(b,st.median(p[3] for p in pre if b<=p[0]<b+0.5)) for b in [x/4 for x in range(10,80)] if sum(1 for p in pre if b<=p[0]<b+0.5)>3]
    c3pkml=max(bm,key=lambda x:x[1])[0]
    aft=[p for p in pre if p[0]>c3pkml]
    c3_50=next(p[0] for p in aft if p[3] < (c3hi+c3lo)/2)
    c3_10=next(p[0] for p in aft if p[3] < c3hi-0.1*(c3hi-c3lo))
    c3_90=next(p[0] for p in aft if p[3] < c3hi-0.9*(c3hi-c3lo))
    q=[(p[0],p[3]/p[1]) for p in pre]
    qmin=min(st.median(v for m,v in q if b<=m<b+0.5) for b in [x/4 for x in range(16,80)] if sum(1 for m,v in q if b<=m<b+0.5)>3)
    cross={}
    for th in (0.8,0.7,0.65,0.6):
        cross[th]=next((m for m,v in q if m>c3pkml and v<=th),None)
    c1max=max(pre,key=lambda p:p[1])
    res[c]=dict(step=step,stepdrop=best[0],c3hi=c3hi,c3lo=c3lo,c3_10=c3_10,c3_50=c3_50,c3_90=c3_90,qmin=qmin,cross=cross)
    print(f"c{c}: skok @{step:.2f} (pokles C1+C2 {best[0]:.2f} pF) | C3 {c3hi:.2f}->{c3lo:.2f}: 10% {c3_10:.2f} 50% {c3_50:.2f} 90% {c3_90:.2f} | qmin {qmin:.2f} | q<=0.8 {cross[0.8]} 0.7 {cross[0.7]} 0.65 {cross[0.65]} 0.6 {cross[0.6]} | C3_50->skok {step-c3_50:.2f}")

print("--- vuci skoku (ml vzduchu pred skokem) ---")
import statistics as st
for key in ['c3_10','c3_50','c3_90']:
    v=[res[c]['step']-res[c][key] for c in res]; print(key, ["%.2f"%x for x in v], f"{st.mean(v):.2f}+-{st.stdev(v):.2f}")
for th in (0.8,0.7,0.65,0.6):
    v=[res[c]['step']-res[c]['cross'][th] for c in res]; print("q<=",th, ["%.2f"%x for x in v], f"{st.mean(v):.2f}+-{st.stdev(v):.2f}")
# konec plato: q > qmin+0.05 po minimu
for c in res:
    S=[p for s in segs(R[c]) for p in s if p[0]<res[c]['step']-0.3]
    q=[(p[0],p[3]/p[1]) for p in S]
    b=[(x/4, st.median(v for m,v in q if x/4<=m<x/4+0.5)) for x in range(16,80) if sum(1 for m,v in q if x/4<=m<x/4+0.5)>3]
    mn=min(b,key=lambda t:t[1])
    rise=next((m for m,v in b if m>mn[0] and v>mn[1]+0.05),None)
    res[c]['qmin_ml']=mn[0]; res[c]['qrise']=rise
    print(f"c{c}: q min {mn[1]:.2f} @{res[c]['step']-mn[0]:.2f} pred skokem, q stoupa (+0.05) @{(res[c]['step']-rise) if rise else None} pred skokem")
print("--- pokles C3 od vlastniho maxima f = (max-C3)/max, vuci skoku ---")
for f0 in (0.25,0.3,0.35,0.4):
    v=[]
    for c in res:
        S=[p for s in segs(R[c]) for p in s if p[0]<res[c]['step']]
        pk=0; hit=None
        for p in S:
            pk=max(pk,p[3])
            if (pk-p[3])/pk>=f0: hit=p[0]; break
        v.append(res[c]['step']-hit if hit else float('nan'))
    print(f0, ["%.2f"%x for x in v], f"{st.mean(v):.2f}+-{st.stdev(v):.2f}")

ev=res
COL={'C1':'#2a78d6','C3':'#eb6834','C2':'#1baf7a'}
CYC={1:'#2a78d6',2:'#eb6834',3:'#1baf7a',4:'#eda100',5:'#e87ba4'}
TXT='#0b0b0b'; TXT2='#52514e'; GRID='#e4e3df'; SURF='#fcfcfb'; BAND='#d9d8d2'
plt.rcParams.update({'font.size':9,'axes.edgecolor':GRID,'axes.labelcolor':TXT2,'xtick.color':TXT2,
    'ytick.color':TXT2,'axes.grid':True,'grid.color':GRID,'grid.linewidth':0.6,'figure.facecolor':SURF,
    'axes.facecolor':SURF,'axes.spines.top':False,'axes.spines.right':False,'text.color':TXT,'lines.linewidth':1.5})

# obr 1: profily
fig,axs=plt.subplots(5,1,figsize=(8,11.5),sharex=True,sharey=True)
for ax,c in zip(axs,range(1,6)):
    e=ev[c]; step=e['step']
    ax.axvspan(e['step']-e['qmin_ml']*0+ (e['qmin_ml']-e['step']) + 0, 0, alpha=0)  # no-op
    ax.axvspan(e['c3_90'], e['qrise'], color=BAND, alpha=0.6, lw=0, zorder=0)
    for s in segs(R[c]):
        x=[p[0] for p in s]
        ax.plot(x,[p[1] for p in s],color=COL['C1'],label='C1 spodní prstenec')
        ax.plot(x,[p[3] for p in s],color=COL['C3'],label='C3 horní prstenec')
        ax.plot(x,[p[2] for p in s],color=COL['C2'],lw=1.1,label='C2 svislá')
    q7=e['cross'][0.7]
    ax.axvline(q7,color=TXT2,lw=1,ls=':'); ax.text(q7-0.12,6.1,f'C3/C1=0,7\n{q7:.1f}',ha='right',va='top',fontsize=7.5,color=TXT2)
    ax.axvline(step,color=TXT,lw=1.2,ls='--'); ax.text(step+0.12,6.1,f'skok\n{step:.1f}',ha='left',va='top',fontsize=7.5)
    ax.set_title(f'Cyklus {c}',loc='left',fontsize=9.5); ax.set_ylabel('pF'); ax.set_ylim(1.8,6.2)
axs[-1].set_xlim(2.5,20.5); axs[-1].set_xlabel('vytlačený vzduch [ml] (včetně předtlaku 2,7 ml)')
h,l=axs[0].get_legend_handles_labels(); u=dict(zip(l,h))
fig.legend(u.values(),u.keys(),loc='upper center',ncol=3,frameon=False,bbox_to_anchor=(0.5,0.997))
fig.text(0.5,0.962,'Šedé pole = plató C3/C1 (od 90 % poklesu C3 do začátku vzestupu poměru)',ha='center',fontsize=8.5,color=TXT2)
fig.tight_layout(rect=(0,0,1,0.95)); fig.savefig(f'{OUT}/obr1_profily.png',dpi=150); plt.close(fig)

# obr 2: zarovnano na skok
fig,(a1,a2,a3)=plt.subplots(3,1,figsize=(8,8.4),sharex=True)
for c in range(1,6):
    st_=ev[c]['step']
    for s in segs(R[c]):
        x=[p[0]-st_ for p in s]
        a1.plot(x,[p[3] for p in s],color=CYC[c],label=f'cyklus {c}')
        a2.plot(x,[p[1] for p in s],color=CYC[c],label=f'cyklus {c}')
        a3.plot(x,[p[3]/p[1] for p in s],color=CYC[c],label=f'cyklus {c}')
for ax in (a1,a2,a3): ax.axvline(0,color=TXT,lw=1.2,ls='--')
a3.axhline(0.7,color=TXT2,lw=1,ls=':'); a3.text(-11.8,0.72,'C3/C1 = 0,7',fontsize=8,color=TXT2)
a1.set_ylabel('C3 horní [pF]'); a2.set_ylabel('C1 spodní [pF]'); a3.set_ylabel('C3 / C1')
a3.set_ylim(0.4,1.3); a3.set_xlim(-12,3)
a3.set_xlabel('vzduch vůči skoku [ml]  (0 = skok všech kanálů)')
a1.set_title('Cykly zarovnané na skok: horní prstenec, spodní prstenec, poměr',loc='left',fontsize=9.5)
h,l=a1.get_legend_handles_labels(); u=dict(zip(l,h)); a1.legend(u.values(),u.keys(),frameon=False,ncol=5,fontsize=8,loc='lower left')
fig.tight_layout(); fig.savefig(f'{OUT}/obr2_zarovnano.png',dpi=150); plt.close(fig)

# obr 3: suche elektrody
dry=[]
# kazdy krok 'D' = 10 s = 200 vzorku; tri vlozeni lahvicky maji stejnou fazi 'l'
seq=[[None,R[0][i:i+200]] for i in range(0,len(R[0]),200)]
labels=['bez lahvičky','prázdná 1','prázdná 2','prázdná 3','bez lahvičky (konec)']
fig,ax=plt.subplots(figsize=(8,3.6))
for i,(nm,ix) in enumerate([('C1 spodní',3),('C2 svislá',4),('C3 horní',5)]):
    vals=[st.mean(r[ix] for r in s[1]) for s in seq]
    sds=[st.pstdev(r[ix] for r in s[1]) for s in seq]
    col=[COL['C1'],COL['C2'],COL['C3']][i]
    ax.errorbar(range(len(vals)),vals,yerr=sds,color=col,marker='o',ms=6,capsize=3,label=nm)
    ax.text(len(vals)-0.85,vals[-1],nm,color=TXT,fontsize=8,va='center')
ax.set_xticks(range(5)); ax.set_xticklabels(labels,fontsize=8); ax.set_ylabel('pF')
ax.set_xlim(-0.3,5.2)
ax.set_title('Suché elektrody: bez lahvičky / prázdná suchá lahvička (3× vložena) / znovu bez',loc='left',fontsize=9.5)
ax.legend(frameon=False,fontsize=8,ncol=3,loc='upper right')
fig.tight_layout(); fig.savefig(f'{OUT}/obr3_suche.png',dpi=150); plt.close(fig)

# obr 4: polohy vuci skoku
fig,ax=plt.subplots(figsize=(8,3.9))
for c in range(1,6):
    e=ev[c]; y=6-c; s=e['step']
    ax.plot([e['c3_90']-s,e['qrise']-s],[y,y],color=BAND,lw=7,solid_capstyle='butt',zorder=1)
    ax.plot(e['c3_10']-s,y,'o',mfc=SURF,mec=COL['C3'],mew=1.5,ms=6,label='C3 10 %')
    ax.plot(e['c3_50']-s,y,'o',mfc=COL['C3'],mec=COL['C3'],ms=7,label='C3 50 %')
    ax.plot(e['c3_90']-s,y,'s',mfc=SURF,mec=COL['C3'],mew=1.5,ms=6,label='C3 90 %')
    ax.plot(e['cross'][0.7]-s,y,'|',color=TXT,ms=14,mew=2,label='C3/C1 = 0,7')
    ax.plot(e['qrise']-s,y,'|',color=COL['C1'],ms=14,mew=2,label='C3/C1 začíná stoupat')
ax.axvline(0,color=TXT,lw=1.2,ls='--'); ax.text(0.1,5.45,'skok',fontsize=8)
ax.set_yticks([5,4,3,2,1]); ax.set_yticklabels([f'cyklus {c}' for c in range(1,6)]); ax.grid(axis='y',visible=False)
ax.set_xlim(-11,1); ax.set_xlabel('vzduch vůči skoku [ml]')
ax.set_title('Polohy příznaků v jednotlivých cyklech; šedý pás = plató C3/C1',loc='left',fontsize=9)
h,l=ax.get_legend_handles_labels(); u=dict(zip(l,h))
ax.legend(u.values(),u.keys(),frameon=False,ncol=3,fontsize=7.5,loc='upper center',bbox_to_anchor=(0.45,-0.25))
fig.tight_layout(); fig.savefig(f'{OUT}/obr4_polohy.png',dpi=150); plt.close(fig)
print('ok')
