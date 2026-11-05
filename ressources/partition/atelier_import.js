// Studio Audio AI 1.0.93 : import dans l'Atelier partition.
//   importerFichier(file) -> Promise<{ abc, nom, remarques }> : ABC (.abc), MusicXML (.musicxml, .xml, .mxl compressé) par
//   xml2abc-js (W. Vree, LGPL), MIDI (.mid) par @tonejs/midi (MIT) + quantification à la double croche.
//   versYuE2(abc, titre) : partition au format attendu par le moteur YuE2 : voix « Vocal » (chant) et « Ins » (instrument),
//   en-tête X/T/M/L/Q/K, blocs de 4 mesures qui alternent les deux voix (comme les partitions écrites par YuE2).
"use strict";

// ---- .mxl : archive zip (MusicXML compressé), lue avec DecompressionStream (aucune bibliothèque de plus)
async function lireZip(buffer) {
  const v = new DataView(buffer), u8 = new Uint8Array(buffer), fichiers = {};
  let fin = -1;
  for (let i = u8.length - 22; i >= 0; i--) if (v.getUint32(i, true) === 0x06054b50) { fin = i; break; }
  if (fin < 0) throw new Error("archive .mxl illisible");
  const n = v.getUint16(fin + 10, true);
  let p = v.getUint32(fin + 16, true);
  for (let k = 0; k < n; k++) {
    const methode = v.getUint16(p + 10, true), taille = v.getUint32(p + 20, true);
    const ln = v.getUint16(p + 28, true), le = v.getUint16(p + 30, true), lc = v.getUint16(p + 32, true), local = v.getUint32(p + 42, true);
    const nom = new TextDecoder().decode(u8.subarray(p + 46, p + 46 + ln));
    const debut = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true);
    const brut = u8.subarray(debut, debut + taille);
    fichiers[nom] = methode === 0 ? brut : methode === 8 ? new Uint8Array(await new Response(new Blob([brut]).stream().pipeThrough(new DecompressionStream("deflate-raw"))).arrayBuffer()) : null;
    p += 46 + ln + le + lc;
  }
  return fichiers;
}
async function texteMxl(buffer) {
  const f = await lireZip(buffer);
  const dec = (b) => new TextDecoder().decode(b);
  let racine = null;
  if (f["META-INF/container.xml"]) racine = (/full-path="([^"]+)"/.exec(dec(f["META-INF/container.xml"])) || [])[1];
  if (!racine || !f[racine]) racine = Object.keys(f).find((k) => !k.startsWith("META-INF") && /\.(xml|musicxml)$/i.test(k));
  if (!racine) throw new Error("aucune partition dans l'archive .mxl");
  return dec(f[racine]);
}

function musicXmlVersAbc(texte) {
  const doc = $.parseXML(texte.replace(/^\uFEFF/, ""));
  const r = vertaal(doc, { u: 0, b: 4 });  // 4 mesures par ligne
  return r[0];
}

// ---- MIDI -> ABC
const NOMS_DIESE = ["C", "^C", "D", "^D", "E", "F", "^F", "G", "^G", "A", "^A", "B"];
const NOMS_BEMOL = ["C", "_D", "D", "_E", "E", "F", "_G", "G", "_A", "A", "_B", "B"];
const PROFIL_MAJ = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];  // Krumhansl-Kessler
const PROFIL_MIN = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
const TONIQUES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];

function estimerTonalite(poids) {  // poids[12] -> { nom: "Em", tonique, mineur }
  let meilleur = { s: -Infinity };
  for (let t = 0; t < 12; t++) for (const mineur of [false, true]) {
    const prof = mineur ? PROFIL_MIN : PROFIL_MAJ;
    const x = poids, y = Array.from({ length: 12 }, (_, i) => prof[(i - t + 12) % 12]);
    const mx = x.reduce((a, b) => a + b, 0) / 12, my = y.reduce((a, b) => a + b, 0) / 12;
    let num = 0, dx = 0, dy = 0;
    for (let i = 0; i < 12; i++) { num += (x[i] - mx) * (y[i] - my); dx += (x[i] - mx) ** 2; dy += (y[i] - my) ** 2; }
    const s = num / Math.sqrt((dx || 1) * (dy || 1));
    if (s > meilleur.s) meilleur = { s, tonique: t, mineur };
  }
  const relatifMaj = (meilleur.tonique + (meilleur.mineur ? 3 : 0)) % 12;
  const bemols = [5, 10, 3, 8, 1, 6].includes(relatifMaj);  // F, Bb, Eb, Ab, Db, Gb majeurs
  let nom = TONIQUES[meilleur.tonique];
  if (!bemols) nom = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][meilleur.tonique];
  return { nom: nom + (meilleur.mineur ? "m" : ""), tonique: meilleur.tonique, mineur: meilleur.mineur, bemols };
}

// Accord de la mesure : triades de la tonalité, les degrés principaux (I, IV, V) favorisés ; « » si rien de net.
// shortcut: estimation d'après les notes de la mesure, sans analyse harmonique fine ; à corriger à la main si besoin.
function accordDeMesure(poids, ton) {
  let meilleur = { s: 0, nom: "" };
  const total = poids.reduce((a, b) => a + b, 0);
  if (total <= 0) return "";
  const t = ton.tonique;
  const degres = ton.mineur  // [écart depuis la tonique, mineur ?, principal ?]
    ? [[0, true, true], [5, true, true], [7, false, true], [8, false, false], [3, false, false], [10, false, false], [7, true, false]]
    : [[0, false, true], [5, false, true], [7, false, true], [9, true, false], [2, true, false], [4, true, false]];
  const noms = ton.bemols ? ["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"] : ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  for (const [ecart, mineur, principal] of degres) {
    const r = (t + ecart) % 12;
    const s = poids[r] * 1.2 + poids[(r + (mineur ? 3 : 4)) % 12] + poids[(r + 7) % 12] * 0.9 - poids[(r + (mineur ? 4 : 3)) % 12] * 0.6 + (principal ? total * 0.3 : 0);
    if (s > meilleur.s) meilleur = { s, nom: noms[r] + (mineur ? "m" : "") };
  }
  return meilleur.s > total * 0.45 ? meilleur.nom : "";
}

function noteAbc(midi, bemols, armureAlt, alterMesure) {
  // nom « absolu » (avec altération explicite) puis simplification selon l'armure et les altérations déjà écrites dans la mesure
  const pc = midi % 12, oct = Math.floor(midi / 12) - 1;
  const brut = (bemols ? NOMS_BEMOL : NOMS_DIESE)[pc];
  const lettre = brut.replace(/[\^_]/g, ""), alt = brut.startsWith("^") ? 1 : brut.startsWith("_") ? -1 : 0;
  const cle = lettre + oct;
  const courant = cle in alterMesure ? alterMesure[cle] : (armureAlt[lettre] || 0);
  let acc = "";
  if (alt !== courant) { acc = alt === 1 ? "^" : alt === -1 ? "_" : "="; alterMesure[cle] = alt; }
  let s = acc + (oct >= 5 ? lettre.toLowerCase() : lettre);
  for (let o = oct; o > 5; o--) s += "'";
  for (let o = oct; o < 4; o++) s += ",";
  return s;
}

function armureDe(nomTonalite) {  // { lettre: ±1 }
  const majeur = { C: 0, G: 1, D: 2, A: 3, E: 4, B: 5, "F#": 6, "C#": 7, F: -1, Bb: -2, Eb: -3, Ab: -4, Db: -5, Gb: -6 };
  const mineur = { A: 0, E: 1, B: 2, "F#": 3, "C#": 4, "G#": 5, "D#": 6, D: -1, G: -2, C: -3, F: -4, Bb: -5, Eb: -6, "A#": 7 };
  const m = /^([A-G][#b]?)(m?)$/.exec(nomTonalite) || [];
  const n = (m[2] ? mineur : majeur)[m[1]] || 0;
  const a = {};
  for (let i = 0; i < Math.abs(n); i++) a[(n > 0 ? "FCGDAEB" : "BEADGCF")[i]] = n > 0 ? 1 : -1;
  return a;
}

function midiVersAbc(buffer, titre) {
  const midi = new Midi(buffer);
  const ppq = midi.header.ppq || 480;
  const bpm = Math.round((midi.header.tempos[0] && midi.header.tempos[0].bpm) || 120);
  const ts = (midi.header.timeSignatures[0] && midi.header.timeSignatures[0].timeSignature) || [4, 4];
  const pas = ppq / 4;  // double croche
  const unitesMesure = Math.max(1, Math.round((ts[0] * 16) / ts[1]));
  const pistes = midi.tracks.filter((t) => t.notes.length >= 4 && !(t.instrument && t.instrument.percussion) && t.channel !== 9);
  if (!pistes.length) throw new Error("aucune note dans ce fichier MIDI");
  const moyenne = (t) => t.notes.reduce((a, n) => a + n.midi, 0) / t.notes.length;
  // chant : la piste la plus aiguë parmi celles qui ont assez de notes ; instrument : la plus fournie des autres
  const parNotes = pistes.slice().sort((a, b) => b.notes.length - a.notes.length);
  const candidates = parNotes.filter((t) => t.notes.length >= parNotes[0].notes.length * 0.25);
  const chant = candidates.slice().sort((a, b) => moyenne(b) - moyenne(a))[0];
  const inst = parNotes.find((t) => t !== chant) || null;
  // ligne mélodique (une note à la fois : la plus aiguë à chaque attaque), sur la grille des doubles croches
  const ligne = (piste) => {
    const parDebut = new Map();
    for (const n of piste.notes) {
      const d = Math.round(n.ticks / pas), l = Math.max(1, Math.round(n.durationTicks / pas));
      const e = parDebut.get(d);
      if (!e || n.midi > e.midi) parDebut.set(d, { d, l, midi: n.midi });
    }
    const notes = [...parDebut.values()].sort((a, b) => a.d - b.d);
    for (let i = 0; i + 1 < notes.length; i++) notes[i].l = Math.max(1, Math.min(notes[i].l, notes[i + 1].d - notes[i].d));
    return notes;
  };
  const voix = [ligne(chant)].concat(inst ? [ligne(inst)] : []);
  const finUnites = Math.max(...voix.map((v) => (v.length ? v[v.length - 1].d + v[v.length - 1].l : 0)));
  const nbMesures = Math.max(1, Math.ceil(finUnites / unitesMesure));
  // tonalité et accords d'après toutes les notes (pondérées par la durée)
  const poids = new Array(12).fill(0), poidsMesure = Array.from({ length: nbMesures }, () => new Array(12).fill(0));
  for (const t of pistes) for (const n of t.notes) {
    const d = Math.round(n.ticks / pas), l = Math.max(1, Math.round(n.durationTicks / pas));
    poids[n.midi % 12] += l;
    const m = Math.min(nbMesures - 1, Math.floor(d / unitesMesure));
    poidsMesure[m][n.midi % 12] += l;
  }
  const ton = estimerTonalite(poids);
  const arm = armureDe(ton.nom);
  const accords = poidsMesure.map((p) => accordDeMesure(p, ton));
  // écriture : chaque voix mesure par mesure (notes coupées et liées à la barre)
  const mesuresVoix = voix.map((notes, iv) => {
    const mesures = Array.from({ length: nbMesures }, () => []);
    let t = 0, k = 0;
    const evts = [];
    for (const n of notes) { if (n.d > t) evts.push({ d: t, l: n.d - t, midi: -1 }); evts.push(n); t = n.d + n.l; }
    if (t < nbMesures * unitesMesure) evts.push({ d: t, l: nbMesures * unitesMesure - t, midi: -1 });
    for (const e of evts) {
      let d = e.d, reste = e.l;
      while (reste > 0) {
        const m = Math.floor(d / unitesMesure), finMesure = (m + 1) * unitesMesure;
        const part = Math.min(reste, finMesure - d);
        mesures[Math.min(m, nbMesures - 1)].push({ midi: e.midi, l: part, lie: e.midi >= 0 && part < reste });
        d += part; reste -= part;
      }
      k++;
    }
    return mesures.map((evs, im) => {
      const alterMesure = {};
      let s = iv === 0 && accords[im] ? `"${accords[im]}"` : "";
      for (const e of evs) s += (e.midi < 0 ? "z" : noteAbc(e.midi, ton.bemols, arm, alterMesure)) + (e.l === 1 ? "" : e.l) + (e.lie ? "-" : "") + " ";
      return s.trim() || "z" + unitesMesure;
    });
  });
  return {
    abc: assemblerYuE2(titre, `${ts[0]}/${ts[1]}`, "1/16", bpm, ton.nom, mesuresVoix),
    remarques: [`MIDI : ${pistes.length} piste(s) ; chant = « ${chant.name || "piste " + (midi.tracks.indexOf(chant) + 1)} »` +
      (inst ? `, instrument = « ${inst.name || "piste " + (midi.tracks.indexOf(inst) + 1)} »` : "") + ` ; tonalité estimée ${ton.nom} ; ${nbMesures} mesures ; rythmes arrondis à la double croche.`],
  };
}

// Partition au format de YuE2 : 2 voix au plus, blocs de 4 mesures alternés, une section « % verse » si aucune
function assemblerYuE2(titre, metre, unite, bpm, cle, mesuresVoix, sections) {
  const noms = [["Vocal", "Vocal Melody", "Vocal"], ["Ins", "Ins Melody", "Inst."]];
  let s = `X:1\nT:${titre || ""}\nM:${metre}\nL:${unite}\n` + (bpm ? `Q:1/4=${bpm}\n` : "");
  mesuresVoix.slice(0, 2).forEach((_, i) => { s += `V: ${noms[i][0]} clef=treble name="${noms[i][1]}" snm="${noms[i][2]}"\n`; });
  s += `K:${cle}\n`;
  const n = Math.max(...mesuresVoix.map((m) => m.length));
  for (let m0 = 0; m0 < n; m0 += 4) {
    const sec = sections && sections[m0];
    if (m0 === 0 || sec) s += `% ${sec || "verse"}\n`;
    mesuresVoix.slice(0, 2).forEach((mes, i) => {
      const bloc = mes.slice(m0, m0 + 4);
      if (bloc.length) s += `V: ${noms[i][0]}\n` + bloc.join("|") + "|\n";
    });
  }
  return s;
}

// ABC quelconque (fichier .abc, ou issu d'un MusicXML) -> format YuE2 : voix gardées (2 au plus), mesures regroupées par 4
function versYuE2(abc, titre) {
  // déjà au format de YuE2 (voix « Vocal » / « Ins ») : gardée telle quelle, sections comprises
  if (/^V:\s*Vocal\b/m.test(abc) && !/^V:\s*(?!Vocal\b|Ins\b)\S/m.test(abc)) return { abc, remarques: [] };
  const voix = separerVoix(abc).filter((v) => /[A-Ga-g]/.test(v.abc.split("\n").filter((l) => !/^[A-Za-z]:|^%/.test(l)).join("")));
  if (!voix.length) throw new Error("partition sans notes");
  const entete = abc.replace(/\r/g, "").split("\n");
  const champ = (c, defaut) => { const l = entete.find((x) => x.startsWith(c + ":")); return l ? l.slice(2).trim() : defaut; };
  const tune = ABCJS.parseOnly(abc)[0];
  const bpm = (tune && tune.metaText && tune.metaText.tempo && tune.metaText.tempo.bpm) || 0;
  // corps de chaque voix découpé en mesures (barres simples, doubles ou de reprise ; paroles « w: » retirées)
  const mesuresVoix = voix.slice(0, 2).map((v) => {
    const lignes = v.abc.split("\n");
    const iK = lignes.findIndex((l) => /^K:/.test(l));
    const corps = lignes.slice(iK + 1).filter((l) => !/^[A-Za-z]:/.test(l) && !/^%/.test(l) && !/^\[[A-Za-z]:/.test(l.trim()))
      .map((l) => l.replace(/%.*$/, "").replace(/\$\s*$/, "")).join(" ");  // commentaires de fin de ligne, sauts de ligne « $ » de xml2abc
    return corps.split(/:?\|+[\]:]?|\[\|/).map((x) => x.trim()).filter((x) => x.length);
  });
  return {
    abc: assemblerYuE2(titre || champ("T", ""), champ("M", "4/4"), champ("L", "1/8"), bpm, champ("K", "C"), mesuresVoix),
    remarques: voix.length > 2 ? [`${voix.length} voix dans le fichier : les 2 premières sont gardées (chant, instrument).`] : [],
  };
}

async function importerFichier(fichier) {
  const nom = fichier.name.replace(/\.[^.]+$/, "");
  const ext = (/\.([^.]+)$/.exec(fichier.name) || [])[1].toLowerCase();
  const buffer = await fichier.arrayBuffer();
  if (ext === "mid" || ext === "midi") { const r = midiVersAbc(buffer, nom); return { abc: r.abc, nom, remarques: r.remarques }; }
  let abc;
  if (ext === "mxl") abc = musicXmlVersAbc(await texteMxl(buffer));
  else if (ext === "musicxml" || ext === "xml") abc = musicXmlVersAbc(new TextDecoder().decode(buffer));
  else if (ext === "abc" || ext === "txt") abc = new TextDecoder().decode(buffer);
  else throw new Error("format non pris en charge : ." + ext);
  const r = versYuE2(abc, nom);
  return { abc: r.abc, nom, remarques: r.remarques };
}
