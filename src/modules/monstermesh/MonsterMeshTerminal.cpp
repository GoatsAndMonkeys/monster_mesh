// SPDX-License-Identifier: MIT
// See MonsterMeshTerminal.h.

#include "MonsterMeshTerminal.h"
#include "Gen1Species.h"
#include "configuration.h"

#if defined(T_DECK) && !MESHTASTIC_EXCLUDE_MONSTERMESH && HAS_TFT

#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include "graphics/view/TFT/Themes.h"

LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_cozette_13;

// LVGL event callbacks need C-style functions; we keep an instance pointer
// so they can dispatch to the right MonsterMeshTerminal.
static MonsterMeshTerminal *g_termInstance = nullptr;

static void term_input_ready_cb(lv_event_t *e)
{
    if (!g_termInstance) return;
    lv_obj_t *ta = lv_event_get_target_obj(e);
    if (!ta) return;
    const char *txt = lv_textarea_get_text(ta);
    g_termInstance->onSubmit(txt);
    lv_textarea_set_text(ta, "");
}

void MonsterMeshTerminal::open(lv_obj_t *parent)
{
    if (panel_) {
        // Already built — unhide, clear scrollback, refresh party, refocus.
        lv_obj_clear_flag(panel_, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(input_, "");
        inbuf_[0] = '\0'; inlen_ = 0;
        open_ = true;
        clearOutput();
        showParty();
        prompt();
        lv_group_focus_obj(input_);
        return;
    }

    panel_ = lv_obj_create(parent);
    lv_obj_set_size(panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_align(panel_, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(panel_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(panel_, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(Themes::darkest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(panel_, lv_color_hex(Themes::lightest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(panel_, &lv_font_cozette_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(panel_, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(panel_, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(panel_, LV_SCROLLBAR_MODE_OFF);

    output_ = lv_obj_create(panel_);
    lv_obj_set_size(output_, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(output_, 1);
    lv_obj_set_scrollbar_mode(output_, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_scroll_dir(output_, LV_DIR_VER);
    lv_obj_set_style_bg_color(output_, lv_color_hex(Themes::darkest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(output_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(output_, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(output_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(output_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_layout(output_, LV_LAYOUT_FLEX, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_flow(output_, LV_FLEX_FLOW_COLUMN, LV_PART_MAIN | LV_STATE_DEFAULT);

    input_ = lv_textarea_create(panel_);
    lv_obj_set_size(input_, LV_PCT(100), 22);
    lv_textarea_set_one_line(input_, true);
    lv_textarea_set_placeholder_text(input_, "type and press enter");
    lv_obj_set_style_bg_color(input_, lv_color_hex(Themes::dark()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(input_, lv_color_hex(Themes::lightest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(input_, &lv_font_cozette_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(input_, lv_color_hex(Themes::accent()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(input_, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(input_, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(input_, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    open_ = true;
    g_termInstance = this;
    lv_obj_add_event_cb(input_, term_input_ready_cb, LV_EVENT_READY, nullptr);
    lv_group_focus_obj(input_);
    // Title is in the top bar now; the terminal output starts with the
    // player's party (or a hint if no SAV is loaded yet).
    showParty();
    prompt();
}

void MonsterMeshTerminal::onSubmit(const char *line)
{
    if (!line) line = "";
    println(line);
    executeLine(line);
    inbuf_[0] = '\0';
    inlen_    = 0;
    prompt();
}

void MonsterMeshTerminal::close()
{
    if (panel_) lv_obj_add_flag(panel_, LV_OBJ_FLAG_HIDDEN);
    open_ = false;
}

void MonsterMeshTerminal::onKey(uint8_t key)
{
    if (!open_) return;
    if (key == 0x0D || key == '\n') {
        println(inbuf_);
        executeLine(inbuf_);
        inbuf_[0] = '\0'; inlen_ = 0;
        if (input_) lv_textarea_set_text(input_, "");
        prompt();
        return;
    }
    if (key == 0x08) {
        if (inlen_ > 0) {
            inlen_--;
            inbuf_[inlen_] = '\0';
            if (input_) lv_textarea_set_text(input_, inbuf_);
        }
        return;
    }
    if (key < 32 || key > 126) return;
    if (inlen_ + 1 >= sizeof(inbuf_)) return;
    inbuf_[inlen_++] = (char)key;
    inbuf_[inlen_] = '\0';
    if (input_) lv_textarea_set_text(input_, inbuf_);
}

void MonsterMeshTerminal::setParty(const Gen1Party &p)
{
    party_ = p;
    partyLoaded_ = (p.count > 0 && p.count <= 6);
}

void MonsterMeshTerminal::println(const char *s)
{
    if (!output_) return;
    lv_obj_t *lbl = lv_label_create(output_);
    lv_label_set_text(lbl, s ? s : "");
    lv_obj_set_style_text_color(lbl, lv_color_hex(Themes::lightest()), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_cozette_13, LV_PART_MAIN | LV_STATE_DEFAULT);
    // Auto-scroll to bottom by scrolling to the new label.
    lv_obj_scroll_to_view(lbl, LV_ANIM_OFF);
}

void MonsterMeshTerminal::prompt()
{
    println("> _");
}

void MonsterMeshTerminal::showParty()
{
    if (!partyLoaded_ || party_.count == 0) {
        println("no party loaded.");
        println("open a ROM in the boot loader first.");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Party (%u):", (unsigned)party_.count);
    println(buf);
    for (uint8_t i = 0; i < party_.count && i < 6; i++) {
        char nick[12] = {};
        gen1NameToAscii(party_.nicknames[i], 11, nick, sizeof(nick));
        snprintf(buf, sizeof(buf), "  %u. %s  Lv%u  spc=%u",
                 (unsigned)(i + 1),
                 nick[0] ? nick : "(no nickname)",
                 (unsigned)party_.mons[i].level,
                 (unsigned)party_.species[i]);
        println(buf);
    }
}

void MonsterMeshTerminal::clearOutput()
{
    if (!output_) return;
    while (lv_obj_get_child_count(output_) > 0) {
        lv_obj_t *c = lv_obj_get_child(output_, 0);
        if (c) lv_obj_delete(c);
    }
}

void MonsterMeshTerminal::executeLine(const char *line)
{
    while (*line == ' ') line++;
    if (*line == '\0') return;
    if (strncmp(line, "help", 4) == 0) {
        println("commands:");
        println("  help        - this list");
        println("  version     - firmware build");
        println("  party       - show your loaded SAV party");
        println("  echo <text> - print <text>");
        println("  clear       - wipe screen");
        return;
    }
    if (strncmp(line, "version", 7) == 0) {
        char buf[64];
#ifdef MONSTERMESH_BUILD
#define MM_STRINGIFY_(x) #x
#define MM_STRINGIFY(x) MM_STRINGIFY_(x)
        snprintf(buf, sizeof(buf), "MonsterMesh build " MM_STRINGIFY(MONSTERMESH_BUILD));
#else
        snprintf(buf, sizeof(buf), "MonsterMesh build (unknown)");
#endif
        println(buf);
        return;
    }
    if (strncmp(line, "echo ", 5) == 0) {
        println(line + 5);
        return;
    }
    if (strcmp(line, "echo") == 0) {
        println("");
        return;
    }
    if (strncmp(line, "clear", 5) == 0) {
        clearOutput();
        return;
    }
    if (strncmp(line, "party", 5) == 0) {
        showParty();
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "unknown: %s", line);
    println(buf);
}

#endif // T_DECK && !MESHTASTIC_EXCLUDE_MONSTERMESH && HAS_TFT
