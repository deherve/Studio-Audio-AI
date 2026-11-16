# 1.1.015 : essai « --eta-test sortie.txt » : estimation (eta_for) comparée au temps réel des morceaux terminés
import sys
p = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src\main.cpp'
s = open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '        if (argv && argc >= 6 && std::wstring(argv[1]) == L"--gen-test") {'
assert s.count(a) == 1
b = '''        if (argv && argc >= 3 && std::wstring(argv[1]) == L"--eta-test") {  // 1.1.015
            FILE * f = _wfopen(argv[2], L"wb");
            double sum_err = 0, sum_abs = 0, sum_real = 0;
            int    n = 0;
            if (f) fprintf(f, "titre ; reel_s ; estime_s ; ecart_%%\\n");
            for (const Song & x : studio.snapshot()) {
                if (x.status != "done" || x.started <= 0 || x.finished <= x.started || x.kind == "extend" || x.kind == "rerender") continue;
                const double real = x.finished - x.started, est = studio.eta_for(x);
                if (est <= 0 || real > 3600) continue;
                sum_err += (est - real) / real;
                sum_abs += std::fabs(est - real) / real;
                sum_real += real;
                n++;
                if (f && n <= 40) fprintf(f, "%s ; %.0f ; %.0f ; %+.0f\\n", x.title.c_str(), real, est, 100.0 * (est - real) / real);
            }
            if (f) {
                fprintf(f, "morceaux=%d ecart_moyen=%+.1f%% ecart_absolu_moyen=%.1f%% temps_reel_total=%.0fs\\n", n, n ? 100 * sum_err / n : 0, n ? 100 * sum_abs / n : 0, sum_real);
                fclose(f);
            }
            return 0;
        }
'''.replace('\n', nl)
s = s.replace(a, b + a)
open(p, 'w', encoding='utf-8', newline='').write(s)
print('OK')
