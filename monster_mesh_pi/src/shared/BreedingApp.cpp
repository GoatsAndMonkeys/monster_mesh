// SPDX-License-Identifier: MIT
#include "BreedingApp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace breeding {

// ── National dex name table (1..151) ──────────────────────────────────────────
const char *BreedingApp::dexName(uint8_t dex) {
    static const char *const N[152] = {
        "???","BULBASAUR","IVYSAUR","VENUSAUR","CHARMANDER","CHARMELEON",
        "CHARIZARD","SQUIRTLE","WARTORTLE","BLASTOISE","CATERPIE","METAPOD",
        "BUTTERFREE","WEEDLE","KAKUNA","BEEDRILL","PIDGEY","PIDGEOTTO",
        "PIDGEOT","RATTATA","RATICATE","SPEAROW","FEAROW","EKANS","ARBOK",
        "PIKACHU","RAICHU","SANDSHREW","SANDSLASH","NIDORAN-F","NIDORINA",
        "NIDOQUEEN","NIDORAN-M","NIDORINO","NIDOKING","CLEFAIRY","CLEFABLE",
        "VULPIX","NINETALES","JIGGLYPUFF","WIGGLYTUFF","ZUBAT","GOLBAT",
        "ODDISH","GLOOM","VILEPLUME","PARAS","PARASECT","VENONAT","VENOMOTH",
        "DIGLETT","DUGTRIO","MEOWTH","PERSIAN","PSYDUCK","GOLDUCK","MANKEY",
        "PRIMEAPE","GROWLITHE","ARCANINE","POLIWAG","POLIWHIRL","POLIWRATH",
        "ABRA","KADABRA","ALAKAZAM","MACHOP","MACHOKE","MACHAMP","BELLSPROUT",
        "WEEPINBELL","VICTREEBEL","TENTACOOL","TENTACRUEL","GEODUDE","GRAVELER",
        "GOLEM","PONYTA","RAPIDASH","SLOWPOKE","SLOWBRO","MAGNEMITE","MAGNETON",
        "FARFETCHD","DODUO","DODRIO","SEEL","DEWGONG","GRIMER","MUK",
        "SHELLDER","CLOYSTER","GASTLY","HAUNTER","GENGAR","ONIX","DROWZEE",
        "HYPNO","KRABBY","KINGLER","VOLTORB","ELECTRODE","EXEGGCUTE","EXEGGUTOR",
        "CUBONE","MAROWAK","HITMONLEE","HITMONCHAN","LICKITUNG","KOFFING",
        "WEEZING","RHYHORN","RHYDON","CHANSEY","TANGELA","KANGASKHAN",
        "HORSEA","SEADRA","GOLDEEN","SEAKING","STARYU","STARMIE","MR-MIME",
        "SCYTHER","JYNX","ELECTABUZZ","MAGMAR","PINSIR","TAUROS","MAGIKARP",
        "GYARADOS","LAPRAS","DITTO","EEVEE","VAPOREON","JOLTEON","FLAREON",
        "PORYGON","OMANYTE","OMASTAR","KABUTO","KABUTOPS","AERODACTYL",
        "SNORLAX","ARTICUNO","ZAPDOS","MOLTRES","DRATINI","DRAGONAIR",
        "DRAGONITE","MEWTWO","MEW",
    };
    return (dex >= 1 && dex <= 151) ? N[dex] : "???";
}

// Earliest evolution (base form) per Gen-1 national dex. Evolved mons map back
// to the first stage of their family; base-stage mons map to themselves.
uint8_t BreedingApp::baseForm(uint8_t dex) {
    static const uint8_t B[152] = {
        0,
        1,1,1,        4,4,4,        7,7,7,        10,10,10,     // 1-12
        13,13,13,     16,16,16,     19,19,        21,21,        // 13-22
        23,23,        25,25,        27,27,        29,29,29,     // 23-31
        32,32,32,     35,35,        37,37,        39,39,        // 32-40
        41,41,        43,43,43,     46,46,        48,48,        // 41-49
        50,50,        52,52,        54,54,        56,56,        // 50-57
        58,58,        60,60,60,     63,63,63,     66,66,66,     // 58-68
        69,69,69,     72,72,        74,74,74,     77,77,        // 69-78
        79,79,        81,81,        83,           84,84,        // 79-85
        86,86,        88,88,        90,90,        92,92,92,     // 86-94
        95,           96,96,        98,98,        100,100,      // 95-101
        102,102,      104,104,      106,          107,          // 102-107
        108,          109,109,      111,111,      113,          // 108-113
        114,          115,          116,116,      118,118,      // 114-119
        120,120,      122,          123,          124,          // 120-124
        125,          126,          127,          128,          // 125-128
        129,129,      131,          132,          133,133,133,133, // 129-136
        137,          138,138,      140,140,      142,          // 137-142
        143,          144,          145,          146,          // 143-146
        147,147,147,  150,          151,                        // 147-151
    };
    return (dex >= 1 && dex <= 151) ? B[dex] : dex;
}

// ── Roster management ─────────────────────────────────────────────────────────
uint32_t BreedingApp::add(BreedMon m) {
    if (m.id == 0) m.id = nextId_++;
    else if (m.id >= nextId_) nextId_ = m.id + 1;
    roster_.push_back(m);
    return m.id;
}

static BreedMon makeMon(uint8_t dex, uint8_t level, const char *nick,
                        Genotype g, ProvKind prov, uint8_t provGen,
                        uint8_t depth) {
    BreedMon m{};
    m.dex = dex; m.level = level; m.geno = g;
    strncpy(m.nick, nick, sizeof(m.nick) - 1);
    m.prov = prov; m.provGen = provGen; m.depth = depth;
    return m;
}

void BreedingApp::seedTestRoster() {
    roster_.clear();
    nextId_ = 1;
    // female, rainbow, shiny, dark, sterile, cantFight, noHatch
    // {rainbow, shiny, dark, sterile, cantFight, noHatch, female}

    // A Pentest Wild Pikachu — a plain catch. Owning this UNLOCKS breeding.
    add(makeMon(25, 12, "Sparky",
        {0,1,0, 0,0,0, 0}, PROV_WILD, 0, 0));      // Ss carrier male

    // Pink female (Rr) — the common gateway color, ♀ so she displays it.
    add(makeMon(25, 14, "Rosa",
        {1,0,0, 0,1,0, 1}, PROV_WILD, 0, 0));      // Rr, Ff carrier

    // Hidden Rainbow carrier male (Rr) — blood-tested stud. Displays Regular.
    add(makeMon(25, 15, "Prism",
        {1,0,0, 0,0,0, 0}, PROV_WILD, 0, 0));      // Rr male (hidden carrier)

    // Dark pair (Dd × Dd) → 25% Blackout target.
    add(makeMon(94, 20, "Shade",
        {0,0,1, 0,0,1, 1}, PROV_WILD, 0, 0));      // Dd female, Hh carrier
    add(makeMon(94, 22, "Umbra",
        {0,0,1, 1,0,0, 0}, PROV_WILD, 0, 0));      // Dd male, Bb carrier

    // Shiny-carrier pair (Ss × Ss) → 25% Shiny.
    add(makeMon(133, 10, "Sable",
        {0,1,0, 0,0,0, 1}, PROV_WILD, 0, 0));      // Ss female
    add(makeMon(133, 11, "Cinder",
        {0,1,0, 0,0,0, 0}, PROV_WILD, 0, 0));      // Ss male

    // The heartbreak: a gorgeous Wild Rainbow female that is bb — STERILE.
    // Battle-ready and stunning, but a genetic dead-end (can't breed).
    add(makeMon(6, 30, "Iris",
        {2,0,0, 2,0,0, 1}, PROV_WILD, 0, 0));      // rr Rainbow ♀, bb sterile

    // Clean foundation stock — plain, but BB FF HH: prime breeding base.
    add(makeMon(143, 25, "Rock",
        {0,0,0, 0,0,0, 0}, PROV_WILD, 0, 0));
}

// ── Provenance derivation ─────────────────────────────────────────────────────
void BreedingApp::deriveProvenance(BreedMon &child, const BreedMon &pa,
                                   const BreedMon &pb) {
    child.parentA = pa.id;
    child.parentB = pb.id;
    child.depth   = (uint8_t)(1 + (pa.depth > pb.depth ? pa.depth : pb.depth));

    if (pa.id == pb.id) {
        // Selfing — bred a mon with itself. Sn.
        child.prov    = PROV_S;
        child.provGen = (uint8_t)((pa.prov == PROV_S ? pa.provGen : 0) + 1);
    } else if (pa.id == pb.parentA || pa.id == pb.parentB ||
               pb.id == pa.parentA || pb.id == pa.parentB) {
        // Backcross — one parent is a parent of the other. BXn.
        uint8_t n = 0;
        if (pa.prov == PROV_BX) n = pa.provGen;
        if (pb.prov == PROV_BX && pb.provGen > n) n = pb.provGen;
        child.prov    = PROV_BX;
        child.provGen = (uint8_t)(n + 1);
    } else {
        // Filial. Fn = max(parent depth)+1. Rainbow first appears in F2.
        child.prov    = PROV_F;
        child.provGen = child.depth;
    }

    // IBL overlay: once a bred mon is homozygous for a rare cosmetic trait that
    // both parents contributed to, the line has locked it true-breeding.
    bool lockedRainbow = child.geno.rainbow == 2 && pa.geno.rainbow && pb.geno.rainbow;
    bool lockedShiny   = child.geno.shiny   == 2 && pa.geno.shiny   && pb.geno.shiny;
    bool lockedDark    = child.geno.dark    == 2 && pa.geno.dark    && pb.geno.dark;
    if (lockedRainbow || lockedShiny || lockedDark) {
        child.prov = PROV_IBL;
    }
}

// ── Breeding gate ─────────────────────────────────────────────────────────────
bool BreedingApp::breedingUnlocked() const {
    for (const auto &m : roster_)
        if (m.prov == PROV_WILD) return true;   // a Pentest catch is on the deck
    return false;
}

// ── Pair & breed ──────────────────────────────────────────────────────────────
BreedResult BreedingApp::breed(size_t a, size_t b, Rng &rng) {
    BreedResult r;
    if (a >= roster_.size() || b >= roster_.size()) {
        r.status = BREED_BAD_INPUT;
        r.message = "Pick two mons from the roster.";
        return r;
    }
    if (!breedingUnlocked()) {
        r.status = BREED_LOCKED;
        r.message = lockedReason();
        return r;
    }
    const BreedMon &pa = roster_[a];
    const BreedMon &pb = roster_[b];

    if (isSterile(pa.geno) || isSterile(pb.geno)) {
        r.status = BREED_STERILE;
        const BreedMon &s = isSterile(pa.geno) ? pa : pb;
        r.message = std::string(s.nick[0] ? s.nick : dexName(s.dex)) +
                    " is bb — STERILE. A dead-end line; it can't breed.";
        return r;
    }

    // Roll the egg.
    BreedMon child{};
    child.geno  = cross(pa.geno, pb.geno, rng);
    // Baby is 50/50 either parent's species, always the EARLIEST evolution.
    uint8_t pickDex = rng.bit() ? pa.dex : pb.dex;
    child.dex   = baseForm(pickDex);
    child.level = 1;
    strncpy(child.nick, dexName(child.dex), sizeof(child.nick) - 1);
    child.id    = nextId_++;   // reserve a lineage id even if it won't hatch
    deriveProvenance(child, pa, pb);

    if (neverHatches(child.geno)) {
        // hh — the lethal recessive. Egg produced but never hatches.
        r.status = BREED_NO_HATCH;
        r.message = "The egg was hh (no-hatch) — it will never hatch. "
                    "Both parents carried Hh.";
        return r;
    }

    r.status  = BREED_OK;
    r.child   = child;
    std::string skin = skinName(child.geno);
    r.message = "A " + skin + " " + std::string(dexName(child.dex)) +
                " hatched! (" + provTag(child) + ")";
    if (cantFight(child.geno))
        r.message += "  It's ff — breeding-only, can't battle.";
    return r;
}

// ── Reporting ─────────────────────────────────────────────────────────────────
std::string BreedingApp::provTag(const BreedMon &m) {
    char buf[8];
    switch (m.prov) {
        case PROV_WILD: return "Wild";
        case PROV_IBL:  return "IBL";
        case PROV_F:    snprintf(buf, sizeof(buf), "F%u", m.provGen); return buf;
        case PROV_S:    snprintf(buf, sizeof(buf), "S%u", m.provGen); return buf;
        case PROV_BX:   snprintf(buf, sizeof(buf), "BX%u", m.provGen); return buf;
    }
    return "?";
}

std::string BreedingApp::summaryLine(const BreedMon &m) {
    char buf[96];
    snprintf(buf, sizeof(buf), "L%u %s \"%s\" [%s] (%s) %c",
             m.level, dexName(m.dex), m.nick[0] ? m.nick : "-",
             skinName(m.geno), provTag(m).c_str(),
             m.geno.female ? 'F' : 'M');
    return buf;
}

std::vector<std::string> BreedingApp::bloodTest(const BreedMon &m) {
    std::vector<std::string> out;
    char buf[96];
    const Genotype &g = m.geno;

    snprintf(buf, sizeof(buf), "%s  \"%s\"  L%u  %s",
             dexName(m.dex), m.nick[0] ? m.nick : "-", m.level,
             m.geno.female ? "\xE2\x99\x80" : "\xE2\x99\x82");
    out.push_back(buf);
    snprintf(buf, sizeof(buf), "Skin: %s     Provenance: %s",
             skinName(g), provTag(m).c_str());
    out.push_back(buf);
    out.push_back("--- Genetics blood test ---");
    out.push_back(std::string("Rainbow : ") + rainbowAllele(g));
    out.push_back(std::string("Shiny   : ") + shinyAllele(g));
    out.push_back(std::string("Dark    : ") + darkAllele(g));
    out.push_back(std::string("Sterile : ") + sterileAllele(g));
    out.push_back(std::string("CantFght: ") + cantFightAllele(g));
    out.push_back(std::string("NoHatch : ") + noHatchAllele(g));

    // Roll-up: what this mon can DO (value axes).
    std::string can = "Status  : ";
    can += cantFight(g) ? "can't battle" : "battle-ok";
    can += isSterile(g) ? ", sterile"   : ", fertile";
    if (isCleanStock(g)) can += ", CLEAN STOCK";
    if (isHomozygousRare(g)) can += ", true-breeding rare";
    out.push_back(can);
    return out;
}

// ── JSON import (flat, strstr-based — no external library) ─────────────────────
namespace {
// Find "key" within [p, end) and return pointer just past the ':' or nullptr.
const char *findKey(const char *p, const char *end, const char *key) {
    size_t klen = strlen(key);
    for (const char *q = p; q + klen + 1 < end; ++q) {
        if (q[0] == '"' && strncmp(q + 1, key, klen) == 0 && q[1 + klen] == '"') {
            const char *c = q + 1 + klen + 1;
            while (c < end && (*c == ' ' || *c == '\t' || *c == ':')) ++c;
            return c;
        }
    }
    return nullptr;
}
int getInt(const char *obj, const char *end, const char *key, int def) {
    const char *c = findKey(obj, end, key);
    if (!c) return def;
    if (*c == 't') return 1;           // true
    if (*c == 'f') return 0;           // false
    return (int)strtol(c, nullptr, 10);
}
// Copy a "string" value into dst (NUL-terminated). Returns true if found.
bool getStr(const char *obj, const char *end, const char *key,
            char *dst, size_t dstLen) {
    const char *c = findKey(obj, end, key);
    if (!c || *c != '"') return false;
    ++c;
    size_t j = 0;
    while (c < end && *c != '"' && j < dstLen - 1) dst[j++] = *c++;
    dst[j] = '\0';
    return true;
}
ProvKind parseProv(const char *tag, uint8_t *genOut) {
    *genOut = 0;
    if (!tag || !tag[0]) return PROV_WILD;
    if (strncmp(tag, "IBL", 3) == 0)  return PROV_IBL;
    if (strncmp(tag, "Wild", 4) == 0) return PROV_WILD;
    if (tag[0] == 'B' && tag[1] == 'X') { *genOut = (uint8_t)atoi(tag + 2); return PROV_BX; }
    if (tag[0] == 'F') { *genOut = (uint8_t)atoi(tag + 1); return PROV_F; }
    if (tag[0] == 'S') { *genOut = (uint8_t)atoi(tag + 1); return PROV_S; }
    return PROV_WILD;
}
}  // namespace

int BreedingApp::importJson(const std::string &json) {
    const char *p = json.c_str();
    const char *end = p + json.size();
    int count = 0;
    // Walk each object between '{' and its matching '}' inside the roster array.
    const char *q = p;
    while (q < end) {
        const char *ob = strchr(q, '{');
        if (!ob || ob >= end) break;
        // find matching close brace (flat objects — no nesting in our schema)
        const char *oe = strchr(ob + 1, '}');
        if (!oe || oe >= end) break;
        // require this object to actually carry a "dex" field, else skip (this
        // also skips the outer wrapper object if present).
        if (findKey(ob, oe, "dex")) {
            BreedMon m{};
            m.dex   = (uint8_t)getInt(ob, oe, "dex", 0);
            m.level = (uint8_t)getInt(ob, oe, "level", 1);
            m.geno.rainbow   = (uint8_t)getInt(ob, oe, "rainbow", 0);
            m.geno.shiny     = (uint8_t)getInt(ob, oe, "shiny", 0);
            m.geno.dark      = (uint8_t)getInt(ob, oe, "dark", 0);
            m.geno.sterile   = (uint8_t)getInt(ob, oe, "sterile", 0);
            m.geno.cantFight = (uint8_t)getInt(ob, oe, "cantFight", 0);
            m.geno.noHatch   = (uint8_t)getInt(ob, oe, "noHatch", 0);
            m.geno.female    = (uint8_t)(getInt(ob, oe, "female", 0) ? 1 : 0);
            char tag[8] = {0};
            getStr(ob, oe, "provenance", tag, sizeof(tag));
            m.prov = parseProv(tag, &m.provGen);
            m.depth = (m.prov == PROV_F) ? m.provGen : 0;
            if (!getStr(ob, oe, "nick", m.nick, sizeof(m.nick)))
                strncpy(m.nick, dexName(m.dex), sizeof(m.nick) - 1);
            add(m);
            ++count;
        }
        q = oe + 1;
    }
    return count;
}

int BreedingApp::importJsonFile(const std::string &path) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return -1; }
    std::string buf((size_t)n, '\0');
    size_t rd = fread(&buf[0], 1, (size_t)n, f);
    fclose(f);
    buf.resize(rd);
    return importJson(buf);
}

// ── Firmware CaughtMon binary import ──────────────────────────────────────────
// Packed 22-byte record, exactly the firmware struct CaughtMon:
//   off 0  : uint8  dex
//   off 1  : uint8  level
//   off 2  : uint8[7] geno  (rainbow,shiny,dark,sterile,cantFight,noHatch,female)
//   off 9  : uint32 caughtSec  (little-endian; ignored on import)
//   off 13 : uint8  provenance (0 = Wild)
//   off 14 : char[8] nick? -- NO: firmware nick is 11 B at off 14 -> total 25.
// The firmware struct is NOT packed(1); to avoid ambiguity the transfer format
// is the EXPLICIT packed 22-byte layout below (nick truncated to 8):
//   off 14 : char[8] nick (8, NUL-padded)  -> record size = 22 bytes.
static constexpr size_t CAUGHTMON_WIRE_SIZE = 22;

int BreedingApp::importCaughtMonBlob(const uint8_t *data, size_t len) {
    if (!data || len < CAUGHTMON_WIRE_SIZE) return -1;
    int count = 0;
    for (size_t off = 0; off + CAUGHTMON_WIRE_SIZE <= len; off += CAUGHTMON_WIRE_SIZE) {
        const uint8_t *rec = data + off;
        BreedMon m{};
        m.dex   = rec[0];
        m.level = rec[1];
        m.geno.rainbow   = rec[2];
        m.geno.shiny     = rec[3];
        m.geno.dark      = rec[4];
        m.geno.sterile   = rec[5];
        m.geno.cantFight = rec[6];
        m.geno.noHatch   = rec[7];
        m.geno.female    = rec[8] ? 1 : 0;
        // rec[9..12] caughtSec — ignored
        uint8_t provByte = rec[13];
        m.prov    = (provByte == 0) ? PROV_WILD : PROV_F;   // 0 = Wild catch
        m.provGen = (provByte == 0) ? 0 : provByte;
        memcpy(m.nick, rec + 14, 8);
        m.nick[8] = '\0';
        if (!m.nick[0]) strncpy(m.nick, dexName(m.dex), sizeof(m.nick) - 1);
        add(m);
        ++count;
    }
    return count;
}

}  // namespace breeding
