#pragma once
#include <Arduino.h>
#include "DungeonModule.h"
#include <string.h>
#include <ctype.h>

// ── Wordle constants ──────────────────────────────────────────────────────────
// Word length is dynamic per session (varies 5-7); buffer holds up to MAX_WORD_LEN.
// Test word "DUNGEON" = 7 letters, so keep the buffer at 8.
static constexpr uint8_t WORDLE_MAX_GUESSES = 6;
static constexpr uint8_t WORDLE_MAX_LEN     = 8;   // max word length + NUL

// ── Tile result per letter ────────────────────────────────────────────────────
enum class WordleTile : uint8_t {
    Unknown = 0,
    Grey    = 1,   // letter absent from word
    Yellow  = 2,   // letter present, wrong position
    Green   = 3,   // letter present, correct position
};

// ── Damage tiers on session end ───────────────────────────────────────────────
// Applied to the party's most-injured Pokemon after the Wordle encounter.
enum class WordleDamageTier : uint8_t {
    None   = 0,  // solved in 1: no penalty
    Spared = 1,  // solved in 2: top-damaged Pokemon spared this tier
    Low    = 2,  // solved in 3: 25% max-HP damage
    Medium = 3,  // solved in 4-5: 50% max-HP damage
    High   = 4,  // solved in 6: 75% max-HP damage
    Severe = 5,  // failed all guesses: faint if ≤50% HP, else 90% HP damage
};

// ── One guess + tile results ──────────────────────────────────────────────────
struct WordleGuess {
    char       word[WORDLE_MAX_LEN];
    WordleTile tiles[WORDLE_MAX_LEN - 1];
};

// ── Per-player session ────────────────────────────────────────────────────────
struct WordleSession {
    char       secretWord[WORDLE_MAX_LEN];
    uint8_t    wordLen;
    uint8_t    guessCount;
    WordleGuess guesses[WORDLE_MAX_GUESSES];
    bool       solved;
    bool       failed;
    uint8_t    hintCount;  // letters pre-revealed from WIS score (0-2)

    // Initialise a new session. wisScore from the trainer's Wisdom stat.
    void init(const char* word, uint8_t wisScore) {
        strncpy(secretWord, word, WORDLE_MAX_LEN - 1);
        secretWord[WORDLE_MAX_LEN - 1] = '\0';
        // Uppercase the secret word for consistent comparison
        for (int i = 0; secretWord[i]; i++)
            secretWord[i] = (char)toupper((uint8_t)secretWord[i]);

        wordLen    = (uint8_t)strlen(secretWord);
        guessCount = 0;
        solved     = false;
        failed     = false;
        memset(guesses, 0, sizeof(guesses));

        // WIS 17+ = 2 hints (first and middle letter revealed)
        // WIS 14-16 = 1 hint (first letter revealed)
        if      (wisScore >= 17) hintCount = 2;
        else if (wisScore >= 14) hintCount = 1;
        else                     hintCount = 0;
    }

    // Return a pre-revealed letter at position pos, or '\0' if not hinted.
    // Hint positions: [0] for first hint, [wordLen/2] for second hint.
    char getHint(uint8_t pos) const {
        if (hintCount >= 1 && pos == 0)             return secretWord[0];
        if (hintCount >= 2 && pos == wordLen / 2)   return secretWord[wordLen / 2];
        return '\0';
    }

    // Submit a guess. Returns false if session over or wrong length.
    bool submitGuess(const char* rawGuess) {
        if (solved || failed || guessCount >= WORDLE_MAX_GUESSES) return false;
        if (!rawGuess) return false;

        // Normalize to uppercase, check length
        char guess[WORDLE_MAX_LEN] = {};
        uint8_t i = 0;
        while (rawGuess[i] && i < wordLen) {
            guess[i] = (char)toupper((uint8_t)rawGuess[i]);
            i++;
        }
        // Skip leading spaces from command-bar paste
        if (i == 0 && rawGuess[0] == ' ') return false;
        if (i != wordLen) return false;

        WordleGuess& wg = guesses[guessCount++];
        strncpy(wg.word, guess, WORDLE_MAX_LEN - 1);
        wg.word[WORDLE_MAX_LEN - 1] = '\0';

        // Standard Wordle tile algorithm:
        // Pass 1 — mark greens and lock those positions
        bool usedGuess[WORDLE_MAX_LEN - 1]  = {};
        bool usedSecret[WORDLE_MAX_LEN - 1] = {};
        for (i = 0; i < wordLen; i++) {
            if (guess[i] == secretWord[i]) {
                wg.tiles[i]    = WordleTile::Green;
                usedGuess[i]   = true;
                usedSecret[i]  = true;
            }
        }
        // Pass 2 — mark yellows and greys for remaining positions
        for (i = 0; i < wordLen; i++) {
            if (usedGuess[i]) continue;
            wg.tiles[i] = WordleTile::Grey;
            for (uint8_t j = 0; j < wordLen; j++) {
                if (!usedSecret[j] && guess[i] == secretWord[j]) {
                    wg.tiles[i]    = WordleTile::Yellow;
                    usedSecret[j]  = true;
                    break;
                }
            }
        }

        // Check win/loss
        bool allGreen = true;
        for (i = 0; i < wordLen; i++) {
            if (wg.tiles[i] != WordleTile::Green) { allGreen = false; break; }
        }
        if (allGreen)                           solved = true;
        else if (guessCount >= WORDLE_MAX_GUESSES) failed = true;
        return true;
    }

    // Damage tier based on how many guesses were used (or failure).
    WordleDamageTier getDamageTier() const {
        if (!solved) return WordleDamageTier::Severe;
        switch (guessCount) {
            case 1:           return WordleDamageTier::None;
            case 2:           return WordleDamageTier::Spared;
            case 3:           return WordleDamageTier::Low;
            case 4: case 5:   return WordleDamageTier::Medium;
            default:          return WordleDamageTier::High;   // 6
        }
    }

    // Compute actual HP damage to deal given a tier and the Pokemon's max HP.
    // Returns 0 for None/Spared (caller handles Spared at the party level).
    static uint16_t tierToHpDamage(WordleDamageTier tier, uint16_t maxHp, uint16_t curHp) {
        switch (tier) {
            case WordleDamageTier::None:   return 0;
            case WordleDamageTier::Spared: return 0;
            case WordleDamageTier::Low:    return (uint16_t)(maxHp / 4);
            case WordleDamageTier::Medium: return (uint16_t)(maxHp / 2);
            case WordleDamageTier::High:   return (uint16_t)(maxHp * 3 / 4);
            case WordleDamageTier::Severe:
                // Faint (return all HP) if ≤50%, else return 90% of max
                return (curHp <= maxHp / 2) ? curHp : (uint16_t)(maxHp * 9 / 10);
            default:                       return 0;
        }
    }

    // Render one guess row as a compact string for the log/TFT overlay.
    // Format example:  "BATTLE [GYG.G.]"
    // Unplayed rows show underscores.
    void formatRow(uint8_t rowIdx, char* out, uint8_t maxLen) const {
        if (rowIdx >= guessCount) {
            // Show blank row with hint letters filled in
            char blank[WORDLE_MAX_LEN] = {};
            for (uint8_t i = 0; i < wordLen; i++) {
                char h = getHint(i);
                blank[i] = h ? h : '_';
            }
            snprintf(out, maxLen, "%.*s [-------]", wordLen, blank);
            return;
        }
        const WordleGuess& g = guesses[rowIdx];
        char tileStr[WORDLE_MAX_LEN] = {};
        for (uint8_t i = 0; i < wordLen; i++) {
            switch (g.tiles[i]) {
                case WordleTile::Green:  tileStr[i] = 'G'; break;
                case WordleTile::Yellow: tileStr[i] = 'Y'; break;
                default:                 tileStr[i] = '.'; break;
            }
        }
        snprintf(out, maxLen, "%.*s [%.*s]", wordLen, g.word, wordLen, tileStr);
    }

    // Summary line: "Solved in 3/6" or "Failed DUNGEON"
    void formatResult(char* out, uint8_t maxLen) const {
        if (solved)
            snprintf(out, maxLen, "Solved in %u/%u!", guessCount, WORDLE_MAX_GUESSES);
        else
            snprintf(out, maxLen, "Failed! Word: %s", secretWord);
    }
};

// ── Hardcoded word bank ───────────────────────────────────────────────────────
// Phase 3 will load from SD; these serve as the embedded fallback.
// Words are dungeon/game themed and vary from 5-7 letters.
static constexpr const char* WORDLE_WORDS[] = {
    "DUNGEON",  // 7 — Phase 1 test word
    "DRAGON",   // 6
    "BATTLE",   // 6
    "WIZARD",   // 6
    "KNIGHT",   // 6
    "SHIELD",   // 6
    "POISON",   // 6
    "FOREST",   // 6
    "CASTLE",   // 6
    "GOBLIN",   // 6
    "MYSTIC",   // 6
    "WYVERN",   // 6
    "CLERIC",   // 6
    "PORTAL",   // 6
    "ROGUE",    // 5
    "FROST",    // 5
    "BLAZE",    // 5
    "CHAOS",    // 5
    "ALTAR",    // 5
    "TORCH",    // 5
};
static constexpr uint8_t WORDLE_WORD_COUNT =
    (uint8_t)(sizeof(WORDLE_WORDS) / sizeof(WORDLE_WORDS[0]));

// Pick a word deterministically from depth + seed.
inline const char* pickWordleWord(uint8_t depth, uint32_t seed) {
    uint32_t rng = (seed ^ ((uint32_t)depth * 0x9e3779b9u)) * 1664525u + 1013904223u;
    return WORDLE_WORDS[rng % WORDLE_WORD_COUNT];
}
