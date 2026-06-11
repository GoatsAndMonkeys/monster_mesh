#pragma once
#include <Arduino.h>
#include "DungeonModule.h"
#include <string.h>
#include <ctype.h>

// ── Trivia categories ─────────────────────────────────────────────────────────
// Bit i of DungeonRun::triviaCategories = category i answered this run.
// All 5 answered = Trivial Pursuit bonus.
enum class TriviaCategory : uint8_t {
    Pokemon    = 0,
    DnD        = 1,
    PopCulture = 2,
    Science    = 3,
    History    = 4,
    COUNT      = 5,
};

// ── Question record ───────────────────────────────────────────────────────────
struct TriviaQuestion {
    const char*    question;
    const char*    answer;      // primary, lowercase, no punctuation
    const char*    altAnswer;   // alternate accepted (nullptr = none)
    TriviaCategory category;
    uint8_t        minDepth;    // minimum dungeon depth to appear (1 = always)
};

// ── Active session ────────────────────────────────────────────────────────────
struct TriviaSession {
    const TriviaQuestion* question;
    bool answered;
    bool correct;
};

// ── Hardcoded question bank ───────────────────────────────────────────────────
// 8 per category — 40 questions total. SD card content tables can extend this.
static constexpr TriviaQuestion TRIVIA_BANK[] = {
    // ── Pokemon (0) ────────────────────────────────────────────────────────
    {"What type is Charizard?",                            "fire flying",    "flying fire",          TriviaCategory::Pokemon,    1},
    {"Which Pokemon is #25 in the Pokedex?",               "pikachu",        nullptr,                TriviaCategory::Pokemon,    1},
    {"What move does Pikachu typically use?",              "thunderbolt",    "thunder",              TriviaCategory::Pokemon,    1},
    {"What does Machop evolve into?",                      "machoke",        nullptr,                TriviaCategory::Pokemon,    2},
    {"What type is super effective against Psychic?",      "bug",            "ghost",                TriviaCategory::Pokemon,    2},
    {"Which Pokemon has base 100 in every stat?",          "mew",            nullptr,                TriviaCategory::Pokemon,    3},
    {"What is Eevee's Fire-type evolution?",               "flareon",        nullptr,                TriviaCategory::Pokemon,    2},
    {"Which Pokemon evolves into Gengar?",                 "haunter",        nullptr,                TriviaCategory::Pokemon,    2},

    // ── D&D (1) ────────────────────────────────────────────────────────────
    {"How many sides does a d20 have?",                    "20",             "twenty",               TriviaCategory::DnD,        1},
    {"What class uses Sneak Attack?",                      "rogue",          nullptr,                TriviaCategory::DnD,        1},
    {"What is the standard max ability score in D&D 5E?",  "20",             "twenty",               TriviaCategory::DnD,        1},
    {"What saving throw does Fireball require?",           "dexterity",      "dex",                  TriviaCategory::DnD,        2},
    {"What class gets Bardic Inspiration?",                "bard",           nullptr,                TriviaCategory::DnD,        1},
    {"What damage type does Eldritch Blast deal?",         "force",          nullptr,                TriviaCategory::DnD,        3},
    {"What is a roll of 20 on a d20 called?",             "critical hit",   "nat 20",               TriviaCategory::DnD,        1},
    {"Which class can Wildshape into animals?",            "druid",          nullptr,                TriviaCategory::DnD,        1},

    // ── Pop Culture (2) ────────────────────────────────────────────────────
    {"What franchise uses the slogan Gotta Catch Em All?", "pokemon",        nullptr,                TriviaCategory::PopCulture, 1},
    {"Name the post-apocalyptic currency used in Fallout.", "caps",          "bottle caps",          TriviaCategory::PopCulture, 1},
    {"What color is Link's tunic in the original Legend of Zelda?", "green", nullptr,               TriviaCategory::PopCulture, 1},
    {"What company makes Meshtastic firmware?",            "meshtastic",     nullptr,                TriviaCategory::PopCulture, 2},
    {"What is the T-Deck device manufacturer called?",     "lilygo",         nullptr,                TriviaCategory::PopCulture, 2},
    {"What does LoRa stand for?",                          "long range",     "lora",                 TriviaCategory::PopCulture, 2},
    {"Who makes the SX1262 LoRa chip?",                    "semtech",        nullptr,                TriviaCategory::PopCulture, 3},
    {"Name any type super effective against Fire.",         "water",          "rock",                 TriviaCategory::PopCulture, 1},

    // ── Science/Nature (3) ─────────────────────────────────────────────────
    {"How many elements are on the periodic table?",       "118",            nullptr,                TriviaCategory::Science,    2},
    {"What is the chemical symbol for gold?",              "au",             "gold",                 TriviaCategory::Science,    1},
    {"What gas do plants absorb from the air?",            "co2",            "carbon dioxide",       TriviaCategory::Science,    1},
    {"How many bones are in the adult human body?",        "206",            nullptr,                TriviaCategory::Science,    2},
    {"What does CPU stand for?",                           "central processing unit", "cpu",         TriviaCategory::Science,    1},
    {"What voltage does a standard AA battery produce?",   "1.5",            "1.5v",                 TriviaCategory::Science,    2},
    {"What frequency band does standard WiFi use?",        "2.4",            "2.4ghz",               TriviaCategory::Science,    2},
    {"What subatomic particle has a negative charge?",     "electron",       nullptr,                TriviaCategory::Science,    1},

    // ── History (4) ────────────────────────────────────────────────────────
    {"In what year did World War 2 end?",                  "1945",           nullptr,                TriviaCategory::History,    1},
    {"Who was the first President of the United States?",  "washington",     "george washington",    TriviaCategory::History,    1},
    {"In what year did the Berlin Wall fall?",             "1989",           nullptr,                TriviaCategory::History,    2},
    {"What empire did Julius Caesar lead?",                "roman",          "rome",                 TriviaCategory::History,    1},
    {"What year did humans first land on the Moon?",       "1969",           nullptr,                TriviaCategory::History,    1},
    {"What country built the Great Wall?",                 "china",          nullptr,                TriviaCategory::History,    1},
    {"Who is credited with inventing the telephone?",      "bell",           "alexander graham bell",TriviaCategory::History,    1},
    {"What year did Nintendo release the original Game Boy?","1989",         nullptr,                TriviaCategory::History,    2},
};

static constexpr uint8_t TRIVIA_BANK_SIZE =
    (uint8_t)(sizeof(TRIVIA_BANK) / sizeof(TRIVIA_BANK[0]));

// ── DungeonTriviaBank ─────────────────────────────────────────────────────────
class DungeonTriviaBank {
public:
    // Select a question preferring an uncovered category, filtered by minDepth.
    // categoryMask: bit i set = category i already answered this run.
    static const TriviaQuestion* getQuestion(uint8_t depth,
                                              uint8_t categoryMask,
                                              uint32_t seed) {
        uint32_t rng = seed * 1664525u + 1013904223u;

        // Split candidates into: uncovered-category pool and fallback pool
        uint8_t prim[TRIVIA_BANK_SIZE], pCount = 0;
        uint8_t fall[TRIVIA_BANK_SIZE], fCount = 0;

        for (uint8_t i = 0; i < TRIVIA_BANK_SIZE; i++) {
            const TriviaQuestion& q = TRIVIA_BANK[i];
            if (q.minDepth > depth) continue;
            uint8_t bit = (uint8_t)(1u << (uint8_t)q.category);
            if (!(categoryMask & bit)) prim[pCount++] = i;
            else                        fall[fCount++] = i;
        }

        if (pCount > 0) return &TRIVIA_BANK[prim[rng % pCount]];
        if (fCount > 0) return &TRIVIA_BANK[fall[rng % fCount]];
        return &TRIVIA_BANK[0];
    }

    // Case-insensitive answer check with trimming and partial match fallback.
    static bool checkAnswer(const TriviaQuestion* q, const char* input) {
        if (!q || !input) return false;

        char norm[64] = {};
        const char* p = input;
        while (*p == ' ') p++;               // ltrim
        int len = 0;
        while (*p && len < 63)
            norm[len++] = (char)tolower((uint8_t)*p++);
        while (len > 0 && norm[len - 1] == ' ')
            norm[--len] = '\0';              // rtrim

        if (strcmp(norm, q->answer) == 0) return true;
        if (q->altAnswer && strcmp(norm, q->altAnswer) == 0) return true;

        // Substring match for paraphrased answers (must be ≥3 chars to avoid false positives)
        if (len >= 3 && strstr(q->answer, norm)) return true;
        if (q->altAnswer && len >= 3 && strstr(q->altAnswer, norm)) return true;

        return false;
    }

    // HP damage dealt to party's active Pokemon for a wrong answer.
    // Scales linearly with depth and enemy attack stat.
    static uint16_t wrongAnswerDamage(uint8_t depth, uint16_t enemyAtk) {
        uint16_t dmg = (uint16_t)(enemyAtk / 4 + depth * 2);
        return (dmg < 5) ? 5u : dmg;
    }

    // Bonus XP awarded when the party answers one question from every category.
    static uint32_t trivialPursuitBonus(uint8_t depth) {
        return 100u + (uint32_t)depth * 25u;
    }

    // Check whether all 5 categories have been answered (bitmask full).
    static bool allCategoriesCovered(uint8_t mask) {
        return (mask & 0x1F) == 0x1F;
    }

    // Return the category bit for a question.
    static uint8_t categoryBit(const TriviaQuestion* q) {
        return (q) ? (uint8_t)(1u << (uint8_t)q->category) : 0u;
    }
};
