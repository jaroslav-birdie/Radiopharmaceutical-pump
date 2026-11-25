// Prehrani logu sketche level_stop pres PRODUKCNI LevelDetector (na PC).
// Preklad (z korene repozitare):
//   g++ -std=c++11 -Itools/level_stop/analyza/host_stub -I.
//       tools/level_stop/analyza/replay.cpp level_detector.cpp -o replay
// Pouziti: ./replay <log1> [log2 ...] - pro kazdy cyklus porovna STOP v logu
// s okamzikem, kdy zastavi produkcni detektor.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "level_detector.h"
static const char *NAMES[] = {"-", "podil", "sablona", "vrchol", "SKOK"};
int main(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "r");
        if (!f) { perror(argv[a]); return 1; }
        char line[512];
        LevelDetector det;
        int cyc = -1; char prev = 0; bool done = false; long logStop = -1;
        long hit = -1; LevelResult res = LVL_NONE; unsigned fit = 0; float q = 0, c3n = 0;
        auto report = [&]() {
            if (cyc > 0)
                printf("%s c%d: log STOP t=%ld | replay %s t=%ld  (rozdil %ld ms) C3/max=%.3f fit=%u qmax=%.3f\n",
                       argv[a], cyc, logStop, NAMES[res], hit, hit - logStop, c3n, fit, q);
        };
        while (fgets(line, sizeof line, f)) {
            char *p = strstr(line, "-> ");
            p = p ? p + 3 : line;
            if (!strncmp(p, "# STOP", 6) || !strncmp(p, "# *** SKOK", 10)) {
                char *t = strstr(p, "@ t=");
                if (t && logStop < 0) logStop = atol(t + 4);
                continue;
            }
            int c; long t; char ph; float ml, c1, c2, c3;
            if (sscanf(p, "%d;%ld;%c;%f;%f;%f;%f", &c, &t, &ph, &ml, &c1, &c2, &c3) != 7 || c == 0) continue;
            if (c != cyc) { report(); cyc = c; det.reset(); prev = 0; done = false; logStop = -1; hit = -1; res = LVL_NONE; }
            bool flow = (ph == 'p' || ph == 'w');
            if (flow && !(prev == 'p' || prev == 'w')) det.flowBreak();
            prev = ph;
            if (!flow || done) continue;
            LevelResult r = det.feed(c1, c3, c2);
            if (r != LVL_NONE) { done = true; res = r; hit = t; fit = det.fitFlags(); q = det.qAtMax(); c3n = det.winC3() / det.maxC3(); }
        }
        report();
        fclose(f);
    }
}
