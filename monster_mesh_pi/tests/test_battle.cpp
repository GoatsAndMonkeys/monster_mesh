// test_battle.cpp — Phase 0 standalone Gen1 battle engine test
// No ncurses, no IPC, no serial. Just runs a battle and prints to stdout.
// Build:  cmake .. && make test_battle
// Run:    ./test_battle [seed]

#include "../src/shared/platform.h"
#include "../src/battle/Gen1BattleEngine.h"
#include "../src/battle/showdown_gen1_moves.h"
#include "../src/battle/showdown_gen1_basestats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ── Print helpers ─────────────────────────────────────────────────────────────

static const char *dexName(uint8_t dex) {
    static const char *N[152] = {
        "???","Bulbasaur","Ivysaur","Venusaur","Charmander","Charmeleon",
        "Charizard","Squirtle","Wartortle","Blastoise","Caterpie","Metapod",
        "Butterfree","Weedle","Kakuna","Beedrill","Pidgey","Pidgeotto",
        "Pidgeot","Rattata","Raticate","Spearow","Fearow","Ekans","Arbok",
        "Pikachu","Raichu","Sandshrew","Sandslash","NidoranF","Nidorina",
        "Nidoqueen","NidoranM","Nidorino","Nidoking","Clefairy","Clefable",
        "Vulpix","Ninetales","Jigglypuff","Wigglytuff","Zubat","Golbat",
        "Oddish","Gloom","Vileplume","Paras","Parasect","Venonat","Venomoth",
        "Diglett","Dugtrio","Meowth","Persian","Psyduck","Golduck","Mankey",
        "Primeape","Growlithe","Arcanine","Poliwag","Poliwhirl","Poliwrath",
        "Abra","Kadabra","Alakazam","Machop","Machoke","Machamp","Bellsprout",
        "Weepinbell","Victreebel","Tentacool","Tentacruel","Geodude","Graveler",
        "Golem","Ponyta","Rapidash","Slowpoke","Slowbro","Magnemite","Magneton",
        "Farfetchd","Doduo","Dodrio","Seel","Dewgong","Grimer","Muk",
        "Shellder","Cloyster","Gastly","Haunter","Gengar","Onix","Drowzee",
        "Hypno","Krabby","Kingler","Voltorb","Electrode","Exeggcute","Exeggutor",
        "Cubone","Marowak","Hitmonlee","Hitmonchan","Lickitung","Koffing",
        "Weezing","Rhyhorn","Rhydon","Chansey","Tangela","Kangaskhan",
        "Horsea","Seadra","Goldeen","Seaking","Staryu","Starmie","MrMime",
        "Scyther","Jynx","Electabuzz","Magmar","Pinsir","Tauros","Magikarp",
        "Gyarados","Lapras","Ditto","Eevee","Vaporeon","Jolteon","Flareon",
        "Porygon","Omanyte","Omastar","Kabuto","Kabutops","Aerodactyl",
        "Snorlax","Articuno","Zapdos","Moltres","Dratini","Dragonair",
        "Dragonite","Mewtwo","Mew"
    };
    return dex < 152 ? N[dex] : "???";
}

static void printBar(uint16_t hp, uint16_t maxHp, int width = 20) {
    if (maxHp == 0) { printf("[--------------------]"); return; }
    int filled = (int)((long)hp * width / maxHp);
    printf("[");
    for (int i = 0; i < width; i++) printf(i < filled ? "#" : ".");
    printf("] %d/%d", hp, maxHp);
}

static void logLine(const char *line, void *) {
    printf("  %s\n", line);
}

// ── Build a simple 1-mon party from base stats ────────────────────────────────

static Gen1Party makeParty(uint8_t dex, uint8_t level,
                            uint8_t m0, uint8_t m1, uint8_t m2, uint8_t m3,
                            const char *nick) {
    Gen1Party p = {};
    p.count = 1;
    p.species[0] = dex;
    p.species[1] = 0xFF;

    // Fill a Gen1Pokemon from base stats (minimal — engine re-derives stats)
    Gen1BattleEngine::BattlePoke tmp;
    uint8_t moves[4] = {m0, m1, m2, m3};
    Gen1BattleEngine::initBattlePokeFromBase(tmp, dex, level, moves);

    // Copy stats back into Gen1Pokemon wire format
    p.mons[0].species   = dex;
    p.mons[0].level     = level;
    p.mons[0].boxLevel  = level;
    p.mons[0].moves[0]  = m0;
    p.mons[0].moves[1]  = m1;
    p.mons[0].moves[2]  = m2;
    p.mons[0].moves[3]  = m3;
    // Set max HP / current HP in BE format
    auto setBE = [](uint8_t *b, uint16_t v){ b[0]=(v>>8)&0xFF; b[1]=v&0xFF; };
    setBE(p.mons[0].maxHp, tmp.maxHp);
    setBE(p.mons[0].hp,    tmp.maxHp);
    setBE(p.mons[0].atk,   tmp.atk);
    setBE(p.mons[0].def,   tmp.def);
    setBE(p.mons[0].spd,   tmp.spd);
    setBE(p.mons[0].spc,   tmp.spc);
    // PP (from move table)
    for (int i = 0; i < 4; i++) {
        const Gen1MoveData *md = gen1Move(moves[i]);
        p.mons[0].pp[i] = md ? md->pp : 0;
    }
    // Nickname
    memset(p.nicknames[0], 0x50, 11);
    if (nick) {
        for (int i = 0; nick[i] && i < 10; i++) {
            char c = nick[i];
            if (c >= 'A' && c <= 'Z')      p.nicknames[0][i] = 0x80 + (c - 'A');
            else if (c >= 'a' && c <= 'z') p.nicknames[0][i] = 0xA0 + (c - 'a');
            else                           p.nicknames[0][i] = 0x7F;  // space
        }
    }

    return p;
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    uint32_t seed = (uint32_t)time(nullptr);
    if (argc > 1) seed = (uint32_t)strtoul(argv[1], nullptr, 0);
    printf("=== MonsterMesh Gen1 Battle Test (seed=0x%08X) ===\n\n", seed);

    // Pikachu Lv30: ThunderShock, Thunder Wave, Quick Attack, Tail Whip
    // Move IDs: ThunderShock=84, ThunderWave=86, QuickAttack=98, TailWhip=39
    Gen1Party p1 = makeParty(25, 30, 84, 86, 98, 39, "PIKACHU");

    // Charmander Lv28: Scratch, Ember, Growl, Leer
    // Move IDs: Scratch=10, Ember=52, Growl=45, Leer=43
    Gen1Party p2 = makeParty(4, 28, 10, 52, 45, 43, "CHARMANDR");

    Gen1BattleEngine engine;
    engine.start(p1, p2, seed);

    const auto &ep1 = engine.party(0);
    const auto &ep2 = engine.party(1);

    printf("P1: %s Lv%d  HP:%d  ATK:%d  SPD:%d\n",
           ep1.mons[0].nickname, ep1.mons[0].level,
           ep1.mons[0].maxHp, ep1.mons[0].atk, ep1.mons[0].spd);
    printf("P2: %s Lv%d  HP:%d  ATK:%d  SPD:%d\n\n",
           ep2.mons[0].nickname, ep2.mons[0].level,
           ep2.mons[0].maxHp, ep2.mons[0].atk, ep2.mons[0].spd);

    int turn = 0;
    while (engine.result() == Gen1BattleEngine::Result::ONGOING && turn < 50) {
        turn++;

        const auto &ap1 = engine.party(0);
        const auto &ap2 = engine.party(1);
        const auto &m1  = ap1.mons[ap1.active];
        const auto &m2  = ap2.mons[ap2.active];

        printf("-- Turn %d --\n", turn);
        printf("  %-12s ", m1.nickname); printBar(m1.hp, m1.maxHp); printf("\n");
        printf("  %-12s ", m2.nickname); printBar(m2.hp, m2.maxHp); printf("\n");

        // P1: always use move 0 (or move 1 if 0 has 0 PP)
        uint8_t p1move = (m1.pp[0] > 0) ? 0 : 1;
        engine.submitAction(0, 0, p1move);

        // P2: CPU picks
        uint8_t cpuAction, cpuIndex;
        engine.cpuPickAction(1, cpuAction, cpuIndex);
        engine.submitAction(1, cpuAction, cpuIndex);

        engine.executeTurn(logLine, nullptr);
        engine.autoReplaceIfFainted(0, logLine, nullptr);
        engine.autoReplaceIfFainted(1, logLine, nullptr);
        printf("\n");
    }

    // Final HP bars
    const auto &fp1 = engine.party(0);
    const auto &fp2 = engine.party(1);
    printf("=== Result after %d turns ===\n", turn);
    printf("  %-12s ", fp1.mons[fp1.active].nickname);
    printBar(fp1.mons[fp1.active].hp, fp1.mons[fp1.active].maxHp); printf("\n");
    printf("  %-12s ", fp2.mons[fp2.active].nickname);
    printBar(fp2.mons[fp2.active].hp, fp2.mons[fp2.active].maxHp); printf("\n\n");

    auto r = engine.result();
    if      (r == Gen1BattleEngine::Result::P1_WIN) printf("Winner: PIKACHU\n");
    else if (r == Gen1BattleEngine::Result::P2_WIN) printf("Winner: CHARMANDER\n");
    else if (r == Gen1BattleEngine::Result::DRAW)   printf("Draw!\n");
    else                                             printf("Battle stopped at turn limit\n");

    return (r != Gen1BattleEngine::Result::ONGOING) ? 0 : 1;
}
