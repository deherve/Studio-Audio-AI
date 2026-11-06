#include "suno.hpp"
#include "util.hpp"  // 1.1.001 : trim, lower, read_text, write_text, url_encode

#include "i18n.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>
#include <utility>

// ---------------------------------------------------------------------------- Profils

// 1.0.85 (dépôt public) : plus aucun nom de chanteur ni de musicien réel, seulement le son de l'influence.
// Un profil décrit le son d'une influence en balises comprises par YuE2 : genre, époque,
// instruments signature, voix, ambiance et tempo typique.
const std::vector<Influence> INFLUENCES = {
    { "Alice In Chains", "grunge, alternative metal, 90s, sludgy distorted guitar, dark, haunting male vocal, stacked vocal harmonies, guitar solo, talk box guitar",
      "", "" },
    { "Astonvilla", "French rock, alternative rock, 2000s, electric guitar, melancholic, male vocal, guitar solo",
      "", "" },
    { "Audioslave", "hard rock, alternative rock, 2000s, heavy riffs, guitar solo, powerful male vocal, whammy guitar effects",
      "", "" },
    { "Bauhaus", "gothic rock, post-punk, 80s, deep bass guitar, dark, theatrical baritone vocal",
      "", "" },
    { "Blink-182", "pop punk, 2000s, fast, power chords, energetic, nasal male vocal",
      "", "" },
    { "Bon Jovi", "hard rock, arena rock, 80s, talk box guitar, anthemic, powerful male vocal, guitar solo, keyboards",
      "", "" },
    { "Bush", "post-grunge, 90s, heavy distorted guitar, brooding, raspy male vocal, guitar solo",
      "", "" },
    { "Candlebox", "post-grunge, hard rock, 90s, distorted guitar, emotional, powerful male vocal, guitar solo",
      "", "" },
    { "Clan of Xymox", "dark wave, gothic rock, 80s, synth pads, drum machine, cold, deep male vocal",
      "", "" },
    { "Coldplay", "alternative rock, britpop, 2000s, piano, atmospheric guitar, uplifting, anthemic, falsetto male vocal, synth pads",
      "", "" },
    { "Corpus Delicti", "gothic rock, deathrock, cold wave, 90s, chorus guitar, dark, theatrical male vocal",
      "", "" },
    { "Dolly", "French rock, alternative rock, grunge, 90s, distorted guitar, energetic, female vocal, guitar solo",
      "", "" },
    { "Eiffel", "French rock, alternative rock, indie rock, 2000s, electric guitar, poetic, male vocal, guitar solo, keyboards",
      "", "" },
    { "Evanescence", "gothic metal, alternative metal, 2000s, piano, strings, heavy guitar, dark, dramatic female vocal, choir, guitar solo",
      "", "" },
    { "Foo Fighters", "post-grunge, alternative rock, 2000s, distorted guitar, energetic, raspy male vocal, guitar solo, drums",
      "", "" },
    { "Green Day", "pop punk, punk rock, 2000s, fast, power chords, energetic, male vocal, guitar solo",
      "", "" },
    { "Héroes del Silencio", "Spanish rock, gothic rock, 90s, delay guitar, epic, dramatic male vocal, passionate, guitar solo",
      "", "" },
    { "Hole", "grunge, alternative rock, 90s, raw distorted guitar, aggressive, raspy female vocal, screamed vocal",
      "", "" },
    { "Joy Division", "post-punk, 80s, melodic bass guitar, drum machine, minimal, cold, baritone male vocal, melancholic",
      "", "" },
    { "Limp Bizkit", "nu metal, rap metal, 2000s, heavy riffs, turntables, aggressive, rapped male vocal, seven-string guitar",
      "", "" },
    { "Mademoiselle K", "French rock, indie rock, 2000s, distorted guitar, energetic, female vocal, guitar solo",
      "", "" },
    { "Matmatah", "French rock, folk rock, celtic, 2000s, acoustic guitar, festive, energetic, male vocal, guitar solo, accordion, bagpipes",
      "", "" },
    { "Muse", "alternative rock, progressive rock, 2000s, epic, arpeggiator, distorted bass, falsetto male vocal, guitar solo, piano",
      "", "" },
    { "Nirvana", "grunge, 90s, distorted guitar, loud-quiet dynamics, raw, raspy male vocal, guitar solo, chorus guitar",
      "", "" },
    { "Oasis", "britpop, rock, 90s, wall of sound, distorted guitar, anthemic, nasal male vocal, guitar solo, strings",
      "", "" },
    { "Pantera", "groove metal, heavy metal, 90s, heavy distorted guitar, guitar solo, aggressive, shouted male vocal, pinch harmonics, double bass drum",
      "", "" },
    { "Papa Roach", "nu metal, alternative metal, 2000s, heavy riffs, aggressive, screamed male vocal, guitar solo",
      "", "" },
    { "Placebo", "alternative rock, 2000s, distorted guitar, dark, androgynous male vocal, nasal vocal, bass guitar",
      "", "" },
    { "Rage Against The Machine", "rap metal, funk metal, 90s, heavy riffs, aggressive, rebellious, rapped male vocal, guitar solo, whammy guitar effects",
      "", "" },
    { "Rise Against", "melodic hardcore, punk rock, 2000s, fast, aggressive, powerful male vocal, shouted vocal, guitar solo",
      "", "" },
    { "Silverchair", "grunge, post-grunge, 90s, distorted guitar, raw, raspy male vocal, guitar solo, strings",
      "", "" },
    { "Soul Asylum", "alternative rock, 90s, jangly guitar, ballad, emotional, raspy male vocal, guitar solo, acoustic guitar",
      "", "" },
    { "Soundgarden", "grunge, heavy metal, 90s, heavy distorted guitar, drop tuning, powerful male vocal, guitar solo",
      "", "" },
    { "The Breeders", "alternative rock, indie rock, 90s, fuzzy bass, quirky, female vocal, vocal harmonies",
      "", "" },
    { "The Cranberries", "alternative rock, dream pop, 90s, jangly guitar, chorus guitar, ethereal female vocal, yodeling vocal, melancholic",
      "", "" },
    { "The Cure", "post-punk, new wave, gothic rock, 80s, chorus guitar, synth pads, melancholic, emotional male vocal, keyboards",
      "", "" },
    { "The Jesus and Mary Chain", "noise pop, shoegaze, 80s, fuzz guitar, feedback, wall of sound, deadpan male vocal",
      "", "" },
    { "The Mission", "gothic rock, 80s, twelve-string guitar, chorus guitar, epic, dramatic male vocal, dark, guitar solo",
      "", "" },
    { "The Sisters of Mercy", "gothic rock, post-punk, 80s, drum machine, chorus guitar, deep baritone vocal, dark, cold",
      "", "" },
    { "The Strokes", "garage rock, indie rock, 2000s, interlocking guitars, lo-fi production, distorted male vocal, guitar solo",
      "", "" },
    { "Tryo", "French reggae, acoustic, folk, acoustic guitar, festive, male vocal harmonies, percussion, ukulele",
      "", "" },
    { "Weezer", "power pop, alternative rock, 90s, fuzzy guitar, catchy, male vocal, guitar solo",
      "", "" },
};

std::string singer_tag(const Influence & inf) {
    return *inf.singer ? std::string(inf.singer) + " vocals" : std::string();
}

std::string influence_style(const Influence & inf) {
    std::string style = inf.tags;
    if (*inf.singer) style += ", " + singer_tag(inf);
    if (*inf.musicians) style += ", " + std::string(inf.musicians);
    return style;
}

const std::vector<Language> LANGUAGES = {
    { N_("Langue : non précisée"), "" },
    { N_("Anglais"), "English" },
    { N_("Français"), "French" },
    { N_("Espagnol"), "Spanish" },
    { N_("Italien"), "Italian" },
    { N_("Allemand"), "German" },
    { N_("Portugais"), "Portuguese" },
    { N_("Japonais"), "Japanese" },
    { N_("Coréen"), "Korean" },
    { N_("Mandarin"), "Mandarin" },
    { N_("Cantonais"), "Cantonese" },
};

// ---------------------------------------------------------------------------- Texte

std::string fold(const std::string & s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = s[i];
        if (c < 0x80) {
            out += (char) tolower(c);
        } else if (c == 0xC3 && i + 1 < s.size() && (unsigned char) s[i + 1] >= 0x80 &&
                   (unsigned char) s[i + 1] <= 0x9E && (unsigned char) s[i + 1] != 0x97) {
            out += (char) c;  // majuscule accentuée Latin-1 → minuscule
            out += (char) ((unsigned char) s[i + 1] + 0x20);
            i++;
        } else {
            out += (char) c;
        }
    }
    return out;
}

static bool word_char(unsigned char c) {
    return isalnum(c) || c >= 0x80;
}

// Recherche d'un mot ou d'une expression entière (bornes de mots respectées).
static size_t find_word(const std::string & hay, const std::string & needle, size_t from = 0) {
    for (size_t pos = hay.find(needle, from); pos != std::string::npos; pos = hay.find(needle, pos + 1)) {
        bool left  = pos == 0 || !word_char(hay[pos - 1]) || !word_char(needle.front());
        size_t end = pos + needle.size();
        bool right = end >= hay.size() || !word_char(hay[end]) || !word_char(needle.back());
        if (left && right) {
            return pos;
        }
    }
    return std::string::npos;
}

std::vector<std::string> split_style(const std::string & style) {
    std::vector<std::string> tags;
    std::string              cur;
    for (char c : style + ",") {
        if (c == ',' || c == '\n') {
            std::string t = trim(cur);
            if (!t.empty()) tags.push_back(t);
            cur.clear();
        } else {
            cur += c;
        }
    }
    return tags;
}

std::string join_style(const std::vector<std::string> & tags) {
    std::string out;
    for (auto & t : tags) {
        out += (out.empty() ? "" : ", ") + t;
    }
    return out;
}

static void add_unique(std::vector<std::string> & tags, const std::string & tag) {
    std::string f = fold(trim(tag));
    if (f.empty()) return;
    for (auto & t : tags) {
        if (fold(t) == f) return;
    }
    tags.push_back(trim(tag));
}

static bool is_bpm(const std::string & tag) {
    return tag.size() > 4 && fold(tag).compare(tag.size() - 3, 3, "bpm") == 0;
}

std::string merge_style(const std::string & style, const std::vector<std::string> & extra) {
    std::vector<std::string> tags = split_style(style);
    // Un tempo venant des paroles ([Tempo: 120 BPM]) remplace celui du style.
    if (std::any_of(extra.begin(), extra.end(), is_bpm)) {
        tags.erase(std::remove_if(tags.begin(), tags.end(), is_bpm), tags.end());
    }
    for (auto & e : extra) add_unique(tags, e);
    return join_style(tags);
}

bool needs_conversion(const std::string & lyrics) {
    std::string f = fold(lyrics);
    if (f.find("[(") != std::string::npos || f.find("[style:") != std::string::npos ||
        f.find("[mood:") != std::string::npos || f.find("[tempo:") != std::string::npos ||
        f.find("[end]") != std::string::npos) {
        return true;
    }
    // Métatags empilés ou sections numérotées : [Verse 1 | …], [Chorus 2]
    for (size_t open = f.find('['); open != std::string::npos; open = f.find('[', open + 1)) {
        size_t close = f.find(']', open);
        if (close == std::string::npos) break;
        std::string inner = f.substr(open + 1, close - open - 1);
        if (inner.find('|') != std::string::npos || inner.find(':') != std::string::npos) return true;
        if (!inner.empty() && isdigit((unsigned char) inner.back())) return true;
    }
    return false;
}

static bool is_language_tag(const std::string & tag) {
    static const char * known[] = { "english", "french", "spanish", "italian", "anglais", "français", "espagnol",
                                    "italien", "mandarin", "chinese", "german", "portuguese", "japanese", "korean", "cantonese",
                                    "allemand", "portugais", "japonais", "coréen", "cantonais" };
    std::string f = fold(tag);
    for (const char * k : known) {
        if (f == k) return true;
    }
    return false;
}

std::string set_language(const std::string & style, const char * tag) {
    std::vector<std::string> tags, in = split_style(style);
    if (tag && *tag) tags.push_back(tag);
    for (auto & t : in) {
        if (!is_language_tag(t)) add_unique(tags, t);
    }
    return join_style(tags);
}

static const char * LANGUAGE_CODES[] = { "", "en", "fr", "es", "it", "de", "pt", "ja", "ko", "zh", "yue" };  // même ordre que LANGUAGES

int language_index_for_code(const std::string & code) {
    for (size_t i = 1; i < LANGUAGES.size() && i < sizeof(LANGUAGE_CODES) / sizeof(LANGUAGE_CODES[0]); i++) {
        if (code == LANGUAGE_CODES[i]) return (int) i;
    }
    return 0;
}

std::string detect_lyrics_language(const std::string & style, const std::string & lyrics, bool * declared) {
    if (declared) *declared = false;
    int idx = 0;
    for (auto & t : split_style(style)) {
        std::string f = fold(t);
        for (size_t i = 1; i < LANGUAGES.size(); i++) {
            if (f == fold(LANGUAGES[i].tag) || f == fold(LANGUAGES[i].label)) idx = (int) i;
        }
        if (idx) break;
    }
    if (idx > 0 && idx < (int) (sizeof(LANGUAGE_CODES) / sizeof(LANGUAGE_CODES[0]))) {
        if (declared) *declared = true;
        return LANGUAGE_CODES[idx];
    }
    // Texte chanté : sans les balises de section [..] ni les notes entre parenthèses.
    std::string text;
    int         depth = 0;
    for (char c : lyrics) {
        if (c == '[' || c == '(') depth++;
        else if (c == ']' || c == ')') depth = std::max(0, depth - 1);
        else if (!depth) text += c;
    }
    // Écritures : kana -> japonais, hangul -> coréen, idéogrammes seuls -> chinois (cantonais si ses particules)
    size_t kana = 0, hangul = 0, han = 0, latin = 0, yue = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char) text[i];
        unsigned      cp;
        int           n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c >> 5) == 6) { cp = c & 0x1F; n = 2; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; n = 3; }
        else if ((c >> 3) == 30) { cp = c & 0x07; n = 4; }
        else { i++; continue; }
        for (int k = 1; k < n && i + k < text.size(); k++) cp = (cp << 6) | ((unsigned char) text[i + k] & 0x3F);
        i += n;
        if (cp >= 0x3040 && cp <= 0x30FF) kana++;
        else if ((cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x1100 && cp <= 0x11FF)) hangul++;
        else if (cp >= 0x4E00 && cp <= 0x9FFF) {
            han++;
            if (cp == 0x5605 || cp == 0x5514 || cp == 0x5497 || cp == 0x4FC2 || cp == 0x5586 || cp == 0x5566 || cp == 0x4F62 || cp == 0x5192) yue++;
        } else if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0x24F)) latin++;
    }
    size_t letters = kana + hangul + han + latin;
    if (letters < 8) return "";
    if (kana * 20 >= letters) return "ja";
    if (hangul * 4 >= letters) return "ko";
    if (han * 2 >= letters) return yue >= 2 ? "yue" : "zh";
    // Alphabet latin : mots courants de chaque langue
    static const struct { const char * code; const char * words; } lists[] = {
        { "en", " the and you i to of in it is that my me we your for on with this are not but all be don't i'm just now never love baby " },
        { "fr", " le les des du un une et est je tu il elle nous vous ne pas que qui dans pour sur avec mon ma mes ton ses plus au aux c'est j'ai comme mais tout " },
        { "es", " el los las del una yo tú que no en por con para mi su lo se te me al más como pero está soy eres " },
        { "it", " il gli di del una che non per con mi ti ci si come ma più è io sono sei nel della " },
        { "de", " der die das und ist ich du nicht ein eine mit von zu den dem auf für im auch wie mein dein wir sie nur " },
        { "pt", " os um uma você que não em por com para meu minha seu se mais como mas é eu está ao dos das nós " },
    };
    double score[6] = {};
    size_t words    = 0;
    std::string word, low = fold(text);
    auto        flush = [&] {
        if (word.empty()) return;
        words++;
        std::string key = " " + word + " ";
        for (int l = 0; l < 6; l++) {
            if (std::string(lists[l].words).find(key) != std::string::npos) score[l] += 1;
        }
        word.clear();
    };
    for (char c : low) {
        if (isalpha((unsigned char) c) || (unsigned char) c >= 0x80 || c == '\'') word += c;
        else flush();
    }
    flush();
    // lettres propres à une langue
    if (low.find("ñ") != std::string::npos) score[2] += 3;
    if (low.find("ß") != std::string::npos || low.find("ü") != std::string::npos) score[4] += 3;
    if (low.find("ã") != std::string::npos || low.find("õ") != std::string::npos) score[5] += 3;
    int best = 0;
    for (int l = 1; l < 6; l++) if (score[l] > score[best]) best = l;
    double second = 0;
    for (int l = 0; l < 6; l++) if (l != best) second = std::max(second, score[l]);
    if (words < 6 || score[best] < 3 || score[best] < second * 1.25) return "";
    return lists[best].code;
}

int detect_language(const std::string & style) {
    for (auto & t : split_style(style)) {
        std::string f = fold(t);
        for (size_t i = 1; i < LANGUAGES.size(); i++) {
            if (f == fold(LANGUAGES[i].tag) || f == fold(LANGUAGES[i].label)) return (int) i;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------- Style

// Expressions reconnues (français et anglais) → balise YuE2. Une balise vide absorbe l'expression
// sans rien produire (consignes de structure glissées dans le style).
static const std::vector<std::pair<const char *, const char *>> & dictionary() {
    static const std::vector<std::pair<const char *, const char *>> d = {
        // consignes sans effet sur le style
        { "pont acoustique obligatoire", "" }, { "pont acoustique", "" }, { "highly original arrangement", "" },
        { "avoid standard clichés", "" }, { "dramatic contrasts", "" }, { "contrastes dramatiques", "" },
        { "éviter clichés", "" }, { "arrangement original", "" },
        // voix
        { "voix masculine", "male vocal" }, { "voix d'homme", "male vocal" }, { "male vocals", "male vocal" },
        { "male vocal", "male vocal" }, { "male voice", "male vocal" }, { "voix féminine", "female vocal" },
        { "voix de femme", "female vocal" }, { "female vocals", "female vocal" }, { "female vocal", "female vocal" },
        { "female voice", "female vocal" }, { "chanteuse", "female vocal" }, { "chanteur", "male vocal" },
        { "male+female", "duet" }, { "duo", "duet" }, { "duet", "duet" }, { "rauque", "raspy vocal" },
        { "éraillée", "raspy vocal" }, { "éraillé", "raspy vocal" }, { "raspy", "raspy vocal" },
        { "chuchotée", "whispered vocal" }, { "chuchoté", "whispered vocal" }, { "whispered", "whispered vocal" },
        { "éthérée", "ethereal vocal" }, { "aérienne", "ethereal vocal" }, { "ethereal", "ethereal vocal" },
        { "hurlée", "screamed vocal" }, { "hurlé", "screamed vocal" }, { "screams", "screamed vocal" },
        { "scream", "screamed vocal" }, { "voix grave", "deep vocal" }, { "baryton", "baritone vocal" },
        { "baritone", "baritone vocal" }, { "falsetto", "falsetto" }, { "chœurs", "backing vocals" },
        { "choeurs", "backing vocals" }, { "chœur", "choir" }, { "choir", "choir" }, { "harmonies vocales", "vocal harmonies" },
        { "stacked harmonies", "vocal harmonies" }, { "harmonies", "vocal harmonies" }, { "spoken word", "spoken word" },
        { "parlé", "spoken word" }, { "rap", "rapped vocal" },
        // instruments
        { "guitares distordues", "distorted guitar" }, { "guitare distordue", "distorted guitar" },
        { "guitares saturées", "distorted guitar" }, { "guitare saturée", "distorted guitar" },
        { "distorted guitars", "distorted guitar" }, { "distorted guitar", "distorted guitar" },
        { "overdrive", "overdrive guitar" }, { "basse saturée", "distorted bass" }, { "basse distordue", "distorted bass" },
        { "guitare acoustique", "acoustic guitar" }, { "guitares acoustiques", "acoustic guitar" },
        { "acoustic guitar", "acoustic guitar" }, { "fingerpicking", "fingerpicking guitar" },
        { "guitare électrique", "electric guitar" }, { "guitares électriques", "electric guitar" },
        { "electric guitar", "electric guitar" }, { "guitare claire", "clean guitar" }, { "clean guitar", "clean guitar" },
        { "solo de guitare", "guitar solo" }, { "guitar solo", "guitar solo" }, { "arpèges", "arpeggiated guitar" },
        { "double pédale", "double bass drum" }, { "double kick", "double bass drum" }, { "boîte à rythmes", "drum machine" },
        { "drum machine", "drum machine" }, { "batterie", "drums" }, { "drums", "drums" }, { "basse", "bass guitar" },
        { "bass", "bass guitar" }, { "piano", "piano" }, { "cordes", "strings" }, { "strings", "strings" },
        { "violoncelle", "cello" }, { "violon", "violin" }, { "orgue", "organ" }, { "synthétiseurs", "synth" },
        { "synthétiseur", "synth" }, { "synthés", "synth" }, { "synthé", "synth" }, { "synths", "synth" },
        { "synth", "synth" }, { "nappes", "synth pads" }, { "pads", "synth pads" }, { "cuivres", "brass" },
        { "saxophone", "saxophone" }, { "harmonica", "harmonica" }, { "accordéon", "accordion" },
        { "cornemuse", "bagpipes" }, { "mandoline", "mandolin" }, { "feedback", "feedback" }, { "larsen", "feedback" },
        // genres
        { "rock alternatif", "alternative rock" }, { "alternative rock", "alternative rock" },
        { "grunge alternatif", "grunge" }, { "post-grunge", "post-grunge" }, { "post grunge", "post-grunge" },
        { "grunge", "grunge" }, { "punk rock", "punk rock" }, { "pop punk", "pop punk" }, { "post-punk", "post-punk" },
        { "post punk", "post-punk" }, { "punk", "punk" }, { "new wave", "new wave" }, { "cold wave", "cold wave" },
        { "coldwave", "cold wave" }, { "dark wave", "dark wave" }, { "darkwave", "dark wave" },
        { "rock gothique", "gothic rock" }, { "gothic rock", "gothic rock" }, { "gothique", "gothic rock" },
        { "gothic", "gothic rock" }, { "shoegaze", "shoegaze" }, { "noise pop", "noise pop" }, { "britpop", "britpop" },
        { "hard rock", "hard rock" }, { "heavy metal", "heavy metal" }, { "nu metal", "nu metal" },
        { "nu-metal", "nu metal" }, { "rap metal", "rap metal" }, { "métal", "metal" }, { "metal", "metal" },
        { "rock progressif", "progressive rock" }, { "progressive rock", "progressive rock" },
        { "rock psychédélique", "psychedelic rock" }, { "psychedelic rock", "psychedelic rock" },
        { "stoner", "stoner rock" }, { "blues rock", "blues rock" }, { "blues", "blues" }, { "folk rock", "folk rock" },
        { "folk", "folk" }, { "celtique", "celtic" }, { "celtic", "celtic" }, { "reggae", "reggae" }, { "ska", "ska" },
        { "rock français", "French rock" }, { "chanson française", "French chanson" }, { "indie rock", "indie rock" },
        { "rock indépendant", "indie rock" }, { "garage rock", "garage rock" }, { "dream pop", "dream pop" },
        { "synth-pop", "synth-pop" }, { "synthpop", "synth-pop" }, { "synthwave", "synthwave" },
        { "industriel", "industrial" }, { "industrial", "industrial" }, { "trip hop", "trip hop" },
        { "trip-hop", "trip hop" }, { "électro", "electronic" }, { "electronic", "electronic" }, { "ambient", "ambient" },
        { "cinematic", "cinematic" }, { "cinématique", "cinematic" }, { "orchestral", "orchestral" },
        { "ballade", "ballad" }, { "ballad", "ballad" }, { "acoustique", "acoustic" }, { "acoustic", "acoustic" },
        { "pop rock", "pop rock" }, { "pop", "pop" }, { "rock", "rock" },
        // époques
        { "années 70", "70s" }, { "années 80", "80s" }, { "années 90", "90s" }, { "années 2000", "2000s" },
        { "70s", "70s" }, { "80s", "80s" }, { "90s", "90s" }, { "2000s", "2000s" }, { "1980s", "80s" }, { "1990s", "90s" },
        // ambiances et énergie
        { "mélancolique", "melancholic" }, { "melancholic", "melancholic" }, { "sombre", "dark" }, { "dark", "dark" },
        { "énergique", "energetic" }, { "energetic", "energetic" }, { "explosif", "explosive" }, { "explosive", "explosive" },
        { "chaotique", "chaotic" }, { "puissant", "powerful" }, { "puissante", "powerful" }, { "planant", "atmospheric" },
        { "atmosphérique", "atmospheric" }, { "atmospheric", "atmospheric" }, { "intime", "intimate" },
        { "intimate", "intimate" }, { "nostalgique", "nostalgic" }, { "nostalgic", "nostalgic" }, { "rêveur", "dreamy" },
        { "onirique", "dreamy" }, { "dreamy", "dreamy" }, { "agressif", "aggressive" }, { "aggressive", "aggressive" },
        { "épique", "epic" }, { "epic", "epic" }, { "triste", "sad" }, { "joyeux", "happy" }, { "lumineux", "uplifting" },
        { "uplifting", "uplifting" }, { "émotionnel", "emotional" }, { "émotion", "emotional" }, { "emotional", "emotional" },
        { "hypnotique", "hypnotic" }, { "froid", "cold" }, { "hanté", "haunting" }, { "haunting", "haunting" },
        { "intense", "intense" }, { "vulnérable", "vulnerable" }, { "anthemic", "anthemic" }, { "hymne", "anthemic" },
        { "mélodique", "melodic" }, { "lent", "slow" }, { "slow", "slow" }, { "rapide", "fast" }, { "mid-tempo", "mid-tempo" },
        { "réverbération", "reverb" }, { "reverb", "reverb" }, { "lo-fi", "lo-fi" },
    };
    return d;
}

std::string convert_style(const std::string & text) {
    std::string                                  hay = fold(text);
    std::vector<std::pair<size_t, std::string>>  found;     // (position, balise) pour garder l'ordre du texte
    std::vector<std::pair<size_t, std::string>>  profiles;  // balises issues des influences citées
    auto blank = [&](size_t pos, size_t len) { std::fill(hay.begin() + pos, hay.begin() + pos + len, ' '); };

    // Influences citées : leur profil complet.
    for (const Influence & inf : INFLUENCES) {
        std::string name = fold(inf.name);
        for (size_t pos = find_word(hay, name); pos != std::string::npos; pos = find_word(hay, name, pos + 1)) {
            if (*inf.singer) found.push_back({ pos, singer_tag(inf) });  // le timbre du chanteur reste prioritaire
            for (auto & t : split_style(inf.tags)) profiles.push_back({ pos, t });
            for (auto & t : split_style(inf.musicians)) profiles.push_back({ pos + 1000000, t });  // en dernier
            blank(pos, name.size());
        }
    }
    // Expressions du dictionnaire, les plus longues d'abord pour ne pas couper « punk rock » en « rock ».
    auto entries = dictionary();
    std::stable_sort(entries.begin(), entries.end(),
                     [](auto & a, auto & b) { return strlen(a.first) > strlen(b.first); });
    for (auto & [needle, tag] : entries) {
        std::string n = fold(needle);
        for (size_t pos = find_word(hay, n); pos != std::string::npos; pos = find_word(hay, n, pos + 1)) {
            if (*tag) found.push_back({ pos, tag });
            blank(pos, n.size());
        }
    }
    // Tempo : « 180 BPM ».
    for (size_t pos = hay.find("bpm"); pos != std::string::npos; pos = hay.find("bpm", pos + 3)) {
        size_t end = pos;
        while (end > 0 && hay[end - 1] == ' ') end--;
        size_t start = end;
        while (start > 0 && isdigit((unsigned char) hay[start - 1])) start--;
        if (start < end && end - start <= 3) found.push_back({ start, hay.substr(start, end - start) + " BPM" });
    }
    // Ce qui est décrit explicitement passe d'abord ; les profils d'influence complètent ensuite.
    std::stable_sort(found.begin(), found.end(), [](auto & a, auto & b) { return a.first < b.first; });
    std::vector<std::string> tags;
    for (auto & [pos, tag] : found) add_unique(tags, tag);
    for (auto & [pos, tag] : profiles) {
        if (tags.size() >= 18) break;
        add_unique(tags, tag);
    }
    // Une seule indication de tempo : la première citée.
    bool bpm = false;
    tags.erase(std::remove_if(tags.begin(), tags.end(), [&](const std::string & t) {
                   bool is_bpm = t.size() > 4 && t.compare(t.size() - 3, 3, "BPM") == 0;
                   if (is_bpm && bpm) return true;
                   bpm |= is_bpm;
                   return false;
               }), tags.end());
    // « rock » seul n'apporte rien si un genre rock plus précis est présent.
    bool specific = std::any_of(tags.begin(), tags.end(), [](const std::string & t) {
        return t != "rock" && t.find("rock") != std::string::npos;
    });
    if (specific) tags.erase(std::remove(tags.begin(), tags.end(), "rock"), tags.end());
    if (tags.size() > 18) tags.resize(18);
    return join_style(tags);
}

// ---------------------------------------------------------------------------- Titre

TitleBlock parse_title_block(const std::string & text) {
    TitleBlock         out;
    std::istringstream in(text);
    std::string        line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        std::string f = fold(line);
        auto value = [&](size_t colon) { return trim(line.substr(colon + 1)); };
        size_t colon = line.find(':');
        if (colon != std::string::npos && (f.rfind("album", 0) == 0)) {
            out.album = value(colon);
        } else if (colon != std::string::npos && (f.rfind("groupe", 0) == 0 || f.rfind("group", 0) == 0 ||
                                                  f.rfind("artiste", 0) == 0 || f.rfind("artist", 0) == 0)) {
            out.artist = value(colon);
        } else if (out.title.empty()) {
            // « Titre : … » ou titre seul ; guillemets et crochets décoratifs retirés.
            std::string t = colon != std::string::npos && f.rfind("titre", 0) == 0 ? value(colon) : line;
            while (!t.empty() && strchr("\"'[«", t.front())) t.erase(0, 1);
            while (!t.empty() && strchr("\"']»", t.back())) t.pop_back();
            out.title = trim(t);
        }
    }
    // Numéro de piste en tête : « 11. Titre », « 03 - Titre ».
    size_t digits = 0;
    while (digits < out.title.size() && isdigit((unsigned char) out.title[digits])) digits++;
    if (digits > 0 && digits <= 3 && digits < out.title.size() && strchr(".-) ", out.title[digits])) {
        out.track = out.title.substr(0, digits);
    }
    return out;
}

// ---------------------------------------------------------------------------- Structure

// Nom de section Suno → section YuE2 ("" = ligne supprimée, nullptr = pas une section).
static const char * map_section(const std::string & raw) {
    std::string n = fold(raw);
    // retire numéros, points de suspension et espaces superflus : « verse 1 », « intro... »
    std::string clean;
    for (char c : n) {
        if (!isdigit((unsigned char) c) && c != '.') clean += c;
    }
    clean = trim(clean);
    static const std::vector<std::pair<const char *, const char *>> map = {
        { "mandatory acoustic bridge", "Bridge" }, { "acoustic bridge", "Bridge" }, { "anthemic chorus", "Chorus" },
        { "final chorus", "Chorus" }, { "post-chorus", "Chorus" }, { "post chorus", "Chorus" },
        { "pre-chorus", "Pre-Chorus" }, { "pre chorus", "Pre-Chorus" }, { "prechorus", "Pre-Chorus" },
        { "pre-refrain", "Pre-Chorus" }, { "pré-refrain", "Pre-Chorus" }, { "build-up", "Pre-Chorus" },
        { "buildup", "Pre-Chorus" }, { "build up", "Pre-Chorus" }, { "build", "Pre-Chorus" },
        { "instrumental break", "Interlude" }, { "instrumental", "Interlude" }, { "guitar solo", "Interlude" },
        { "piano solo", "Interlude" }, { "bass solo", "Interlude" }, { "drum solo", "Interlude" }, { "solo", "Interlude" },
        { "breakdown", "Bridge" }, { "break", "Interlude" }, { "interlude", "Interlude" }, { "fade in", "Intro" },
        { "fade out", "Outro" }, { "intro", "Intro" }, { "opening", "Intro" }, { "verse", "Verse" }, { "couplet", "Verse" },
        { "chorus", "Chorus" }, { "refrain", "Chorus" }, { "hook", "Chorus" }, { "drop", "Chorus" },
        { "bridge", "Bridge" }, { "pont", "Bridge" }, { "middle", "Bridge" }, { "outro", "Outro" }, { "coda", "Outro" },
        { "end", "" }, { "fin", "" },
    };
    for (auto & [key, tag] : map) {
        size_t len = strlen(key);
        if (clean.compare(0, len, key) == 0 && (clean.size() == len || !word_char(clean[len]))) {
            return tag;
        }
    }
    return nullptr;
}

// Retire les crochets intégrés au texte (phonétique « [(les-ta)] », indications).
static std::string strip_inline_brackets(const std::string & line) {
    std::string out;
    int         depth = 0;
    for (char c : line) {
        if (c == '[') depth++;
        else if (c == ']' && depth > 0) depth--;
        else if (depth == 0) out += c;
    }
    // espaces doublés laissés par les suppressions
    std::string clean;
    for (char c : out) {
        if (!(c == ' ' && !clean.empty() && clean.back() == ' ')) clean += c;
    }
    return trim(clean);
}

std::string convert_lyrics(const std::string & text, bool instrumental, std::vector<std::string> & style_extra) {
    std::istringstream       in(text);
    std::string              line;
    std::vector<std::string> out;  // sections et lignes, "" = ligne vide
    auto push_blank = [&] {
        if (!instrumental && !out.empty() && !out.back().empty()) out.push_back("");
    };
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) {
            push_blank();
            continue;
        }
        std::string f = fold(line);
        if (line.front() == '[') {
            // En-tête « [Style: …], [Mood: …], [Tempo: 180 BPM] » : versé dans le style.
            if (f.rfind("[style:", 0) == 0 || f.rfind("[mood:", 0) == 0 || f.rfind("[tempo:", 0) == 0 ||
                f.rfind("[genre:", 0) == 0 || f.rfind("[era:", 0) == 0 || f.rfind("[energy:", 0) == 0) {
                std::string tags = convert_style(line);
                for (auto & t : split_style(tags)) style_extra.push_back(t);
                continue;
            }
            size_t close = line.find(']');
            if (close == line.size() - 1) {
                std::string inner = line.substr(1, close - 1);
                inner.erase(std::remove(inner.begin(), inner.end(), '('), inner.end());
                inner.erase(std::remove(inner.begin(), inner.end(), ')'), inner.end());
                std::string name = inner.substr(0, inner.find('|'));
                const char * section = map_section(name);
                // Les descripteurs d'une section (« | whispered | acoustic ») ne valent que pour elle : YuE2 n'a
                // qu'un style global, les y verser le rend contradictoire et le son devient brouillé. Ignorés.
                if (!section || !*section) {
                    continue;
                }
                std::string tag = section;
                if (instrumental) {
                    std::string lower = fold(tag);
                    if (lower == "interlude") lower = "bridge";  // la LoRA instrumentale ne connaît pas [interlude]
                    if (out.empty() || out.back() != "[" + lower + "]") out.push_back("[" + lower + "]");
                } else {
                    push_blank();
                    out.push_back("[" + tag + "]");
                }
                continue;
            }
        }
        if (instrumental) {
            continue;  // un instrumental n'a pas de paroles
        }
        std::string lyric = strip_inline_brackets(line);
        // Chœurs entre parenthèses « (low tide) » : les mots sont gardés, pas les parenthèses.
        lyric.erase(std::remove(lyric.begin(), lyric.end(), '('), lyric.end());
        lyric.erase(std::remove(lyric.begin(), lyric.end(), ')'), lyric.end());
        lyric = trim(lyric);
        if (!lyric.empty()) out.push_back(lyric);
    }
    while (!out.empty() && out.back().empty()) out.pop_back();

    // Refrain répété sans texte (implicite chez Suno) : YuE2 a besoin des paroles, on recopie
    // celles de la première occurrence de la même section.
    if (!instrumental) {
        std::map<std::string, std::vector<std::string>> first_text;
        std::vector<std::string>                        filled;
        for (size_t i = 0; i < out.size(); i++) {
            filled.push_back(out[i]);
            const std::string & tag = out[i];
            if (tag.empty() || tag.front() != '[' || (tag != "[Chorus]" && tag != "[Pre-Chorus]")) continue;
            std::vector<std::string> text;
            for (size_t j = i + 1; j < out.size() && !(out[j].size() && out[j].front() == '['); j++) {
                if (!out[j].empty()) text.push_back(out[j]);
            }
            if (!text.empty()) {
                first_text.emplace(tag, text);
            } else if (first_text.count(tag)) {
                for (auto & l : first_text[tag]) filled.push_back(l);
            }
        }
        out = filled;
    }
    std::string result;
    for (auto & l : out) result += l + "\n";
    return result;
}
