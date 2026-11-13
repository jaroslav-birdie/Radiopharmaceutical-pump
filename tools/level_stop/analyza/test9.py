# Test 9 (sablona C3 / max C3) - namerene vysky proti testum 7 a 8.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test9.py <log_t7> <log_t8> <log_t9> [slozka_obrazku]
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

OUT = sys.argv[4] if len(sys.argv) > 4 else 'img9'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'

def heights(fn):
    h, stops = [], []
    for ln in open(fn, errors='ignore'):
        ln = ln.split('-> ', 1)[-1].strip()
        m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln)
        if m:
            h.append(float(m.group(1).replace(',', '.')))
        m = re.match(r'# STOP (\w+)', ln)
        if m:
            stops.append(m.group(1))
        m = re.match(r'#   C3/maxC3=([0-9.]+) C1/maxC1=([0-9.]+) \(d-d0\)/maxC3=([0-9.]+)\s+(.*)$', ln)
        if m:
            print('   ', ln)
    return h, stops

sets = []
for fn, name, col in ((sys.argv[1], 'test 7 – vrchol', '#2a78d6'),
                      (sys.argv[2], 'test 8 – koleno', '#eb6834'),
                      (sys.argv[3], 'test 9 – šablona', '#1baf7a')):
    h, s = heights(fn)
    vol = [(x - 2.5) / 1.5 for x in h]
    print(f'{name}: {h}  prumer {st.mean(h):.2f} sd {st.stdev(h):.2f} mm, '
          f'objem {st.mean(vol):.2f} +- {st.stdev(vol):.2f} ml, zastavil: {s}')
    sets.append((name, col, h))

fig, ax = plt.subplots(figsize=(8, 5))
for lo, hi, n in ((5.5, 8.5, 'spodní prstenec'), (11.5, 14.5, 'horní prstenec')):
    ax.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
    ax.text(19.5, (lo + hi) / 2, n, va='center', color=INK2, fontsize=9)
ax.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
ax.text(19.5, 10.4, 'mezera', va='center', color=INK2, fontsize=9)
ax.axhline(8.5, color=INK2, lw=1, ls='--')
ax.text(19.5, 8.75, 'cíl ≈ 8,5 mm (4 ml)', color=INK2, fontsize=9)
x0 = 1
for name, col, h in sets:
    xs = list(range(x0, x0 + len(h)))
    ax.plot(xs, h, 'o', color=col, ms=8, label=name)
    for x, v in zip(xs, h):
        ax.annotate(f'{v:.1f}', (x, v), xytext=(0, -14), textcoords='offset points',
                    ha='center', color=INK, fontsize=8)
    x0 += len(h) + 1
ax.set_xlim(0.3, 25); ax.set_ylim(0, 16); ax.set_xticks([])
ax.set_ylabel('výška hladiny od vnějšího dna [mm]', color=INK2)
ax.set_title('Naměřená výška hladiny po zastavení – testy 7, 8, 9', color=INK, fontsize=11, loc='left')
ax.legend(frameon=False, loc='upper left'); ax.grid(axis='y', color=GRID, lw=0.8)
for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_vysky.png', dpi=150)
