// ── Pokemon Daycare Event Generator ──────────────────────────────────────────
// NOT wired into build yet — standalone for validation.

#include "DaycareEventGen.h"
#include "DaycareData.h"   // auto-generated species/move tables
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// ── RNG (simple xorshift32, seeded from millis) ─────────────────────────────

static uint32_t rngState = 1;

static void rngSeed(uint32_t s) { rngState = s ? s : 1; }

static uint32_t rngNext() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

static uint8_t rngRange(uint8_t max) {
    return (max == 0) ? 0 : (rngNext() % max);
}

static bool rngChance(uint8_t percentChance) {
    return (rngNext() % 100) < percentChance;
}

// ── Type affinity helpers ───────────────────────────────────────────────────

// Eevee family: Eevee(133), Vaporeon(134), Jolteon(135), Flareon(136)
static bool isEeveeFamily(uint8_t dex) {
    return dex >= 133 && dex <= 136;
}

// Check if two Pokemon have type affinity (same type OR Eevee family)
static bool hasTypeAffinity(uint8_t dex1, uint8_t dex2) {
    // Eevee family always has affinity with each other
    if (isEeveeFamily(dex1) && isEeveeFamily(dex2)) return true;

    const DaycareSpecies *sp1 = daycareGetSpecies(dex1);
    const DaycareSpecies *sp2 = daycareGetSpecies(dex2);
    if (!sp1 || !sp2) return false;

    // Share any type
    return sp1->type1 == sp2->type1 || sp1->type1 == sp2->type2 ||
           sp1->type2 == sp2->type1 || sp1->type2 == sp2->type2;
}

// Format: "TRAINER's Species, NICK" or "TRAINER's Species" (no nick)
// Uses static buffers — call one at a time, copy result if you need both simultaneously.
static char _nameBuf1[48];
static char _nameBuf2[48];

// Format local Pokemon name: "Your Species" or "Your Species, NICK"
static const char *fmtLocalName(const DaycarePokemonState &pkmn) {
    const char *species = (pkmn.speciesDex >= 1 && pkmn.speciesDex <= DAYCARE_SPECIES_COUNT)
                          ? daycareSpeciesNames[pkmn.speciesDex] : "Pokemon";
    if (pkmn.nickname[0] != '\0') {
        snprintf(_nameBuf1, sizeof(_nameBuf1), "%s, %s", species, pkmn.nickname);
    } else {
        snprintf(_nameBuf1, sizeof(_nameBuf1), "%s", species);
    }
    return _nameBuf1;
}

// Format trainer tag: "SHORT-GAME" or just "SHORT" if no game name
static const char *fmtTrainerTag(const char *shortName, const char *gameName) {
    static char _tagBuf[14]; // "XXXX-XXXXXXX\0"
    if (gameName[0] != '\0') {
        snprintf(_tagBuf, sizeof(_tagBuf), "%s-%s", shortName, gameName);
    } else {
        snprintf(_tagBuf, sizeof(_tagBuf), "%s", shortName);
    }
    return _tagBuf;
}

// Format neighbor Pokemon name: "SHORT-GAME's Species, NICK" or "SHORT-GAME's Species"
static const char *fmtNeighborName(const DaycareNeighborPokemon &n) {
    const char *tag = fmtTrainerTag(n.shortName, n.gameName);
    const char *species = (n.speciesDex >= 1 && n.speciesDex <= DAYCARE_SPECIES_COUNT)
                          ? daycareSpeciesNames[n.speciesDex] : "Pokemon";
    if (n.nickname[0] != '\0') {
        snprintf(_nameBuf2, sizeof(_nameBuf2), "%s's %s, %s", tag, species, n.nickname);
    } else {
        snprintf(_nameBuf2, sizeof(_nameBuf2), "%s's %s", tag, species);
    }
    return _nameBuf2;
}

// Simple species name for solo events (backward compat with flat templates that use %s)
static const char *getDisplayName(const DaycarePokemonState &pkmn) {
    return fmtLocalName(pkmn);
}

static const char *getNeighborDisplayName(const DaycareNeighborPokemon &n) {
    const char *species = (n.speciesDex >= 1 && n.speciesDex <= DAYCARE_SPECIES_COUNT)
                          ? daycareSpeciesNames[n.speciesDex] : "a Pokemon";
    if (n.nickname[0] != '\0') {
        snprintf(_nameBuf2, sizeof(_nameBuf2), "%s, %s", species, n.nickname);
    } else {
        snprintf(_nameBuf2, sizeof(_nameBuf2), "%s", species);
    }
    return _nameBuf2;
}

// Pick a neighbor, weighted toward same-type affinity with the given local Pokemon
static uint8_t pickNeighbor(uint8_t localDex,
                            const DaycareNeighborPokemon *neighbors, uint8_t count) {
    if (count <= 1) return 0;

    // Weight: 3x for type affinity, 1x for others
    uint16_t weights[16];
    uint16_t total = 0;
    for (uint8_t i = 0; i < count && i < 16; i++) {
        weights[i] = hasTypeAffinity(localDex, neighbors[i].speciesDex) ? 3 : 1;
        total += weights[i];
    }

    uint16_t roll = rngNext() % total;
    uint16_t acc = 0;
    for (uint8_t i = 0; i < count; i++) {
        acc += weights[i];
        if (roll < acc) return i;
    }
    return 0;
}

// ── Compositional slot pools ─────────────────────────────────────────────────

// Actions — move practice
static const char *actionsMove[] = {
    "practiced %s",
    "tried %s",
    "showed off %s",
    "failed at %s",
    "perfected %s",
    "accidentally used %s",
    "experimented with %s",
    "drilled %s",
};
static constexpr uint8_t ACTIONS_MOVE_COUNT = sizeof(actionsMove) / sizeof(actionsMove[0]);

// Actions — social
static const char *actionsSocial[] = {
    "played with",
    "chased",
    "wrestled",
    "raced",
    "shared a berry with",
    "hid from",
    "startled",
    "groomed",
    "napped beside",
    "sparred with",
};
static constexpr uint8_t ACTIONS_SOCIAL_COUNT = sizeof(actionsSocial) / sizeof(actionsSocial[0]);

// Actions — explore
static const char *actionsExplore[] = {
    "discovered",
    "investigated",
    "dug up",
    "stumbled upon",
    "followed a trail to",
    "sniffed out",
    "climbed up to",
};
static constexpr uint8_t ACTIONS_EXPLORE_COUNT = sizeof(actionsExplore) / sizeof(actionsExplore[0]);

// Actions — rest (no trailing preposition — location provides it)
static const char *actionsRest[] = {
    "napped",
    "dozed off",
    "curled up",
    "sprawled out",
    "settled down",
    "found a cozy spot",
};
static constexpr uint8_t ACTIONS_REST_COUNT = sizeof(actionsRest) / sizeof(actionsRest[0]);

// Targets — move practice
static const char *targetsMove[] = {
    "on a tree stump",
    "on a boulder",
    "on a leaf pile",
    "on the ground",
    "on thin air",
    "on a passing Pidgey",
    "on its own shadow",
    "on the pond surface",
    "on a fallen log",
    "on a dirt mound",
};
static constexpr uint8_t TARGETS_MOVE_COUNT = sizeof(targetsMove) / sizeof(targetsMove[0]);

// Targets — found objects
static const char *targetsFound[] = {
    "a shiny pebble",
    "an old berry",
    "a weird mushroom",
    "a lost coin",
    "a feather",
    "a smooth stone",
    "a buried bone",
    "a pretty flower",
    "a strange seed",
    "a piece of driftwood",
    "a sparkling crystal",
    "a round acorn",
};
static constexpr uint8_t TARGETS_FOUND_COUNT = sizeof(targetsFound) / sizeof(targetsFound[0]);

// Locations
static const char *locations[] = {
    "by the pond",
    "in the meadow",
    "near the big oak",
    "on the hilltop",
    "in the tall grass",
    "under the berry tree",
    "behind the rocks",
    "along the stream",
    "at the edge of the clearing",
    "in a sunny patch",
    "in the shade",
    "by the fence",
    "near the den",
    "on a warm rock",
    "under the stars",
};
static constexpr uint8_t LOCATIONS_COUNT = sizeof(locations) / sizeof(locations[0]);

// Outcomes — generic
static const char *outcomesGeneric[] = {
    "and nailed it.",
    "and missed completely.",
    "and scared everyone nearby.",
    "and fell asleep after.",
    "and seemed proud.",
    "but nothing happened.",
    "and did it again.",
    "and walked away satisfied.",
    "and looked confused.",
    "and got distracted halfway through.",
};
static constexpr uint8_t OUTCOMES_GENERIC_COUNT = sizeof(outcomesGeneric) / sizeof(outcomesGeneric[0]);

// Outcomes — personality-weighted
static const char *outcomesProud[] = {
    "and looked extremely pleased with itself.",
    "and roared triumphantly.",
    "and waited for applause.",
    "and expected everyone to be watching.",
};
static const char *outcomesClumsy[] = {
    "and tripped over its own feet.",
    "then forgot what it was doing.",
    "and somehow made it worse.",
    "but got lucky anyway.",
};
static const char *outcomesAggressive[] = {
    "and broke it.",
    "and challenged it to a rematch.",
    "and punched a tree for good measure.",
    "then picked a fight with a rock.",
};
static const char *outcomesGentle[] = {
    "and helped clean up after.",
    "and checked if everyone was okay.",
    "and offered to share the result.",
    "and quietly went back to the group.",
};
static const char *outcomesSneaky[] = {
    "and vanished before anyone noticed.",
    "and blamed someone else.",
    "and stashed the evidence.",
    "and grinned.",
};
static const char *outcomesLazy[] = {
    "then immediately fell asleep.",
    "and decided that was enough for the day.",
    "and yawned.",
    "then didn't move for three hours.",
};
static const char *outcomesAnxious[] = {
    "and looked around nervously.",
    "and ran back to a safe spot.",
    "but seemed a little braver than yesterday.",
    "and hid behind the nearest rock.",
};

static constexpr uint8_t OUTCOMES_PERSONALITY_COUNT = 4;  // each pool has 4

static const char **personalityOutcomes[] = {
    outcomesProud,      // PERS_PROUD
    outcomesClumsy,     // PERS_CLUMSY
    outcomesAggressive, // PERS_AGGRESSIVE
    outcomesGentle,     // PERS_GENTLE
    outcomesSneaky,     // PERS_SNEAKY
    outcomesLazy,       // PERS_LAZY
    outcomesAnxious,    // PERS_ANXIOUS
};

// ── Dream templates ──────────────────────────────────────────────────────────

static const char *dreamTemplates[] = {
    "Your %s dreamed about evolving.",
    "Your %s had a peaceful dream about a sunny field.",
    "Your %s dreamed about running through a thunderstorm.",
    "Your %s dreamed about an endless buffet. It smiled in its sleep.",
    "Your %s dreamed about flying over a waterfall.",
    "Your %s dreamed about all the things it could become.",
    "Your %s dreamed about the shadows. It seemed happy.",
    "Your %s had a dream about its trainer. Its tail wagged in its sleep.",
    "Your %s dreamed about swimming in an ocean of stars.",
    "Your %s had a restless dream. It muttered in its sleep.",
    "Your %s dreamed about winning a great battle.",
    "Your %s dreamed about a warm campfire and good friends.",
    "Your %s twitched in its sleep. Chasing something in a dream.",
    "Your %s dreamed about discovering a hidden cave.",
    "Your %s had a nightmare but then something comforted it in the dream.",
};
static constexpr uint8_t DREAM_COUNT = sizeof(dreamTemplates) / sizeof(dreamTemplates[0]);

// Level-up foreshadowing dreams
static const char *foreshadowDreams[] = {
    "Your %s's body glowed faintly in its dream. Something is changing.",
    "Your %s dreamed about being stronger. It might be close to a breakthrough.",
    "Your %s dreamed it could do things it couldn't before.",
};
static constexpr uint8_t FORESHADOW_COUNT = sizeof(foreshadowDreams) / sizeof(foreshadowDreams[0]);

// ── Visitor templates ────────────────────────────────────────────────────────

static const char *visitorWelcomeTemplates[] = {
    "A new trainer arrived! Your %s ran over to say hello.",
    "Your %s is curious about the new Pokemon in the daycare!",
    "Your %s perked up — new friends!",
    "A new trainer joined the mesh! Your %s sniffed the air excitedly.",
    "Your %s noticed unfamiliar Pokemon and went to investigate.",
};
static constexpr uint8_t VISITOR_WELCOME_COUNT = sizeof(visitorWelcomeTemplates) / sizeof(visitorWelcomeTemplates[0]);

// ── Escape templates (by type affinity) ──────────────────────────────────────

static const char *escapeFlying[] = {
    "Your %s flew over the fence and explored the hills. It came back at sunset.",
    "Your %s soared above the daycare for a while. It came back looking refreshed.",
};
static const char *escapeDigger[] = {
    "Your %s tunneled under the fence. It came back covered in mud and looking proud.",
    "Your %s dug an escape tunnel but came back before anyone noticed.",
};
static const char *escapeGhost[] = {
    "Your %s phased through the wall and scared some wild Rattata outside. It came back laughing.",
    "Your %s slipped into the shadows and disappeared for an hour. It came back grinning.",
};
static const char *escapeWater[] = {
    "Your %s followed the stream past the boundary. It floated back an hour later.",
    "Your %s swam downstream and explored. It came back smelling like the river.",
};
static const char *escapeSmall[] = {
    "Your %s squeezed through a gap in the fence. It came back with a leaf in its fur.",
    "Your %s found a hole in the fence and went on an adventure. It came back safe.",
};

// ── Signature flat templates (Layer 1) ───────────────────────────────────────
// Species Dex -> specific message. These fire first when the species is picked.

struct FlatTemplate {
    uint8_t speciesDex;  // 0 = any species
    const char *message;
    bool hasXp;
    uint8_t xp;
};

static const FlatTemplate flatTemplates[] = {
    // Snorlax (143)
    {143, "Your Snorlax slept for 14 hours. It briefly woke up, ate everything, and went back to sleep.", false, 0},
    {143, "Your Snorlax is blocking the path again. Nobody can get through.", false, 0},
    {143, "Your Snorlax yawned so hard it created a small breeze.", false, 0},

    // Pikachu (25)
    {25, "Your Pikachu refused to go in its Poke Ball again. It's sitting on Snorlax.", false, 0},
    {25, "Your Pikachu found a bottle of ketchup. It won't share.", false, 0},
    {25, "Your Pikachu thunderbolted a tree stump. Just because.", true, 15},

    // Magikarp (129)
    {129, "Your Magikarp used Splash. Nothing happened. Magikarp used Splash again.", false, 0},
    {129, "Your Magikarp flopped around for a while. It's trying its best.", false, 0},
    {129, "Your Magikarp splashed with incredible determination.", true, 5},

    // Jigglypuff (39)
    {39, "Your Jigglypuff gave a concert. Everyone fell asleep. Everyone has marker on their face.", false, 0},
    {39, "Your Jigglypuff sang to the clouds. They didn't fall asleep. Finally, an appreciative audience.", false, 0},

    // Psyduck (54)
    {54, "Your Psyduck stared at a wall for 3 hours. Then accidentally Psychic'd Geodude into the air.", true, 20},
    {54, "Your Psyduck's headache got worse. Then it got better. Nobody knows why.", false, 0},

    // Charizard (6)
    {6, "Your Charizard ignored everyone. Except Dragonite — it challenged Dragonite to a fight.", true, 20},
    {6, "Your Charizard perched on the highest rock and surveyed everything below.", false, 0},

    // Gengar (94)
    {94, "Your Gengar hid in someone's shadow and scared them. Then it laughed for 20 minutes.", true, 10},
    {94, "Your Gengar tried to be scary but everyone was used to it by now. It looked disappointed.", false, 0},

    // Cubone (104)
    {104, "Your Cubone was alone by the pond. It stared at the moon for a long time.", false, 0},
    {104, "Your Cubone found a stick and pretended it was a bone club. It practiced swinging.", true, 10},

    // Mr. Mime (122)
    {122, "Your Mr. Mime built an invisible wall across the path. Three Pokemon walked into it.", false, 0},
    {122, "Your Mr. Mime mimed a battle. It was surprisingly intense.", true, 15},

    // Muk (89)
    {89, "Your Muk tried to hug someone. They ran. Muk doesn't understand why.", false, 0},
    {89, "Your Muk oozed into a comfortable puddle shape. It seems content.", false, 0},

    // Eevee (133)
    {133, "Your Eevee can't decide what to do. It started three activities and finished none.", false, 0},
    {133, "Your Eevee sat with everyone in the daycare today. One at a time.", true, 10},

    // Bulbasaur (1)
    {1, "Your Bulbasaur tended to a wilting flower. It perked right up.", true, 10},
    {1, "Your Bulbasaur settled a fight between two other Pokemon. Natural peacekeeper.", true, 15},

    // Squirtle (7)
    {7, "Your Squirtle organized a water gun fight. It won.", true, 15},
    {7, "Your Squirtle put on sunglasses it found somewhere. It looks cool.", false, 0},

    // Charmander (4)
    {4, "Your Charmander is guarding its tail flame from the wind. Brave little guy.", false, 0},
    {4, "Your Charmander practiced Ember until the tree stump caught fire. Oops.", true, 15},

    // Mewtwo (150)
    {150, "Your Mewtwo sat alone on the hilltop. It was thinking about the nature of existence.", false, 0},
    {150, "Your Mewtwo levitated every rock in the clearing. Just to see if it could.", true, 25},

    // Slowpoke (79)
    {79, "Your Slowpoke realized something funny from three days ago. It laughed.", false, 0},
    {79, "Your Slowpoke stared at the pond for 6 hours. It blinked once.", false, 0},

    // Ditto (132)
    {132, "Your Ditto transformed into a rock. Nobody noticed for hours.", false, 0},
    {132, "Your Ditto copied someone's appearance. There was briefly a lot of confusion.", false, 0},

    // Dragonite (149)
    {149, "Your Dragonite gave smaller Pokemon rides on its back. Everyone had a great time.", true, 15},
    {149, "Your Dragonite delivered a berry to every Pokemon in the daycare. Mail carrier instincts.", true, 10},

    // Lapras (131)
    {131, "Your Lapras sang across the pond. The other Pokemon stopped to listen.", true, 10},
    {131, "Your Lapras gave a swimming lesson. Only the Water types passed.", true, 15},

    // Primeape (57)
    {57, "Your Primeape punched a tree. The tree lost.", true, 15},
    {57, "Your Primeape got angry at a cloud. It's still angry.", false, 0},

    // Gyarados (130)
    {130, "Your Gyarados surfaced in the pond and everyone backed away slowly.", false, 0},
    {130, "Your Gyarados roared and the ripples reached the far shore.", true, 20},

    // Haunter (93)
    {93, "Your Haunter licked someone. They're paralyzed but it's wearing off.", false, 0},
    {93, "Your Haunter made funny faces until someone laughed. Mission accomplished.", true, 10},

    // Abra (63)
    {63, "Your Abra teleported in its sleep. It's now napping in a tree.", false, 0},
    {63, "Your Abra woke up for 30 seconds. Then teleported back to its napping spot.", false, 0},

    // Machamp (68)
    {68, "Your Machamp set up an obstacle course for the other Pokemon.", true, 15},
    {68, "Your Machamp carried a boulder just to prove it could.", true, 10},

    // Chansey (113)
    {113, "Your Chansey bandaged a scraped knee on another Pokemon. Nurse on duty.", true, 10},
    {113, "Your Chansey offered its egg to someone who looked hungry.", false, 0},

    // Koffing (109)
    {109, "Your Koffing floated around happily. It doesn't know why the others keep their distance.", false, 0},
    {109, "Your Koffing sat with Muk. They had a lovely time. Nobody else joined.", false, 0},
};
static constexpr uint8_t FLAT_TEMPLATE_COUNT = sizeof(flatTemplates) / sizeof(flatTemplates[0]);

// ── Helper: format message ───────────────────────────────────────────────────

void DaycareEventGen::formatMessage(char *buf, size_t bufSize, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, bufSize, fmt, args);
    va_end(args);
}

// ── Helper: pick random pokemon from party ───────────────────────────────────

uint8_t DaycareEventGen::pickPokemon(uint8_t partyCount) {
    return rngRange(partyCount);
}

// ── Helper: calculate XP with modifiers ──────────────────────────────────────

uint16_t DaycareEventGen::calcXp(uint8_t baseXp, uint8_t friendship,
                                  uint8_t rivalry, uint8_t weatherMult100,
                                  bool isMentor) {
    uint32_t xp = baseXp;

    // Friendship multiplier
    xp = (xp * friendshipXpMultiplier100(friendship)) / 100;

    // Rivalry bonus (flat add)
    xp += rivalryXpBonus(rivalry);

    // Weather multiplier
    xp = (xp * weatherMult100) / 100;

    // Mentor bonus
    if (isMentor) xp = (xp * 150) / 100;

    // Cap at 200 per event
    if (xp > 200) xp = 200;

    return (uint16_t)xp;
}

// ── Layer 1: Flat template check ─────────────────────────────────────────────

bool DaycareEventGen::tryFlatTemplate(
    DaycareEvent &out,
    const DaycarePokemonState *localParty, uint8_t localPartyCount,
    const DaycareNeighborPokemon *neighbors, uint8_t neighborCount,
    DaycareState &state, DaycareWeatherType weather, bool isNight)
{
    // 30% chance to try a flat template (keeps them special)
    if (!rngChance(30)) return false;

    uint8_t idx = pickPokemon(localPartyCount);
    uint8_t dex = localParty[idx].speciesDex;

    // Collect matching templates for this species
    uint8_t matches[FLAT_TEMPLATE_COUNT];
    uint8_t matchCount = 0;
    for (uint8_t i = 0; i < FLAT_TEMPLATE_COUNT && matchCount < 16; i++) {
        if (flatTemplates[i].speciesDex == dex) {
            matches[matchCount++] = i;
        }
    }
    if (matchCount == 0) return false;

    const FlatTemplate &tmpl = flatTemplates[matches[rngRange(matchCount)]];

    const char *name = getDisplayName(localParty[idx]);

    // Check if template has %s (for species name)
    if (strstr(tmpl.message, "%s")) {
        snprintf(out.message, sizeof(out.message), tmpl.message, name);
    } else {
        strncpy(out.message, tmpl.message, sizeof(out.message) - 1);
        out.message[sizeof(out.message) - 1] = '\0';
    }

    out.targetSpeciesIdx = idx;
    out.targetNodeId = 0;
    out.isBroadcast = false;
    out.rarity = RARITY_COMMON;

    if (tmpl.hasXp) {
        out.xp = tmpl.xp;
    } else if (rngChance(50)) {
        // Even "flavor" species moments can teach something
        out.xp = 5 + rngRange(8);  // 5-12
    } else {
        out.xp = 0;
    }

    if (out.xp > 0) {
        char xpTag[20];
        snprintf(xpTag, sizeof(xpTag), " (+%d XP)", out.xp);
        size_t msgLen = strlen(out.message);
        if (msgLen + strlen(xpTag) < sizeof(out.message)) {
            strcat(out.message, xpTag);
        }
    }

    // Track Magikarp splashes
    if (dex == 129) {
        state.pokemon[idx].splashCount++;
    }

    return true;
}

// ── Layer 2: Compositional generation ────────────────────────────────────────

void DaycareEventGen::generateCompositional(
    DaycareEvent &out,
    const DaycarePokemonState *localParty, uint8_t localPartyCount,
    const DaycareNeighborPokemon *neighbors, uint8_t neighborCount,
    DaycareState &state, DaycareWeatherType weather, bool isNight)
{
    uint8_t idx = pickPokemon(localPartyCount);
    uint8_t dex = localParty[idx].speciesDex;
    const DaycareSpecies *sp = daycareGetSpecies(dex);
    if (!sp) {
        snprintf(out.message, sizeof(out.message), "Your Pokemon had a quiet day.");
        out.xp = 0;
        out.targetSpeciesIdx = idx;
        out.targetNodeId = 0;
        return;
    }

    const char *name = getDisplayName(localParty[idx]);
    const TypeBehavior &tb = daycareTypeBehaviors[sp->type1];

    // Roll event category based on type behavior weights
    // Boost social weight 3x when neighbors are present
    uint16_t socialW = (neighborCount > 0) ? (uint16_t)(tb.social * 3) : 0;
    uint16_t totalWeight = socialW + tb.combat + tb.explore + tb.rest + tb.mischief;
    uint16_t roll = rngNext() % totalWeight;
    DaycareEventCategory cat;

    if (roll < socialW && neighborCount > 0) {
        cat = EVCAT_SOCIAL;
    } else if (roll < socialW + tb.combat) {
        cat = EVCAT_COMBAT;
    } else if (roll < socialW + tb.combat + tb.explore) {
        cat = EVCAT_EXPLORE;
    } else if (roll < socialW + tb.combat + tb.explore + tb.rest) {
        cat = EVCAT_REST;
    } else {
        cat = EVCAT_MISCHIEF;
    }

    // If no neighbors but rolled social, switch to explore
    if (cat == EVCAT_SOCIAL && neighborCount == 0) cat = EVCAT_EXPLORE;

    char msgBuf[200];
    bool giveXp = rngChance(67);  // 67% of events give XP
    uint8_t baseXp = 0;
    uint8_t friendship = 0;
    uint8_t rivalry = 0;
    uint8_t weatherMult = 100;
    bool isMentor = false;

    const char *loc = locations[rngRange(LOCATIONS_COUNT)];

    if (weather != WEATHER_NONE) {
        weatherMult = weatherTypeMultiplier100(weather, sp->type1);
    }

    switch (cat) {
        case EVCAT_COMBAT: {
            // Move practice
            if (sp->moveCount > 0) {
                uint8_t moveIdx = rngRange(sp->moveCount);
                uint8_t moveId = sp->moves[moveIdx];
                const char *moveName = (moveId > 0 && moveId <= DAYCARE_MOVE_COUNT)
                                       ? daycareMoveNames[moveId] : "a move";
                const char *actionFmt = actionsMove[rngRange(ACTIONS_MOVE_COUNT)];
                const char *target = targetsMove[rngRange(TARGETS_MOVE_COUNT)];

                char actionBuf[64];
                snprintf(actionBuf, sizeof(actionBuf), actionFmt, moveName);

                snprintf(msgBuf, sizeof(msgBuf), "Your %s %s %s %s",
                         name, actionBuf, target, loc);
            } else {
                snprintf(msgBuf, sizeof(msgBuf), "Your %s trained %s", name, loc);
            }
            baseXp = 10 + rngRange(11);  // 10-20
            break;
        }

        case EVCAT_SOCIAL: {
            // Pick a neighbor — weighted toward same type / Eevee family
            uint8_t ni = pickNeighbor(dex, neighbors, neighborCount);
            const DaycareNeighborPokemon &neighbor = neighbors[ni];
            const char *otherFull = fmtNeighborName(neighbor);
            const char *action = actionsSocial[rngRange(ACTIONS_SOCIAL_COUNT)];

            snprintf(msgBuf, sizeof(msgBuf), "Your %s %s %s %s",
                     name, action, otherFull, loc);

            // Look up relationship
            DaycareRelationship *rel = getOrCreateRelationship(
                state, neighbor.nodeId, idx, neighbor.speciesDex);
            if (rel) {
                friendship = rel->friendship;
                rivalry = rel->rivalry;

                // Build friendship — type affinity gets a BIG boost
                bool affinity = hasTypeAffinity(dex, neighbor.speciesDex);
                uint8_t gain = affinity ? 7 : 3;  // 7 for same type/Eevee family, 3 for different

                // Mentoring bonus on top of affinity (10+ level gap)
                if (affinity) {
                    int levelDiff = abs((int)localParty[idx].totalHours - (int)neighbor.level);
                    if (levelDiff >= 10) {
                        gain = 10;
                        isMentor = true;
                    }
                }
                if (rel->friendship <= 255 - gain) rel->friendship += gain;
                else rel->friendship = 255;

                // Sparring actions build rivalry
                if (strcmp(action, "sparred with") == 0 ||
                    strcmp(action, "raced") == 0 ||
                    strcmp(action, "wrestled") == 0) {
                    uint8_t rivalGain = 5;
                    if (rel->rivalry <= 255 - rivalGain) rel->rivalry += rivalGain;
                    else rel->rivalry = 255;
                    rel->sparCount++;
                }

                rel->lastSeenMs = millis();
            }

            // Append friendship/rivalry score + delta tag
            if (rel) {
                uint8_t newF = rel->friendship;
                uint8_t newR = rel->rivalry;
                uint8_t dF = newF - friendship;
                uint8_t dR = newR - rivalry;
                char tierTag[60] = {};
                if (dF > 0 && dR > 0)
                    snprintf(tierTag, sizeof(tierTag), " [Friendship %d +%d, Rivalry %d +%d]", newF, dF, newR, dR);
                else if (dF > 0)
                    snprintf(tierTag, sizeof(tierTag), " [Friendship %d +%d]", newF, dF);
                else if (dR > 0)
                    snprintf(tierTag, sizeof(tierTag), " [Rivalry %d +%d]", newR, dR);
                if (tierTag[0]) {
                    size_t ml = strlen(msgBuf);
                    if (ml + strlen(tierTag) < sizeof(msgBuf))
                        strcat(msgBuf, tierTag);
                }
            }

            out.targetNodeId = neighbor.nodeId;
            baseXp = 15 + rngRange(16);  // 15-30
            break;
        }

        case EVCAT_EXPLORE: {
            const char *action = actionsExplore[rngRange(ACTIONS_EXPLORE_COUNT)];
            const char *found = targetsFound[rngRange(TARGETS_FOUND_COUNT)];
            snprintf(msgBuf, sizeof(msgBuf), "Your %s %s %s %s", name, action, found, loc);
            baseXp = 10 + rngRange(16);  // 10-25
            state.pokemon[idx].exploreCount++;
            break;
        }

        case EVCAT_REST: {
            const char *action = actionsRest[rngRange(ACTIONS_REST_COUNT)];
            snprintf(msgBuf, sizeof(msgBuf), "Your %s %s %s", name, action, loc);
            giveXp = false;  // rest = no XP, just flavor
            break;
        }

        case EVCAT_MISCHIEF: {
            // Mischief templates — personality-driven
            static const char *mischiefTemplates[] = {
                "Your %s stole a berry from the pile and hid it.",
                "Your %s startled a sleeping Pokemon and ran away giggling.",
                "Your %s knocked over a pile of rocks just to see what would happen.",
                "Your %s pretended to be asleep and then jumped out at someone.",
                "Your %s dug a hole in the path. Someone's going to trip.",
                "Your %s hid someone's favorite napping spot.",
            };
            static constexpr uint8_t MISCHIEF_COUNT = sizeof(mischiefTemplates) / sizeof(mischiefTemplates[0]);
            snprintf(msgBuf, sizeof(msgBuf), mischiefTemplates[rngRange(MISCHIEF_COUNT)], name);
            giveXp = false;  // mischief = no XP, just flavor
            break;
        }

        default:
            snprintf(msgBuf, sizeof(msgBuf), "Your %s had a quiet moment %s", name, loc);
            giveXp = false;
            break;
    }

    // Copy to output
    strncpy(out.message, msgBuf, sizeof(out.message) - 1);
    out.message[sizeof(out.message) - 1] = '\0';
    out.targetSpeciesIdx = idx;
    out.isBroadcast = false;
    out.rarity = RARITY_COMMON;

    if (giveXp && baseXp > 0) {
        out.xp = calcXp(baseXp, friendship, rivalry, weatherMult, isMentor);
        char xpTag[20];
        snprintf(xpTag, sizeof(xpTag), " (+%d XP)", out.xp);
        size_t msgLen = strlen(out.message);
        if (msgLen + strlen(xpTag) < sizeof(out.message)) {
            strcat(out.message, xpTag);
        }
    } else {
        out.xp = 0;
    }
}

// ── Dream events ─────────────────────────────────────────────────────────────

void DaycareEventGen::generateDream(DaycareEvent &out,
    const DaycarePokemonState *localParty, uint8_t localPartyCount)
{
    uint8_t idx = pickPokemon(localPartyCount);
    uint8_t dex = localParty[idx].speciesDex;
    const char *name = getDisplayName(localParty[idx]);

    // Check for foreshadowing (if lots of XP accumulated — proxy for near level-up)
    if (localParty[idx].totalXpGained > 0 && rngChance(20)) {
        snprintf(out.message, sizeof(out.message),
                 foreshadowDreams[rngRange(FORESHADOW_COUNT)], name);
    } else {
        snprintf(out.message, sizeof(out.message),
                 dreamTemplates[rngRange(DREAM_COUNT)], name);
    }

    out.xp = 0;  // dreams are always flavor
    out.targetSpeciesIdx = idx;
    out.targetNodeId = 0;
    out.isBroadcast = false;
    out.rarity = RARITY_COMMON;
}

// ── Visitor events ───────────────────────────────────────────────────────────

void DaycareEventGen::generateVisitor(DaycareEvent &out, uint32_t newNodeId,
    const DaycarePokemonState *localParty, uint8_t localPartyCount)
{
    uint8_t idx = pickPokemon(localPartyCount);
    uint8_t dex = localParty[idx].speciesDex;
    const char *name = getDisplayName(localParty[idx]);

    snprintf(out.message, sizeof(out.message),
             visitorWelcomeTemplates[rngRange(VISITOR_WELCOME_COUNT)], name);

    out.xp = 0;
    out.targetSpeciesIdx = idx;
    out.targetNodeId = newNodeId;  // DM the new node
    out.isBroadcast = false;
    out.rarity = RARITY_UNCOMMON;
}

// ── Escape attempts ──────────────────────────────────────────────────────────

bool DaycareEventGen::tryEscape(DaycareEvent &out,
    const DaycarePokemonState *localParty, uint8_t localPartyCount)
{
    // ~1% chance per call (called once per hour = ~24% per day)
    // Adjusted: 0.04% per call = ~1% per day
    if (!rngChance(1)) return false;

    uint8_t idx = pickPokemon(localPartyCount);
    uint8_t dex = localParty[idx].speciesDex;
    const DaycareSpecies *sp = daycareGetSpecies(dex);
    const char *name = getDisplayName(localParty[idx]);

    const char *msg;
    if (sp) {
        if (sp->type1 == 2 || sp->type2 == 2) {  // Flying
            msg = escapeFlying[rngRange(2)];
        } else if (sp->type1 == 4 || sp->type2 == 4) {  // Ground
            msg = escapeDigger[rngRange(2)];
        } else if (sp->type1 == 7 || sp->type2 == 7) {  // Ghost
            msg = escapeGhost[rngRange(2)];
        } else if (sp->type1 == 9 || sp->type2 == 9) {  // Water
            msg = escapeWater[rngRange(2)];
        } else {
            msg = escapeSmall[rngRange(2)];
        }
    } else {
        msg = escapeSmall[0];
    }

    snprintf(out.message, sizeof(out.message), msg, name);
    out.xp = 0;
    out.targetSpeciesIdx = idx;
    out.targetNodeId = 0;
    out.isBroadcast = false;
    out.rarity = RARITY_RARE;
    return true;
}

// ── Weather events ───────────────────────────────────────────────────────────

void DaycareEventGen::generateWeatherEvent(DaycareEvent &out,
    const DaycarePokemonState *localParty, uint8_t localPartyCount,
    DaycareWeatherType weather)
{
    uint8_t idx = pickPokemon(localPartyCount);
    uint8_t dex = localParty[idx].speciesDex;
    const DaycareSpecies *sp = daycareGetSpecies(dex);
    const char *name = getDisplayName(localParty[idx]);

    uint8_t mult = sp ? weatherTypeMultiplier100(weather, sp->type1) : 100;
    bool boosted = (mult > 100);
    bool penalized = (mult < 100);

    // Weather-specific messages
    const char *msg = "Your %s watched the weather change.";
    bool giveXp = false;
    uint8_t baseXp = 0;

    switch (weather) {
        case WEATHER_RAIN:
            if (boosted) {
                static const char *rainBoost[] = {
                    "Your %s is having the time of its life splashing in puddles.",
                    "Your %s spread its leaves wide to catch the rain.",
                    "Your %s danced in the downpour.",
                };
                msg = rainBoost[rngRange(3)];
                giveXp = true; baseXp = 20;
            } else if (penalized) {
                static const char *rainPen[] = {
                    "Your %s is huddled under a rock, keeping dry. It looks miserable.",
                    "Your %s is staying away from the puddles. Smart.",
                    "Your %s found shelter and refuses to come out until the rain stops.",
                };
                msg = rainPen[rngRange(3)];
            } else {
                msg = "Your %s watched the rain from under a tree.";
            }
            break;

        case WEATHER_THUNDERSTORM:
            if (boosted) {
                static const char *tsBoot[] = {
                    "Your %s ran to the highest hill and absorbed a bolt of lightning! It's vibrating with energy!",
                    "Your %s is crackling with electricity from the storm!",
                };
                msg = tsBoot[rngRange(2)];
                giveXp = true; baseXp = 30;
            } else if (penalized) {
                static const char *tsPen[] = {
                    "Your %s is grounded by the storm. It's pacing impatiently.",
                    "Your %s dove for cover when the lightning started.",
                };
                msg = tsPen[rngRange(2)];
            } else {
                msg = "Your %s watched the lightning from a safe distance.";
            }
            break;

        case WEATHER_CLEAR:
        case WEATHER_HOT:
            if (boosted) {
                static const char *sunBoost[] = {
                    "Your %s basked in the sun. Its energy is radiating.",
                    "Your %s photosynthesized all morning. It looks healthier.",
                    "Your %s loves this heat!",
                };
                msg = sunBoost[rngRange(3)];
                giveXp = true; baseXp = 20;
            } else if (penalized) {
                static const char *sunPen[] = {
                    "Your %s is desperately staying in the shade.",
                    "Your %s made a small cold patch to lie on. It's suffering.",
                };
                msg = sunPen[rngRange(2)];
            } else {
                msg = "Your %s enjoyed the sunny weather.";
            }
            break;

        case WEATHER_SNOW:
        case WEATHER_COLD:
            if (boosted) {
                static const char *snowBoost[] = {
                    "Your %s is singing in the snowfall! It hasn't been this happy in weeks.",
                    "Your %s built an ice sculpture. It's beautiful.",
                    "Your %s is thriving in the cold!",
                };
                msg = snowBoost[rngRange(3)];
                giveXp = true; baseXp = 25;
            } else if (penalized) {
                static const char *snowPen[] = {
                    "Your %s is huddled for warmth. Its energy is low.",
                    "Your %s found a sheltered spot and went dormant.",
                };
                msg = snowPen[rngRange(2)];
            } else {
                msg = "Your %s watched the snow fall.";
            }
            break;

        case WEATHER_WINDY:
            if (boosted) {
                static const char *windBoost[] = {
                    "Your %s soared higher than ever on the updrafts!",
                    "Your %s rode the wind currents with joy!",
                };
                msg = windBoost[rngRange(2)];
                giveXp = true; baseXp = 25;
            } else if (penalized) {
                static const char *windPen[] = {
                    "Your %s got blown off a branch.",
                    "Your %s is struggling against the wind.",
                };
                msg = windPen[rngRange(2)];
            } else {
                msg = "Your %s braced against the wind.";
            }
            break;

        case WEATHER_FOG:
            if (boosted) {
                static const char *fogBoost[] = {
                    "Your %s is THRIVING in the fog. It scared four Pokemon before breakfast.",
                    "Your %s disappeared into the fog. It's probably fine.",
                };
                msg = fogBoost[rngRange(2)];
                giveXp = true; baseXp = 20;
            } else if (penalized) {
                static const char *fogPen[] = {
                    "Your %s is sticking very close to the den. It can't see anything.",
                    "Your %s bumped into three things in the fog.",
                };
                msg = fogPen[rngRange(2)];
            } else {
                msg = "Your %s peered into the fog cautiously.";
            }
            break;

        default:
            break;
    }

    snprintf(out.message, sizeof(out.message), msg, name);
    out.targetSpeciesIdx = idx;
    out.targetNodeId = 0;
    out.isBroadcast = false;
    out.rarity = RARITY_UNCOMMON;

    if (giveXp) {
        out.xp = calcXp(baseXp, 0, 0, mult, false);
        char xpTag[20];
        snprintf(xpTag, sizeof(xpTag), " (+%d XP)", out.xp);
        size_t msgLen = strlen(out.message);
        if (msgLen + strlen(xpTag) < sizeof(out.message)) {
            strcat(out.message, xpTag);
        }
    } else {
        out.xp = 0;
    }
}

// ── Main generate() entry point ──────────────────────────────────────────────

DaycareEvent DaycareEventGen::generate(
    const DaycarePokemonState *localParty, uint8_t localPartyCount,
    const DaycareNeighborPokemon *neighbors, uint8_t neighborCount,
    DaycareState &state,
    DaycareWeatherType weather,
    bool isNight,
    uint32_t newNodeId)
{
    DaycareEvent ev = {};
    rngSeed(millis() ^ (uint32_t)(state.totalEvents * 2654435761UL));

    // Priority 1: visitor event (new node just arrived)
    if (newNodeId != 0) {
        generateVisitor(ev, newNodeId, localParty, localPartyCount);
        state.totalEvents++;
        return ev;
    }

    // Priority 2: night = 50% chance of dream event, otherwise normal event
    if (isNight && rngChance(50)) {
        generateDream(ev, localParty, localPartyCount);
        state.pokemon[ev.targetSpeciesIdx].dreamCount++;
        state.totalEvents++;
        return ev;
    }

    // Priority 3: escape attempt (very rare)
    if (tryEscape(ev, localParty, localPartyCount)) {
        state.pokemon[ev.targetSpeciesIdx].escapeCount++;
        state.totalEvents++;
        return ev;
    }

    // Priority 4: weather event — dramatic weather triggers events often,
    // calm/clear weather rarely (10%) since nothing notable is happening
    uint8_t weatherChance = 10;  // default for clear, hot
    if (weather == WEATHER_WINDY) weatherChance = 20;
    else if (weather == WEATHER_THUNDERSTORM) weatherChance = 50;
    else if (weather == WEATHER_SNOW) weatherChance = 40;
    else if (weather == WEATHER_FOG) weatherChance = 30;
    else if (weather == WEATHER_COLD) weatherChance = 20;
    else if (weather == WEATHER_RAIN) weatherChance = 25;
    if (weather != WEATHER_NONE && rngChance(weatherChance)) {
        generateWeatherEvent(ev, localParty, localPartyCount, weather);
        state.totalEvents++;
        return ev;
    }

    // Priority 5: flat template (Layer 1, 30% chance)
    if (tryFlatTemplate(ev, localParty, localPartyCount,
                        neighbors, neighborCount, state, weather, isNight)) {
        state.totalEvents++;
        return ev;
    }

    // Priority 6: compositional generation (Layer 2, always works)
    generateCompositional(ev, localParty, localPartyCount,
                          neighbors, neighborCount, state, weather, isNight);
    state.totalEvents++;
    return ev;
}

// ── Type names for arrival messages ─────────────────────────────────────────

static const char *typeNameStr[] = {
    "Normal", "Fighting", "Flying", "Poison", "Ground",
    "Rock", "Bug", "Ghost", "Fire", "Water",
    "Grass", "Electric", "Psychic", "Ice", "Dragon"
};

// ── Arrival "dog park" templates ────────────────────────────────────────────
// %s1 = local pokemon name, %s2 = neighbor pokemon (with trainer tag), %s3 = shared type

static const char *arrivalAffinityTemplates[] = {
    "Your %s ran over to greet %s — kindred %s spirits!",
    "Your %s and %s hit it off immediately! Fellow %s types.",
    "Your %s spotted %s and got excited — a fellow %s type!",
    "Your %s bounded up to %s. %s types stick together!",
    "Your %s sniffed the air and dashed toward %s. %s pals!",
};
static constexpr uint8_t ARRIVAL_AFFINITY_COUNT = sizeof(arrivalAffinityTemplates) / sizeof(arrivalAffinityTemplates[0]);

static const char *arrivalRivalryTemplates[] = {
    "Your %s locked eyes with %s — an instant rivalry!",
    "Your %s sized up %s. Sparks are already flying!",
    "Your %s and %s circled each other warily. This could get interesting!",
    "Your %s puffed up as %s approached. A challenge!",
};
static constexpr uint8_t ARRIVAL_RIVALRY_COUNT = sizeof(arrivalRivalryTemplates) / sizeof(arrivalRivalryTemplates[0]);

static const char *arrivalCuriousTemplates[] = {
    "Your %s wandered over to check out %s.",
    "Your %s curiously approached %s.",
    "Your %s noticed %s and went to investigate.",
};
static constexpr uint8_t ARRIVAL_CURIOUS_COUNT = sizeof(arrivalCuriousTemplates) / sizeof(arrivalCuriousTemplates[0]);

// ── Super-effective type matchups (attacker type → weak defender type) ───────

static bool isSuperEffective(uint8_t atkType, uint8_t defType) {
    switch (atkType) {
        case 8:  return defType == 10 || defType == 13 || defType == 6;   // Fire > Grass,Ice,Bug
        case 9:  return defType == 8  || defType == 4  || defType == 5;   // Water > Fire,Ground,Rock
        case 10: return defType == 9  || defType == 4  || defType == 5;   // Grass > Water,Ground,Rock
        case 11: return defType == 9  || defType == 2;                     // Electric > Water,Flying
        case 13: return defType == 10 || defType == 4  || defType == 2  || defType == 14; // Ice > Grass,Ground,Flying,Dragon
        case 1:  return defType == 0  || defType == 5  || defType == 13;  // Fighting > Normal,Rock,Ice
        case 3:  return defType == 10;                                     // Poison > Grass
        case 4:  return defType == 8  || defType == 11 || defType == 5  || defType == 3; // Ground > Fire,Electric,Rock,Poison
        case 2:  return defType == 1  || defType == 10 || defType == 6;   // Flying > Fighting,Grass,Bug
        case 12: return defType == 1  || defType == 3;                     // Psychic > Fighting,Poison
        case 5:  return defType == 8  || defType == 13 || defType == 2  || defType == 6; // Rock > Fire,Ice,Flying,Bug
        case 6:  return defType == 10 || defType == 12;                    // Bug > Grass,Psychic
        case 7:  return defType == 7  || defType == 12;                    // Ghost > Ghost,Psychic
        case 14: return defType == 14;                                     // Dragon > Dragon
        default: return false;
    }
}

// ── Arrival event generator ─────────────────────────────────────────────────

void DaycareEventGen::generateArrivalEvent(
    DaycareEvent &out,
    const DaycarePokemonState *localParty, uint8_t localPartyCount,
    const DaycareBeacon &newcomer,
    DaycareState &state,
    const char *localShortName,
    const char *localGameName)
{
    rngSeed(millis() ^ newcomer.nodeId);

    // Find the best local Pokemon to interact with the newcomer's party
    // Priority: type affinity > type rivalry > random
    int8_t bestLocal = -1;
    int8_t bestRemote = -1;
    uint8_t sharedType = 0;
    bool isRivalry = false;

    // First pass: look for type affinity (same type = friends at the dog park)
    for (uint8_t li = 0; li < localPartyCount && bestLocal < 0; li++) {
        const DaycareSpecies *localSp = daycareGetSpecies(localParty[li].speciesDex);
        if (!localSp) continue;

        for (uint8_t ri = 0; ri < newcomer.partyCount && bestLocal < 0; ri++) {
            const DaycareSpecies *remoteSp = daycareGetSpecies(newcomer.pokemon[ri].species);
            if (!remoteSp) continue;

            if (localSp->type1 == remoteSp->type1 || localSp->type1 == remoteSp->type2) {
                bestLocal = li; bestRemote = ri; sharedType = localSp->type1;
            } else if (localSp->type2 == remoteSp->type1 || localSp->type2 == remoteSp->type2) {
                bestLocal = li; bestRemote = ri; sharedType = localSp->type2;
            }
        }
    }

    // Second pass: look for type rivalry (super-effective matchup)
    if (bestLocal < 0) {
        for (uint8_t li = 0; li < localPartyCount && bestLocal < 0; li++) {
            const DaycareSpecies *localSp = daycareGetSpecies(localParty[li].speciesDex);
            if (!localSp) continue;

            for (uint8_t ri = 0; ri < newcomer.partyCount && bestLocal < 0; ri++) {
                const DaycareSpecies *remoteSp = daycareGetSpecies(newcomer.pokemon[ri].species);
                if (!remoteSp) continue;

                if (isSuperEffective(localSp->type1, remoteSp->type1) ||
                    isSuperEffective(localSp->type1, remoteSp->type2) ||
                    isSuperEffective(remoteSp->type1, localSp->type1) ||
                    isSuperEffective(remoteSp->type1, localSp->type2)) {
                    bestLocal = li; bestRemote = ri; isRivalry = true;
                }
            }
        }
    }

    // Fallback: random local, first remote
    if (bestLocal < 0) {
        bestLocal = pickPokemon(localPartyCount);
        bestRemote = 0;
    }

    const char *localName = getDisplayName(localParty[bestLocal]);

    // Format remote Pokemon name with trainer tag
    char remoteName[60];
    const char *remoteSpecies = (newcomer.pokemon[bestRemote].species >= 1 &&
                                  newcomer.pokemon[bestRemote].species <= DAYCARE_SPECIES_COUNT)
                                 ? daycareSpeciesNames[newcomer.pokemon[bestRemote].species] : "Pokemon";
    char trainerTag[14];
    if (newcomer.gameName[0])
        snprintf(trainerTag, sizeof(trainerTag), "%s-%s", newcomer.shortName, newcomer.gameName);
    else
        snprintf(trainerTag, sizeof(trainerTag), "%s", newcomer.shortName);

    if (newcomer.pokemon[bestRemote].nickname[0])
        snprintf(remoteName, sizeof(remoteName), "%s's %s (%s)",
                 trainerTag, remoteSpecies, newcomer.pokemon[bestRemote].nickname);
    else
        snprintf(remoteName, sizeof(remoteName), "%s's %s", trainerTag, remoteSpecies);

    // Build partner-perspective names: from the newcomer's POV, OUR pokemon
    // gets our trainer tag prefix, and THEIR pokemon is "your <species>".
    char localTrainerTag[14] = {};
    if (localShortName && localShortName[0]) {
        if (localGameName && localGameName[0])
            snprintf(localTrainerTag, sizeof(localTrainerTag), "%s-%s",
                     localShortName, localGameName);
        else
            snprintf(localTrainerTag, sizeof(localTrainerTag), "%s", localShortName);
    }
    char localNameForRemote[80];
    if (localTrainerTag[0])
        snprintf(localNameForRemote, sizeof(localNameForRemote), "%s's %s",
                 localTrainerTag, localName);
    else
        snprintf(localNameForRemote, sizeof(localNameForRemote), "%s", localName);
    char remoteNameForRemote[60];
    if (newcomer.pokemon[bestRemote].nickname[0])
        snprintf(remoteNameForRemote, sizeof(remoteNameForRemote),
                 "your %s (%s)", remoteSpecies,
                 newcomer.pokemon[bestRemote].nickname);
    else
        snprintf(remoteNameForRemote, sizeof(remoteNameForRemote),
                 "your %s", remoteSpecies);

    // Generate message (local perspective + remote perspective)
    if (!isRivalry && sharedType > 0 && sharedType < TYPE_COUNT) {
        const char *tName = typeNameStr[sharedType];
        const char *tmpl = arrivalAffinityTemplates[rngRange(ARRIVAL_AFFINITY_COUNT)];
        char tmpBuf[200];
        // All affinity templates are "Your %s ... %s ... %s ..."
        snprintf(tmpBuf, sizeof(tmpBuf), tmpl, localName, remoteName, tName);
        strncpy(out.message, tmpBuf, sizeof(out.message) - 1);
        snprintf(tmpBuf, sizeof(tmpBuf), tmpl,
                 localNameForRemote, remoteNameForRemote, tName);
        const char *src = tmpBuf;
        if (strncmp(src, "Your ", 5) == 0) src += 5;
        strncpy(out.remoteMessage, src, sizeof(out.remoteMessage) - 1);
    } else if (isRivalry) {
        const char *tmpl = arrivalRivalryTemplates[rngRange(ARRIVAL_RIVALRY_COUNT)];
        snprintf(out.message, sizeof(out.message), tmpl, localName, remoteName);
        char tmpBuf[200];
        snprintf(tmpBuf, sizeof(tmpBuf), tmpl, localNameForRemote, remoteNameForRemote);
        const char *src = tmpBuf;
        if (strncmp(src, "Your ", 5) == 0) src += 5;
        strncpy(out.remoteMessage, src, sizeof(out.remoteMessage) - 1);
    } else {
        const char *tmpl = arrivalCuriousTemplates[rngRange(ARRIVAL_CURIOUS_COUNT)];
        snprintf(out.message, sizeof(out.message), tmpl, localName, remoteName);
        char tmpBuf[200];
        snprintf(tmpBuf, sizeof(tmpBuf), tmpl, localNameForRemote, remoteNameForRemote);
        const char *src = tmpBuf;
        if (strncmp(src, "Your ", 5) == 0) src += 5;
        strncpy(out.remoteMessage, src, sizeof(out.remoteMessage) - 1);
    }

    out.xp = 10 + rngRange(11);  // 10-20 XP for the meetup
    out.targetSpeciesIdx = bestLocal;
    out.targetNodeId = newcomer.nodeId;
    out.isBroadcast = false;
    out.rarity = RARITY_UNCOMMON;

    // Build relationship — affinity starts friendship, rivalry starts rivalry
    DaycareRelationship *rel = getOrCreateRelationship(
        state, newcomer.nodeId, bestLocal, newcomer.pokemon[bestRemote].species);
    if (rel) {
        if (!isRivalry && sharedType > 0) {
            uint8_t gain = 10;  // big first-meeting bonus for type affinity
            if (rel->friendship <= 255 - gain) rel->friendship += gain;
            else rel->friendship = 255;
        } else if (isRivalry) {
            uint8_t gain = 8;
            if (rel->rivalry <= 255 - gain) rel->rivalry += gain;
            else rel->rivalry = 255;
        }
        rel->lastSeenMs = millis();
    }
}
