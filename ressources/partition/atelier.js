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

// 1.0.97 : abcjs ne coupe plus les lignes après un silence de plusieurs mesures « Z » (partitions reconnues : toute la fin se retrouve dans une
// seule ligne énorme, qui donne la même largeur à toutes les lignes et rend le zoom impossible). À l'écran, chaque « Z » est donc remplacé par
// des silences de mesure (« z8| »), et une mesure sans barre sur plus de NOTES_MAX notes est coupée. SEULEMENT pour l'affichage : le texte ABC
// (etat.abc) reste intact ; etat.edits garde chaque remplacement (position d'origine, longueur d'origine, longueur affichée) pour convertir
// les positions de clic et de surlignage.
const NOTES_MAX = Infinity;  // coupure des longues mesures désactivée : une barre ajoutée dans une seule voix désaligne les autres voix et casse la mise en ligne
function dureeMesure(abc) {
  const m = /^M:\s*(C\|?|\d+\/\d+)/m.exec(abc);
  let num = 4, den = 4;
  if (m) { if (m[1] === "C|") { num = 2; den = 2; } else if (m[1] !== "C") { const q = m[1].split("/"); num = +q[0]; den = +q[1]; } }
  const l = /^L:\s*1\/(\d+)/m.exec(abc);
  const unite = l ? +l[1] : (num / den < 0.75 ? 16 : 8);
  const v = (num / den) * unite;
  return Number.isInteger(v) && v > 0 ? v : 0;
}
function affichable(abc, sansSons) {
  const edits = [];
  const vuesVoix = new Set();
  const mes = dureeMesure(abc);
  let out = "", pos = 0;
  for (const ligne of abc.split("\n")) {
    const musique = ligne !== "" && !/^\s*%/.test(ligne) && !/^[A-Za-z]:/.test(ligne) && !/^\s*w:/.test(ligne);
    if (!musique) {
      out += ligne + "\n";
      const mv = /^V:\s*(\S+)/.exec(ligne);
      if (mv && !sansSons && !vuesVoix.has(mv[1])) {
        vuesVoix.add(mv[1]);
        const prog = programmeDe(etat.instruments[mv[1]] || "acoustic_grand_piano");
        const ajout = ["%%MIDI program " + prog, ""].join(String.fromCharCode(10));  // lecture abcjs : un canal par voix, choisi par abcjs
        out += ajout; edits.push({ o: pos + ligne.length + 1, ol: 0, dl: ajout.length });
      }
      pos += ligne.length + 1;
      continue;
    }
    let n = 0, i = 0;
    while (i < ligne.length) {
      const c = ligne[i];
      if (c === '"') { const j = ligne.indexOf('"', i + 1); const f = j < 0 ? ligne.length : j + 1; out += ligne.slice(i, f); i = f; continue; }
      if (c === "!") { const j = ligne.indexOf("!", i + 1); const f = j < 0 ? ligne.length : j + 1; out += ligne.slice(i, f); i = f; continue; }
      if (c === "[" && /^\[[A-Za-z]:/.test(ligne.slice(i, i + 3))) { const j = ligne.indexOf("]", i); const f = j < 0 ? ligne.length : j + 1; out += ligne.slice(i, f); i = f; continue; }
      if (c === "|" || c === ":") { n = 0; out += c; i++; continue; }
      if (c === "Z" && mes) {
        const z = /^Z(\d*)/.exec(ligne.slice(i));
        const k = z[1] ? +z[1] : 1;
        const rempl = Array.from({ length: k }, () => "z" + mes).join("|");
        edits.push({ o: pos + i, ol: z[0].length, dl: rempl.length });
        out += rempl;
        i += z[0].length;
        n = 0;
        continue;
      }
      const sub = ligne.slice(i);
      const m = c === "[" ? /^\[[^\]\[:]*\][0-9]*\/*[0-9]*-?/.exec(sub) : /^[\^_=]*[A-Ga-gzxZ][,']*[0-9]*\/*[0-9]*-?/.exec(sub);
      if (m) {
        if (n >= NOTES_MAX) { edits.push({ o: pos + i, ol: 0, dl: 2 }); out += "| "; n = 0; }
        n++;
        out += m[0];
        i += m[0].length;
        continue;
      }
      out += c; i++;
    }
    out += "\n";
    pos += ligne.length + 1;
  }
  etat.edits = edits;
  return out.slice(0, -1);
}
// position affichée -> position dans le texte d'origine
function versOriginal(p) {
  let dec = 0;
  for (const e of etat.edits || []) {
    const ds = e.o + dec;
    if (p >= ds + e.dl) dec += e.dl - e.ol;
    else if (p >= ds) return e.o;
    else break;
  }
  return p - dec;
}
// position d'origine -> position affichée
function versAffiche(p) {
  let dec = 0;
  for (const e of etat.edits || []) {
    if (p >= e.o + e.ol) dec += e.dl - e.ol;
    else if (p >= e.o && e.ol > 0) return e.o + dec;
    else break;
  }
  return p + dec;
}

// 1.0.97 : instrument de chaque portée. etat.sons = [{ id, label, program, installed }] (donné par le Studio) ; le choix de chaque voix
// est ajouté à l'affichage sous la forme « %%MIDI program N » (abcjs, MIDI et banque SoundFont le suivent) sans toucher à etat.abc.
etat.sons = []; etat.banque = ""; etat.instruments = {};
function cleInstr(id) { return "instr:" + etat.name + ":" + id; }
function programmeDe(idSon) { const s = etat.sons.find((x) => x.id === idSon); return s ? s.program : 0; }
function defautInstrument(vid) {
  const v = vid.toLowerCase();
  const choisir = (...ids) => ids.find((i) => etat.sons.some((s) => s.id === i && s.installed));
  if (/bass|basse/.test(v)) return choisir("electric_bass_finger", "acoustic_bass") || "acoustic_grand_piano";
  if (/other|autre|^ins/.test(v)) return choisir("acoustic_guitar_steel", "electric_guitar_clean", "string_ensemble_1") || "acoustic_grand_piano";
  if (/vocal|voix/.test(v)) return choisir("voice_oohs", "synth_choir", "flute") || "acoustic_grand_piano";
  return "acoustic_grand_piano";
}
function majInstruments() {
  const conteneur = el("liste_instruments");
  conteneur.textContent = "";
  const voix = etat.abc.trim() ? separerVoix(etat.abc) : [];
  etat.instruments = {};
  for (const v of voix) {
    let choix = null;
    try { choix = localStorage.getItem(cleInstr(v.id)); } catch (e) { /* stockage indisponible */ }
    choix = choix || defautInstrument(v.id);
    if (!etat.sons.some((s) => s.id === choix && s.installed)) choix = "acoustic_grand_piano";
    etat.instruments[v.id] = choix;
    const lab = document.createElement("label");
    lab.className = "lib";
    lab.textContent = v.nom + " ";
    const sel = document.createElement("select");
    for (const s of etat.sons) {
      const o = document.createElement("option");
      o.value = s.id; o.textContent = s.label + (s.installed ? "" : " ↓"); o.disabled = !s.installed; o.selected = s.id === choix;
      sel.appendChild(o);
    }
    sel.onchange = () => { etat.instruments[v.id] = sel.value; try { localStorage.setItem(cleInstr(v.id), sel.value); } catch (e) { /* ignoré */ } dessiner(false); };
    lab.appendChild(sel);
    conteneur.appendChild(lab);
  }
  el("barre_sons").style.display = voix.length && etat.sons.length ? "" : "none";
}

function dessiner(impression) {
  el("erreurs").textContent = "";
  // une image par ligne de portées (les pages se coupent entre deux lignes) ; à l'écran la largeur suit la fenêtre,
  // pour l'impression et le PDF elle est fixe (feuille A4, marges de 10 mm)
  el("partition").style.maxWidth = impression ? "none" : Math.round(11.5 * etat.zoom) + "px";
  const vus = ABCJS.renderAbc("partition", affichable(etat.abc), {
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
  // 1.0.97 : instruments et banque SoundFont
  etat.sons = m.sons || [];
  etat.banque = m.banque || "";
  el("lib_instruments").textContent = tx("instruments", "Instruments :");
  el("hq_lire").textContent = tx("hq_play", "Écouter (banque SoundFont)");
  el("hq_wav").textContent = tx("hq_wav", "Exporter en audio…");
  el("sons_gerer").textContent = tx("sons_manage", "Gérer les sons…");
  el("hq_lire").title = tx("hq_play_tip", "");
  el("sons_gerer").title = tx("sons_manage_tip", "");
  el("hq_audio").hidden = true;
  majInstruments();
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
    majInstruments();
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
// 1.0.97 : lecture et export audio avec la banque SoundFont choisie dans le Studio (MIDI avec les instruments de chaque portée)
// Le fichier MIDI écrit par abcjs ne place pas le programme sur le bon canal : instruments et canaux sont donc remis voix par voix (tonejs-midi),
// un canal par voix en sautant le canal 9 (percussions).
function midiHq() {
  const brut = atob(abcVersMidi(affichable(etat.abc, true)));
  const m = new Midi(Uint8Array.from(brut, (c) => c.charCodeAt(0)).buffer);
  const voix = separerVoix(etat.abc);
  let n = 0;
  m.tracks.forEach((t) => {
    if (!t.notes.length) return;
    const v = voix[n];
    t.channel = n >= 9 ? n + 1 : n;
    t.instrument.number = programmeDe((v && etat.instruments[v.id]) || "acoustic_grand_piano");
    n++;
  });
  for (let i = m.tracks.length - 1; i >= 0; i--) if (!m.tracks[i].notes.length) m.tracks.splice(i, 1);  // une piste vide remettrait le piano sur le canal 0
  const o = m.toArray();
  return versBase64(o instanceof Uint8Array ? o : new Uint8Array(o));
}
el("hq_lire").onclick = () => {
  if (!etat.banque) { envoyer({ type: "sons_settings" }); return; }
  el("hq_lire").disabled = true;
  try { envoyer({ type: "render", play: true, name: etat.name, midi: midiHq() }); } catch (e) { el("erreurs").textContent = String(e); el("hq_lire").disabled = false; }
};
el("hq_wav").onclick = () => {
  if (!etat.banque) { envoyer({ type: "sons_settings" }); return; }
  try { envoyer({ type: "render", play: false, name: etat.name, midi: midiHq() }); } catch (e) { el("erreurs").textContent = String(e); }
};
el("sons_gerer").onclick = () => envoyer({ type: "sons_settings" });
window.addEventListener("resize", () => { clearTimeout(window._r); window._r = setTimeout(() => dessiner(false), 200); });
if (studio) studio.addEventListener("message", (e) => {
  const m = e.data;
  if (m && m.type === "load") charger(m);
  else if (m && m.type === "pdf_done") dessiner(false);
  else if (m && m.type === "test_render") el("hq_lire").click();  // essai sans clic
  else if (m && m.type === "audio") {  // rendu terminé par le Studio : lecture
    el("hq_lire").disabled = false;
    if (m.error) { el("erreurs").textContent = m.error; return; }
    const a = el("hq_audio");
    a.src = m.url; a.hidden = false; a.play().catch(() => {});
  }
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
