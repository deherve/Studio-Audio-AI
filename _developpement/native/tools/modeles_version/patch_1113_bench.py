# 1.1.013 : banc de mesure (STUDIO_TEST_BENCH=<fichier de sortie>) : temps de calcul par image, rescan, snapshot.
import sys
p = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src\main.cpp'
s = open(p, encoding='utf-8', newline='').read()
NL = '\r\n'


def rep(a, b):
    global s
    a = a.replace('\n', NL)
    b = b.replace('\n', NL)
    if s.count(a) != 1:
        print('ERREUR', s.count(a), a[:80])
        sys.exit(1)
    s = s.replace(a, b)


rep('''    bool   done        = false;
    int    frames      = 3;
    double last_rescan = 0;
    while (!done) {
        if (frames <= 0) {''', '''    bool   done        = false;
    int    frames      = 3;
    double last_rescan = 0;
    // Banc de mesure (1.1.013) : « STUDIO_TEST_BENCH=<fichier> » dessine 60 images d'échauffement puis 300 images mesurées
    // (calcul de l'image seulement, sans l'attente de l'écran), mesure aussi rescan() et snapshot(), écrit le fichier et quitte.
    const wchar_t *     bench_out = _wgetenv(L"STUDIO_TEST_BENCH");
    int                 bench_n   = 0;
    std::vector<double> bench_ms;
    while (!done) {
        if (bench_out) frames = 3;
        if (frames <= 0) {''')
rep('''        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        journal::imgui_frame_end();
        ImGui::Render();
''', '''        const auto bench_t0 = std::chrono::steady_clock::now();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        app.frame();
        journal::imgui_frame_end();
        ImGui::Render();
        if (bench_out && ++bench_n > 60) {
            bench_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bench_t0).count());
            if (bench_ms.size() >= 300) {
                std::sort(bench_ms.begin(), bench_ms.end());
                double sum = 0;
                for (double v : bench_ms) sum += v;
                auto timed = [](int n, auto && fn) {
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int i = 0; i < n; i++) fn();
                    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
                };
                const double rescan_ms = timed(10, [&] { studio.rescan(); });
                size_t       count     = 0;
                const double snap_ms   = timed(200, [&] { count = studio.snapshot().size(); });
                if (FILE * f = _wfopen(bench_out, L"wb")) {
                    fprintf(f, "morceaux=%d image_moy_ms=%.3f image_mediane_ms=%.3f image_p95_ms=%.3f image_max_ms=%.3f rescan_ms=%.3f snapshot_ms=%.4f\\n",
                            (int) count, sum / bench_ms.size(), bench_ms[bench_ms.size() / 2], bench_ms[bench_ms.size() * 95 / 100], bench_ms.back(),
                            rescan_ms, snap_ms);
                    fclose(f);
                }
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                bench_out = nullptr;
            }
        }
''')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('OK')
