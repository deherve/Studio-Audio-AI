// Studio Audio AI 1.0.91-1.0.92 : Atelier partition (page affichée par le Studio dans WebView2).
// Le Studio envoie { type: "load", abc, title, subtitle, composer, info, name, t: { textes traduits } } ; la page répond
// { type: "ready" } au chargement, { type: "pdf", name } pour un export PDF et { type: "save", ext, name, data, base64 }
// pour enregistrer un fichier (le Studio demande où).
"use strict";

const etat = { abc: "", name: "partition", titre: "", auteur: "", zoom: 100, visuel: null, t: {} };
const el = (id) => document.getElementById(id);  // (pas « $ » : réservé à jQuery, utilisé par xml2abc)
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
  el("erreurs").textContent = "";
  // une image par ligne de portées (les pages se coupent entre deux lignes) ; à l'écran la largeur suit la fenêtre,
  // pour l'impression et le PDF elle est fixe (feuille A4, marges de 10 mm)
  el("partition").style.maxWidth = impression ? "none" : Math.round(11.5 * etat.zoom) + "px";
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
  if (msgs.length) el("erreurs").textContent = tx("warnings", "Remarques de lecture :") + "\n" + msgs.join("\n").replace(/<[^>]+>/g, "");
  if (!impression && synthe && etat.visuel) synthe.setTune(etat.visuel, false, { soundFontUrl: "https://atelier.studio/sons/", program: 0 }).catch(() => {});
}

function charger(m) {
  etat.abc = m.abc || "";
  etat.name = m.name || "partition";
  etat.t = m.t || {};
  etat.titre = m.title || "";
  etat.auteur = m.composer || "";
  el("exp_midi").title = tx("midi_tip", "");
  el("exp_xml").title = tx("xml_tip", "");
  el("exp_abc").title = tx("abc_tip", "");
  el("titre").textContent = m.title || "";
  el("soustitre").textContent = m.subtitle || "";
  el("infos").textContent = m.info || "";
  document.title = m.title || "Atelier partition";
  el("imprimer").textContent = tx("print", "Imprimer…");
  el("pdf").textContent = tx("pdf", "Exporter en PDF…");
  el("zoom_moins").title = tx("zoom_out", "Réduire");
  el("zoom_plus").title = tx("zoom_in", "Agrandir");
  dessiner(false);
}

function zoomer(d) {
  etat.zoom = Math.max(60, Math.min(180, etat.zoom + d));
  el("zoom_val").textContent = etat.zoom + " %";
  dessiner(false);
}

el("zoom_moins").onclick = () => zoomer(-10);
el("zoom_plus").onclick = () => zoomer(10);
window.addEventListener("beforeprint", () => dessiner(true));
window.addEventListener("afterprint", () => dessiner(false));
el("imprimer").onclick = () => window.print();
// PDF fait par le Studio : mise en page A4 d'abord, rétablie quand le Studio a fini ({ type: "pdf_done" })
el("pdf").onclick = () => { dessiner(true); envoyer({ type: "pdf", name: etat.name }); };
// 1.0.92 : exports (le Studio demande où enregistrer) : MIDI (abcjs), MusicXML (atelier_formats.js), ABC (texte du moteur)
function exporter(ext) {
  try {
    if (ext === "mid") envoyer({ type: "save", ext, name: etat.name, base64: true, data: abcVersMidi(etat.abc) });
    else if (ext === "musicxml") envoyer({ type: "save", ext, name: etat.name, base64: false, data: abcVersMusicXML(etat.abc, etat.titre, etat.auteur) });
    else envoyer({ type: "save", ext: "abc", name: etat.name, base64: false, data: etat.abc });
  } catch (e) {
    el("erreurs").textContent = tx("export_error", "Export impossible :") + " " + e;
  }
}
el("exp_midi").onclick = () => exporter("mid");
el("exp_xml").onclick = () => exporter("musicxml");
el("exp_abc").onclick = () => exporter("abc");
window.addEventListener("resize", () => { clearTimeout(window._r); window._r = setTimeout(() => dessiner(false), 200); });
if (studio) studio.addEventListener("message", (e) => {
  const m = e.data;
  if (m && m.type === "load") charger(m);
  else if (m && m.type === "pdf_done") dessiner(false);
  else if (m && m.type === "print_layout") dessiner(true);  // essai sans clic
  else if (m && m.type === "test_export") { exporter("mid"); exporter("musicxml"); exporter("abc"); }
});
envoyer({ type: "ready" });
