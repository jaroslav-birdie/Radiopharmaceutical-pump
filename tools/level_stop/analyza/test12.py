# Testy 9-12: vyska po zastaveni podle naplne na startu a stineni.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test12.py <t9> <t10> <t11> <t11_stineni> <t12> [slozka]
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

OUT = sys.argv[6] if len(sys.argv) > 6 else 'img12'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'

def heights(fn):
    h = []
    for ln in open(fn, errors='ignore'):
        m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln.split('-> ', 1)[-1].strip())
        if m:
            h.append(float(m.group(1).replace(',', '.')))
    return h

t9, t10, t11, t11s, t12 = (heights(f) for f in sys.argv[1:6])
# naplne podle protokolu testu
pts = ([(h, '9–14 ml', False) for h in t9] + [(h, '9–14 ml', False) for h in t10[:2]]
       + [(h, '7 ml', False) for h in t10[2:]]
       + [(t11[0], '9–14 ml', False)] + [(h, '7 ml', False) for h in t11[1:3]]
       + [(h, '6 ml', False) for h in t11[3:]]
       + [(t11s[0], '9–14 ml', True)] + [(h, '7 ml', True) for h in t11s[1:3]]
       + [(h, '6 ml', True) for h in t11s[3:]]
       + [(t12[0], '20 ml', True), (t12[1], '9–14 ml', True), (t12[2], '7 ml', True)]
       + [(h, '6 ml', True) for h in t12[3:]])
cats = ['20 ml', '9–14 ml', '7 ml', '6 ml']
allh = [p[0] for p in pts]
for c in cats:
    h = [p[0] for p in pts if p[1] == c]
    print(f'{c}: n={len(h)} {st.mean(h):.2f} +- {st.stdev(h) if len(h) > 1 else 0:.2f}')
print(f'vse ({len(allh)}): {st.mean(allh):.2f} +- {st.stdev(allh):.2f}, {min(allh)}-{max(allh)}')

fig, ax = plt.subplots(figsize=(8, 5))
for lo, hi, n in ((5.5, 8.5, 'spodní prstenec'), (11.5, 14.5, 'horní prstenec')):
    ax.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
    ax.text(4.45, (lo + hi) / 2, n, va='center', color=INK2, fontsize=9)
ax.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
ax.text(4.45, 10.4, 'mezera', va='center', color=INK2, fontsize=9)
ax.axhline(8.5, color=INK2, lw=1, ls='--')
for shield, col, mk, lab, dx in ((False, '#eb6834', 'o', 'bez stínění', -0.12),
                                 (True, '#2a78d6', 's', 'se stíněním', 0.12)):
    first = True
    for i, c in enumerate(cats):
        h = [p[0] for p in pts if p[1] == c and p[2] == shield]
        xs = [i + 1 + dx + (j - (len(h) - 1) / 2) * 0.035 for j in range(len(h))]
        ax.plot(xs, h, mk, color=col, ms=7, alpha=0.85, label=lab if first and h else None)
        if h:
            first = False
for i, c in enumerate(cats):
    h = [p[0] for p in pts if p[1] == c]
    ax.plot([i + 0.7, i + 1.3], [st.mean(h)] * 2, color=INK, lw=1.5)
ax.set_xticks(range(1, 5)); ax.set_xticklabels(cats)
ax.set_xlim(0.4, 5.3); ax.set_ylim(4, 16)
ax.set_xlabel('náplň na startu tlačení', color=INK2)
ax.set_ylabel('výška hladiny po zastavení [mm]', color=INK2)
ax.set_title(f'Testy 9–12: {len(allh)} cyklů, {st.mean(allh):.2f} ± {st.stdev(allh):.2f} mm (čára = průměr)',
             color=INK, fontsize=11, loc='left')
ax.legend(frameon=False, loc='upper left'); ax.grid(axis='y', color=GRID, lw=0.8)
for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_vysky.png', dpi=150)
