import json, statistics as st, re
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
exec(open('analyza/analyza_profil.py').read().split("F3=2.806")[0])   # nacte R, ev
out={int(k):v for k,v in json.load(open('analyza/summary.json')).items()}
F3=2.806; D1=3.533
COL={'C1':'#2a78d6','C3':'#eb6834','C2':'#1baf7a'}
CYC={2:'#2a78d6',3:'#eb6834',4:'#1baf7a',5:'#eda100'}
TXT='#0b0b0b'; TXT2='#52514e'; GRID='#e4e3df'; SURF='#fcfcfb'; BAND='#d9d8d2'
plt.rcParams.update({'font.size':9,'axes.edgecolor':GRID,'axes.labelcolor':TXT2,'xtick.color':TXT2,
    'ytick.color':TXT2,'axes.grid':True,'grid.color':GRID,'grid.linewidth':0.6,'figure.facecolor':SURF,
    'axes.facecolor':SURF,'axes.spines.top':False,'axes.spines.right':False,'text.color':TXT,'lines.linewidth':1.6})
def segs(rows):
    """klouzavy median 10 vzorku po souvislych usecich faze p/w"""
    out=[];cur=[];b=[]
    prev=None
    for t,ph,ml,c1,c2,c3 in rows:
        if ph not in 'pw':
            if cur: out.append(cur); cur=[]; b=[]
            continue
        b=(b+[(c1,c2,c3)])[-10:]
        if len(b)<10: continue
        cur.append((ml,st.median(x[0] for x in b),st.median(x[1] for x in b),st.median(x[2] for x in b)))
    if cur: out.append(cur)
    return out

# ---- obr. 1: profily, maly nasobek po cyklech
cross=json.load(open('analyza/cross.json'))
fig,axs=plt.subplots(5,1,figsize=(8,11.5),sharex=True,sharey=True)
for ax,c in zip(axs,sorted(R)):
    v=out[c]; g=v['guard']['ml']
    top=v['c3half']+1.0; bot=v['c1half']-1.0
    ax.axvspan(top,bot,color=BAND,alpha=0.6,lw=0,zorder=0)
    for s_ in segs(R[c]):
        x=[p[0] for p in s_]
        ax.plot(x,[p[1] for p in s_],color=COL['C1'],label='C1 spodní prstenec')
        ax.plot(x,[p[3] for p in s_],color=COL['C3'],label='C3 horní prstenec')
        ax.plot(x,[p[2] for p in s_],color=COL['C2'],lw=1.1,label='C2 svislá')
    ax.axvline(g,color=TXT,lw=1.2,ls='--')
    ax.text(g+0.15,7.6,f'pojistka C1\n{g:.1f} ml',ha='left',va='top',fontsize=7.5,color=TXT)
    if c==1:
        ax.axvline(v['stop']['ml'],color=TXT,lw=1.2)
        ax.text(v['stop']['ml']-0.15,7.6,f"učení: konec\npoklesu C3 {v['stop']['ml']:.1f}",ha='right',va='top',fontsize=7.5)
    else:
        d7=cross[str(c)]['0.7']; d95=cross[str(c)]['0.95']
        ax.axvline(d7,color=TXT2,lw=1,ls=':')
        ax.axvline(d95,color=TXT,lw=1.2)
        ax.text(d7-0.12,7.6,f'δ=0,7\n{d7:.1f}',ha='right',va='top',fontsize=7.5,color=TXT2)
        ax.text(d95+0.12,7.6,f'δ=0,95\n{d95:.1f}',ha='left',va='top',fontsize=7.5,color=TXT)
    ax.set_title(f'Cyklus {c}' + ('  (učení F3)' if c==1 else ''),loc='left',fontsize=9.5,color=TXT)
    ax.set_ylabel('pF'); ax.set_ylim(1.5,7.8)
axs[-1].set_xlim(2,20); axs[-1].set_xlabel('vytlačený vzduch [ml] (včetně předtlaku 2,7 ml)')
h,l=axs[0].get_legend_handles_labels(); u=dict(zip(l,h))
fig.legend(u.values(),u.keys(),loc='upper center',ncol=3,frameon=False,bbox_to_anchor=(0.5,0.997))
fig.text(0.5,0.962,'Šedé pole = mezera mezi prstenci (odhad z polohy přechodů, 1 ml vzduchu ≈ 1 mm hladiny)',ha='center',fontsize=8.5,color=TXT2)
fig.tight_layout(rect=(0,0,1,0.95)); fig.savefig('img/obr1_profily.png',dpi=150); plt.close(fig)

# ---- obr. 2: C1-C3 a delta zarovnane na pojistku
fig,(a1,a2)=plt.subplots(2,1,figsize=(8,6.4),sharex=True)
gt=st.mean(out[c]['c3half']+1-out[c]['guard']['ml'] for c in (2,3,4,5))
gb=st.mean(out[c]['c1half']-1-out[c]['guard']['ml'] for c in (2,3,4,5))
r3=st.mean(out[c]['c3half']-out[c]['guard']['ml'] for c in (2,3,4,5))
r1=st.mean(out[c]['c1half']-out[c]['guard']['ml'] for c in (2,3,4,5))
for ax in (a1,a2):
    ax.axvspan(gt,gb,color=BAND,alpha=0.6,lw=0,zorder=0)
    ax.axvspan(r3-1,r3+1,color=COL['C3'],alpha=0.10,lw=0,zorder=0)
    ax.axvspan(r1-1,r1+1,color=COL['C1'],alpha=0.10,lw=0,zorder=0)
a1.text(r3,5.35,'horní\nprstenec',ha='center',va='top',fontsize=7.5,color=TXT2)
a1.text((gt+gb)/2,5.35,'mezera mezi prstenci',ha='center',va='top',fontsize=7.5,color=TXT)
a1.text(r1,5.35,'spodní\nprstenec',ha='center',va='top',fontsize=7.5,color=TXT2)
for c in (2,3,4,5):
    g=out[c]['guard']['ml']
    for s in segs(R[c]):
        x=[p[0]-g for p in s]
        a1.plot(x,[p[1]-p[3] for p in s],color=CYC[c],label=f'cyklus {c}')
        dd=[(p[0]-g,(p[1]-p[3])/(p[1]-F3)) for p in s if p[1]-F3>=0.3*D1]
        if dd: a2.plot([d[0] for d in dd],[d[1] for d in dd],color=CYC[c],label=f'cyklus {c}')
a2.axhline(0.7,color=TXT2,lw=0.9,ls=':'); a2.text(-11.8,0.6,'dosavadní práh δ = 0,7',fontsize=8,color=TXT2)
a2.axhline(0.95,color=TXT,lw=1.2); a2.text(-11.8,0.97,'navržený práh δ = 0,95',fontsize=8,color=TXT)
for ax in (a1,a2): ax.axvline(0,color=TXT,lw=1,ls='--')
a2.text(0.12,0.05,'pojistka C1',fontsize=8,color=TXT)
a1.set_ylabel('C1 − C3 [pF]'); a2.set_ylabel('δ = (C1 − C3)/(C1 − F3)')
a2.set_ylim(-0.1,1.25); a2.set_xlabel('vzduch vůči zásahu pojistky C1 [ml]  (0 = hladina prochází spodním prstencem)')
a2.set_xlim(-12,4)
a1.set_title('C1 − C3 a δ, cykly 2–5 zarovnané na zásah pojistky C1',loc='left',fontsize=9.5,pad=14)
a1.set_ylim(-1.6,5.5)
h,l=a1.get_legend_handles_labels(); u=dict(zip(l,h)); a1.legend(u.values(),u.keys(),frameon=False,ncol=4,loc='lower right',fontsize=8)
fig.tight_layout(); fig.savefig('img/obr2_delta.png',dpi=150); plt.close(fig)

# ---- obr. 3: polohy udalosti
cross=json.load(open('analyza/cross.json'))
fig,ax=plt.subplots(figsize=(8,3.9))
for i,c in enumerate(sorted(R)):
    v=out[c]; y=5-i
    ax.plot(v['c3half'],y,'o',ms=8,mfc=SURF,mec=COL['C3'],mew=2,zorder=3,label='C3 50 % (horní prstenec)')
    ax.plot(v['c1half'],y,'o',ms=8,mfc=SURF,mec=COL['C1'],mew=2,zorder=3,label='C1 50 % (spodní prstenec)')
    sx=v['stop']['ml'] if c==1 else cross[str(c)]['0.95']
    ax.plot([v['c3half']+1,v['c1half']-1],[y,y],color=BAND,lw=7,solid_capstyle='butt',zorder=1)
    if c>1: ax.plot(cross[str(c)]['0.7'],y,'|',ms=12,mew=1.5,color=TXT2,zorder=4,label='δ = 0,7 (dosavadní)')
    ax.plot(sx,y,'|',ms=16,mew=2.5,color=TXT,zorder=4,label='STOP (δ = 0,95; c1 učení)')
    ax.plot(v['guard']['ml'],y,'|',ms=16,mew=1.5,color=TXT2,zorder=4,label='pojistka C1')
    ax.text(17.2,y,f"rezerva {v['guard']['ml']-sx:.1f} ml",va='center',fontsize=8,color=TXT)
ax.set_yticks([5,4,3,2,1]); ax.set_yticklabels([f'cyklus {c}'+(' (učení)' if c==1 else '') for c in sorted(R)])
ax.set_xlim(6,19.5); ax.set_xticks(range(6,17,2)); ax.set_xlabel('vytlačený vzduch [ml]'); ax.grid(axis='y',visible=False)
h,l=ax.get_legend_handles_labels(); u=dict(zip(l,h))
ax.legend(u.values(),u.keys(),frameon=False,ncol=3,fontsize=7.5,loc='upper center',bbox_to_anchor=(0.45,-0.28))
ax.set_title('Polohy událostí; šedý pás = mezera mezi prstenci (odhad)',loc='left',fontsize=9,color=TXT)
fig.tight_layout(); fig.savefig('img/obr3_polohy.png',dpi=150); plt.close(fig)
print("ok")
