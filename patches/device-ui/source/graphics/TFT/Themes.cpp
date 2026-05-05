#ifdef VIEW_320x240

#include "graphics/view/TFT/Themes.h"
#include "stdint.h"

// ── Green palette definitions (7 shades each: 4 base + 3 interpolated) ──────

// A) Classic DMG LCD — the iconic yellow-green Game Boy screen
#define DMG_0  0xff0F380F  // darkest  (base)
#define DMG_1  0xff1B4A1B  // interp
#define DMG_2  0xff275C27  // interp
#define DMG_3  0xff306230  // dark     (base)
#define DMG_4  0xff5A8A1E  // interp
#define DMG_5  0xff8BAC0F  // light    (base)
#define DMG_6  0xff9BBC0F  // lightest (base)

// B) GBC Dark — teal-shifted Game Boy Color style
#define GBC_0  0xff081820  // darkest  (base)
#define GBC_1  0xff1E4038  // interp
#define GBC_2  0xff2A5447  // interp
#define GBC_3  0xff346856  // dark     (base)
#define GBC_4  0xff5E9464  // interp
#define GBC_5  0xff88C070  // light    (base)
#define GBC_6  0xffE0F8D0  // lightest (base)

// E) GB Pocket — high contrast, bright lightest shade
#define PKT_0  0xff0F380F  // darkest  (base)
#define PKT_1  0xff1A4C1A  // interp
#define PKT_2  0xff256025  // interp
#define PKT_3  0xff306230  // dark     (base)
#define PKT_4  0xff5C9C3C  // interp
#define PKT_5  0xff88D048  // light    (base)
#define PKT_6  0xffC4E878  // lightest (base)

// F) Pokemon Red cartridge — light grey shell tinted red
#define RED_0  0xff300000
#define RED_1  0xff5A0808
#define RED_2  0xff841010
#define RED_3  0xffB81818
#define RED_4  0xffE85050
#define RED_5  0xffFF9090
#define RED_6  0xffFFE0E0

// G) Pokemon Blue cartridge — light grey shell tinted blue
#define BLU_0  0xff000820
#define BLU_1  0xff081848
#define BLU_2  0xff102870
#define BLU_3  0xff1838A0
#define BLU_4  0xff5878D0
#define BLU_5  0xff90B0F0
#define BLU_6  0xffE0E8FF

LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_cozette_13;

static enum Themes::Theme theme = Themes::eGbcGreen;

Themes::Theme Themes::get(void)
{
    return theme;
}

// All themes use cozette pixel fonts — return true unconditionally so the
// existing isGreen() font/line-spacing gates apply to Dark, Light, Red, and
// Blue too. Originally only the 3 green themes used cozette.
static inline bool isGreen() { return true; }

enum ThemeColor {
    eMainScreenStyle,
    eTopPanelBg,
    eTopPanelText,
    eTopImageBg,
    eTopImageRecolor,
    eTopImageRecolorOpa,
    ePositiveImageRecolor,
    ePanelBg,
    ePanelPressedBg,
    ePanelText,
    ePanelBorder,
    eNodePanelBg,
    eNodePanelBorder,
    eNodePanelText,
    eNodeButtonBg,
    eNodeButtonBgOpa,
    eButtonPanelBg,
    eMainButtonBg,
    eMainButtonText,
    eMainButtonBorder,
    eMainButtonShadow,
    eMainButtonImageRecolor,
    eMainButtonImageRecolorOpa,
    eHomeContainerBg,
    eHomeContainerBorder,
    eHomeContainerShadow,
    eHomeContainerText,
    eHomeButtonBg,
    eHomeButtonText,
    eHomeButtonBorder,
    eHomeButtonImageRecolor,
    eHomeButtonImageRecolorOpa,
    eChannelButtonBg,
    eChannelButtonBorder,
    eChannelButtonText,
    eSettingsPanelBg,
    eSettingsPanelText,
    eSettingsPanelBorder,
    eSettingsPanelShadow,
    eSettingsPanelBgOpa,
    eSettingsButtonBg,
    eSettingsButtonText,
    eSettingsButtonBorder,
    eSettingsButtonImageRecolor,
    eSettingsButtonImageRecolorOpa,
    eSettingsLabelBg,
    eSettingsLabelBorder,
    eTabViewBg,
    eTabViewText,
    eTabButtonDefaultBg,
    eTabButtonActiveBg,
    eTabButtonPressedBg,
    eTabButtonDefaultText,
    eTabButtonActiveText,
    eTabButtonPressedText,
    eTabButtonDefaultBorder,
    eChatMessageBg,
    eChatMessageBgOpa,
    eChatMessageText,
    eChatMessageBorder,
    eNewMessageBg,
    eNewMessageBgOpa,
    eNewMessageText,
    eNewMessageBorder,
    eAlertPanelBg,
    eBtnMatrixBorderMain,
    eBtnMatrixBorderItems,
    eBtnMatrixBgItems,
    eBtnMatrixTextItems,
    eBatteryPercentageText,
    eColorTextLabel,
    eSpinnerMainArc,
    eSpinnerIndicatorArc,
    eTableHeadingText,
    eTableHeadingBg,
    eTableItemText,
    eTableItemBg,
    eTableItemDarkBg,
    eTableBorder,
    eTableCellBorder
};

// Color mapping table: column order matches Themes::Theme enum.
// Order is GBC, DMG, Pocket, Red, Blue, Dark, Light — GBC first because
// it is the default; Dark/Light pushed to the end since the green and
// cartridge palettes are the user-facing identity of MonsterMesh.
uint32_t themeColor[][7] = {
    //  GBC,    DMG,   Pocket, Red,   Blue,  Dark,        Light
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfff4f4f4 }, // eMainScreenStyle
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff436C70, 0xff67ea94 }, // eTopPanelBg
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffE0E0E0, 0xff212121 }, // eTopPanelText
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff436C70, 0xff67ea94 }, // eTopImageBg
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xff212121 }, // eTopImageRecolor
    {255, 255, 255, 255, 255, 255, 255},                              // eTopImageRecolorOpa
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xff212121 }, // ePositiveImageRecolor
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfff4f4f0 }, // ePanelBg
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff303030, 0xfffafafa }, // ePanelPressedBg
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xfff0f0f0, 0xff212121 }, // ePanelText
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xff67ea94, 0xff67ea94 }, // ePanelBorder
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xffffffff }, // eNodePanelBg
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff808080, 0xff979797 }, // eNodePanelBorder
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xfff0f0f0, 0xff212121 }, // eNodePanelText
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xffffffff }, // eNodeButtonBg
    {0, 0, 0, 0, 0, 0, 0},                                        // eNodeButtonBgOpa
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff585858, 0xffffffff }, // eButtonPanelBg
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff585858, 0xffeaeae0 }, // eMainButtonBg
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffaafbff, 0xff101010 }, // eMainButtonText
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xff67ea94, 0xff67ea94 }, // eMainButtonBorder
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff9e9e9e, 0xffc0c0c0 }, // eMainButtonShadow
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xff67ea94, 0xff757575 }, // eMainButtonImageRecolor
    {255, 255, 255, 255, 255, 0, 255},                                // eMainButtonImageRecolorOpa
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfffafaf4 }, // eHomeContainerBg
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xff67EA94, 0xffaaaaaa }, // eHomeContainerBorder
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff2B824A, 0xff999999 }, // eHomeContainerShadow
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffaafbff, 0xff294337 }, // eHomeContainerText
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff303030, 0xffffffff }, // eHomeButtonBg
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xff101010 }, // eHomeButtonText
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff303030, 0xffd0d0d0 }, // eHomeButtonBorder
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xff606060, 0xff57a6b3 }, // eHomeButtonImageRecolor
    {255, 255, 255, 255, 255, 0, 255},                                // eHomeButtonImageRecolorOpa
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xfffafaf4 }, // eChannelButtonBg
    {GBC_4, DMG_4, PKT_4, RED_4, BLU_4, 0xffA0A0A0, 0xffD0D0D0 }, // eChannelButtonBorder
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xff101010 }, // eChannelButtonText
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfff0f0f0 }, // eSettingsPanelBg
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xffaafbff, 0xff003c9f }, // eSettingsPanelText
    {0, 0, 0, 0, 0, 0, 0xff979797},                               // eSettingsPanelBorder
    {0, 0, 0, 0, 0, 0, 0xff7e7e7e},                               // eSettingsPanelShadow
    {250, 250, 250, 250, 250, 250, 250},                              // eSettingsPanelBgOpa
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff505050, 0xffeaeae0 }, // eSettingsButtonBg
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffaafbff, 0xff294337 }, // eSettingsButtonText
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff303030, 0xffd0d0d0 }, // eSettingsButtonBorder
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0,          0xff67ea94 }, // eSettingsButtonImageRecolor
    {255, 255, 255, 255, 255, 0, 255},                                // eSettingsButtonImageRecolorOpa
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xffffffff }, // eSettingsLabelBg
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff404040, 0xff808080 }, // eSettingsLabelBorder
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfff4f4f4 }, // eTabViewBg
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xffaafbff, 0xff003c9f }, // eTabViewText
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xffe0e0e0 }, // eTabButtonDefaultBg
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff303030, 0xffffffff }, // eTabButtonActiveBg
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xff67ea94, 0xffaafbff }, // eTabButtonPressedBg
    {GBC_4, DMG_4, PKT_4, RED_4, BLU_4, 0xffA0A0A0, 0xff606060 }, // eTabButtonDefaultText
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xff101010 }, // eTabButtonActiveText
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xffffffff }, // eTabButtonPressedText
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff505050, 0xffb0b0b0 }, // eTabButtonDefaultBorder
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff303030, 0xfffbfce9 }, // eChatMessageBg
    {255, 255, 255, 255, 255, 255, 255},                              // eChatMessageBgOpa
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffffffff, 0xff294337 }, // eChatMessageText
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff707070, 0xff888888 }, // eChatMessageBorder
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff404040, 0xffffffff }, // eNewMessageBg
    {255, 255, 255, 255, 255, 255, 255},                              // eNewMessageBgOpa
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffd0d0d0, 0xff294337 }, // eNewMessageText
    {GBC_4, DMG_4, PKT_4, RED_4, BLU_4, 0xff808080, 0xff888888 }, // eNewMessageBorder
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfffbfbfb }, // eAlertPanelBg
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xfff4f4f4 }, // eBtnMatrixBorderMain
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xff67ea94, 0xff67ea94 }, // eBtnMatrixBorderItems
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff606060, 0xfffffff8 }, // eBtnMatrixBgItems
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffaafbff, 0xff212121 }, // eBtnMatrixTextItems
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xffaafbff, 0xff212121 }, // eBatteryPercentageText
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xffaafbff, 0xff003c9f }, // eColorTextLabel
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xffe0e0e0 }, // eSpinnerMainArc
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xff67ea94, 0xff67ea94 }, // eSpinnerIndicatorArc
    {GBC_6, DMG_6, PKT_6, RED_6, BLU_6, 0xffaafbff, 0xff212121 }, // eTableHeadingText
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff303030, 0xfff4f4f0 }, // eTableHeadingBg
    {GBC_5, DMG_5, PKT_5, RED_5, BLU_5, 0xffaafbff, 0xff212121 }, // eTableItemText
    {GBC_3, DMG_3, PKT_3, RED_3, BLU_3, 0xff505050, 0xfff4f4f0 }, // eTableItemBg
    {GBC_0, DMG_0, PKT_0, RED_0, BLU_0, 0xff303030, 0xffd4d4d0 }, // eTableItemDarkBg
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xffe0e0e0 }, // eTableBorder
    {GBC_1, DMG_1, PKT_1, RED_1, BLU_1, 0xff404040, 0xffe0e0e0 }, // eTableCellBorder
};

#include "fonts.h"
#include "images.h"
#include "styles.h"
#include "screens.h"

#define THEME(COLOR) (themeColor[COLOR][theme])

// the following styles are copied from eez-studio generated styles and parametrized
extern "C" {
void apply_style_top_panel_style(void)
{
    lv_style_t *style = get_style_top_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eTopPanelBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eTopPanelText)));
};
void apply_style_panel_style_MAIN_DEFAULT(void)
{
    lv_style_t *style = get_style_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(ePanelBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(ePanelText)));
    lv_style_set_border_color(style, lv_color_hex(THEME(ePanelBorder)));
    if (isGreen())
        lv_style_set_text_line_space(style, 4);
};
void apply_style_panel_style_MAIN_PRESSED(void)
{
    lv_style_t *style = get_style_panel_style_MAIN_PRESSED();
    lv_style_set_bg_color(style, lv_color_hex(THEME(ePanelPressedBg)));
};
void apply_style_home_container_style(void)
{
    lv_style_t *style = get_style_home_container_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eHomeContainerBorder)));
    lv_style_set_border_width(style, 3);
    lv_style_set_border_side(style, LV_BORDER_SIDE_FULL);
    lv_style_set_bg_color(style, lv_color_hex(THEME(eHomeContainerBg)));
    lv_style_set_shadow_color(style, lv_color_hex(THEME(eHomeContainerShadow)));
    lv_style_set_text_font(style, &lv_font_cozette_13);
    lv_style_set_radius(style, 10);
    lv_style_set_text_color(style, lv_color_hex(THEME(eHomeContainerText)));
    if (isGreen())
        lv_style_set_text_line_space(style, 28);
};
void apply_style_settings_panel_style(void)
{
    lv_style_t *style = get_style_settings_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eSettingsPanelBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eSettingsPanelText)));
    lv_style_set_shadow_color(style, lv_color_hex(THEME(eSettingsPanelShadow)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eSettingsPanelBorder)));
    lv_style_set_bg_opa(style, THEME(eSettingsPanelBgOpa));
};
void apply_style_node_panel_style(void)
{
    lv_style_t *style = get_style_node_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eNodePanelBg)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eNodePanelBorder)));
    lv_style_set_text_font(style, &lv_font_cozette_13);
    lv_style_set_text_color(style, lv_color_hex(THEME(eNodePanelText)));
    if (isGreen())
        lv_style_set_text_line_space(style, 16);
};
void apply_style_node_button_style(void)
{
    lv_style_t *style = get_style_node_button_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eNodeButtonBg)));
    lv_style_set_bg_opa(style, THEME(eNodeButtonBgOpa));
};
void apply_style_button_panel_style(void)
{
    lv_style_t *style = get_style_button_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eButtonPanelBg)));
};
void apply_style_home_button_style(void)
{
    lv_style_t *style = get_style_home_button_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eHomeButtonBg)));
    lv_style_set_bg_image_recolor_opa(style, THEME(eHomeButtonImageRecolorOpa));
    lv_style_set_bg_image_recolor(style, lv_color_hex(THEME(eHomeButtonImageRecolor)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eHomeButtonBorder)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eHomeButtonText)));
};
void apply_style_settings_button_style(void)
{
    lv_style_t *style = get_style_settings_button_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eSettingsButtonBg)));
    lv_style_set_bg_image_recolor_opa(style, THEME(eSettingsButtonImageRecolorOpa));
    lv_style_set_bg_image_recolor(style, lv_color_hex(THEME(eSettingsButtonImageRecolor)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eSettingsButtonBorder)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eSettingsButtonText)));
    if (isGreen())
        lv_style_set_text_font(style, &lv_font_cozette_13);
};
void apply_style_main_button_style(void)
{
    lv_style_t *style = get_style_main_button_style_MAIN_DEFAULT();
    lv_style_set_bg_image_recolor_opa(style, THEME(eMainButtonImageRecolorOpa));
    lv_style_set_bg_image_recolor(style, lv_color_hex(THEME(eMainButtonImageRecolor)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eMainButtonBorder)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eMainButtonBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eMainButtonText)));
    lv_style_set_shadow_color(style, lv_color_hex(THEME(eMainButtonShadow)));
    if (isGreen())
        lv_style_set_text_font(style, &lv_font_cozette_13);
};
void apply_style_new_message_style(void)
{
    lv_style_t *style = get_style_new_message_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eNewMessageBorder)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eNewMessageBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eNewMessageText)));
    lv_style_set_bg_opa(style, THEME(eNewMessageBgOpa));
    if (isGreen())
        lv_style_set_text_font(style, &lv_font_cozette_13);
};
void apply_style_chat_message_style(void)
{
    lv_style_t *style = get_style_chat_message_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eChatMessageBorder)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eChatMessageBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eChatMessageText)));
    lv_style_set_bg_opa(style, THEME(eChatMessageBgOpa));
    if (isGreen()) {
        lv_style_set_text_font(style, &lv_font_cozette_13);
        lv_style_set_text_line_space(style, 2);
    }
};
void apply_style_tab_view_style(void)
{
    lv_style_t *style = get_style_tab_view_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eTabViewBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eTabViewText)));
    lv_style_set_text_font(style, &lv_font_cozette_13);
};
void apply_style_drop_down_style(void){};
void apply_style_bw_label_style(void)
{
    lv_style_t *style = get_style_bw_label_style_MAIN_DEFAULT();
    lv_style_set_text_color(style, lv_color_hex(THEME(eBatteryPercentageText)));
};
void apply_style_color_label_style(void)
{
    lv_style_t *style = get_style_color_label_style_MAIN_DEFAULT();
    lv_style_set_text_color(style, lv_color_hex(THEME(eColorTextLabel)));
};
void apply_style_top_image_style(void)
{
    lv_style_t *style = get_style_top_image_style_MAIN_DEFAULT();
    lv_style_set_bg_image_recolor(style, lv_color_hex(THEME(eTopImageRecolor)));
    lv_style_set_bg_image_recolor_opa(style, THEME(eTopImageRecolorOpa));
    lv_style_set_image_recolor(style, lv_color_hex(THEME(eTopImageRecolor)));
    lv_style_set_image_recolor_opa(style, THEME(eTopImageRecolorOpa));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eTopImageBg)));
};
void apply_style_alert_panel_style(void)
{
    lv_style_t *style = get_style_alert_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eAlertPanelBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(ePanelText)));
};
void apply_style_main_screen_style(void)
{
    lv_style_t *style = get_style_main_screen_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eMainScreenStyle)));
    if (isGreen()) {
        lv_style_set_text_font(style, &lv_font_cozette_13);
        lv_style_set_text_line_space(style, 3);
    }
};
void apply_style_channel_button_style(void)
{
    lv_style_t *style = get_style_channel_button_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eChannelButtonBg)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eChannelButtonBorder)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eChannelButtonText)));
    if (isGreen())
        lv_style_set_text_font(style, &lv_font_cozette_13);
};
void apply_style_button_matrix_style_ITEMS_DEFAULT(void)
{
    lv_style_t *style = get_style_button_matrix_style_ITEMS_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eBtnMatrixBorderItems)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eBtnMatrixBgItems)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eBtnMatrixTextItems)));
};
void apply_style_button_matrix_style_MAIN_DEFAULT(void)
{
    lv_style_t *style = get_style_button_matrix_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eBtnMatrixBorderMain)));
};
void apply_style_spinner_style_MAIN_DEFAULT(void)
{
    lv_style_t *style = get_style_spinner_style_MAIN_DEFAULT();
    lv_style_set_arc_color(style, lv_color_hex(THEME(eSpinnerMainArc)));
};
void apply_style_spinner_style_INDICATOR_DEFAULT(void)
{
    lv_style_t *style = get_style_spinner_style_INDICATOR_DEFAULT();
    lv_style_set_arc_color(style, lv_color_hex(THEME(eSpinnerIndicatorArc)));
};
void apply_style_settings_label_style(void)
{
    lv_style_t *style = get_style_settings_label_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eSettingsLabelBorder)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eSettingsLabelBg)));
};
void apply_style_positive_image_style(void)
{
    lv_style_t *style = get_style_positive_image_style_MAIN_DEFAULT();
    lv_style_set_image_recolor(style, lv_color_hex(THEME(ePositiveImageRecolor)));
};
void apply_style_statistics_table_style_MAIN_DEFAULT(void)
{
    lv_style_t *style = get_style_statistics_table_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eTableBorder)));
};
void apply_style_statistics_table_style_ITEMS_DEFAULT(void)
{
    lv_style_t *style = get_style_statistics_table_style_ITEMS_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eTableItemBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eTableItemText)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eTableCellBorder)));
};
}

void Themes::set(enum Theme th)
{
    theme = th;
    apply_style_top_panel_style();
    apply_style_panel_style_MAIN_DEFAULT();
    apply_style_panel_style_MAIN_PRESSED();
    apply_style_home_container_style();
    apply_style_settings_panel_style();
    apply_style_node_panel_style();
    apply_style_node_button_style();
    apply_style_button_panel_style();
    apply_style_home_button_style();
    apply_style_settings_button_style();
    apply_style_main_button_style();
    apply_style_new_message_style();
    apply_style_chat_message_style();
    apply_style_tab_view_style();
    apply_style_drop_down_style();
    apply_style_bw_label_style();
    apply_style_color_label_style();
    apply_style_top_image_style();
    apply_style_alert_panel_style();
    apply_style_main_screen_style();
    apply_style_channel_button_style();
    apply_style_button_matrix_style_ITEMS_DEFAULT();
    apply_style_button_matrix_style_MAIN_DEFAULT();
    apply_style_spinner_style_MAIN_DEFAULT();
    apply_style_spinner_style_INDICATOR_DEFAULT();
    apply_style_settings_label_style();
    apply_style_positive_image_style();
    apply_style_statistics_table_style_MAIN_DEFAULT();
    apply_style_statistics_table_style_ITEMS_DEFAULT();

    // MM terminal re-theming hooks live here in tft-color but the terminal
    // isn't on this branch yet — see screens.c #if 0 blocks.
}

void Themes::initStyles(void)
{
    lv_style_init(&style_btn_default);
    lv_style_set_text_color(&style_btn_default, lv_color_hex(THEME(eTabButtonDefaultText)));
    lv_style_set_bg_color(&style_btn_default, lv_color_hex(THEME(eTabButtonDefaultBg)));
    lv_style_set_bg_opa(&style_btn_default, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_default, lv_color_hex(THEME(eTabButtonDefaultBorder)));
    lv_style_set_border_opa(&style_btn_default, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_default, 1);
    lv_style_set_border_side(&style_btn_default, LV_BORDER_SIDE_FULL);

    uint32_t accentColor = isGreen() ? THEME(ePanelBorder) : 0xff67ea94;

    lv_style_init(&style_btn_active);
    lv_style_set_text_color(&style_btn_active, lv_color_hex(THEME(eTabButtonActiveText)));
    lv_style_set_bg_color(&style_btn_active, lv_color_hex(THEME(eTabButtonActiveBg)));
    lv_style_set_bg_opa(&style_btn_active, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_active, lv_color_hex(accentColor));
    lv_style_set_border_opa(&style_btn_active, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_active, 3);
    lv_style_set_border_side(&style_btn_active, LV_BORDER_SIDE_BOTTOM);

    lv_style_init(&style_btn_pressed);
    lv_style_set_text_color(&style_btn_pressed, lv_color_hex(THEME(eTabButtonPressedText)));
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(THEME(eTabButtonPressedBg)));
    lv_style_set_bg_opa(&style_btn_pressed, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_pressed, lv_color_hex(accentColor));
    lv_style_set_border_opa(&style_btn_pressed, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_pressed, 3);
    lv_style_set_border_side(&style_btn_pressed, LV_BORDER_SIDE_BOTTOM);
}

void Themes::recolorButton(lv_obj_t *obj, bool enabled, lv_opa_t opa)
{
    lv_color_t color;
    if (isGreen())
        color = enabled ? lv_color_hex(THEME(ePanelText)) : lv_color_hex(THEME(eChannelButtonBorder));
    else if (theme == eLight)
        color = enabled ? lv_color_hex(THEME(eHomeButtonImageRecolor)) : lv_color_hex(0xffc0c0c0);
    else
        color = enabled ? lv_color_hex(0xff67ea94) : lv_color_hex(0xff606060);
    lv_obj_set_style_bg_image_recolor(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorImage(lv_obj_t *obj, bool enabled)
{
    lv_color_t color;
    if (isGreen())
        color = enabled ? lv_color_hex(THEME(ePanelText)) : lv_color_hex(THEME(eChannelButtonBorder));
    else if (theme == eLight)
        color = enabled ? lv_color_hex(THEME(eHomeButtonImageRecolor)) : lv_color_hex(0xffc0c0c0);
    else
        color = enabled ? lv_color_hex(0xff67ea94) : lv_color_hex(0xff606060);
    lv_obj_set_style_image_recolor(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorText(lv_obj_t *obj, bool enabled)
{
    lv_color_t color;
    if (isGreen())
        color = enabled ? lv_color_hex(THEME(ePanelText)) : lv_color_hex(THEME(eChannelButtonBorder));
    else if (theme == eLight)
        color = enabled ? lv_color_hex(THEME(eHomeContainerText)) : lv_color_hex(0xffc0c0c0);
    else
        color = enabled ? lv_color_hex(0xffffffff) : lv_color_hex(0xff606060);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorTopLabel(lv_obj_t *obj, bool alert)
{
    lv_color_t color = alert ? lv_color_hex(0xfff72b2b) : lv_color_hex(THEME(eTopPanelText));
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorTableRow(lv_draw_fill_dsc_t *fill_draw_dsc, bool odd)
{
    if (odd) {
        fill_draw_dsc->color = lv_color_hex(THEME(eTableItemBg));
    } else {
        fill_draw_dsc->color = lv_color_hex(THEME(eTableItemDarkBg));
    }
}

// ── Color getters for external consumers ────────────────────────────────────
uint32_t Themes::darkest()  { return THEME(eMainScreenStyle); }
uint32_t Themes::dark()     { return THEME(ePanelPressedBg); }
uint32_t Themes::mid()      { return THEME(eNodePanelBorder); }
uint32_t Themes::accent()   { return THEME(eChannelButtonBorder); }
uint32_t Themes::light()    { return THEME(eBatteryPercentageText); }
uint32_t Themes::lightest() { return THEME(ePanelText); }

#endif // VIEW_320x240
