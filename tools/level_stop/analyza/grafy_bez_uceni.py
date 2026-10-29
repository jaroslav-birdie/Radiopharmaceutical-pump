import json, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
exec(open('analyza/grafy_profil.py').read().split("# ---- obr. 1")[0])
pr={int(k):v for k,v in json.load(open('analyza/bez_uceni.json')).items()}
CY={1:'#2a78d6',2:'#eb6834',3:'#1baf7a',4:'#eda100',5:'#e87ba4'}
fig,(a1,a2)=plt.subplots(2,1,figsize=(8,6.4),sharex=True)
for ax in (a1,a2): ax.axvspan(0,4,color=BAND,alpha=0.6,lw=0,zorder=0); ax.axvspan(-2,0,color=COL['C3'],alpha=0.10,lw=0); ax.axvspan(4,6,color=COL['C1'],alpha=0.10,lw=0)
for c in sorted(R):
    top=pr[c]['top']
    pk=-9
    for s_ in segs(R[c]):
        x=[p[0]-top for p in s_]
        a1.plot(x,[p[3]/p[1] for p in s_],color=CY[c],label=f'cyklus {c}')
        fr=[]
        for p in s_:
            pk=max(pk,p[3]); fr.append((pk-p[3])/pk)
        a2.plot(x,fr,color=CY[c],label=f'cyklus {c}')
    a1.plot(pr[c]['guard']-top,0.05,'v',color=CY[c],ms=7)
a1.axhline(0.45,color=TXT,lw=1.2); a1.text(-5.8,0.47,'C3/C1 = 0,45',fontsize=8)
a1.axhline(0.40,color=TXT2,lw=0.9,ls=':'); a1.text(-5.8,0.33,'C3/C1 = 0,40',fontsize=8,color=TXT2)
a2.axhline(0.5,color=TXT,lw=1.2); a2.text(-5.8,0.52,'pokles C3 o 50 % od vlastního maxima',fontsize=8)
a1.set_ylabel('C3 / C1'); a2.set_ylabel('(max C3 − C3) / max C3')
a1.set_ylim(0,1.4); a2.set_ylim(-0.05,0.7); a2.set_xlim(-6,9)
a1.text(-1,1.36,'horní\nprstenec',ha='center',va='top',fontsize=7.5,color=TXT2); a1.text(2,1.36,'mezera mezi prstenci',ha='center',va='top',fontsize=7.5)
a1.text(5,1.36,'spodní\nprstenec',ha='center',va='top',fontsize=7.5,color=TXT2)
a1.text(8.9,0.08,'▼ = pojistka C1',ha='right',fontsize=7.5,color=TXT2)
a2.set_xlabel('vzduch od horního okraje mezery [ml]  (každý cyklus zvlášť, bez učení)')
a1.set_title('Kritéria počítaná jen z vlastního běhu, všech 5 cyklů',loc='left',fontsize=9.5,pad=14)
h,l=a2.get_legend_handles_labels(); u=dict(zip(l,h)); a2.legend(u.values(),u.keys(),frameon=False,ncol=5,fontsize=8,loc='upper left')
fig.tight_layout(); fig.savefig('img/obr4_bez_uceni.png',dpi=150)
