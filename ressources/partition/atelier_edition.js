// Studio Audio AI 1.0.94 : édition dans l'Atelier partition. Toutes les retouches modifient le texte ABC (etat.abc) à la
// position de la note choisie (startChar / endChar donnés par abcjs), puis la partition est redessinée et la note reste choisie.
//   Note : hauteur (degré, octave, ♯ ♭ ♮), durée (ronde à double croche, point), silence, dupliquer, supprimer, accord.
//   Paroles : placées sous la mélodie chantée (lignes « w: »), syllabe par syllabe, d'après un texte modifiable.
//   Annuler / rétablir, texte ABC modifiable avec redessin en direct, contrôle de la durée des mesures.
"use strict";

const edition = { choix: null, annuler: [], refaire: [] };

// « "Dm"^F4- » -> morceaux de la note ; null si ce n'est pas une note simple (accord de notes [CEG], etc.)
const NOTE_RE = /^((?:"[^"]*"\s*)*)((?:![^!]*!|[.~HLMOPSTuv])*)(\^\^|\^|__|_|=)?([A-Ga-gzZx])([,']*)(\d*(?:\/+\d*)?)(-?)(\s*)$/;
function decouperNote(t) {
  const m = NOTE_RE.exec(t);
  return m ? { accord: m[1], deco: m[2], acc: m[3] || "", lettre: m[4], oct: m[5], duree: m[6], lie: m[7], fin: m[8] } : null;
}
function recoller(n) { return n.accord + n.deco + n.acc + n.lettre + n.oct + n.duree + n.lie + n.fin; }

function uniteL(abc) {  // L: -> fraction de ronde (1/8 par défaut)
  const m = /^L:\s*(\d+)\/(\d+)/m.exec(abc);
  return m ? +m[1] / +m[2] : 1 / 8;
}
function valeurDuree(s) {  // « 4 », « 3/2 », « / », « //» -> multiple de L
  if (!s) return 1;
  const m = /^(\d*)(\/*)(\d*)$/.exec(s);
  const num = m[1] ? +m[1] : 1;
  if (!m[2]) return num;
  const den = m[3] ? +m[3] : Math.pow(2, m[2].length);
  return num / den;
}
function ecrireDuree(v) {  // multiple de L -> « », « 4 », « 3/2 », « /2 »
  if (Math.abs(v - 1) < 1e-9) return "";
  for (const den of [1, 2, 4, 8, 16, 3, 6, 12]) {
    const num = Math.round(v * den);
    if (Math.abs(num / den - v) < 1e-9) return den === 1 ? String(num) : (num === 1 ? "" : num) + "/" + den;
  }
  return String(Math.round(v));
}

// hauteur diatonique : lettre + octave (C = do du milieu = 0, c = 7) ; garde l'écriture ABC
function position(n) {
  const L = n.lettre.toUpperCase(), base = "CDEFGAB".indexOf(L);
  let p = base + (n.lettre === n.lettre.toLowerCase() ? 7 : 0);
  for (const c of n.oct) p += c === "'" ? 7 : -7;
  return p;
}
function depuisPosition(n, p) {
  const oct = Math.floor(p / 7), deg = ((p % 7) + 7) % 7;
  const L = "CDEFGAB"[deg];
  n.lettre = oct >= 1 ? L.toLowerCase() : L;
  n.oct = oct > 1 ? "'".repeat(oct - 1) : oct < 0 ? ",".repeat(-oct) : "";
}

function remplacer(debut, fin, texte) {
  edition.annuler.push(etat.abc);
  if (edition.annuler.length > 200) edition.annuler.shift();
  edition.refaire = [];
  etat.abc = etat.abc.slice(0, debut) + texte + etat.abc.slice(fin);
  const delta = texte.length - (fin - debut);
  return delta;
}

function noteChoisie() {
  if (!edition.choix) return null;
  const t = etat.abc.slice(edition.choix.debut, edition.choix.fin);
  const n = decouperNote(t);
  return n ? { n, t } : null;
}

// Applique fn(n) à la note choisie ; la sélection suit la nouvelle longueur
function modifierNote(fn) {
  const c = noteChoisie();
  if (!c) return;
  if (fn(c.n) === false) return;
  const nouveau = recoller(c.n);
  const delta = remplacer(edition.choix.debut, edition.choix.fin, nouveau);
  edition.choix.fin += delta;
  apresEdition();
}

const actions = {
  monter: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; depuisPosition(n, position(n) + 1); }),
  descendre: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; depuisPosition(n, position(n) - 1); }),
  octave_haut: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; depuisPosition(n, position(n) + 7); }),
  octave_bas: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; depuisPosition(n, position(n) - 7); }),
  diese: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; n.acc = n.acc === "^" ? "" : "^"; }),
  bemol: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; n.acc = n.acc === "_" ? "" : "_"; }),
  becarre: () => modifierNote((n) => { if (/[zZx]/.test(n.lettre)) return false; n.acc = n.acc === "=" ? "" : "="; }),
  pointee: () => modifierNote((n) => {
    const v = valeurDuree(n.duree), L = uniteL(etat.abc);
    const base = v * L, pointee = [1, 0.5, 0.25, 0.125, 0.0625].some((b) => Math.abs(base - b * 1.5) < 1e-9);
    n.duree = ecrireDuree(pointee ? v / 1.5 : v * 1.5);
  }),
  silence: () => modifierNote((n) => { n.acc = ""; n.oct = ""; n.lettre = "z"; n.lie = ""; n.deco = ""; }),
  dupliquer: () => {
    const c = noteChoisie();
    if (!c) return;
    const copie = recoller(Object.assign({}, c.n, { accord: "", fin: c.n.fin || " " }));
    const fin = edition.choix.fin;
    remplacer(fin, fin, (/\s$/.test(c.t) ? "" : " ") + copie);
    edition.choix = { debut: fin + (/\s$/.test(c.t) ? 0 : 1), fin: fin + (/\s$/.test(c.t) ? 0 : 1) + copie.length };
    apresEdition();
  },
  supprimer: () => {
    if (!edition.choix) return;
    remplacer(edition.choix.debut, edition.choix.fin, "");
    edition.choix = null;
    apresEdition();
  },
  accord: () => {
    const c = noteChoisie();
    if (!c) return;
    const actuel = (/"([^"]*)"/.exec(c.n.accord) || [])[1] || "";
    const nouveau = window.prompt(tx("chord_prompt", "Accord (ex. : Dm, G7, Cmaj7, F#m, Bb/D) ; vide pour l'enlever :"), actuel);
    if (nouveau === null) return;
    modifierNote((n) => { n.accord = nouveau.trim() ? `"${nouveau.trim()}"` : ""; });
  },
};
function duree(valeur) {  // valeur en fraction de ronde
  modifierNote((n) => { n.duree = ecrireDuree(valeur / uniteL(etat.abc)); });
}

function annuler() {
  if (!edition.annuler.length) return;
  edition.refaire.push(etat.abc);
  etat.abc = edition.annuler.pop();
  edition.choix = null;
  apresEdition();
}
function refaire() {
  if (!edition.refaire.length) return;
  edition.annuler.push(etat.abc);
  etat.abc = edition.refaire.pop();
  edition.choix = null;
  apresEdition();
}

// Durée de chaque mesure, voix par voix : mesures trop courtes ou trop longues (hors première et dernière, souvent incomplètes)
function mesuresFausses(abc) {
  const tune = ABCJS.parseOnly(abc)[0];
  if (!tune) return [];
  const metre = tune.getMeterFraction ? tune.getMeterFraction() : { num: 4, den: 4 };
  const attendu = metre.num / metre.den, fautes = [];
  for (const v of separerVoix(abc)) {
    const { els } = elementsDe(v.abc);
    let t = 0, num = 1, tuplet = null;
    const mesures = [];
    for (const e of els) {
      if (e.el_type === "bar") { mesures.push([num, t]); num++; t = 0; continue; }
      if (e.el_type !== "note") continue;
      if (e.rest && e.rest.type === "multimeasure") { const k = parseInt(e.rest.text, 10) || 1; for (let i = 0; i < k; i++) { mesures.push([num, attendu]); num++; } continue; }
      if (e.startTriplet) tuplet = e.tripletMultiplier || 2 / 3;
      t += e.duration * (tuplet || 1);
      if (e.endTriplet) tuplet = null;
    }
    if (t > 0) mesures.push([num, t]);
    mesures.forEach(([n, d], i) => {
      if (i === 0 || i === mesures.length - 1 || d === 0) return;
      if (Math.abs(d - attendu) > 1e-6) fautes.push(`${v.nom} ${n} (${+(d / (1 / metre.den)).toFixed(2)}/${metre.num})`);
    });
  }
  return fautes;
}

// Paroles sous la mélodie chantée : lignes « w: » après chaque ligne de la voix Vocal (ou de la première voix)
function placerParoles(abc, texte) {
  const mots = texte.replace(/\[[^\]]*\]/g, " ").replace(/[\r\n]+/g, " ").split(/\s+/).filter((m) => m);
  const syllabes = [];
  for (const m of mots) {
    const parts = m.split("-").filter((p) => p);
    parts.forEach((p, i) => syllabes.push(p + (i < parts.length - 1 ? "-" : "")));
  }
  const lignes = abc.replace(/\r/g, "").split("\n").filter((l) => !/^w:/.test(l));
  const premiere = (/^V:\s*(\S+)/m.exec(abc) || [])[1];
  const chant = /^V:\s*Vocal\b/m.test(abc) ? "Vocal" : premiere;
  const out = [];
  let voix = null, corps = false, k = 0;
  for (const l of lignes) {
    out.push(l);
    if (/^K:/.test(l)) { corps = true; continue; }
    const m = /^V:\s*(\S+)/.exec(l);
    if (m) { voix = m[1]; continue; }
    const mi = /^\[V:\s*([^\]\s]+)[^\]]*\]/.exec(l);  // voix en début de ligne : « [V: Vocal] … »
    if (mi) voix = mi[1];
    if (!corps || /^[A-Za-z]:|^%/.test(l) || (chant && voix && voix !== chant)) continue;
    // notes qui reçoivent une syllabe : ni silence, ni note liée à la précédente
    const sansAccords = l.replace(/^\[V:[^\]]*\]/, "").replace(/"[^"]*"/g, "").replace(/![^!]*!/g, "");
    const notes = sansAccords.match(/(\^\^|\^|__|_|=)?[A-Ga-gzZx][,']*\d*(\/+\d*)?-?/g) || [];
    let n = 0, lie = false;
    for (const t of notes) {
      const silence = /[zZx]/.test(t.replace(/[\^_=]/g, "")[0]);
      if (!silence && !lie) n++;
      lie = /-$/.test(t);
    }
    if (n && k < syllabes.length) {
      const part = syllabes.slice(k, k + n);
      k += part.length;
      out.push("w:" + part.join(" ").replace(/- /g, "-"));
    }
  }
  return { abc: out.join("\n"), places: k, total: syllabes.length };
}

function apresEdition() {
  majBoutons();
  dessiner(false);
  if (edition.choix) surligner();
  const fautes = mesuresFausses(etat.abc);
  el("mesures").textContent = fautes.length ? tx("bad_measures", "Mesures de durée incorrecte :") + " " + fautes.slice(0, 12).join(", ") + (fautes.length > 12 ? "…" : "") : "";
  el("annuler").disabled = !edition.annuler.length;
  el("refaire").disabled = !edition.refaire.length;
  if (el("texte_abc") && document.activeElement !== el("texte_abc")) el("texte_abc").value = etat.abc;
}

// Surlignage de la note choisie après un redessin
function surligner() {
  if (!etat.visuel || !edition.choix) return;
  try { etat.visuel.engraver.rangeHighlight(versAffiche(edition.choix.debut), versAffiche(edition.choix.fin)); } catch (e) { /* abcjs : pas de surlignage */ }
  const c = noteChoisie();
  el("note_choisie").textContent = c ? c.t.trim() : "";
}

function clicNote(abcElem) {
  if (!abcElem || abcElem.el_type !== "note" || abcElem.startChar === undefined) { edition.choix = null; el("note_choisie").textContent = ""; return; }
  edition.choix = { debut: versOriginal(abcElem.startChar), fin: versOriginal(abcElem.endChar) };
  const c = noteChoisie();
  el("note_choisie").textContent = c ? c.t.trim() : tx("not_editable", "(non modifiable ici : utilisez le texte ABC)");
}

// note suivante / précédente dans le texte (même voix ou non : ordre du texte)
function voisine(sens) {
  if (!etat.visuel) return;
  const notes = [];
  for (const l of etat.visuel.lines) if (l.staff) for (const st of l.staff) for (const v of st.voices) for (const e of v) if (e.el_type === "note") notes.push(e);
  notes.sort((a, b) => a.startChar - b.startChar);
  if (!notes.length) return;
  const orig = (e) => versOriginal(e.startChar);
  let i = edition.choix ? notes.findIndex((e) => orig(e) >= edition.choix.debut) : -1;
  i = sens > 0 ? (i < 0 ? 0 : Math.min(notes.length - 1, edition.choix && orig(notes[i]) === edition.choix.debut ? i + 1 : i)) : Math.max(0, (i < 0 ? notes.length : i) - 1);
  clicNote(notes[i]);
  surligner();
}

function clavier(ev) {
  if (ev.target && /^(TEXTAREA|INPUT)$/.test(ev.target.tagName)) return;
  const k = ev.key;
  if ((ev.ctrlKey || ev.metaKey) && k.toLowerCase() === "z") { ev.preventDefault(); annuler(); return; }
  if ((ev.ctrlKey || ev.metaKey) && k.toLowerCase() === "y") { ev.preventDefault(); refaire(); return; }
  if (!edition.choix && !/^Arrow(Left|Right)$/.test(k)) return;
  const table = {
    ArrowUp: () => (ev.shiftKey ? actions.octave_haut() : actions.monter()),
    ArrowDown: () => (ev.shiftKey ? actions.octave_bas() : actions.descendre()),
    ArrowLeft: () => voisine(-1), ArrowRight: () => voisine(1),
    Delete: actions.silence, Backspace: actions.silence, ".": actions.pointee,
    "1": () => duree(1), "2": () => duree(0.5), "3": () => duree(0.25), "4": () => duree(0.125), "5": () => duree(0.0625),
    "#": actions.diese, b: actions.bemol, n: actions.becarre, c: actions.accord, d: actions.dupliquer,
  };
  if (table[k]) { ev.preventDefault(); table[k](); }
}
