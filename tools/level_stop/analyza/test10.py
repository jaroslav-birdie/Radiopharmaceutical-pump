# Test 10 - vysky testu 9 a 10, start iterace (~7 ml) zvlast.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test10.py <log_t9> <log_t10> [slozka_obrazku]
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

OUT = sys.argv[3] if len(sys.argv) > 3 else 'img10'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'

def parse(fn):
    cyc = []
    cur = {}
    for ln in open(fn, errors='ignore'):
        ln = ln.split('-> ', 1)[-1].strip()
        m = re.match(r'# start C3/C1=([0-9.]+)', ln)
        if m: cur = {'q0': float(m.group(1))}
        m = re.search(r'C3/C1 v maximu=([0-9.]+)', ln)
        if m: cur['qmax'] = float(m.group(1))
        m = re.match(r'# STOP (\w+ ?\S*)', ln)
        if m: cur['stop'] = m.group(1)
        m = re.match(r'#   C3/maxC3=([0-9.]+)', ln)
        if m: cur['c3n'] = float(m.group(1))
        m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln)
        if m and cur:
            cur['h'] = float(m.group(1).replace(',', '.')); cyc.append(cur); cur = {}
    return cyc

t9, t10 = parse(sys.argv[1]), parse(sys.argv[2])
for name, c in (('test 9', t9), ('test 10', t10)):
    for i, x in enumerate(c, 1):
        print(name, i, x)
full = [x['h'] for x in t9] + [x['h'] for x in t10[:2]]
it = [x['h'] for x in t10[2:]]
allh = full + it
print(f'plna lahvicka (9-14 ml): {full} prumer {st.mean(full):.2f} sd {st.stdev(full):.2f}')
print(f'start iterace (~7 ml):   {it} prumer {st.mean(it):.2f} sd {st.stdev(it):.2f}')
print(f'vse: prumer {st.mean(allh):.2f} sd {st.stdev(allh):.2f} min {min(allh)} max {max(allh)}')

fig, ax = plt.subplots(figsize=(8, 5))
for lo, hi, n in ((5.5, 8.5, 'spodní prstenec'), (11.5, 14.5, 'horní prstenec')):
    ax.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
    ax.text(12.6, (lo + hi) / 2, n, va='center', color=INK2, fontsize=9)
ax.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
ax.text(12.6, 10.4, 'mezera', va='center', color=INK2, fontsize=9)
ax.axhline(8.5, color=INK2, lw=1, ls='--')
groups = (('test 9 – 9–14 ml', '#1baf7a', [x['h'] for x in t9], 1),
          ('test 10 – 9–14 ml', '#2a78d6', [x['h'] for x in t10[:2]], 7),
          ('test 10 – ≈ 7 ml (start iterace)', '#eb6834', it, 9))
for name, col, h, x0 in groups:
    xs = list(range(x0, x0 + len(h)))
    ax.plot(xs, h, 'o', color=col, ms=8, label=name)
    for x, v in zip(xs, h):
        ax.annotate(f'{v:.2f}'.rstrip('0').rstrip('.'), (x, v), xytext=(0, -14),
                    textcoords='offset points', ha='center', color=INK, fontsize=8)
ax.set_xlim(0.3, 15.5); ax.set_ylim(4, 16); ax.set_xticks([])
ax.set_ylabel('výška hladiny od vnějšího dna [mm]', color=INK2)
ax.set_title('Zastavení šablonou – testy 9 a 10', color=INK, fontsize=11, loc='left')
ax.legend(frameon=False, loc='upper left'); ax.grid(axis='y', color=GRID, lw=0.8)
for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_vysky.png', dpi=150)
