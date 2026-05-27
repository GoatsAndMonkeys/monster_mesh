# Theme system

MonsterMesh ships a device-wide theme system with **seven** palettes, selectable from the Meshtastic **Settings → Theme** dropdown. Themes recolor the entire device-ui (boot screen, node list, terminal, dropdowns) — not just the emulator.

## The seven themes

| Theme | Look |
|---|---|
| **Dark** | Stock dark UI |
| **Light** | Stock light UI |
| **DMG** | Original Game Boy LCD green (classic four-shade `#0F380F → #9BBC0F`) |
| **GBC** | Game Boy Color dark-teal palette |
| **Pocket** | High-contrast green (the default Game Boy Pocket look) |
| **Poke Blue** | Generation-1 "Blue" palette |
| **Poke Red** | Generation-1 "Red" palette |

The three "green" themes (DMG, GBC, Pocket) switch the whole UI over to crisp **Cozette** bitmap fonts with tuned line spacing, for an authentic handheld feel; Dark/Light use the stock fonts.

## How it works

Theming lives in the **device-ui patches** under `patches/device-ui/` (these are full-file copies that get dropped into the `meshtastic-device-ui` library during the build — see [BUILD.md](BUILD.md)):

- **`Themes.{h,cpp}`** — the theme enum plus a color table (`themeColor[COLOR][theme]`) and getter helpers (`darkest()`, `dark()`, `mid()`, `accent()`, `light()`, `lightest()`). `Themes::set()` re-applies every LVGL style, re-themes the live MonsterMesh terminal panel, and walks existing labels to recolor them. An `isGreen()` helper drives the Cozette-font / line-spacing path.
- **`screens.c`** — the LVGL screen definitions (boot screen, the MonsterMesh terminal, the theme dropdown). The boot screen is hard-coded to the Pocket palette because it draws before the theme system has loaded.
- **`lv_font_cozette_13.c`, `lv_font_cozette_20.c`, `lv_font_cozette_26.c`** — the Cozette bitmap fonts at three sizes.
- **`lv_i18n.c`** — localized strings, including the theme dropdown labels (`Dark / Light / DMG / GBC / Pocket / Poke Blue / Poke Red`).

In the MonsterMesh module itself, the ROM browser asks for the active palette (`getBrowserPalette()`) and rebuilds its screen when the theme changes, so the file picker matches your chosen look.

## Fonts & credit

- **Cozette** — © Slavfox, MIT. <https://github.com/slavfox/Cozette> — the bitmap font the green themes switch the UI to.

See [CREDITS.md](CREDITS.md).

## Adding or editing a theme

1. Add the column to the `themeColor[][]` table in `patches/device-ui/Themes.cpp` (and the enum in `Themes.h`).
2. If it's a green/handheld palette, make `isGreen()` return true for it so it gets the Cozette font.
3. Add its label to the dropdown strings in `lv_i18n.c`.
4. Rebuild and re-copy the patched device-ui files (see [BUILD.md](BUILD.md)).
