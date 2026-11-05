// Studio Audio AI 1.0.92 : formats de l'Atelier partition.
//   abcVersMusicXML(abc, titre, auteur) : MusicXML 3.1 (partwise) écrit d'après l'analyse d'abcjs (ABCJS.parseOnly) :
//     une partie par voix (V:), mesures, notes (hauteur, altérations de l'armure et de la mesure), durées (points, triolets),
//     silences (y compris sur plusieurs mesures), liaisons, symboles d'accords (<harmony>), clé, armure, mesure, tempo.
//   abcVersMidi(abc) : fichier MIDI (abcjs), en base 64.
//   notesDe(abc) : suite (voix, hauteur MIDI, durée) servant aux contrôles aller-retour.
"use strict";

const ATELIER_DIVISIONS = 960;  // divisions par noire (tient les triolets de triples croches)

function echapperXml(s) {
  return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

// Accord (« Dm », « B♭maj7 », « G7/B », « Fsus4 ») -> <harmony>
const ATELIER_TYPES_ACCORD = [  // du plus long au plus court
  ["m7b5", "half-diminished"], ["ø", "half-diminished"], ["dim7", "diminished-seventh"], ["°7", "diminished-seventh"],
  ["maj9", "major-ninth"], ["maj7", "major-seventh"], ["M7", "major-seventh"], ["Δ", "major-seventh"],
  ["mMaj7", "major-minor"], ["m9", "minor-ninth"], ["m7", "minor-seventh"], ["m6", "minor-sixth"], ["min", "minor"],
  ["sus2", "suspended-second"], ["sus4", "suspended-fourth"], ["sus", "suspended-fourth"],
  ["dim", "diminished"], ["°", "diminished"], ["aug", "augmented"], ["+", "augmented"],
  ["13", "dominant-13th"], ["11", "dominant-11th"], ["9", "dominant-ninth"], ["7", "dominant"], ["6", "major-sixth"],
  ["5", "power"], ["m", "minor"], ["", "major"],
];
function harmonieXml(nom) {
  const n = String(nom).replace(/♭/g, "b").replace(/♯/g, "#").trim();
  const m = /^([A-G])([#b]?)(.*)$/.exec(n);
  if (!m) return "";
  let reste = m[3], basse = "";
  const slash = reste.indexOf("/");
  if (slash >= 0) { basse = reste.slice(slash + 1); reste = reste.slice(0, slash); }
  let kind = "other", txt = reste, degres = [];
  // « 7sus4 », « 9sus4 » : accord suspendu + septième / neuvième ajoutée ; « add9 », « madd9 » : accord + note ajoutée
  const sus = /^(\d+)sus([24])$/.exec(reste), add = /^(.*?)add(\d+)$/.exec(reste);
  if (sus) { kind = sus[2] === "2" ? "suspended-second" : "suspended-fourth"; degres.push(sus[1]); }
  else {
    const base = add ? add[1] : reste;
    for (const [suffixe, k] of ATELIER_TYPES_ACCORD) if (base === suffixe) { kind = k; break; }
    if (add && kind !== "other") degres.push(add[2]);
  }
  const alt = (a) => (a === "#" ? "<root-alter>1</root-alter>" : a === "b" ? "<root-alter>-1</root-alter>" : "");
  let x = `<harmony><root><root-step>${m[1]}</root-step>${alt(m[2])}</root><kind text="${echapperXml(txt)}">${kind}</kind>`;
  const b = /^([A-G])([#b]?)$/.exec(basse);
  if (b) x += `<bass><bass-step>${b[1]}</bass-step>${b[2] === "#" ? "<bass-alter>1</bass-alter>" : b[2] === "b" ? "<bass-alter>-1</bass-alter>" : ""}</bass>`;
  for (const dg of degres) x += `<degree><degree-value>${dg}</degree-value><degree-alter>0</degree-alter><degree-type>add</degree-type></degree>`;
  return x + "</harmony>";
}

// Durée (fraction de ronde) -> type MusicXML et points
function typeNote(d) {
  const bases = [[1, "whole"], [0.5, "half"], [0.25, "quarter"], [0.125, "eighth"], [0.0625, "16th"], [0.03125, "32nd"], [0.015625, "64th"], [2, "breve"]];
  for (const [b, t] of bases) {
    if (Math.abs(d - b) < 1e-9) return [t, 0];
    if (Math.abs(d - b * 1.5) < 1e-9) return [t, 1];
    if (Math.abs(d - b * 1.75) < 1e-9) return [t, 2];
  }
  return [null, 0];
}

const ATELIER_QUINTES = { C: 0, G: 1, D: 2, A: 3, E: 4, B: 5, F: -1 };
function armure(cle) {  // { fifths, mode, alter: { lettre: ±1 } }
  const alter = {};
  let fifths = 0;
  for (const a of (cle && cle.accidentals) || []) {
    const l = String(a.note).toUpperCase();
    alter[l] = a.acc === "sharp" ? 1 : a.acc === "flat" ? -1 : 0;
    fifths += a.acc === "sharp" ? 1 : a.acc === "flat" ? -1 : 0;
  }
  const mode = cle && /^m/.test(cle.mode || "") && !/^mix/i.test(cle.mode || "") ? "minor" : "major";
  return { fifths, mode, alter };
}
function alterationDe(acc) {
  return { sharp: 1, flat: -1, natural: 0, dblsharp: 2, dblflat: -2 }[acc];
}

// Sépare une partition en voix (« V: ») : en-tête jusqu'à « K: », puis le corps de chaque voix. Une ligne de partition qui
// ne contient qu'une voix ne décale ainsi jamais les autres (abcjs numérote les portées par ligne).
function separerVoix(abc) {
  const lignes = abc.replace(/\r/g, "").split("\n");
  const entete = [], defs = {}, corps = {}, ordre = [];
  let i = 0, courante = null;
  const ajouter = (id) => { if (!(id in corps)) { corps[id] = []; ordre.push(id); } };
  for (; i < lignes.length; i++) {
    const l = lignes[i];
    if (/^V:/.test(l)) { const id = l.slice(2).trim().split(/\s+/)[0]; defs[id] = l; ajouter(id); continue; }
    entete.push(l);
    if (/^K:/.test(l)) { i++; break; }
  }
  for (; i < lignes.length; i++) {
    const l = lignes[i];
    const m = /^V:\s*(\S+)/.exec(l) || /^\[V:\s*([^\]\s]+)[^\]]*\]\s*$/.exec(l);
    if (m) { courante = m[1]; ajouter(courante); if (!defs[courante] && /^V:/.test(l)) defs[courante] = l; continue; }
    if (courante === null) { courante = ordre[0] || "1"; ajouter(courante); }
    corps[courante].push(l);
  }
  return ordre.map((id) => {
    const def = defs[id] || "V:" + id;
    const nom = (/(?:name|nm)="([^"]*)"/.exec(def) || [])[1] || id;
    const sansK = entete.filter((l) => !/^K:/.test(l));
    const k = entete.filter((l) => /^K:/.test(l));
    return { id, nom, abc: sansK.join("\n") + "\n" + def + "\n" + k.join("\n") + "\n" + corps[id].join("\n") + "\n" };
  }).filter((v) => v.abc.split("\n").some((l) => /[A-Ga-gzZ]/.test(l) && !/^[A-Za-z]:/.test(l) && !/^%/.test(l)));
}

function elementsDe(abcVoix) {  // éléments de l'unique voix d'une partition d'une voix
  const tune = ABCJS.parseOnly(abcVoix)[0];
  const els = [];
  let clef = null, cle = null;
  for (const ligne of (tune && tune.lines) || []) {
    if (!ligne.staff || !ligne.staff[0]) continue;
    if (!clef) { clef = ligne.staff[0].clef; cle = ligne.staff[0].key; }
    els.push(...ligne.staff[0].voices[0]);
  }
  return { tune, clef, cle, els };
}

function abcVersMusicXML(abc, titre, auteur) {
  const tune = ABCJS.parseOnly(abc)[0];
  if (!tune) throw new Error("partition illisible");
  const voix = separerVoix(abc).map((v) => { const e = elementsDe(v.abc); return { nom: v.nom, clef: e.clef, cle: e.cle, elements: e.els }; });
  const metre = tune.getMeterFraction ? tune.getMeterFraction() : { num: 4, den: 4 };
  const dureeMesure = metre.num / metre.den;  // en rondes
  const bpm = tune.metaText && tune.metaText.tempo && tune.metaText.tempo.bpm;
  const div = ATELIER_DIVISIONS;
  const parts = [];
  voix.forEach((v, iv) => {
    const id = "P" + (iv + 1);
    const ar = armure(v.cle);
    const cleXml = v.clef && v.clef.type && v.clef.type.indexOf("bass") === 0 ? "<sign>F</sign><line>4</line>" : "<sign>G</sign><line>2</line>";
    const mesures = [];
    let notes = "", temps = 0, alterMesure = {}, harm = "", tuplet = null;
    const fermer = (complet) => {
      mesures.push(notes);
      notes = ""; temps = 0; alterMesure = {};
    };
    for (const e of v.elements) {
      if (e.el_type === "bar") { if (temps > 0 || notes) fermer(); continue; }
      if (e.el_type !== "note") continue;
      if (e.chord) for (const c of e.chord) if (c.name) harm += harmonieXml(c.name);
      // silence sur plusieurs mesures : autant de mesures entières
      if (e.rest && e.rest.type === "multimeasure") {
        const n = parseInt(e.rest.text, 10) || 1;
        for (let k = 0; k < n; k++) {
          notes += harm + `<note><rest measure="yes"/><duration>${Math.round(dureeMesure * 4 * div)}</duration><voice>1</voice></note>`;
          harm = "";
          fermer();
        }
        continue;
      }
      if (e.startTriplet) tuplet = { n: e.startTriplet, m: e.tripletMultiplier || (2 / 3) };
      const mult = tuplet ? (e.tripletMultiplier || tuplet.m) : 1;
      const d = e.duration;  // durée écrite (fraction de ronde)
      const reel = d * mult;
      const duree = Math.round(reel * 4 * div);
      const [type, points] = typeNote(d);
      const fin = (type ? `<type>${type}</type>` : "") + "<dot/>".repeat(points);
      const tm = tuplet ? `<time-modification><actual-notes>${tuplet.n}</actual-notes><normal-notes>${Math.round(tuplet.n * mult)}</normal-notes></time-modification>` : "";
      if (e.rest || !e.pitches) {
        notes += harm + `<note><rest/><duration>${duree}</duration><voice>1</voice>${fin}${tm}</note>`;
      } else {
        e.pitches.forEach((p, ip) => {
          const pos = p.pitch, lettre = "CDEFGAB"[((pos % 7) + 7) % 7], octave = 4 + Math.floor(pos / 7);
          const cleNote = lettre + octave;
          let alt = alterationDe(p.accidental);
          if (alt !== undefined) alterMesure[cleNote] = alt;  // l'altération vaut jusqu'à la barre
          else alt = cleNote in alterMesure ? alterMesure[cleNote] : (ar.alter[lettre] || 0);
          const lie = (p.startTie || e.startTie ? '<tie type="start"/>' : "") + (p.endTie || e.endTie ? '<tie type="stop"/>' : "");
          const lieN = (p.startTie || e.startTie ? '<tied type="start"/>' : "") + (p.endTie || e.endTie ? '<tied type="stop"/>' : "");
          notes += (ip === 0 ? harm : "") + "<note>" + (ip > 0 ? "<chord/>" : "") +
            `<pitch><step>${lettre}</step>${alt ? `<alter>${alt}</alter>` : ""}<octave>${octave}</octave></pitch>` +
            `<duration>${duree}</duration>${lie}<voice>1</voice>${fin}${tm}` +
            (p.accidental ? `<accidental>${{ sharp: "sharp", flat: "flat", natural: "natural", dblsharp: "double-sharp", dblflat: "flat-flat" }[p.accidental] || "natural"}</accidental>` : "") +
            (lieN ? `<notations>${lieN}</notations>` : "") + "</note>";
        });
      }
      harm = "";
      temps += reel;
      if (e.endTriplet) tuplet = null;
    }
    if (notes) fermer();
    let xml = `<part id="${id}">`;
    mesures.forEach((contenu, im) => {
      xml += `<measure number="${im + 1}">`;
      if (im === 0) {
        xml += `<attributes><divisions>${div}</divisions><key><fifths>${ar.fifths}</fifths><mode>${ar.mode}</mode></key>` +
          `<time><beats>${metre.num}</beats><beat-type>${metre.den}</beat-type></time><clef>${cleXml}</clef></attributes>`;
        if (bpm && iv === 0) xml += `<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>${bpm}</per-minute></metronome></direction-type><sound tempo="${bpm}"/></direction>`;
      }
      xml += contenu + "</measure>";
    });
    parts.push({ id, nom: v.nom, xml: xml + "</part>" });
  });
  return '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">\n' +
    '<score-partwise version="3.1">' +
    (titre ? `<work><work-title>${echapperXml(titre)}</work-title></work>` : "") +
    `<identification>${auteur ? `<creator type="composer">${echapperXml(auteur)}</creator>` : ""}<encoding><software>Studio Audio AI</software></encoding></identification>` +
    "<part-list>" + parts.map((p) => `<score-part id="${p.id}"><part-name>${echapperXml(p.nom)}</part-name></score-part>`).join("") + "</part-list>" +
    parts.map((p) => p.xml).join("") + "</score-partwise>\n";
}

function versBase64(octets) {
  let s = "";
  for (let i = 0; i < octets.length; i += 0x8000) s += String.fromCharCode.apply(null, octets.subarray(i, i + 0x8000));
  return btoa(s);
}

function abcVersMidi(abc) {
  const fichiers = ABCJS.synth.getMidiFile(abc, { midiOutputType: "binary", chordsOff: true });
  const octets = fichiers && fichiers[0];
  return octets ? versBase64(octets instanceof Uint8Array ? octets : new Uint8Array(octets)) : "";
}

// Suite des notes (voix, hauteur MIDI ou -1 pour un silence, durée en rondes), pour comparer deux partitions
function notesDe(abc) {
  const tune = ABCJS.parseOnly(abc)[0];
  const out = [];
  if (!tune) return out;
  const voix = separerVoix(abc).map((v) => { const e = elementsDe(v.abc); return { cle: e.cle, el: e.els }; });
  const metre = tune.getMeterFraction ? tune.getMeterFraction() : { num: 4, den: 4 };
  voix.forEach((v, iv) => {
    const ar = armure(v.cle);
    let alterMesure = {}, tuplet = null;
    for (const e of v.el) {
      if (e.el_type === "bar") { alterMesure = {}; continue; }
      if (e.el_type !== "note") continue;
      if (e.rest && e.rest.type === "multimeasure") { for (let k = 0; k < (parseInt(e.rest.text, 10) || 1); k++) out.push([iv, -1, metre.num / metre.den]); continue; }
      if (e.startTriplet) tuplet = e.tripletMultiplier || 2 / 3;
      const d = +(e.duration * (tuplet || 1)).toFixed(6);
      if (e.rest || !e.pitches) out.push([iv, -1, d]);
      else {
        const p = e.pitches[0], lettre = "CDEFGAB"[((p.pitch % 7) + 7) % 7], octave = 4 + Math.floor(p.pitch / 7), cle = lettre + octave;
        let alt = alterationDe(p.accidental);
        if (alt !== undefined) alterMesure[cle] = alt; else alt = cle in alterMesure ? alterMesure[cle] : (ar.alter[lettre] || 0);
        out.push([iv, 12 * (octave + 1) + { C: 0, D: 2, E: 4, F: 5, G: 7, A: 9, B: 11 }[lettre] + alt, d]);
      }
      if (e.endTriplet) tuplet = null;
    }
  });
  return out;
}
