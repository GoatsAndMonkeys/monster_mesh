#include "BattleWindow.h"
#include "BitmapFont.h"
#include "Gen2SpriteCache.h"
#include "Gen2ColorIcons.h"  // GEN2_COLOR_W/H
#include "Gen2BackIcons.h"   // GEN2_BACK_W/H

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>

// ── Logical resolution ──────────────────────────────────────────────────────
// 640x480 native — matches the RetroFlag GPI Case 2W panel.  Window is
// resizable; SDL_RenderSetLogicalSize handles the upscale.
static constexpr int LOGICAL_W = 640;
static constexpr int LOGICAL_H = 480;

// GBC remake palette: white card, GB paper-green frame, black ink for borders.
static constexpr SDL_Color COL_WHITE   = { 255, 255, 255, 255 };
static constexpr SDL_Color COL_PAPER   = { 0x9B, 0xBC, 0x0F, 255 };  // GB greenish
static constexpr SDL_Color COL_INK     = {   0,   0,   0, 255 };
static constexpr SDL_Color COL_DIMINK  = {  40,  40,  40, 255 };
static constexpr SDL_Color COL_HP_HI   = {  80, 200,  80, 255 };
static constexpr SDL_Color COL_HP_MID  = { 232, 200,  56, 255 };
static constexpr SDL_Color COL_HP_LO   = { 232,  72,  56, 255 };
static constexpr SDL_Color COL_EXP     = {  72, 160, 232, 255 };
static constexpr SDL_Color COL_SHADOW  = { 0xA0, 0xA0, 0xA0, 153 };  // 60% gray
static constexpr SDL_Color COL_HILITE  = { 0xFF, 0xE6, 0x4D, 255 };  // selection
static constexpr SDL_Color COL_BAR_BG  = { 0xE0, 0xE0, 0xE0, 255 };

BattleWindow::BattleWindow() {}
BattleWindow::~BattleWindow() { close(); }

// ── open / close ─────────────────────────────────────────────────────────────

bool BattleWindow::open() {
    if (window_) return true;

    // Make sure SDL_INIT_VIDEO is up.  If already initialised by someone
    // else, SDL_InitSubSystem is a no-op.
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            // SDL unavailable — fail silently, ncurses keeps going.
            return false;
        }
    } else {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
            return false;
        }
    }

    // Crunchy pixel scaling everywhere (texture upscale + logical-size letterbox).
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window_ = SDL_CreateWindow("MonsterMesh Battle",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               LOGICAL_W, LOGICAL_H,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) return false;

    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        // Try software fallback (e.g., headless / no GL).
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    SDL_RenderSetLogicalSize(renderer_, LOGICAL_W, LOGICAL_H);
    // Lock the logical-to-physical mapping to integer multiples so a 1300×900
    // window renders at exactly 2× with letterbox bars instead of fractionally
    // sampling pixels.  Combined with SDL_HINT_RENDER_SCALE_QUALITY=0 above,
    // every source pixel becomes an N×N block of identical output pixels —
    // no blending, no anti-aliasing, no dropped/doubled columns.
    SDL_RenderSetIntegerScale(renderer_, SDL_TRUE);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_ShowCursor(SDL_DISABLE);   // no mouse pointer over the battle window

    Gen2SpriteCache::init();
    initOk_ = true;
    return true;
}

void BattleWindow::close() {
    if (renderer_) {
        Gen2SpriteCache::clear();
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    initOk_ = false;
    // Leave SDL_INIT_VIDEO up — cheap to re-create the window next time and
    // SDL_Quit on shutdown drops everything anyway.
}

// ── setState / pumpEvents ───────────────────────────────────────────────────

void BattleWindow::setState(const State &s) { state_ = s; }

void BattleWindow::pumpEvents() {
    if (!window_) return;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            close();
            return;
        }
        // Keyboard is intentionally ignored — ncurses owns it.
    }
}

// ── Primitive helpers ───────────────────────────────────────────────────────

void BattleWindow::drawBox(int x, int y, int w, int h, SDL_Color fill, SDL_Color stroke) {
    // Fill
    SDL_SetRenderDrawColor(renderer_, fill.r, fill.g, fill.b, fill.a);
    SDL_Rect rc = { x, y, w, h };
    SDL_RenderFillRect(renderer_, &rc);

    // 2-px stroke with chamfered corners (a poor-man's rounded edge — drop one
    // pixel out of each of the 4 corners on each line so it reads as rounded
    // at this scale).
    SDL_SetRenderDrawColor(renderer_, stroke.r, stroke.g, stroke.b, stroke.a);
    // Top/bottom edges
    for (int i = 0; i < 2; i++) {
        SDL_Rect top    = { x + 2, y + i,         w - 4, 1 };
        SDL_Rect bot    = { x + 2, y + h - 1 - i, w - 4, 1 };
        SDL_RenderFillRect(renderer_, &top);
        SDL_RenderFillRect(renderer_, &bot);
    }
    // Left/right edges
    for (int i = 0; i < 2; i++) {
        SDL_Rect lft    = { x + i,         y + 2, 1, h - 4 };
        SDL_Rect rgt    = { x + w - 1 - i, y + 2, 1, h - 4 };
        SDL_RenderFillRect(renderer_, &lft);
        SDL_RenderFillRect(renderer_, &rgt);
    }
    // Corner dots — pull in the diagonal so the corner reads as rounded.
    SDL_Rect cTL = { x + 1, y + 1, 1, 1 };
    SDL_Rect cTR = { x + w - 2, y + 1, 1, 1 };
    SDL_Rect cBL = { x + 1, y + h - 2, 1, 1 };
    SDL_Rect cBR = { x + w - 2, y + h - 2, 1, 1 };
    SDL_RenderFillRect(renderer_, &cTL);
    SDL_RenderFillRect(renderer_, &cTR);
    SDL_RenderFillRect(renderer_, &cBL);
    SDL_RenderFillRect(renderer_, &cBR);
}

// Gen-1 status byte -> 3-letter battle-card tag.  Sleep is the low 3 bits (the
// turn counter); poison/burn/freeze/paralysis are single bits.
static const char *statusTag(uint8_t st) {
    if (st & 0x07) return "SLP";
    if (st & 0x08) return "PSN";
    if (st & 0x10) return "BRN";
    if (st & 0x20) return "FRZ";
    if (st & 0x40) return "PAR";
    return nullptr;
}

void BattleWindow::drawHpBar(int x, int y, int w, int h, uint16_t hp, uint16_t maxHp) {
    // Background
    SDL_SetRenderDrawColor(renderer_, COL_BAR_BG.r, COL_BAR_BG.g, COL_BAR_BG.b, COL_BAR_BG.a);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(renderer_, &bg);
    // Fill
    double pct = (maxHp > 0) ? (double)hp / (double)maxHp : 0.0;
    if (pct < 0.0) pct = 0.0;
    if (pct > 1.0) pct = 1.0;
    int fillW = (int)(w * pct);
    SDL_Color fg = (pct > 0.5) ? COL_HP_HI : (pct > 0.20 ? COL_HP_MID : COL_HP_LO);
    SDL_SetRenderDrawColor(renderer_, fg.r, fg.g, fg.b, fg.a);
    SDL_Rect fr = { x, y, fillW, h };
    SDL_RenderFillRect(renderer_, &fr);
    // 1-px ink stroke
    SDL_SetRenderDrawColor(renderer_, COL_INK.r, COL_INK.g, COL_INK.b, COL_INK.a);
    SDL_RenderDrawRect(renderer_, &bg);
}

void BattleWindow::drawExpBar(int x, int y, int w, int h) {
    SDL_SetRenderDrawColor(renderer_, COL_BAR_BG.r, COL_BAR_BG.g, COL_BAR_BG.b, COL_BAR_BG.a);
    SDL_Rect bg = { x, y, w, h };
    SDL_RenderFillRect(renderer_, &bg);
    SDL_SetRenderDrawColor(renderer_, COL_EXP.r, COL_EXP.g, COL_EXP.b, COL_EXP.a);
    double xp = state_.expPermille / 1000.0;
    if (xp < 0.0) xp = 0.0;
    if (xp > 1.0) xp = 1.0;
    SDL_Rect fr = { x, y, (int)(w * xp), h };     // real EXP progress to next level
    SDL_RenderFillRect(renderer_, &fr);
    SDL_SetRenderDrawColor(renderer_, COL_INK.r, COL_INK.g, COL_INK.b, COL_INK.a);
    SDL_RenderDrawRect(renderer_, &bg);
}

void BattleWindow::drawShadowEllipse(int cx, int cy, int rx, int ry, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    // Filled ellipse via scanline raster — cheap; runs once per frame each side.
    for (int yy = -ry; yy <= ry; yy++) {
        double t = (double)yy / (double)ry;
        double scale = 1.0 - t * t;
        if (scale < 0.0) scale = 0.0;
        int hw = (int)(rx * (scale > 0 ? sqrt(scale) : 0));
        SDL_Rect line = { cx - hw, cy + yy, hw * 2, 1 };
        SDL_RenderFillRect(renderer_, &line);
    }
}

// ── Layout ──────────────────────────────────────────────────────────────────
//
// 640x480 canvas, entirely white (GBC remake card look — no green frame).
// Battle scene occupies the top region; message log + FIGHT panel sit below
// with thin black ink strokes separating them.

void BattleWindow::drawBackground() {
    // Fill the whole canvas white — no green margin, no card border.  The
    // user prefers the pure-white GBC battle look, so the entire 640×480
    // logical surface is just paper.
    SDL_SetRenderDrawColor(renderer_, COL_WHITE.r, COL_WHITE.g, COL_WHITE.b, COL_WHITE.a);
    SDL_Rect full = { 0, 0, LOGICAL_W, LOGICAL_H };
    SDL_RenderFillRect(renderer_, &full);
}

void BattleWindow::drawFoePlatform() {
    // Foe platform: smaller (perspective).  Centered under foe sprite.
    // cy lowered 165->182 so a 3× (192px) foe fits under the top edge without
    // clipping while its feet still rest on the ellipse.
    int cx = 470;
    int cy = 182;
    drawShadowEllipse(cx, cy, 110, 18, COL_SHADOW);
}

void BattleWindow::drawYouPlatform() {
    // Sprite is anchored to this platform's cy even though we no longer
    // paint the ellipse — keeping the call dead-coded is cheaper than
    // re-plumbing the anchor.  cy=255 pulls the you sprite up another
    // 8 px (was 263) so the message log can claim 8 more px below.
    int cx = 200;
    int cy = 255;
    drawShadowEllipse(cx, cy, 140, 22, COL_SHADOW);
}

// Sprite zoom — integer multiple so every source pixel becomes an N×N
// output block (bit-perfect, no blending).  3× of native 64 = 192px on the
// 640×480 GPi panel.  The player rises out of the log box (box drawn over its
// lower body); the foe sits in open space up top, so it's re-anchored (lower
// foot + shadow) to keep all 192px on-screen without clipping.
static constexpr int SPRITE_SCALE = 3;  // native 64 -> 192px, bit-perfect

void BattleWindow::drawFoeSprite() {
    if (state_.foe.species < 1 || state_.foe.species > 386) return;
    int srcW, srcH;
    Gen2SpriteCache::dims(false, &srcW, &srcH);  // 64x64 native
    SDL_Texture *tex = Gen2SpriteCache::get(renderer_, state_.foe.species, false);
    if (!tex) return;
    // 2× scale → 128×128 (bit-perfect).  Anchored so the foot of the sprite sits on the
    // platform ellipse, centered horizontally over (470, 165).
    int dstW = srcW * SPRITE_SCALE;
    int dstH = srcH * SPRITE_SCALE;
    // Foot rests on the platform ellipse (cy=182); clamp the top so a tall 3×
    // sprite never clips above the screen edge.
    int fy = 182 - dstH + 14;
    if (fy < 2) fy = 2;
    SDL_Rect dst = { 470 - dstW / 2, fy, dstW, dstH };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
}

void BattleWindow::drawYouSprite() {
    if (state_.you.species < 1 || state_.you.species > 386) return;
    int srcW, srcH;
    Gen2SpriteCache::dims(true, &srcW, &srcH);  // 64x64 native
    SDL_Texture *tex = Gen2SpriteCache::get(renderer_, state_.you.species, true);
    if (!tex) return;
    // 2× scale → 128×128 (bit-perfect).
    int dstW = srcW * SPRITE_SCALE;
    int dstH = srcH * SPRITE_SCALE;
    SDL_Rect dst = { 200 - dstW / 2, 295 - dstH + 22, dstW, dstH };
    SDL_RenderCopy(renderer_, tex, nullptr, &dst);
}

void BattleWindow::drawFoeBox() {
    // Top-left status box: FOE
    int bx = 32, by = 36;
    int bw = 240, bh = 76;
    drawBox(bx, by, bw, bh, COL_WHITE, COL_INK);

    // Corner tag — normally "FOE", but the pentest ROM puts the WiFi network
    // name here so the opponent reads as the target network.
    BitmapFont::drawString(renderer_, bx + 8, by + 6,
                           state_.foeTag[0] ? state_.foeTag : "FOE",
                           COL_DIMINK, 1);

    // NICKNAME  Lv##
    char line[32];
    snprintf(line, sizeof(line), "%s", state_.foe.nickname[0] ? state_.foe.nickname : "?");
    BitmapFont::drawString(renderer_, bx + 8, by + 20, line, COL_INK, 2);

    char lv[16];
    snprintf(lv, sizeof(lv), "L%d", (int)state_.foe.level);
    int lvW = BitmapFont::stringWidth(lv, 2);
    BitmapFont::drawString(renderer_, bx + bw - 8 - lvW, by + 20, lv, COL_INK, 2);
    // Status tag (SLP/PAR/PSN/BRN/FRZ) just left of the level.  Confusion is a
    // volatile condition (not in the status byte), so fall back to "CNF" when
    // there's no persistent status to show.
    const char *st = statusTag(state_.foe.status);
    if (!st && state_.foe.confused) st = "CNF";
    if (st) {
        int stW = BitmapFont::stringWidth(st, 2);
        BitmapFont::drawString(renderer_, bx + bw - 8 - lvW - 10 - stW, by + 20,
                               st, COL_INK, 2);
    }

    // HP label at scale 2 so it reads at arm's length on the GPI Case.
    // 24-px "HP" + 6-px gap → bar starts at bx + 38.
    BitmapFont::drawString(renderer_, bx + 8, by + 46, "HP", COL_INK, 2);
    drawHpBar(bx + 38, by + 50, bw - 46, 10, state_.foe.hp, state_.foe.maxHp);
}

void BattleWindow::drawYouBox() {
    // Moved up another 8 px (was by=200) to follow the you platform shift.
    int bx = 368, by = 192;
    int bw = 240, bh = 88;
    drawBox(bx, by, bw, bh, COL_WHITE, COL_INK);

    // Header tag: "<short> / <trainer>" if the daemon has shipped them,
    // else fall back to the classic "YOU".  Stays at scale 1 in the
    // corner so it doesn't push the nickname row down.
    char who[24];
    if (state_.localShort[0] && state_.localTrainer[0])
        snprintf(who, sizeof(who), "%s / %s", state_.localShort, state_.localTrainer);
    else if (state_.localShort[0])
        snprintf(who, sizeof(who), "%s", state_.localShort);
    else
        snprintf(who, sizeof(who), "YOU");
    BitmapFont::drawString(renderer_, bx + 8, by + 6, who, COL_DIMINK, 1);

    char line[32];
    snprintf(line, sizeof(line), "%s", state_.you.nickname[0] ? state_.you.nickname : "?");
    BitmapFont::drawString(renderer_, bx + 8, by + 20, line, COL_INK, 2);

    char lv[16];
    snprintf(lv, sizeof(lv), "L%d", (int)state_.you.level);
    int lvW = BitmapFont::stringWidth(lv, 2);
    BitmapFont::drawString(renderer_, bx + bw - 8 - lvW, by + 20, lv, COL_INK, 2);
    // Status tag (SLP/PAR/PSN/BRN/FRZ) just left of the level.  Confusion is a
    // volatile condition (not in the status byte), so fall back to "CNF" when
    // there's no persistent status to show.
    const char *st = statusTag(state_.you.status);
    if (!st && state_.you.confused) st = "CNF";
    if (st) {
        int stW = BitmapFont::stringWidth(st, 2);
        BitmapFont::drawString(renderer_, bx + bw - 8 - lvW - 10 - stW, by + 20,
                               st, COL_INK, 2);
    }

    // HP row: scale-2 "HP" label, scale-1 bar shrunk to leave room for the
    // scale-2 numeric on the right.  24-px label + 6-px gap = bar starts
    // at bx+38; numeric "278/278" at scale 2 is ~84 px, anchored at
    // bx + bw - 8 - 84.
    BitmapFont::drawString(renderer_, bx + 8, by + 46, "HP", COL_INK, 2);
    char hps[20];
    snprintf(hps, sizeof(hps), "%3d/%-3d", (int)state_.you.hp, (int)state_.you.maxHp);
    int hpsW = BitmapFont::stringWidth(hps, 2);
    int hpsX = bx + bw - 8 - hpsW;
    // HP bar starts at the same x as the EXP bar (bx+46) so their left edges
    // line up — the wider "EXP" label sets the shared left margin.
    int barW = hpsX - (bx + 46) - 8;
    if (barW < 20) barW = 20;
    drawHpBar(bx + 46, by + 50, barW, 10, state_.you.hp, state_.you.maxHp);
    BitmapFont::drawString(renderer_, hpsX, by + 46, hps, COL_INK, 2);

    // EXP row: same scale-2 label treatment.  Bar runs full remaining width and
    // is the same thickness as the HP bar (there's room in the box).
    BitmapFont::drawString(renderer_, bx + 8, by + 68, "EXP", COL_INK, 2);
    drawExpBar(bx + 46, by + 70, bw - 54, 10);
}

void BattleWindow::drawMessageLog() {
    // Pulled up to y=285 (right under the you sprite at y=277, 8 px gap)
    // and bh grown to 107 px.  Combined with the 17-px line stride below
    // (scale-2 glyphs are 14 px tall, plus 3 px breathing room so lines
    // don't touch), the box fits ~5–6 dialogue lines without the previous
    // ascender/descender collisions ("NIDOKING" running into the line
    // above, etc.).
    int bx = 16, by = 285;
    int bw = LOGICAL_W - 32;
    // Pentest Pikachu has no move panel, so the log grows to fill the bottom of
    // the screen (a big scrolling readout of the exploit run).
    int bh = state_.pentest ? (LOGICAL_H - 16 - by) : 107;
    drawBox(bx, by, bw, bh, COL_WHITE, COL_INK);

    // Header is optional (gym round, "vs MEWTWO_OF_DOOM", etc.)
    int textY = by + 8;
    if (state_.header[0]) {
        BitmapFont::drawString(renderer_, bx + 10, textY, state_.header, COL_DIMINK, 1);
        textY += 10;
    }

    // 14-px scale-2 glyph + 3 px breathing room — readable, no overlap.
    static constexpr int LINE_STRIDE = BitmapFont::GLYPH_H * 2 + 3;  // 17 px
    int maxLines = (bh - (textY - by) - 8) / LINE_STRIDE;
    if (maxLines < 1) maxLines = 1;

    int n = (int)state_.log.size();
    int start = std::max(0, n - maxLines);
    int maxChars = (bw - 20) / (BitmapFont::GLYPH_ADVANCE * 2);
    for (int i = start; i < n; i++) {
        char line[260];
        if (state_.menuMode) {
            snprintf(line, sizeof(line), "%s", state_.log[i].c_str());
        } else {
            snprintf(line, sizeof(line), "> %s", state_.log[i].c_str());
        }
        if ((int)strlen(line) > maxChars) line[maxChars] = '\0';
        BitmapFont::drawString(renderer_, bx + 10, textY, line, COL_INK, 2);
        textY += LINE_STRIDE;
    }
}

void BattleWindow::drawBottomPanel() {
    // Pentest Pikachu: no moves — the enlarged message log fills this space.
    if (state_.pentest) return;
    // Panel needs to host the SWITCH 2×3 grid (which expects 3 rows tall
    // for the existing terminal nav code).  3 rows × 18 px = 54 px, plus
    // 8 px top/bottom internal padding = 64 px panel total.
    int bx = 16, by = 400;
    int bw = LOGICAL_W - 32;
    int bh = LOGICAL_H - by - 16;          // 64 px tall
    drawBox(bx, by, bw, bh, COL_WHITE, COL_INK);

    if (state_.mode == Mode::SWITCH) {
        // 2×3 grid (2 columns × 3 rows) to match the existing terminal
        // switchButton nav code — LEFT/RIGHT swaps column, UP/DOWN walks
        // rows.  Single line per slot: nickname (scale 2) + level/HP
        // (scale 1) at a shared cell-relative offset for column alignment.
        int cellW = (bw - 16) / 2;
        int cellH = (bh - 8) / 3;
        int maxNickW = 0;
        for (int i = 0; i < state_.switchCount && i < 6; i++) {
            const auto &s = state_.switchSlots[i];
            char name[24];
            snprintf(name, sizeof(name), "%s%s",
                     s.nickname[0] ? s.nickname : "?",
                     s.fainted ? " FNT" : (s.active ? " *" : ""));
            int w = BitmapFont::stringWidth(name, 2);
            if (w > maxNickW) maxNickW = w;
        }
        int hpOffset = maxNickW + 12;
        // Vertical-centre the scale-2 text in the cell.  Both nickname
        // and HP are scale 2 (14 px tall) and ride the same baseline.
        int textY = (cellH - 14) / 2;
        for (int i = 0; i < state_.switchCount && i < 6; i++) {
            int row = i / 2;
            int col = i % 2;
            int cx = bx + 8 + col * cellW;
            int cy = by + 4 + row * cellH;
            const auto &s = state_.switchSlots[i];
            if (i == state_.selectedSwitch) {
                SDL_SetRenderDrawColor(renderer_, COL_HILITE.r, COL_HILITE.g, COL_HILITE.b, COL_HILITE.a);
                // Highlight = the full cell, no top/bottom offset, so it
                // sits symmetric around the centred text.
                SDL_Rect hi = { cx - 2, cy, cellW - 4, cellH };
                SDL_RenderFillRect(renderer_, &hi);
            }
            char name[24];
            snprintf(name, sizeof(name), "%s%s",
                     s.nickname[0] ? s.nickname : "?",
                     s.fainted ? " FNT" : (s.active ? " *" : ""));
            BitmapFont::drawString(renderer_, cx + 4, cy + textY, name,
                                   s.fainted ? COL_DIMINK : COL_INK, 2);
            // Level + HP at scale 2 so they're as legible as the nickname.
            // Sits on the same baseline (cy + textY) as the nickname now
            // that both glyphs are 14 px tall.
            char hp[24];
            snprintf(hp, sizeof(hp), "L%d  %d/%d",
                     (int)s.level, (int)s.hp, (int)s.maxHp);
            BitmapFont::drawString(renderer_, cx + 4 + hpOffset, cy + textY,
                                   hp, COL_DIMINK, 2);
        }
        return;
    }

    // FIGHT 2×2 grid — single-line rows: move name (scale 2) on the left,
    // PP (scale 1) at a shared cell-relative offset so PP columns line up
    // regardless of how many letters a move name has.  Pre-compute the
    // widest scale-2 move name across all four slots and place every PP
    // at name_x + maxNameW + 12 px.
    int cellW = (bw - 16) / 2;
    int cellH = (bh - 8) / 2;
    int maxNameW = 0;
    for (int i = 0; i < 4; i++) {
        const auto &mv = state_.moves[i];
        if (!mv.slotUsed) continue;
        int w = BitmapFont::stringWidth(mv.name, 2);
        if (w > maxNameW) maxNameW = w;
    }
    int ppOffset = maxNameW + 12;   // PP x within each cell, relative to text origin
    // Same vertical centring as SWITCH: scale-2 name centred in cellH,
    // scale-1 PP on the same baseline.
    int textY = (cellH - 14) / 2;
    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int cx = bx + 8 + col * cellW;
        int cy = by + 4 + row * cellH;
        if (i == state_.selectedMove) {
            SDL_SetRenderDrawColor(renderer_, COL_HILITE.r, COL_HILITE.g, COL_HILITE.b, COL_HILITE.a);
            SDL_Rect hi = { cx - 2, cy, cellW - 4, cellH };
            SDL_RenderFillRect(renderer_, &hi);
        }
        const auto &mv = state_.moves[i];
        if (!mv.slotUsed) {
            BitmapFont::drawString(renderer_, cx + 4, cy + textY, "---", COL_DIMINK, 2);
            continue;
        }
        BitmapFont::drawString(renderer_, cx + 4, cy + textY, mv.name, COL_INK, 2);
        // PP at scale 2 — same baseline as the move name (both 14 px tall
        // now, no subY shift needed).
        char pp[24];
        snprintf(pp, sizeof(pp), "PP %2d/%2d", (int)mv.pp, (int)mv.maxPp);
        BitmapFont::drawString(renderer_, cx + 4 + ppOffset, cy + textY, pp, COL_DIMINK, 2);
    }
}

void BattleWindow::drawEndOverlay() {
    if (state_.endResult == EndResult::ONGOING) return;
    const char *msg = "BATTLE END";
    SDL_Color tint = COL_INK;
    switch (state_.endResult) {
        case EndResult::WIN:  msg = "YOU WIN!";    tint = COL_HP_HI; break;
        case EndResult::LOSE: msg = "YOU LOSE..."; tint = COL_HP_LO; break;
        case EndResult::DRAW: msg = "DRAW!";       tint = COL_EXP;   break;
        default: break;
    }
    // Dim the white card so the result stands out.
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 110);
    SDL_Rect dim = { 16, 16, LOGICAL_W - 32, LOGICAL_H - 32 };
    SDL_RenderFillRect(renderer_, &dim);

    int scale = 5;
    int w = BitmapFont::stringWidth(msg, scale);
    int x = (LOGICAL_W - w) / 2;
    int y = (LOGICAL_H - BitmapFont::GLYPH_H * scale) / 2;
    // Drop shadow
    BitmapFont::drawString(renderer_, x + 3, y + 3, msg, COL_INK, scale);
    BitmapFont::drawString(renderer_, x,     y,     msg, tint,    scale);
}

// ── Main render ─────────────────────────────────────────────────────────────

void BattleWindow::render() {
    if (!renderer_) return;

    // Letterbox bars (visible when the window's aspect != 4:3 thanks to
    // SDL_RenderSetIntegerScale) paint white too, so the whole window reads
    // as one continuous GBC-card background regardless of how the user
    // resizes it.
    SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
    SDL_RenderClear(renderer_);

    drawBackground();

    // Diagonal layout: foe top-right, you bottom-left.  Platforms
    // (drawFoePlatform / drawYouPlatform) intentionally not called — the
    // gray shadow ellipse showed through every transparent pixel inside
    // the sprite body and looked terrible.  Sprites float on white now,
    // which matches the Gen 1 battle look anyway.
    drawFoeSprite();
    drawYouSprite();

    drawFoeBox();
    drawYouBox();

    drawMessageLog();
    drawBottomPanel();

    drawEndOverlay();

    SDL_RenderPresent(renderer_);
}
