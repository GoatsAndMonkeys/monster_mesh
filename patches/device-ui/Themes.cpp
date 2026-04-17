#ifdef VIEW_320x240

#include "graphics/view/TFT/Themes.h"
#include "stdint.h"

// DMG Game Boy greenscale palette
#define DMG_BG      0xff88C070  // lightest — main background
#define DMG_PANEL   0xff7cb068  // slightly darker — panels, containers
#define DMG_BTN     0xff6a9850  // medium — buttons, interactive elements
#define DMG_BORDER  0xff306230  // dark green — borders, secondary text
#define DMG_TEXT    0xff0f380f  // darkest — primary text, icons

LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_unscii_8;
LV_ATTRIBUTE_EXTERN_DATA extern const lv_font_t lv_font_cozette_13;

static enum Themes::Theme theme = Themes::eDark;

Themes::Theme Themes::get(void)
{
    return theme;
}

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

uint32_t themeColor[][2] = {
    // DMG green,  light
    {DMG_BG,      0xfff4f4f4}, // eMainScreenStyle
    {DMG_BORDER,  0xff67ea94}, // eTopPanelBg
    {DMG_BG,      0xff212121}, // eTopPanelText
    {DMG_BORDER,  0xff67ea94}, // eTopImageBg
    {DMG_BG,      0xff212121}, // eTopImageRecolor
    {255, 255},                // eTopImageRecolorOpa
    {DMG_BG,      0xff212121}, // ePositiveImageRecolor
    {DMG_PANEL,   0xfff4f4f0}, // ePanelBg
    {DMG_BTN,     0xfffafafa}, // ePanelPressedBg
    {DMG_TEXT,    0xff212121}, // ePanelText
    {DMG_BORDER,  0xff67ea94}, // ePanelBorder
    {DMG_PANEL,   0xffffffff}, // eNodePanelBg
    {DMG_BORDER,  0xff979797}, // eNodePanelBorder
    {DMG_TEXT,    0xff212121}, // eNodePanelText
    {DMG_PANEL,   0xffffffff}, // eNodeButtonBg
    {0, 0},                    // eNodeButtonBgOpa
    {DMG_BTN,     0xffffffff}, // eButtonPanelBg
    {DMG_BTN,     0xffeaeae0}, // eMainButtonBg
    {DMG_TEXT,    0xff101010}, // eMainButtonText
    {DMG_BORDER,  0xff67ea94}, // eMainButtonBorder
    {DMG_BORDER,  0xffc0c0c0}, // eMainButtonShadow
    {DMG_TEXT,    0xff757575}, // eMainButtonImageRecolor
    {255, 255},                // eMainButtonImageRecolorOpa
    {DMG_BG,      0xfffafaf4}, // eHomeContainerBg
    {DMG_BORDER,  0xffaaaaaa}, // eHomeContainerBorder
    {DMG_TEXT,    0xff999999}, // eHomeContainerShadow
    {DMG_TEXT,    0xff294337}, // eHomeContainerText
    {DMG_PANEL,   0xffffffff}, // eHomeButtonBg
    {DMG_TEXT,    0xff101010}, // eHomeButtonText
    {DMG_BORDER,  0xffd0d0d0}, // eHomeButtonBorder
    {DMG_TEXT,    0xff57a6b3}, // eHomeButtonImageRecolor
    {255, 255},                // eHomeButtonImageRecolorOpa
    {DMG_PANEL,   0xfffafaf4}, // eChannelButtonBg
    {DMG_BORDER,  0xffD0D0D0}, // eChannelButtonBorder
    {DMG_TEXT,    0xff101010}, // eChannelButtonText
    {DMG_PANEL,   0xfff0f0f0}, // eSettingsPanelBg
    {DMG_TEXT,    0xff003c9f}, // eSettingsPanelText
    {DMG_BORDER,  0xff979797}, // eSettingsPanelBorder
    {DMG_BORDER,  0xff7e7e7e}, // eSettingsPanelShadow
    {250, 250},                // eSettingsPanelBgOpa
    {DMG_BTN,     0xffeaeae0}, // eSettingsButtonBg
    {DMG_TEXT,    0xff294337}, // eSettingsButtonText
    {DMG_BORDER,  0xffd0d0d0}, // eSettingsButtonBorder
    {DMG_TEXT,    0xff67ea94}, // eSettingsButtonImageRecolor
    {255, 255},                // eSettingsButtonImageRecolorOpa
    {DMG_PANEL,   0xffffffff}, // eSettingsLabelBg
    {DMG_BORDER,  0xff808080}, // eSettingsLabelBorder
    {DMG_BG,      0xfff4f4f4}, // eTabViewBg
    {DMG_TEXT,    0xff003c9f}, // eTabViewText
    {DMG_BG,      0xffe0e0e0}, // eTabButtonDefaultBg
    {DMG_BTN,     0xffffffff}, // eTabButtonActiveBg
    {DMG_BORDER,  0xffaafbff}, // eTabButtonPressedBg
    {DMG_BORDER,  0xff606060}, // eTabButtonDefaultText
    {DMG_TEXT,    0xff101010}, // eTabButtonActiveText
    {DMG_BG,      0xffffffff}, // eTabButtonPressedText
    {DMG_BORDER,  0xffb0b0b0}, // eTabButtonDefaultBorder
    {DMG_PANEL,   0xfffbfce9}, // eChatMessageBg
    {255, 255},                // eChatMessageBgOpa
    {DMG_TEXT,    0xff294337}, // eChatMessageText
    {DMG_BORDER,  0xff888888}, // eChatMessageBorder
    {DMG_BTN,     0xffffffff}, // eNewMessageBg
    {255, 255},                // eNewMessageBgOpa
    {DMG_TEXT,    0xff294337}, // eNewMessageText
    {DMG_BORDER,  0xff888888}, // eNewMessageBorder
    {DMG_BG,      0xfffbfbfb}, // eAlertPanelBg
    {DMG_BORDER,  0xfff4f4f4}, // eBtnMatrixBorderMain
    {DMG_BORDER,  0xff67ea94}, // eBtnMatrixBorderItems
    {DMG_BTN,     0xfffffff8}, // eBtnMatrixBgItems
    {DMG_TEXT,    0xff212121}, // eBtnMatrixTextItems
    {DMG_TEXT,    0xff212121}, // eBatteryPercentageText
    {DMG_TEXT,    0xff003c9f}, // eColorTextLabel
    {DMG_BORDER,  0xffe0e0e0}, // eSpinnerMainArc
    {DMG_TEXT,    0xff67ea94}, // eSpinnerIndicatorArc
    {DMG_TEXT,    0xff212121}, // eTableHeadingText
    {DMG_PANEL,   0xfff4f4f0}, // eTableHeadingBg
    {DMG_TEXT,    0xff212121}, // eTableItemText
    {DMG_BG,      0xfff4f4f0}, // eTableItemBg
    {DMG_PANEL,   0xffd4d4d0}, // eTableItemDarkBg
    {DMG_BORDER,  0xffe0e0e0}, // eTableBorder
    {DMG_BORDER,  0xffe0e0e0}  // eTableCellBorder
};

#include "fonts.h"
#include "images.h"
#include "styles.h"

#define THEME(COLOR) (themeColor[COLOR][theme])

// the following styles are copied from eez-studio generated styles and parametrized
extern "C" {
void apply_style_top_panel_style(void)
{
    lv_style_t *style = get_style_top_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eTopPanelBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eTopPanelText)));
    // lv_style_set_text_font(style, &ui_font_montserrat_16);
};
void apply_style_panel_style_MAIN_DEFAULT(void)
{
    lv_style_t *style = get_style_panel_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(ePanelBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(ePanelText)));
    lv_style_set_border_color(style, lv_color_hex(THEME(ePanelBorder)));
    if (theme == Themes::eDark)
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
    lv_style_set_text_font(style, &ui_font_montserrat_16);
    lv_style_set_radius(style, 10);
    lv_style_set_text_color(style, lv_color_hex(THEME(eHomeContainerText)));
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
    lv_style_set_text_color(style, lv_color_hex(THEME(eNodePanelText)));
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
};
void apply_style_new_message_style(void)
{
    lv_style_t *style = get_style_new_message_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eNewMessageBorder)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eNewMessageBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eNewMessageText)));
    lv_style_set_bg_opa(style, THEME(eNewMessageBgOpa));
};
void apply_style_chat_message_style(void)
{
    lv_style_t *style = get_style_chat_message_style_MAIN_DEFAULT();
    lv_style_set_border_color(style, lv_color_hex(THEME(eChatMessageBorder)));
    lv_style_set_bg_color(style, lv_color_hex(THEME(eChatMessageBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eChatMessageText)));
    lv_style_set_bg_opa(style, THEME(eChatMessageBgOpa));
    if (theme == Themes::eDark)
        lv_style_set_text_line_space(style, 2);
};
void apply_style_tab_view_style(void)
{
    lv_style_t *style = get_style_tab_view_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eTabViewBg)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eTabViewText)));
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
    if (theme == Themes::eDark) {
        lv_style_set_text_line_space(style, 3);
    }
};
void apply_style_channel_button_style(void)
{
    lv_style_t *style = get_style_channel_button_style_MAIN_DEFAULT();
    lv_style_set_bg_color(style, lv_color_hex(THEME(eChannelButtonBg)));
    lv_style_set_border_color(style, lv_color_hex(THEME(eChannelButtonBorder)));
    lv_style_set_text_color(style, lv_color_hex(THEME(eChannelButtonText)));
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
    // lv_style_set_bg_opa(style, 255);
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
}

void Themes::initStyles(void)
{
    // set(get());
    //  lvgl v9 tabview buttons are not btn-matrix anymore but array of buttons
    //  see https://forum.lvgl.io/t/style-a-tabview-widget-in-v9-0-0/14747
    lv_style_init(&style_btn_default);
    lv_style_set_text_color(&style_btn_default, lv_color_hex(THEME(eTabButtonDefaultText)));
    lv_style_set_bg_color(&style_btn_default, lv_color_hex(THEME(eTabButtonDefaultBg)));
    lv_style_set_bg_opa(&style_btn_default, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_default, lv_color_hex(THEME(eTabButtonDefaultBorder)));
    lv_style_set_border_opa(&style_btn_default, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_default, 1);
    lv_style_set_border_side(&style_btn_default, LV_BORDER_SIDE_FULL);

    lv_style_init(&style_btn_active);
    lv_style_set_text_color(&style_btn_active, lv_color_hex(THEME(eTabButtonActiveText)));
    lv_style_set_bg_color(&style_btn_active, lv_color_hex(THEME(eTabButtonActiveBg)));
    lv_style_set_bg_opa(&style_btn_active, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_active, lv_color_hex(theme == eDark ? DMG_TEXT : 0xff67ea94));
    lv_style_set_border_opa(&style_btn_active, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_active, 3);
    lv_style_set_border_side(&style_btn_active, LV_BORDER_SIDE_BOTTOM);

    lv_style_init(&style_btn_pressed);
    lv_style_set_text_color(&style_btn_pressed, lv_color_hex(THEME(eTabButtonPressedText)));
    lv_style_set_bg_color(&style_btn_pressed, lv_color_hex(THEME(eTabButtonPressedBg)));
    lv_style_set_bg_opa(&style_btn_pressed, LV_OPA_COVER);
    lv_style_set_border_color(&style_btn_pressed, lv_color_hex(theme == eDark ? DMG_TEXT : 0xff67ea94));
    lv_style_set_border_opa(&style_btn_pressed, LV_OPA_COVER);
    lv_style_set_border_width(&style_btn_pressed, 3);
    lv_style_set_border_side(&style_btn_pressed, LV_BORDER_SIDE_BOTTOM);
}

void Themes::recolorButton(lv_obj_t *obj, bool enabled, lv_opa_t opa)
{
    lv_color_t color;
    switch (theme) {
    case eLight:
        color = enabled ? lv_color_hex(THEME(eHomeButtonImageRecolor)) : lv_color_hex(0xffc0c0c0);
        break;
    case eDark:
        color = enabled ? lv_color_hex(DMG_TEXT) : lv_color_hex(DMG_BORDER);
        break;
    default:
        break;
    }
    lv_obj_set_style_bg_image_recolor(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_image_recolor_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorImage(lv_obj_t *obj, bool enabled)
{
    lv_color_t color;
    switch (theme) {
    case eLight:
        color = enabled ? lv_color_hex(THEME(eHomeButtonImageRecolor)) : lv_color_hex(0xffc0c0c0);
        break;
    case eDark:
        color = enabled ? lv_color_hex(DMG_TEXT) : lv_color_hex(DMG_BORDER);
        break;
    default:
        break;
    }
    lv_obj_set_style_image_recolor(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorText(lv_obj_t *obj, bool enabled)
{
    lv_color_t color;
    switch (theme) {
    case eLight:
        color = enabled ? lv_color_hex(THEME(eHomeContainerText)) : lv_color_hex(0xffc0c0c0);
        break;
    case eDark:
        color = enabled ? lv_color_hex(DMG_TEXT) : lv_color_hex(DMG_BORDER);
        break;
    default:
        break;
    }
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void Themes::recolorTopLabel(lv_obj_t *obj, bool alert)
{
    lv_color_t color;
    switch (theme) {
    case eLight:
        color = alert ? lv_color_hex(0xfff72b2b) : lv_color_hex(THEME(eTopPanelText));
        break;
    case eDark:
        color = alert ? lv_color_hex(0xfff72b2b) : lv_color_hex(THEME(eTopPanelText));
        break;
    default:
        break;
    }
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

#endif // VIEW_320x240