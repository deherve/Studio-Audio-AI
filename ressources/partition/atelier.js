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
    clickListener: impression ? undefined : (abcElem) => clicNote(abcElem),  // 1.0.94 : choix de la note à modifier
    selectionColor: "#d9480f",
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
  el("importer").textContent = tx("import", "Importer…");
  el("importer").title = tx("import_tip", "");
  el("utiliser").textContent = tx("use", "Utiliser dans le Studio");
  el("utiliser").title = tx("use_tip", "");
  // 1.0.94 : édition
  edition.choix = null; edition.annuler = []; edition.refaire = [];
  el("paroles").value = m.lyrics || "";
  const textes = {
    lib_note: ["note", "Note choisie :"], ed_silence: ["rest", "Silence"], ed_dupliquer: ["duplicate", "Dupliquer"], ed_supprimer: ["delete", "Supprimer"],
    ed_accord: ["chord", "Accord…"], ed_paroles: ["lyrics", "Paroles…"], ed_texte: ["abc_text", "Texte ABC"], lib_paroles: ["lyrics_help",
      "Paroles placées sous la mélodie chantée, une syllabe par note (séparez les syllabes d'un mot par « - », ex. : « le ma-tin se lè-ve »). Les sections entre crochets sont ignorées."],
    placer_paroles: ["place_lyrics", "Placer sous les notes"], retirer_paroles: ["remove_lyrics", "Retirer les paroles"],
    lib_texte: ["abc_help", "Texte de la partition (format ABC) : chaque modification est redessinée aussitôt."],
  };
  for (const [id, [cle, defaut]] of Object.entries(textes)) el(id).textContent = tx(cle, defaut);
  const bulles = { ed_descendre: "down", ed_monter: "up", ed_oct_bas: "oct_down", ed_oct_haut: "oct_up", ed_diese: "sharp", ed_bemol: "flat", ed_becarre: "natural",
    ed_point: "dot", annuler: "undo", refaire: "redo", ed_silence: "rest_tip", ed_dupliquer: "duplicate_tip", ed_supprimer: "delete_tip", ed_accord: "chord_tip" };
  for (const [id, cle] of Object.entries(bulles)) el(id).title = tx(cle, "") + (el(id).dataset.k ? "  (" + el(id).dataset.k + ")" : "");
  const noms = ["whole", "half", "quarter", "eighth", "sixteenth"];
  el("durees").querySelectorAll("button[data-v]").forEach((b, i) => { b.title = tx(noms[i], "") + "  (" + b.dataset.k + ")"; });
  majBoutons();
  apresEdition();
}

function majBoutons() {
  const vide = !etat.abc.trim();
  for (const id of ["utiliser", "exp_midi", "exp_xml", "exp_abc", "pdf", "imprimer"]) el(id).disabled = vide;
  el("barre_edition").style.display = vide ? "none" : "";
  if (vide) el("infos").textContent = tx("empty", "Aucune partition : « Importer… » pour ouvrir un fichier MusicXML, MIDI ou ABC.");
}

// 1.0.93 : import d'un fichier (MusicXML, .mxl, MIDI, ABC), mis au format de YuE2, puis « Utiliser dans le Studio »
el("importer").onclick = () => el("fichier").click();
el("fichier").onchange = async () => {
  const f = el("fichier").files[0];
  el("fichier").value = "";
  if (!f) return;
  try {
    const r = await importerFichier(f);
    etat.abc = r.abc;
    etat.name = r.nom;
    etat.titre = r.nom;
    el("titre").textContent = r.nom;
    el("soustitre").textContent = tx("imported", "Partition importée") + " · " + f.name;
    majBoutons();
    const sc = ABCJS.parseOnly(etat.abc)[0];
    const k = sc && sc.lines[0] && sc.lines[0].staff && sc.lines[0].staff[0].key;
    el("infos").textContent = (r.remarques || []).join(" ") + (k ? "  " + tx("key", "Tonalité") + " : " + k.root + (k.acc === "sharp" ? "♯" : k.acc === "flat" ? "♭" : "") + (k.mode === "m" ? "m" : "") : "");
    edition.choix = null; edition.annuler = []; edition.refaire = [];
    apresEdition();
  } catch (e) {
    el("erreurs").textContent = tx("import_error", "Import impossible :") + " " + (e && e.message ? e.message : e);
  }
};
// le moteur n'utilise pas les paroles placées sous les notes (« w: ») : retirées avant l'envoi
el("utiliser").onclick = () => envoyer({ type: "use", abc: etat.abc.split("\n").filter((l) => !/^w:/.test(l)).join("\n"), name: etat.name });

// 1.0.94 : boutons d'édition, clavier, paroles, texte ABC
el("ed_monter").onclick = actions.monter;
el("ed_descendre").onclick = actions.descendre;
el("ed_oct_haut").onclick = actions.octave_haut;
el("ed_oct_bas").onclick = actions.octave_bas;
el("ed_diese").onclick = actions.diese;
el("ed_bemol").onclick = actions.bemol;
el("ed_becarre").onclick = actions.becarre;
el("ed_point").onclick = actions.pointee;
el("ed_silence").onclick = actions.silence;
el("ed_dupliquer").onclick = actions.dupliquer;
el("ed_supprimer").onclick = actions.supprimer;
el("ed_accord").onclick = actions.accord;
el("annuler").onclick = annuler;
el("refaire").onclick = refaire;
el("durees").querySelectorAll("button[data-v]").forEach((b) => { b.onclick = () => duree(+b.dataset.v); });
document.addEventListener("keydown", clavier);
el("ed_paroles").onclick = () => { el("panneau_paroles").hidden = !el("panneau_paroles").hidden; };
el("ed_texte").onclick = () => { el("panneau_texte").hidden = !el("panneau_texte").hidden; el("texte_abc").value = etat.abc; };
el("placer_paroles").onclick = () => {
  const r = placerParoles(etat.abc, el("paroles").value);
  edition.annuler.push(etat.abc); edition.refaire = [];
  etat.abc = r.abc;
  el("paroles_etat").textContent = tx("lyrics_placed", "%d syllabe(s) placée(s) sur %d.").replace("%d", r.places).replace("%d", r.total);
  apresEdition();
};
el("retirer_paroles").onclick = () => {
  edition.annuler.push(etat.abc); edition.refaire = [];
  etat.abc = etat.abc.split("\n").filter((l) => !/^w:/.test(l)).join("\n");
  el("paroles_etat").textContent = "";
  apresEdition();
};
el("texte_abc").addEventListener("input", () => {
  clearTimeout(window._t);
  window._t = setTimeout(() => {
    if (el("texte_abc").value === etat.abc) return;
    edition.annuler.push(etat.abc); edition.refaire = [];
    etat.abc = el("texte_abc").value;
    edition.choix = null;
    apresEdition();
  }, 400);
});

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
  else if (m && m.type === "test_import") {  // essai sans clic : fichier lu par le Studio, passé en base 64
    const octets = Uint8Array.from(atob(m.data), (c) => c.charCodeAt(0));
    const f = new File([octets], m.name);
    const dt = new DataTransfer(); dt.items.add(f); el("fichier").files = dt.files;
    el("fichier").onchange().then(() => { if (m.use) setTimeout(() => el("utiliser").click(), 1500); });
  }
});
envoyer({ type: "ready" });
