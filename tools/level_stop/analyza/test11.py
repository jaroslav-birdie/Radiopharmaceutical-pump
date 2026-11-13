# Testy 9-11: vyska po zastaveni podle naplne pri startu.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test11.py <log_t9> <log_t10> <log_t11> [slozka_obrazku]
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

OUT = sys.argv[4] if len(sys.argv) > 4 else 'img11'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'

def heights(fn):
    h = []
    for ln in open(fn, errors='ignore'):
        m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln.split('-> ', 1)[-1].strip())
        if m:
            h.append(float(m.group(1).replace(',', '.')))
    return h

t9, t10, t11 = (heights(f) for f in sys.argv[1:4])
groups = (('9–14 ml', '#1baf7a', t9 + t10[:2] + t11[:1]),
          ('7 ml (start iterace)', '#2a78d6', t10[2:] + t11[1:3]),
          ('6 ml (konec plató C3)', '#eb6834', t11[3:]))
allh = []
for name, _, h in groups:
    allh += h
    print(f'{name}: {h} prumer {st.mean(h):.2f} sd {st.stdev(h):.2f}')
print(f'vse ({len(allh)}): prumer {st.mean(allh):.2f} sd {st.stdev(allh):.2f} '
      f'min {min(allh)} max {max(allh)}, zbytek {(st.mean(allh) - 2.5) / 1.5:.2f} ml')

fig, ax = plt.subplots(figsize=(8, 5))
for lo, hi, n in ((5.5, 8.5, 'spodní prstenec'), (11.5, 14.5, 'horní prstenec')):
    ax.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
    ax.text(3.45, (lo + hi) / 2, n, va='center', color=INK2, fontsize=9)
ax.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
ax.text(3.45, 10.4, 'mezera', va='center', color=INK2, fontsize=9)
ax.axhline(8.5, color=INK2, lw=1, ls='--')
for i, (name, col, h) in enumerate(groups):
    xs = [i + 1 + (j - (len(h) - 1) / 2) * 0.08 for j in range(len(h))]
    ax.plot(xs, h, 'o', color=col, ms=8, alpha=0.85)
    ax.plot([i + 0.75, i + 1.25], [st.mean(h)] * 2, color=INK, lw=1.5)
ax.set_xticks([1, 2, 3]); ax.set_xticklabels([g[0] for g in groups])
ax.set_xlim(0.4, 4.2); ax.set_ylim(4, 16)
ax.set_ylabel('výška hladiny od vnějšího dna [mm]', color=INK2)
ax.set_title('Výška po zastavení podle náplně na startu – testy 9–11 (čára = průměr)',
             color=INK, fontsize=11, loc='left')
ax.grid(axis='y', color=GRID, lw=0.8)
for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_vysky_podle_naplne.png', dpi=150)
