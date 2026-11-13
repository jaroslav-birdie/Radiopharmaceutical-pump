# Test 7 - zastaveni na vrcholu C1 - C3, obsluha zmerila vysku hladiny.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test7.py <log.txt> [vystupni_slozka_obrazku]
# Osa = cas toku: jen vzorky s otevrenym pacientem (faze p, w), 50 ms na
# vzorek, 0 = zastaveni. Spotreba vzduchu se nepouziva.
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

LOG = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else 'img7'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'
C_C1, C_C3, C_D = '#2a78d6', '#eb6834', '#1baf7a'

rows, stops, heights = {}, [], []
for ln in open(LOG, errors='ignore'):
    ln = ln.split('-> ', 1)[-1].strip()
    p = ln.split(';')
    if len(p) == 7 and p[0].isdigit():
        try:
            rows.setdefault(int(p[0]), []).append((int(p[1]), p[2], float(p[4]), float(p[6])))
        except ValueError:
            pass
    m = re.search(r'STOP vrchol @ t=(\d+)', ln)
    if m:
        stops.append(int(m.group(1)))
    m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln)
    if m:
        heights.append(float(m.group(1).replace(',', '.')))

def flow_profile(rs, ts):
    """Vzorky s otevrenym pacientem do zastaveni, klouzavy prumer 2 s."""
    f = [(c1, c3) for t, ph, c1, c3 in rs if ph in 'pw' and t <= ts]
    out = []
    for i in range(len(f)):
        w = f[max(0, i - 40):i + 1]
        out.append(((i - len(f) + 1) * 0.05, st.mean(x[0] for x in w), st.mean(x[1] for x in w)))
    return out

prof = {c: flow_profile(rows[c], stops[c - 1]) for c in sorted(rows)}
feat = []
for c, P in prof.items():
    d = [a - b for _, a, b in P]
    im = max(range(len(d)), key=lambda i: d[i])
    i1 = max(range(len(P)), key=lambda i: P[i][1])
    c3 = [x[2] for x in P]; ix = c3.index(max(c3))
    i3 = next(i for i in range(ix, len(c3)) if c3[i] <= 0.5 * (c3[ix] + c3[-1]))
    feat.append((P[im][0], P[i1][0], P[i3][0]))
for c, (a, b, e) in zip(prof, feat):
    print(f'cyklus {c}: vyska {heights[c - 1]} mm  vrchol d {a:+.1f} s  max C1 {b:+.1f} s  C3 50 % {e:+.1f} s')
h_mean = st.mean(heights)
t_c3 = st.mean(f[2] for f in feat)
rate = (13.0 - h_mean) / -t_c3          # C3 50 % = stred horniho prstence 13,0 mm
print(f'vyska {h_mean:.2f} +- {st.stdev(heights):.2f} mm, odhad rychlosti hladiny {rate:.2f} mm/s')
for n, k in (('vrchol d', 0), ('max C1', 1), ('C3 50 %', 2)):
    t = [f[k] for f in feat]
    print(f'{n:9s} {st.mean(t):+.1f} +- {st.stdev(t):.1f} s  => hladina ~{h_mean - rate * st.mean(t):.1f} mm')

# --- obr1: profily zarovnane na zastaveni ---
fig, ax = plt.subplots(1, 3, figsize=(13, 4), sharex=True)
for P in prof.values():
    t = [x[0] for x in P]
    ax[0].plot(t, [x[1] for x in P], color=C_C1, lw=1.2, alpha=0.8)
    ax[1].plot(t, [x[2] for x in P], color=C_C3, lw=1.2, alpha=0.8)
    ax[2].plot(t, [x[1] - x[2] for x in P], color=C_D, lw=1.2, alpha=0.8)
for a, title in zip(ax, ('C1 spodní prstenec [pF]', 'C3 horní prstenec [pF]', 'C1 − C3 [pF]')):
    a.set_title(title, color=INK, fontsize=11, loc='left')
    a.axvline(0, color=INK2, lw=1, ls='--')
    a.grid(color=GRID, lw=0.8); a.set_xlim(-60, 1)
    for s in ('top', 'right'): a.spines[s].set_visible(False)
    a.tick_params(colors=INK2); a.set_xlabel('čas toku před zastavením [s]', color=INK2)
tm = st.mean(f[0] for f in feat)
ax[2].axvline(tm, color=C_D, lw=1, ls=':')
ax[2].annotate(f'průměrný vrchol {tm:.1f} s', (tm, ax[2].get_ylim()[1]), xytext=(-6, -12),
               textcoords='offset points', ha='right', color=INK2, fontsize=9)
ax[0].annotate('zastavení', (0, ax[0].get_ylim()[0]), xytext=(-6, 8),
               textcoords='offset points', ha='right', color=INK2, fontsize=9)
fig.suptitle('Test 7 – 5 cyklů zarovnaných na zastavení (klouzavý průměr 2 s)', color=INK, x=0.01, ha='left')
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_profily.png', dpi=150)

# --- obr2: namerene vysky proti geometrii studny ---
fig, a = plt.subplots(figsize=(6.5, 5))
bands = ((5.5, 8.5, 'spodní prstenec'), (11.5, 14.5, 'horní prstenec'))
for lo, hi, n in bands:
    a.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
    a.text(5.6, (lo + hi) / 2, n, va='center', color=INK2, fontsize=9)
a.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
a.text(5.6, 10, 'mezera (cíl testu 7)', va='center', color=INK2, fontsize=9)
a.plot(range(1, len(heights) + 1), heights, 'o', color=C_C1, ms=8)
for i, h in enumerate(heights, 1):
    a.annotate(f'{h:.1f}', (i, h), xytext=(0, -14), textcoords='offset points', ha='center', color=INK, fontsize=9)
a.set_xlim(0.5, 7.4); a.set_ylim(0, 16); a.set_xticks(range(1, len(heights) + 1))
a.set_xlabel('cyklus', color=INK2); a.set_ylabel('výška hladiny od vnějšího dna [mm]', color=INK2)
a.set_title('Naměřená výška hladiny po zastavení', color=INK, fontsize=11, loc='left')
a.grid(axis='y', color=GRID, lw=0.8)
for s in ('top', 'right'): a.spines[s].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr2_vysky.png', dpi=150)
