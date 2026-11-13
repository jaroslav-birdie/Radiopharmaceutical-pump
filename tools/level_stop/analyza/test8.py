# Test 8 (koleno C1 - C3) + kalibrace rychlosti hladiny z testu 7 a 8.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test8.py <log_test7.txt> <log_test8.txt> [slozka_obrazku]
# Osa = cas toku (jen pacient otevreny: faze p, w), okna jako ve sketchi:
# kos 12 vzorku, okno 8 kosu (4,8 s). Spotreba vzduchu se nepouziva.
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

OUT = sys.argv[3] if len(sys.argv) > 3 else 'img8'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'
C_T7, C_T8 = '#2a78d6', '#eb6834'

def load(fn):
    rows, stops, heights = {}, [], []
    for ln in open(fn, errors='ignore'):
        ln = ln.split('-> ', 1)[-1].strip()
        p = ln.split(';')
        if len(p) == 7 and p[0].isdigit():
            try:
                rows.setdefault(int(p[0]), []).append((int(p[1]), p[2], float(p[4]), float(p[6])))
            except ValueError:
                pass
        m = re.search(r'STOP \w+ @ t=(\d+)', ln)
        if m:
            stops.append(int(m.group(1)))
        m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln)
        if m:
            heights.append(float(m.group(1).replace(',', '.')))
    return rows, stops, heights

def windows(rs, ts):
    """(cas toku do zastaveni [s], okno C1, okno C3) jako ve sketchi."""
    f = [(a, b) for t, ph, a, b in rs if ph in 'pw' and t <= ts]
    out, b1, b3 = [], [], []
    for k in range(12, len(f) + 1, 12):
        w = f[k - 12:k]
        b1 = (b1 + [st.mean(x[0] for x in w)])[-8:]
        b3 = (b3 + [st.mean(x[1] for x in w)])[-8:]
        if len(b1) == 8:
            out.append(((len(f) - k) * 0.05, st.mean(b1), st.mean(b3)))
    return out

def first(o, cond):
    c3m, armed = -9.0, False
    for t, w1, w3 in o:
        c3m = max(c3m, w3)
        armed = armed or w3 <= 0.85 * c3m
        if armed and cond(w1, w3, c3m):
            return t
    return None

sets = []
for fn, name in ((sys.argv[1], 'test 7'), (sys.argv[2], 'test 8')):
    rows, stops, H = load(fn)
    sets.append((name, [(H[c - 1], windows(rows[c], stops[c - 1])) for c in sorted(rows)]))

crit = {'C3 <= 0,75 x max': lambda a, b, m: b <= 0.75 * m,
        'C3 <= 0,78 x max': lambda a, b, m: b <= 0.78 * m,
        'C3/C1 <= 0,65': lambda a, b, m: b / a <= 0.65,
        'C3/C1 <= 0,68': lambda a, b, m: b / a <= 0.68}
allpts = [p for _, s in sets for p in s]
fits = {}
for name, cond in crit.items():
    xs = [first(o, cond) for h, o in allpts]; ys = [h for h, o in allpts]
    mx, my = st.mean(xs), st.mean(ys)
    r = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    a = my - r * mx
    sd = st.pstdev([y - a - r * x for x, y in zip(xs, ys)])
    fits[name] = (a, -r, sd, xs, ys)
    print(f'{name:18s} priznak lezi na {a:.2f} mm, rychlost hladiny {-r:.3f} mm/s, rezidua sd {sd:.2f} mm')
for name, s in sets:
    h = [x[0] for x in s]
    print(f'{name}: vysky {h}  prumer {st.mean(h):.2f} sd {st.stdev(h):.2f} mm')

# --- obr1: namerene vysky testu 7 a 8 proti geometrii ---
fig, ax = plt.subplots(figsize=(7, 5))
for lo, hi, n in ((5.5, 8.5, 'spodní prstenec'), (11.5, 14.5, 'horní prstenec')):
    ax.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
    ax.text(11.6, (lo + hi) / 2, n, va='center', color=INK2, fontsize=9)
ax.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
ax.text(11.6, 10, 'mezera', va='center', color=INK2, fontsize=9)
ax.axhline(8.5, color=INK2, lw=1, ls='--')
ax.text(11.6, 8.75, 'cíl ≈ 8,5 mm (4 ml)', color=INK2, fontsize=9)
for (name, s), col, off in zip(sets, (C_T7, C_T8), (0, 5.5)):
    xs = [i + 1 + off for i in range(len(s))]
    ax.plot(xs, [h for h, _ in s], 'o', color=col, ms=8, label=name + (' – vrchol' if off == 0 else ' – koleno'))
    for x, (h, _) in zip(xs, s):
        ax.annotate(f'{h:.1f}', (x, h), xytext=(0, -14), textcoords='offset points', ha='center', color=INK, fontsize=9)
ax.set_xlim(0.3, 15); ax.set_ylim(0, 16); ax.set_xticks([])
ax.set_ylabel('výška hladiny od vnějšího dna [mm]', color=INK2)
ax.set_title('Naměřená výška hladiny po zastavení', color=INK, fontsize=11, loc='left')
ax.legend(frameon=False, loc='upper left'); ax.grid(axis='y', color=GRID, lw=0.8)
for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_vysky.png', dpi=150)

# --- obr2: kalibrace - vyska proti casu toku od priznaku do zastaveni ---
a, rate, sd, xs, ys = fits['C3 <= 0,75 x max']
fig, ax = plt.subplots(figsize=(7, 4.5))
n7 = len(sets[0][1])
ax.plot(xs[:n7], ys[:n7], 'o', color=C_T7, ms=8, label='test 7')
ax.plot(xs[n7:], ys[n7:], 'o', color=C_T8, ms=8, label='test 8')
X = [0, max(xs) + 1]
ax.plot(X, [a - rate * x for x in X], color=INK2, lw=1.5)
ax.annotate(f'proložení: výška = {a:.1f} − {rate:.2f}·t  (sd {sd:.2f} mm)', (10.5, 6.6), color=INK2, fontsize=9)
ax.plot([0], [a], 's', color=INK, ms=7)
ax.annotate(f'příznak C3 ≤ 0,75·max leží na ≈ {a:.1f} mm', (0, a), xytext=(10, -4), textcoords='offset points', color=INK, fontsize=9)
ax.set_xlabel('čas toku od C3 ≤ 0,75·max do zastavení [s]', color=INK2)
ax.set_ylabel('naměřená výška [mm]', color=INK2)
ax.set_title('Na jaké výšce leží příznak C3 ≤ 0,75·max (jen analýza, ne řízení)', color=INK, fontsize=11, loc='left')
ax.legend(frameon=False, loc='upper right'); ax.grid(color=GRID, lw=0.8)
for sp in ('top', 'right'): ax.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr2_kalibrace.png', dpi=150)
