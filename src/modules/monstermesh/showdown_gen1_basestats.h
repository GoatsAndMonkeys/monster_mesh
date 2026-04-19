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

struct Gen1BaseStats {
    uint8_t hp, atk, def, spd, spc;
    uint8_t type1, type2;
};

static constexpr Gen1BaseStats GEN1_BASE_STATS[152] = {
    { 0, 0, 0, 0, 0, 0, 0 },  // 0 placeholder
    {  45,  49,  49,  45,  65, 11,  3 },  // 1 Bulbasaur
    {  60,  62,  63,  60,  80, 11,  3 },  // 2 Ivysaur
    {  80,  82,  83,  80, 100, 11,  3 },  // 3 Venusaur-Gmax
    {  39,  52,  43,  65,  50,  9,  9 },  // 4 Charmander
    {  58,  64,  58,  80,  65,  9,  9 },  // 5 Charmeleon
    {  78,  84,  78, 100, 109,  9,  2 },  // 6 Charizard-Gmax
    {  44,  48,  65,  43,  50, 10, 10 },  // 7 Squirtle
    {  59,  63,  80,  58,  65, 10, 10 },  // 8 Wartortle
    {  79,  83, 100,  78,  85, 10, 10 },  // 9 Blastoise-Gmax
    {  45,  30,  35,  45,  20,  7,  7 },  // 10 Caterpie
    {  50,  20,  55,  30,  25,  7,  7 },  // 11 Metapod
    {  60,  45,  50,  70,  90,  7,  2 },  // 12 Butterfree-Gmax
    {  40,  35,  30,  50,  20,  7,  3 },  // 13 Weedle
    {  45,  25,  50,  35,  25,  7,  3 },  // 14 Kakuna
    {  65, 150,  40, 145,  15,  7,  3 },  // 15 Beedrill-Mega
    {  40,  45,  40,  56,  35,  0,  2 },  // 16 Pidgey
    {  63,  60,  55,  71,  50,  0,  2 },  // 17 Pidgeotto
    {  83,  80,  80, 121, 135,  0,  2 },  // 18 Pidgeot-Mega
    {  30,  56,  35,  72,  25,  0,  0 },  // 19 Rattata-Alola
    {  75,  71,  70,  77,  40,  0,  0 },  // 20 Raticate-Alola-Totem
    {  40,  60,  30,  70,  31,  0,  2 },  // 21 Spearow
    {  65,  90,  65, 100,  61,  0,  2 },  // 22 Fearow
    {  35,  60,  44,  55,  40,  3,  3 },  // 23 Ekans
    {  60,  85,  69,  80,  65,  3,  3 },  // 24 Arbok
    {  35,  55,  40,  90,  50, 12, 12 },  // 25 Pikachu-World
    {  60, 100,  55, 130, 160, 12, 12 },  // 26 Raichu-Mega-Y
    {  50,  75,  90,  40,  10, 14,  0 },  // 27 Sandshrew-Alola
    {  75, 100, 120,  65,  25, 14,  0 },  // 28 Sandslash-Alola
    {  55,  47,  52,  41,  40,  3,  3 },  // 29 Nidoran-F
    {  70,  62,  67,  56,  55,  3,  3 },  // 30 Nidorina
    {  90,  82,  87,  76,  75,  3,  4 },  // 31 Nidoqueen
    {  46,  57,  40,  50,  40,  3,  3 },  // 32 Nidoran-M
    {  61,  72,  57,  65,  55,  3,  3 },  // 33 Nidorino
    {  81,  92,  77,  85,  75,  3,  4 },  // 34 Nidoking
    {  70,  45,  48,  35,  60,  0,  0 },  // 35 Clefairy
    {  95,  80,  93,  70, 135,  0,  2 },  // 36 Clefable-Mega
    {  38,  41,  40,  65,  50, 14, 14 },  // 37 Vulpix-Alola
    {  73,  67,  75, 109,  81, 14,  0 },  // 38 Ninetales-Alola
    { 115,  45,  20,  20,  25,  0,  0 },  // 39 Jigglypuff
    { 140,  70,  45,  45,  50,  0,  0 },  // 40 Wigglytuff
    {  40,  45,  35,  55,  40,  3,  2 },  // 41 Zubat
    {  75,  80,  70,  90,  75,  3,  2 },  // 42 Golbat
    {  45,  50,  55,  30,  75, 11,  3 },  // 43 Oddish
    {  60,  65,  70,  40,  85, 11,  3 },  // 44 Gloom
    {  75,  80,  85,  50, 100, 11,  3 },  // 45 Vileplume
    {  35,  70,  55,  25,  55,  7, 11 },  // 46 Paras
    {  60,  95,  80,  30,  80,  7, 11 },  // 47 Parasect
    {  60,  55,  50,  45,  40,  7,  3 },  // 48 Venonat
    {  70,  65,  60,  90,  90,  7,  3 },  // 49 Venomoth
    {  10,  55,  30,  90,  35,  4,  0 },  // 50 Diglett-Alola
    {  35, 100,  60, 110,  50,  4,  0 },  // 51 Dugtrio-Alola
    {  40,  45,  35,  90,  40,  0,  0 },  // 52 Meowth-Gmax
    {  65,  60,  60, 115,  75,  0,  0 },  // 53 Persian-Alola
    {  50,  52,  48,  55,  50, 10, 10 },  // 54 Psyduck
    {  80,  82,  78,  85,  80, 10, 10 },  // 55 Golduck
    {  40,  80,  35,  70,  35,  1,  1 },  // 56 Mankey
    {  65, 105,  60,  95,  60,  1,  1 },  // 57 Primeape
    {  60,  75,  45,  55,  65,  9,  5 },  // 58 Growlithe-Hisui
    {  95, 115,  80,  90,  95,  9,  5 },  // 59 Arcanine-Hisui
    {  40,  50,  40,  90,  40, 10, 10 },  // 60 Poliwag
    {  65,  65,  65,  90,  50, 10, 10 },  // 61 Poliwhirl
    {  90,  85,  95,  70,  70, 10,  1 },  // 62 Poliwrath
    {  25,  20,  15,  90, 105, 13, 13 },  // 63 Abra
    {  40,  35,  30, 105, 120, 13, 13 },  // 64 Kadabra
    {  55,  50,  65, 150, 175, 13, 13 },  // 65 Alakazam-Mega
    {  70,  80,  50,  35,  35,  1,  1 },  // 66 Machop
    {  80, 100,  70,  45,  50,  1,  1 },  // 67 Machoke
    {  90, 130,  80,  55,  65,  1,  1 },  // 68 Machamp-Gmax
    {  50,  75,  35,  40,  70, 11,  3 },  // 69 Bellsprout
    {  65,  90,  50,  55,  85, 11,  3 },  // 70 Weepinbell
    {  80, 125,  85,  70, 135, 11,  3 },  // 71 Victreebel-Mega
    {  40,  40,  35,  70, 100, 10,  3 },  // 72 Tentacool
    {  80,  70,  65, 100, 120, 10,  3 },  // 73 Tentacruel
    {  40,  80, 100,  20,  30,  5, 12 },  // 74 Geodude-Alola
    {  55,  95, 115,  35,  45,  5, 12 },  // 75 Graveler-Alola
    {  80, 120, 130,  45,  55,  5, 12 },  // 76 Golem-Alola
    {  50,  85,  55,  90,  65, 13, 13 },  // 77 Ponyta-Galar
    {  65, 100,  70, 105,  80, 13,  0 },  // 78 Rapidash-Galar
    {  90,  65,  65,  15,  40, 13, 13 },  // 79 Slowpoke-Galar
    {  95, 100,  95,  30, 100,  3, 13 },  // 80 Slowbro-Galar
    {  25,  35,  70,  45,  95, 12, 12 },  // 81 Magnemite
    {  50,  60,  95,  70, 120, 12, 12 },  // 82 Magneton
    {  52,  95,  55,  55,  58,  1,  1 },  // 83 Farfetch\u2019d-Galar
    {  35,  85,  45,  75,  35,  0,  2 },  // 84 Doduo
    {  60, 110,  70, 100,  60,  0,  2 },  // 85 Dodrio
    {  65,  45,  55,  45,  70, 10, 10 },  // 86 Seel
    {  90,  70,  80,  70,  95, 10, 14 },  // 87 Dewgong
    {  80,  80,  50,  25,  40,  3,  0 },  // 88 Grimer-Alola
    { 105, 105,  75,  50,  65,  3,  0 },  // 89 Muk-Alola
    {  30,  65, 100,  40,  45, 10, 10 },  // 90 Shellder
    {  50,  95, 180,  70,  85, 10, 14 },  // 91 Cloyster
    {  30,  35,  30,  80, 100,  8,  3 },  // 92 Gastly
    {  45,  50,  45,  95, 115,  8,  3 },  // 93 Haunter
    {  60,  65,  60, 110, 130,  8,  3 },  // 94 Gengar-Gmax
    {  35,  45, 160,  70,  30,  5,  4 },  // 95 Onix
    {  60,  48,  45,  42,  90, 13, 13 },  // 96 Drowzee
    {  85,  73,  70,  67, 115, 13, 13 },  // 97 Hypno
    {  30, 105,  90,  50,  25, 10, 10 },  // 98 Krabby
    {  55, 130, 115,  75,  50, 10, 10 },  // 99 Kingler-Gmax
    {  40,  30,  50, 100,  55, 12, 11 },  // 100 Voltorb-Hisui
    {  60,  50,  70, 150,  80, 12, 11 },  // 101 Electrode-Hisui
    {  60,  40,  80,  40,  60, 11, 13 },  // 102 Exeggcute
    {  95, 105,  85,  45, 125, 11, 15 },  // 103 Exeggutor-Alola
    {  50,  50,  95,  35,  40,  4,  4 },  // 104 Cubone
    {  60,  80, 110,  45,  50,  9,  8 },  // 105 Marowak-Alola-Totem
    {  50, 120,  53,  87,  35,  1,  1 },  // 106 Hitmonlee
    {  50, 105,  79,  76,  35,  1,  1 },  // 107 Hitmonchan
    {  90,  55,  75,  30,  60,  0,  0 },  // 108 Lickitung
    {  40,  65,  95,  35,  60,  3,  3 },  // 109 Koffing
    {  65,  90, 120,  60,  85,  3,  0 },  // 110 Weezing-Galar
    {  80,  85,  95,  25,  30,  4,  5 },  // 111 Rhyhorn
    { 105, 130, 120,  40,  45,  4,  5 },  // 112 Rhydon
    { 250,   5,   5,  50, 105,  0,  0 },  // 113 Chansey
    {  65,  55, 115,  60, 100, 11, 11 },  // 114 Tangela
    { 105, 125, 100, 100,  60,  0,  0 },  // 115 Kangaskhan-Mega
    {  30,  40,  70,  60,  70, 10, 10 },  // 116 Horsea
    {  55,  65,  95,  85,  95, 10, 10 },  // 117 Seadra
    {  45,  67,  60,  63,  50, 10, 10 },  // 118 Goldeen
    {  80,  92,  65,  68,  80, 10, 10 },  // 119 Seaking
    {  30,  45,  55,  85,  70, 10, 10 },  // 120 Staryu
    {  60, 100, 105, 120, 130, 10, 13 },  // 121 Starmie-Mega
    {  50,  65,  65, 100,  90, 14, 13 },  // 122 Mr. Mime-Galar
    {  70, 110,  80, 105,  55,  7,  2 },  // 123 Scyther
    {  65,  50,  35,  95,  95, 14, 13 },  // 124 Jynx
    {  65,  83,  57, 105,  85, 12, 12 },  // 125 Electabuzz
    {  65,  95,  57,  93,  85,  9,  9 },  // 126 Magmar
    {  65, 155, 120, 105,  65,  7,  2 },  // 127 Pinsir-Mega
    {  75, 110, 105, 100,  30,  1, 10 },  // 128 Tauros-Paldea-Aqua
    {  20,  10,  55,  80,  20, 10, 10 },  // 129 Magikarp
    {  95, 155, 109,  81,  70, 10,  0 },  // 130 Gyarados-Mega
    { 130,  85,  80,  60,  85, 10, 14 },  // 131 Lapras-Gmax
    {  48,  48,  48,  48,  48,  0,  0 },  // 132 Ditto
    {  55,  55,  50,  55,  45,  0,  0 },  // 133 Eevee-Gmax
    { 130,  65,  60,  65, 110, 10, 10 },  // 134 Vaporeon
    {  65,  65,  60, 130, 110, 12, 12 },  // 135 Jolteon
    {  65, 130,  60,  65, 110,  9,  9 },  // 136 Flareon
    {  65,  60,  70,  40,  75,  0,  0 },  // 137 Porygon
    {  35,  40, 100,  35,  90,  5, 10 },  // 138 Omanyte
    {  70,  60, 125,  55, 115,  5, 10 },  // 139 Omastar
    {  30,  80,  90,  55,  45,  5, 10 },  // 140 Kabuto
    {  60, 115, 105,  80,  70,  5, 10 },  // 141 Kabutops
    {  80, 135,  85, 150,  70,  5,  2 },  // 142 Aerodactyl-Mega
    { 160, 110,  65,  30,  65,  0,  0 },  // 143 Snorlax-Gmax
    {  90,  85,  85,  95, 125, 13,  2 },  // 144 Articuno-Galar
    {  90, 125,  90, 100,  85,  1,  2 },  // 145 Zapdos-Galar
    {  90,  85,  90,  90, 100,  0,  2 },  // 146 Moltres-Galar
    {  41,  64,  45,  50,  50, 15, 15 },  // 147 Dratini
    {  61,  84,  65,  70,  70, 15, 15 },  // 148 Dragonair
    {  91, 124, 115, 100, 145, 15,  2 },  // 149 Dragonite-Mega
    { 106, 150,  70, 140, 194, 13, 13 },  // 150 Mewtwo-Mega-Y
    { 100, 100, 100, 100, 100, 13, 13 },  // 151 Mew
};
