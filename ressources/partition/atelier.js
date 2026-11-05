// Studio Audio AI 1.0.91 : Atelier partition (page affichée par le Studio dans WebView2).
// Le Studio envoie { type: "load", abc, title, subtitle, info, name, t: { textes traduits } } ; la page répond
// { type: "ready" } au chargement et { type: "pdf", name } pour un export PDF (fait par le Studio).
"use strict";

const etat = { abc: "", name: "partition", zoom: 100, visuel: null, t: {} };
const $ = (id) => document.getElementById(id);
const studio = window.chrome && window.chrome.webview ? window.chrome.webview : null;
const envoyer = (m) => { if (studio) studio.postMessage(m); };
const tx = (cle, defaut) => (etat.t && etat.t[cle]) || defaut;

// Lecteur : sons de piano livrés avec le Studio (ressources\partition\sons), aucun accès à internet
const synthe = ABCJS.synth.supportsAudio() ? new ABCJS.synth.SynthController() : null;
const curseur = {
  onEvent(ev) {  // note jouée en couleur
    document.querySelectorAll(".abcjs-note_playing").forEach((e) => e.classList.remove("abcjs-note_playing"));
    if (ev && ev.elements) ev.elements.forEach((g) => g.forEach((e) => e.classList.add("abcjs-note_playing")));
  },
  onFinished() { document.querySelectorAll(".abcjs-note_playing").forEach((e) => e.classList.remove("abcjs-note_playing")); },
};
if (synthe) synthe.load("#lecteur", curseur, { displayLoop: true, displayRestart: true, displayPlay: true, displayProgress: true, displayWarp: true });

function dessiner(impression) {
  $("erreurs").textContent = "";
  // une image par ligne de portées (les pages se coupent entre deux lignes) ; à l'écran la largeur suit la fenêtre,
  // pour l'impression et le PDF elle est fixe (feuille A4, marges de 10 mm)
  $("partition").style.maxWidth = impression ? "none" : Math.round(11.5 * etat.zoom) + "px";
  const vus = ABCJS.renderAbc("partition", etat.abc, {
    add_classes: true,
    oneSvgPerLine: true,
    responsive: impression ? undefined : "resize",
    staffwidth: impression ? 640 : 760,
    wrap: { minSpacing: 1.6, maxSpacing: 2.7, preferredMeasuresPerLine: 4 },
    format: { gchordfont: "Segoe UI 12", vocalfont: "Segoe UI 12", titlefont: "Segoe UI 20 bold", subtitlefont: "Segoe UI 13" },
  });
  etat.visuel = vus && vus[0];
  const msgs = (etat.visuel && etat.visuel.warnings) || [];
  if (msgs.length) $("erreurs").textContent = tx("warnings", "Remarques de lecture :") + "\n" + msgs.join("\n").replace(/<[^>]+>/g, "");
  if (!impression && synthe && etat.visuel) synthe.setTune(etat.visuel, false, { soundFontUrl: "https://atelier.studio/sons/", program: 0 }).catch(() => {});
}

function charger(m) {
  etat.abc = m.abc || "";
  etat.name = m.name || "partition";
  etat.t = m.t || {};
  $("titre").textContent = m.title || "";
  $("soustitre").textContent = m.subtitle || "";
  $("infos").textContent = m.info || "";
  document.title = m.title || "Atelier partition";
  $("imprimer").textContent = tx("print", "Imprimer…");
  $("pdf").textContent = tx("pdf", "Exporter en PDF…");
  $("zoom_moins").title = tx("zoom_out", "Réduire");
  $("zoom_plus").title = tx("zoom_in", "Agrandir");
  dessiner(false);
}

function zoomer(d) {
  etat.zoom = Math.max(60, Math.min(180, etat.zoom + d));
  $("zoom_val").textContent = etat.zoom + " %";
  dessiner(false);
}

$("zoom_moins").onclick = () => zoomer(-10);
$("zoom_plus").onclick = () => zoomer(10);
window.addEventListener("beforeprint", () => dessiner(true));
window.addEventListener("afterprint", () => dessiner(false));
$("imprimer").onclick = () => window.print();
// PDF fait par le Studio : mise en page A4 d'abord, rétablie quand le Studio a fini ({ type: "pdf_done" })
$("pdf").onclick = () => { dessiner(true); envoyer({ type: "pdf", name: etat.name }); };
window.addEventListener("resize", () => { clearTimeout(window._r); window._r = setTimeout(() => dessiner(false), 200); });
if (studio) studio.addEventListener("message", (e) => {
  const m = e.data;
  if (m && m.type === "load") charger(m);
  else if (m && m.type === "pdf_done") dessiner(false);
  else if (m && m.type === "print_layout") dessiner(true);  // essai sans clic
});
envoyer({ type: "ready" });
