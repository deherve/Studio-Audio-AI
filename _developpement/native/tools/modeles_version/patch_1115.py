# 1.1.015 : prévision de la file (Studio::eta_for, fin estimée de la file, temps estimé du formulaire, avertissement morceau long)
import sys
SRC = r'C:\Users\deher\Downloads\dev\music\_developpement\native\src'


def patch(name, pairs):
    p = SRC + '\\' + name
    s = open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl)
        b = b.replace('\n', nl)
        if s.count(a) != 1:
            print('ERREUR', name, s.count(a), a[:80])
            sys.exit(1)
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


patch('studio.hpp', [
    ('''    std::vector<Song> snapshot();''', '''    std::vector<Song> snapshot();
    // 1.1.015 : temps estimé (secondes) pour générer ce morceau en entier, d'après les mesures de l'historique (même modèle que la
    // barre de progression). Un morceau en cours : voir Song::eta. 0 si le morceau n'est pas à générer.
    double            eta_for(const Song & s);'''),
])

patch('studio.cpp', [
    ('''void Studio::refit_model() {''', '''double Studio::eta_for(const Song & s) {
    EtaModel m;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        m = model_;
    }
    const bool   rerender = s.kind == "rerender", extend = s.kind == "extend";
    const double solver   = solver_factor(s.solver);
    const bool   has_score = s.cot != "off" && !rerender && !extend && s.kind != "cover" && s.abc.empty();
    double       frames;
    if (s.expected_seconds > 0) frames = std::min(s.expected_seconds, s.duration > 0 ? s.duration : s.expected_seconds) * 25;
    else frames = s.duration * 25 * (s.instrumental ? m.fill_inst : m.fill_song);
    if (frames <= 0) return 0;
    double t = 6;  // chargement du modèle
    if (has_score) t += 1100 / m.score_rate;
    if (!rerender) t += frames / m.sem_rate;
    return t + 32 * m.nar_step(frames) * solver + 12;
}

void Studio::refit_model() {'''),
])

patch('app_score.cpp', [
    ('''    count(held, tr("en pause"), P.amber);
    if (studio->gate_closed()) {''', '''    count(held, tr("en pause"), P.amber);
    {   // 1.1.015 : fin estimée de la file (morceau en cours + morceaux en attente, hors pause)
        double left = 0;
        const double now = now_seconds();
        for (const Song & s : songs) {
            if (s.status == "running") left += std::max(0.0, s.eta - (now - s.eta_at));
            else if (s.waiting()) left += studio->eta_for(s);
        }
        if (left > 60 && running + waiting > 0) {
            time_t end = time(nullptr) + (time_t) left;
            tm     lt{};
            localtime_s(&lt, &end);
            char clock[16];
            snprintf(clock, sizeof(clock), "%02d:%02d", lt.tm_hour, lt.tm_min);
            ImGui::SameLine(0, 12 * g_dpi);
            ImGui::TextColored(P.muted, "%s", trf("Fin estimée : %s (vers %s)", fmt_time(left).c_str(), clock).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Estimation d'après la durée des morceaux déjà générés sur ce PC ; elle s'affine au fil des générations."));
        }
    }
    if (studio->gate_closed()) {'''),
])

patch('app_form.cpp', [
    ('''    if (!missing.empty()) muted_wrapped(trf("À renseigner pour lancer la génération : %s.", missing.c_str()));
}''', '''    if (!missing.empty()) muted_wrapped(trf("À renseigner pour lancer la génération : %s.", missing.c_str()));
    else {  // 1.1.015 : temps estimé de ce morceau, et avertissement pour un morceau long sur une carte de 12 Go
        Song p;
        p.kind         = f.cover.empty() ? "new" : "cover";
        p.instrumental = f.instrumental;
        p.cot          = f.cot == 1 ? "melody" : f.cot == 2 ? "off" : "full";
        p.solver       = current_solver;
        p.duration     = f.auto_duration ? (p.kind == "cover" ? 360 : f.instrumental ? estimate_seconds(lyrics, true) : 360) : f.duration;
        p.abc          = submit_abc;
        const double est = f.cover.empty() ? estimate_seconds(lyrics, f.instrumental) : f.cover_seconds;
        p.expected_seconds = f.auto_duration ? est : std::min<double>(f.duration, est);
        const double secs  = studio->eta_for(p) * std::max(1, f.versions);
        if (secs > 0) {
            ImGui::TextColored(P.muted, "%s", (f.versions > 1 ? trf("Temps estimé : environ %s pour %d versions.", fmt_time(secs).c_str(), f.versions)
                                                              : trf("Temps estimé : environ %s.", fmt_time(secs).c_str())).c_str());
        }
        const GpuMem g = gpu_monitor.get();
        if (p.expected_seconds > 240 && g.total > 0 && g.total <= (unsigned long long) (12.5 * 1073741824.0))
            colored_wrapped(P.amber, tr("Morceau long : sur une carte de 12 Go ou moins, la mémoire vidéo peut manquer vers la fin au-delà de 4 minutes environ ; le Studio refait alors un essai tout seul."));
    }
}'''),
])
print('OK')
