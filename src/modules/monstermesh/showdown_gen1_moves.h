// SPDX-License-Identifier: MIT
//
// Auto-generated from Pokemon Showdown data — DO NOT EDIT BY HAND.
// Regenerate with: python3 scripts/gen_showdown_data.py
//
// Source: https://github.com/smogon/Pokemon-Showdown
// Copyright (c) Smogon University / Pokemon Showdown contributors (MIT).
//
// Pokemon and all related marks are trademarks of Nintendo / Game Freak / Creatures.
// This file contains only mechanical/statistical data, used for fan-game research.

#pragma once
#include <stdint.h>

// Effect category enum — kept in sync with EFFECT_NAMES in gen_showdown_data.py
enum Gen1MoveEffect : uint8_t {
    EFF_NONE = 0,
    EFF_STATUS_PSN = 1,
    EFF_STATUS_BRN = 2,
    EFF_STATUS_PAR = 3,
    EFF_STATUS_SLP = 4,
    EFF_STATUS_FRZ = 5,
    EFF_STATUS_CONFUSE = 6,
    EFF_BOOST_ATK = 7,
    EFF_BOOST_DEF = 8,
    EFF_BOOST_SPD = 9,
    EFF_BOOST_SPC = 10,
    EFF_BOOST_ACC = 11,
    EFF_BOOST_EVA = 12,
    EFF_DROP_ATK = 13,
    EFF_DROP_DEF = 14,
    EFF_DROP_SPD = 15,
    EFF_DROP_SPC = 16,
    EFF_DROP_ACC = 17,
    EFF_DROP_EVA = 18,
    EFF_DRAIN_HP = 19,
    EFF_RECOIL = 20,
    EFF_OHKO = 21,
    EFF_MULTI_HIT = 22,
    EFF_DOUBLE_HIT = 23,
    EFF_FIXED_DMG = 24,
    EFF_LEVEL_DMG = 25,
    EFF_PSYWAVE = 26,
    EFF_SUPER_FANG = 27,
    EFF_COUNTER = 28,
    EFF_CHARGE_TURN = 29,
    EFF_RECHARGE = 30,
    EFF_HYPER_BEAM = 31,
    EFF_RAGE = 32,
    EFF_MIMIC = 33,
    EFF_METRONOME = 34,
    EFF_TRANSFORM = 35,
    EFF_SUBSTITUTE = 36,
    EFF_REST = 37,
    EFF_DISABLE = 38,
    EFF_MIST = 39,
    EFF_LIGHT_SCREEN = 40,
    EFF_REFLECT = 41,
    EFF_FOCUS_ENERGY = 42,
    EFF_HAZE = 43,
    EFF_BIDE = 44,
    EFF_THRASH = 45,
    EFF_TRAPPING = 46,
    EFF_FLINCH = 47,
    EFF_SWIFT_NEVERMISS = 48,
    EFF_DREAM_EATER = 49,
    EFF_EXPLODE = 50,
    EFF_PAY_DAY = 51,
    EFF_CONVERSION = 52,
    EFF_SUBSTITUTE_DAMAGE = 53,
};

struct Gen1MoveData {
    uint8_t  num;          // Gen 1 move id (1-165)
    char     name[16];     // PascalCase ("Tackle")
    uint8_t  type;         // index into GEN1_TYPES
    uint8_t  power;        // base power, 0 = status-only
    uint8_t  accuracy;     // 1-100 = miss chance, 0 = never miss
    uint8_t  pp;           // base PP
    int8_t   priority;     // mostly 0 in Gen 1
    uint8_t  effect;       // Gen1MoveEffect
    uint8_t  effectChance; // 0-100 (secondary effect probability)
};

static constexpr uint8_t GEN1_MOVE_COUNT = 165;
static constexpr Gen1MoveData GEN1_MOVES[] = {
    {   1, "Pound",  0,  40, 100, 35,  0, EFF_NONE,   0 },  // pound
    {   2, "Karate Chop",  0,  50, 100, 25,  0, EFF_NONE,   0 },  // karatechop
    {   3, "Double Slap",  0,  15,  85, 10,  0, EFF_MULTI_HIT,   0 },  // doubleslap
    {   4, "Comet Punch",  0,  18,  85, 15,  0, EFF_MULTI_HIT,   0 },  // cometpunch
    {   5, "Mega Punch",  0,  80,  85, 20,  0, EFF_NONE,   0 },  // megapunch
    {   6, "Pay Day",  0,  40, 100, 20,  0, EFF_PAY_DAY,   0 },  // payday
    {   7, "Fire Punch",  9,  75, 100, 15,  0, EFF_STATUS_BRN,  10 },  // firepunch
    {   8, "Ice Punch", 14,  75, 100, 15,  0, EFF_STATUS_FRZ,  10 },  // icepunch
    {   9, "Thunder Punch", 12,  75, 100, 15,  0, EFF_STATUS_PAR,  10 },  // thunderpunch
    {  10, "Scratch",  0,  40, 100, 35,  0, EFF_NONE,   0 },  // scratch
    {  11, "Vise Grip",  0,  55, 100, 30,  0, EFF_NONE,   0 },  // visegrip
    {  12, "Guillotine",  0,   0,  30,  5,  0, EFF_OHKO,   0 },  // guillotine
    {  13, "Razor Wind",  0,  80, 100, 10,  0, EFF_CHARGE_TURN,   0 },  // razorwind
    {  14, "Swords Dance",  0,   0,   0, 20,  0, EFF_BOOST_ATK,   0 },  // swordsdance
    {  15, "Cut",  0,  50,  95, 30,  0, EFF_NONE,   0 },  // cut
    {  16, "Gust",  0,  40, 100, 35,  0, EFF_NONE,   0 },  // gust
    {  17, "Wing Attack",  2,  35, 100, 35,  0, EFF_NONE,   0 },  // wingattack
    {  18, "Whirlwind",  0,   0,  85, 20,  0, EFF_NONE,   0 },  // whirlwind
    {  19, "Fly",  2,  90,  95, 15,  0, EFF_CHARGE_TURN,   0 },  // fly
    {  20, "Bind",  0,  15,  85, 20,  0, EFF_TRAPPING,   0 },  // bind
    {  21, "Slam",  0,  80,  75, 20,  0, EFF_NONE,   0 },  // slam
    {  22, "Vine Whip", 11,  45, 100, 25,  0, EFF_NONE,   0 },  // vinewhip
    {  23, "Stomp",  0,  65, 100, 20,  0, EFF_FLINCH,  30 },  // stomp
    {  24, "Double Kick",  1,  30, 100, 30,  0, EFF_DOUBLE_HIT,   0 },  // doublekick
    {  25, "Mega Kick",  0, 120,  75,  5,  0, EFF_NONE,   0 },  // megakick
    {  26, "Jump Kick",  1, 100,  95, 10,  0, EFF_NONE,   0 },  // jumpkick
    {  27, "Rolling Kick",  1,  60,  85, 15,  0, EFF_FLINCH,  30 },  // rollingkick
    {  28, "Sand Attack",  0,   0, 100, 15,  0, EFF_DROP_ACC,   0 },  // sandattack
    {  29, "Headbutt",  0,  70, 100, 15,  0, EFF_FLINCH,  30 },  // headbutt
    {  30, "Horn Attack",  0,  65, 100, 25,  0, EFF_NONE,   0 },  // hornattack
    {  31, "Fury Attack",  0,  15,  85, 20,  0, EFF_MULTI_HIT,   0 },  // furyattack
    {  32, "Horn Drill",  0,   0,  30,  5,  0, EFF_OHKO,   0 },  // horndrill
    {  33, "Tackle",  0,  40, 100, 35,  0, EFF_NONE,   0 },  // tackle
    {  34, "Body Slam",  0,  85, 100, 15,  0, EFF_STATUS_PAR,  30 },  // bodyslam
    {  35, "Wrap",  0,  15,  85, 20,  0, EFF_TRAPPING,   0 },  // wrap
    {  36, "Take Down",  0,  90,  85, 20,  0, EFF_RECOIL,   0 },  // takedown
    {  37, "Thrash",  0, 120, 100, 10,  0, EFF_THRASH,   0 },  // thrash
    {  38, "Double-Edge",  0, 100, 100, 15,  0, EFF_RECOIL,   0 },  // doubleedge
    {  39, "Tail Whip",  0,   0, 100, 30,  0, EFF_DROP_DEF,   0 },  // tailwhip
    {  40, "Poison Sting",  3,  15, 100, 35,  0, EFF_STATUS_PSN,  30 },  // poisonsting
    {  41, "Twineedle",  7,  25, 100, 20,  0, EFF_NONE,  20 },  // twineedle
    {  42, "Pin Missile",  7,  25,  95, 20,  0, EFF_MULTI_HIT,   0 },  // pinmissile
    {  43, "Leer",  0,   0, 100, 30,  0, EFF_DROP_DEF,   0 },  // leer
    {  44, "Bite",  0,  60, 100, 25,  0, EFF_FLINCH,  30 },  // bite
    {  45, "Growl",  0,   0, 100, 40,  0, EFF_DROP_ATK,   0 },  // growl
    {  46, "Roar",  0,   0,   0, 20,  0, EFF_NONE,   0 },  // roar
    {  47, "Sing",  0,   0,  55, 15,  0, EFF_STATUS_SLP,   0 },  // sing
    {  48, "Supersonic",  0,   0,  55, 20,  0, EFF_STATUS_CONFUSE,   0 },  // supersonic
    {  49, "Sonic Boom",  0,   1,  90, 20,  0, EFF_FIXED_DMG,   0 },  // sonicboom
    {  50, "Disable",  0,   0, 100, 20,  0, EFF_DISABLE,   0 },  // disable
    {  51, "Acid",  3,  40, 100, 30,  0, EFF_NONE,  10 },  // acid
    {  52, "Ember",  9,  40, 100, 25,  0, EFF_STATUS_BRN,  10 },  // ember
    {  53, "Flamethrower",  9,  90, 100, 15,  0, EFF_STATUS_BRN,  10 },  // flamethrower
    {  54, "Mist", 14,   0,   0, 30,  0, EFF_MIST,   0 },  // mist
    {  55, "Water Gun", 10,  40, 100, 25,  0, EFF_NONE,   0 },  // watergun
    {  56, "Hydro Pump", 10, 110,  80,  5,  0, EFF_NONE,   0 },  // hydropump
    {  57, "Surf", 10,  90, 100, 15,  0, EFF_NONE,   0 },  // surf
    {  58, "Ice Beam", 14,  90, 100, 10,  0, EFF_STATUS_FRZ,  10 },  // icebeam
    {  59, "Blizzard", 14, 110,  90,  5,  0, EFF_STATUS_FRZ,  10 },  // blizzard
    {  60, "Psybeam", 13,  65, 100, 20,  0, EFF_STATUS_CONFUSE,  10 },  // psybeam
    {  61, "Bubble Beam", 10,  65, 100, 20,  0, EFF_NONE,  10 },  // bubblebeam
    {  62, "Aurora Beam", 14,  65, 100, 20,  0, EFF_NONE,  10 },  // aurorabeam
    {  63, "Hyper Beam",  0, 150,  90,  5,  0, EFF_HYPER_BEAM,   0 },  // hyperbeam
    {  64, "Peck",  2,  35, 100, 35,  0, EFF_NONE,   0 },  // peck
    {  65, "Drill Peck",  2,  80, 100, 20,  0, EFF_NONE,   0 },  // drillpeck
    {  66, "Submission",  1,  80,  80, 20,  0, EFF_RECOIL,   0 },  // submission
    {  67, "Low Kick",  1,   0, 100, 20,  0, EFF_FLINCH,   0 },  // lowkick
    {  68, "Counter",  1,   1, 100, 20, -5, EFF_COUNTER,   0 },  // counter
    {  69, "Seismic Toss",  1,   1, 100, 20,  0, EFF_LEVEL_DMG,   0 },  // seismictoss
    {  70, "Strength",  0,  80, 100, 15,  0, EFF_NONE,   0 },  // strength
    {  71, "Absorb", 11,  20, 100, 25,  0, EFF_DRAIN_HP,   0 },  // absorb
    {  72, "Mega Drain", 11,  40, 100, 15,  0, EFF_DRAIN_HP,   0 },  // megadrain
    {  73, "Leech Seed", 11,   0,  90, 10,  0, EFF_DRAIN_HP,   0 },  // leechseed
    {  74, "Growth",  0,   0,   0, 20,  0, EFF_BOOST_SPC,   0 },  // growth
    {  75, "Razor Leaf", 11,  55,  95, 25,  0, EFF_NONE,   0 },  // razorleaf
    {  76, "Solar Beam", 11, 120, 100, 10,  0, EFF_CHARGE_TURN,   0 },  // solarbeam
    {  77, "Poison Powder",  3,   0,  75, 35,  0, EFF_STATUS_PSN,   0 },  // poisonpowder
    {  78, "Stun Spore", 11,   0,  75, 30,  0, EFF_STATUS_PAR,   0 },  // stunspore
    {  79, "Sleep Powder", 11,   0,  75, 15,  0, EFF_STATUS_SLP,   0 },  // sleeppowder
    {  80, "Petal Dance", 11, 120, 100, 10,  0, EFF_THRASH,   0 },  // petaldance
    {  81, "String Shot",  7,   0,  95, 40,  0, EFF_DROP_SPD,   0 },  // stringshot
    {  82, "Dragon Rage", 15,   1, 100, 10,  0, EFF_FIXED_DMG,   0 },  // dragonrage
    {  83, "Fire Spin",  9,  15,  70, 15,  0, EFF_TRAPPING,   0 },  // firespin
    {  84, "Thunder Shock", 12,  40, 100, 30,  0, EFF_STATUS_PAR,  10 },  // thundershock
    {  85, "Thunderbolt", 12,  90, 100, 15,  0, EFF_STATUS_PAR,  10 },  // thunderbolt
    {  86, "Thunder Wave", 12,   0,  90, 20,  0, EFF_STATUS_PAR,   0 },  // thunderwave
    {  87, "Thunder", 12, 110,  70, 10,  0, EFF_STATUS_PAR,  30 },  // thunder
    {  88, "Rock Throw",  5,  50,  65, 15,  0, EFF_NONE,   0 },  // rockthrow
    {  89, "Earthquake",  4, 100, 100, 10,  0, EFF_NONE,   0 },  // earthquake
    {  90, "Fissure",  4,   0,  30,  5,  0, EFF_OHKO,   0 },  // fissure
    {  91, "Dig",  4, 100, 100, 10,  0, EFF_CHARGE_TURN,   0 },  // dig
    {  92, "Toxic",  3,   0,  90, 10,  0, EFF_STATUS_PSN,   0 },  // toxic
    {  93, "Confusion", 13,  50, 100, 25,  0, EFF_STATUS_CONFUSE,  10 },  // confusion
    {  94, "Psychic", 13,  90, 100, 10,  0, EFF_NONE,  10 },  // psychic
    {  95, "Hypnosis", 13,   0,  60, 20,  0, EFF_STATUS_SLP,   0 },  // hypnosis
    {  96, "Meditate", 13,   0,   0, 40,  0, EFF_BOOST_ATK,   0 },  // meditate
    {  97, "Agility", 13,   0,   0, 30,  0, EFF_BOOST_SPD,   0 },  // agility
    {  98, "Quick Attack",  0,  40, 100, 30,  1, EFF_NONE,   0 },  // quickattack
    {  99, "Rage",  0,  20, 100, 20,  0, EFF_RAGE,   0 },  // rage
    { 100, "Teleport", 13,   0,   0, 20, -6, EFF_NONE,   0 },  // teleport
    { 101, "Night Shade",  8,   1, 100, 15,  0, EFF_LEVEL_DMG,   0 },  // nightshade
    { 102, "Mimic",  0,   0,   0, 10,  0, EFF_MIMIC,   0 },  // mimic
    { 103, "Screech",  0,   0,  85, 40,  0, EFF_DROP_DEF,   0 },  // screech
    { 104, "Double Team",  0,   0,   0, 15,  0, EFF_BOOST_EVA,   0 },  // doubleteam
    { 105, "Recover",  0,   0,   0,  5,  0, EFF_DRAIN_HP,   0 },  // recover
    { 106, "Harden",  0,   0,   0, 30,  0, EFF_BOOST_DEF,   0 },  // harden
    { 107, "Minimize",  0,   0,   0, 10,  0, EFF_BOOST_EVA,   0 },  // minimize
    { 108, "Smokescreen",  0,   0, 100, 20,  0, EFF_DROP_ACC,   0 },  // smokescreen
    { 109, "Confuse Ray",  8,   0, 100, 10,  0, EFF_STATUS_CONFUSE,   0 },  // confuseray
    { 110, "Withdraw", 10,   0,   0, 40,  0, EFF_BOOST_DEF,   0 },  // withdraw
    { 111, "Defense Curl",  0,   0,   0, 40,  0, EFF_BOOST_DEF,   0 },  // defensecurl
    { 112, "Barrier", 13,   0,   0, 20,  0, EFF_NONE,   0 },  // barrier
    { 113, "Light Screen", 13,   0,   0, 30,  0, EFF_LIGHT_SCREEN,   0 },  // lightscreen
    { 114, "Haze", 14,   0,   0, 30,  0, EFF_HAZE,   0 },  // haze
    { 115, "Reflect", 13,   0,   0, 20,  0, EFF_REFLECT,   0 },  // reflect
    { 116, "Focus Energy",  0,   0,   0, 30,  0, EFF_FOCUS_ENERGY,   0 },  // focusenergy
    { 117, "Bide",  0,   0,   0, 10,  1, EFF_BIDE,   0 },  // bide
    { 118, "Metronome",  0,   0,   0, 10,  0, EFF_METRONOME,   0 },  // metronome
    { 119, "Mirror Move",  2,   0,   0, 20,  0, EFF_NONE,   0 },  // mirrormove
    { 120, "Self-Destruct",  0, 130, 100,  5,  0, EFF_EXPLODE,   0 },  // selfdestruct
    { 121, "Egg Bomb",  0, 100,  75, 10,  0, EFF_NONE,   0 },  // eggbomb
    { 122, "Lick",  8,  30, 100, 30,  0, EFF_STATUS_PAR,  30 },  // lick
    { 123, "Smog",  3,  30,  70, 20,  0, EFF_STATUS_PSN,  40 },  // smog
    { 124, "Sludge",  3,  65, 100, 20,  0, EFF_STATUS_PSN,  30 },  // sludge
    { 125, "Bone Club",  4,  65,  85, 20,  0, EFF_NONE,  10 },  // boneclub
    { 126, "Fire Blast",  9, 110,  85,  5,  0, EFF_STATUS_BRN,  10 },  // fireblast
    { 127, "Waterfall", 10,  80, 100, 15,  0, EFF_NONE,  20 },  // waterfall
    { 128, "Clamp", 10,  35,  75, 10,  0, EFF_TRAPPING,   0 },  // clamp
    { 129, "Swift",  0,  60,   0, 20,  0, EFF_SWIFT_NEVERMISS,   0 },  // swift
    { 130, "Skull Bash",  0, 130, 100, 10,  0, EFF_CHARGE_TURN,   0 },  // skullbash
    { 131, "Spike Cannon",  0,  20, 100, 15,  0, EFF_MULTI_HIT,   0 },  // spikecannon
    { 132, "Constrict",  0,  10, 100, 35,  0, EFF_NONE,  10 },  // constrict
    { 133, "Amnesia", 13,   0,   0, 20,  0, EFF_BOOST_SPC,   0 },  // amnesia
    { 134, "Kinesis", 13,   0,  80, 15,  0, EFF_DROP_ACC,   0 },  // kinesis
    { 135, "Soft-Boiled",  0,   0,   0,  5,  0, EFF_DRAIN_HP,   0 },  // softboiled
    { 136, "High Jump Kick",  1, 130,  90, 10,  0, EFF_NONE,   0 },  // highjumpkick
    { 137, "Glare",  0,   0, 100, 30,  0, EFF_STATUS_PAR,   0 },  // glare
    { 138, "Dream Eater", 13, 100, 100, 15,  0, EFF_DREAM_EATER,   0 },  // dreameater
    { 139, "Poison Gas",  3,   0,  90, 40,  0, EFF_STATUS_PSN,   0 },  // poisongas
    { 140, "Barrage",  0,  15,  85, 20,  0, EFF_MULTI_HIT,   0 },  // barrage
    { 141, "Leech Life",  7,  80, 100, 10,  0, EFF_DRAIN_HP,   0 },  // leechlife
    { 142, "Lovely Kiss",  0,   0,  75, 10,  0, EFF_STATUS_SLP,   0 },  // lovelykiss
    { 143, "Sky Attack",  2, 140,  90,  5,  0, EFF_CHARGE_TURN,  30 },  // skyattack
    { 144, "Transform",  0,   0,   0, 10,  0, EFF_TRANSFORM,   0 },  // transform
    { 145, "Bubble", 10,  40, 100, 30,  0, EFF_NONE,  10 },  // bubble
    { 146, "Dizzy Punch",  0,  70, 100, 10,  0, EFF_NONE,  20 },  // dizzypunch
    { 147, "Spore", 11,   0, 100, 15,  0, EFF_STATUS_SLP,   0 },  // spore
    { 148, "Flash",  0,   0, 100, 20,  0, EFF_DROP_ACC,   0 },  // flash
    { 149, "Psywave", 13,   1, 100, 15,  0, EFF_PSYWAVE,   0 },  // psywave
    { 150, "Splash",  0,   0,   0, 40,  0, EFF_NONE,   0 },  // splash
    { 151, "Acid Armor",  3,   0,   0, 20,  0, EFF_NONE,   0 },  // acidarmor
    { 152, "Crabhammer", 10, 100,  90, 10,  0, EFF_NONE,   0 },  // crabhammer
    { 153, "Explosion",  0, 170, 100,  5,  0, EFF_EXPLODE,   0 },  // explosion
    { 154, "Fury Swipes",  0,  18,  80, 15,  0, EFF_MULTI_HIT,   0 },  // furyswipes
    { 155, "Bonemerang",  4,  50,  90, 10,  0, EFF_DOUBLE_HIT,   0 },  // bonemerang
    { 156, "Rest", 13,   0,   0,  5,  0, EFF_REST,   0 },  // rest
    { 157, "Rock Slide",  5,  75,  90, 10,  0, EFF_NONE,  30 },  // rockslide
    { 158, "Hyper Fang",  0,  80,  90, 15,  0, EFF_FLINCH,  10 },  // hyperfang
    { 159, "Sharpen",  0,   0,   0, 30,  0, EFF_BOOST_ATK,   0 },  // sharpen
    { 160, "Conversion",  0,   0,   0, 30,  0, EFF_CONVERSION,   0 },  // conversion
    { 161, "Tri Attack",  0,  80, 100, 10,  0, EFF_NONE,  20 },  // triattack
    { 162, "Super Fang",  0,   1,  90, 10,  0, EFF_SUPER_FANG,   0 },  // superfang
    { 163, "Slash",  0,  70, 100, 20,  0, EFF_NONE,   0 },  // slash
    { 164, "Substitute",  0,   0,   0, 10,  0, EFF_SUBSTITUTE,   0 },  // substitute
    { 165, "Struggle",  0,  50,   0, 10,  0, EFF_RECOIL,   0 },  // struggle
};

inline const Gen1MoveData *gen1Move(uint8_t num) {
    for (uint8_t i = 0; i < GEN1_MOVE_COUNT; ++i)
        if (GEN1_MOVES[i].num == num) return &GEN1_MOVES[i];
    return nullptr;
}
