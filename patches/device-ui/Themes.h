#pragma once

#include "lvgl.h"
class Themes
{
  public:
    enum Theme { eDark, eLight, eDmgGreen, eGbcGreen, ePocketGreen, ePokemonBlue, ePokemonRed };

    static void initStyles(void);
    static enum Theme get(void);
    static void set(enum Theme th);
    static void recolorButton(lv_obj_t *obj, bool enabled, lv_opa_t opa = 255);
    static void recolorImage(lv_obj_t *obj, bool enabled);
    static void recolorText(lv_obj_t *obj, bool enabled);
    static void recolorTopLabel(lv_obj_t *obj, bool alert);
    static void recolorTableRow(lv_draw_fill_dsc_t *fill_draw_dsc, bool odd);

    // Expose themed colors for external consumers (MonsterMeshTerminal, etc.)
    static uint32_t darkest();   // shade 0 (bg)
    static uint32_t dark();      // shade 1 (input bg)
    static uint32_t mid();       // shade 3 (borders, separators)
    static uint32_t accent();    // shade 4 (input border)
    static uint32_t light();     // shade 5 (secondary text)
    static uint32_t lightest();  // shade 6 (primary text, icons)

  private:
    Themes(void) = default;
};