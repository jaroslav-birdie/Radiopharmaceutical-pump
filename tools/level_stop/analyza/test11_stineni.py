# Test 11 bez stineni proti testu 11 se stinenim: vysky a sum.
# Pouziti (ze slozky tools/level_stop/):
#   python3 analyza/test11_stineni.py <log_t9> <log_t10> <log_t11> <log_t11_stineni> [slozka]
import re, os, sys, statistics as st
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

OUT = sys.argv[5] if len(sys.argv) > 5 else 'img11s'
os.makedirs(OUT, exist_ok=True)
INK, INK2, GRID = '#0b0b0b', '#52514e', '#e4e3df'
C_NO, C_SH = '#eb6834', '#2a78d6'

def parse(fn):
    h, rows = [], {}
    for ln in open(fn, errors='ignore'):
        ln = ln.split('-> ', 1)[-1].strip()
        m = re.match(r'# obsluha: ([0-9]+[,.]?[0-9]*)$', ln)
        if m:
            h.append(float(m.group(1).replace(',', '.')))
        p = ln.split(';')
        if len(p) == 7 and p[0].isdigit():
            try:
                rows.setdefault(int(p[0]), []).append((p[2], float(p[4]), float(p[6])))
            except ValueError:
                pass
    rows.pop(0, None)
    noise = []
    for c, rs in sorted(rows.items()):
        w = [(a, b) for ph, a, b in rs if ph == 'w']
        n1 = st.median(st.pstdev([x[0] for x in w[i:i + 12]]) for i in range(0, len(w) - 12, 12))
        n3 = st.median(st.pstdev([x[1] for x in w[i:i + 12]]) for i in range(0, len(w) - 12, 12))
        noise.append((n1, n3))
    return h, noise

h9, _ = parse(sys.argv[1]); h10, _ = parse(sys.argv[2])
h11, n11 = parse(sys.argv[3]); h11s, n11s = parse(sys.argv[4])
no = h9 + h10 + h11
print(f'bez stineni (testy 9-11): {st.mean(no):.2f} +- {st.stdev(no):.2f} mm ({min(no)}-{max(no)})')
print(f'se stinenim (test 11):    {h11s}  {st.mean(h11s):.2f} +- {st.stdev(h11s):.2f} mm')
allh = no + h11s
print(f'vse ({len(allh)}): {st.mean(allh):.2f} +- {st.stdev(allh):.2f} mm, min {min(allh)}, '
      f'zbytek {(st.mean(allh) - 2.5) / 1.5:.2f} ml')
for name, n in (('bez stineni', n11), ('se stinenim', n11s)):
    print(f'{name}: sum C1 {st.mean(x[0] for x in n):.3f} pF, C3 {st.mean(x[1] for x in n):.3f} pF')

fig, ax = plt.subplots(1, 2, figsize=(12, 4.6), gridspec_kw={'width_ratios': [3, 2]})
a = ax[0]
for lo, hi in ((5.5, 8.5), (11.5, 14.5)):
    a.axhspan(lo, hi, color='#d9d8d3', alpha=0.6)
a.axhspan(8.5, 11.5, color='#1baf7a', alpha=0.10)
a.axhline(8.5, color=INK2, lw=1, ls='--')
a.text(21.3, 7.0, 'spodní\nprstenec', color=INK2, fontsize=8, va='center')
a.text(21.3, 10.0, 'mezera', color=INK2, fontsize=8, va='center')
a.plot(range(1, len(no) + 1), no, 'o', color=C_NO, ms=7, label='bez stínění (testy 9–11)')
a.plot(range(len(no) + 2, len(no) + 2 + len(h11s)), h11s, 's', color=C_SH, ms=7,
       label='se stíněním (test 11)')
a.set_xlim(0, 24); a.set_ylim(4, 14); a.set_xticks([])
a.set_ylabel('výška hladiny po zastavení [mm]', color=INK2)
a.set_title('Výška po zastavení – 20 cyklů', color=INK, fontsize=11, loc='left')
a.legend(frameon=False, loc='upper left'); a.grid(axis='y', color=GRID, lw=0.8)
b = ax[1]
x = [0, 1]
b.bar([v - 0.18 for v in x], [st.mean(n[0] for n in n11), st.mean(n[1] for n in n11)],
      width=0.34, color=C_NO, label='bez stínění')
b.bar([v + 0.18 for v in x], [st.mean(n[0] for n in n11s), st.mean(n[1] for n in n11s)],
      width=0.34, color=C_SH, label='se stíněním')
b.set_xticks(x); b.set_xticklabels(['C1 spodní', 'C3 horní'])
b.set_ylabel('šum při tlačení [pF] (sd ve 12 vzorcích)', color=INK2)
b.set_title('Šum signálu – test 11', color=INK, fontsize=11, loc='left')
b.legend(frameon=False); b.grid(axis='y', color=GRID, lw=0.8)
for axx in ax:
    for sp in ('top', 'right'): axx.spines[sp].set_visible(False)
fig.tight_layout(); fig.savefig(f'{OUT}/obr1_stineni.png', dpi=150)
