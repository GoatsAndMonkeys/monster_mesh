"""Pre-build hook: copy patches/device-ui/* into libdeps + neutralize bsec2 hard-exit.

Without the patch copy, patched Themes/fonts/TFTView don't take effect and the
module fails to link (Themes::lightest, Themes::mid, etc. missing).
The bsec2 upstream package's extra_script.py hard-exits when no pre-built
algobsec .a exists for the target CPU; T-Deck ESP32-S3 has no such binary
and doesn't need BSEC2 (no BME680 on-board), so skip gracefully.
"""
import os
import shutil

Import("env")


def neutralize_bsec2_exit(libdeps_dir):
    path = os.path.join(libdeps_dir, "bsec2", "extra_script.py")
    if not os.path.isfile(path):
        return
    with open(path) as f:
        txt = f.read()
    if "# mm-neutralized" in txt:
        return
    txt = txt.replace("    exit(1)", "    pass  # mm-neutralized: no BME680 on T-Deck")
    with open(path, "w") as f:
        f.write(txt)
    print(f"[patch-bsec2] neutralized hard-exit in {path}")


project_dir = env["PROJECT_DIR"]
env_name = env["PIOENV"]
# PIO's PROJECT_LIBDEPS_DIR honors the [platformio]libdeps_dir setting, so we
# find the correct location whether it's the default .pio/libdeps or a custom
# path like ~/.pio_libdeps_pokemesh (used to escape iCloud sync).
libdeps_base = env.subst("$PROJECT_LIBDEPS_DIR")
if not libdeps_base:
    libdeps_base = os.path.join(project_dir, ".pio", "libdeps")
libdeps_dir = os.path.join(libdeps_base, env_name)
patches_src = os.path.join(project_dir, "patches", "device-ui")
lib_root_dir = os.path.join(libdeps_dir, "meshtastic-device-ui")
lib_source_fallback = os.path.join(lib_root_dir, "source")


def inject_tftview_mm_terminal_methods(header_path):
    """The patched TFTView_320x240.cpp defines ui_event_mm_terminal(_input) but
    the upstream header doesn't declare them. Inject declarations if missing."""
    if not os.path.isfile(header_path):
        return
    with open(header_path) as f:
        txt = f.read()
    if "ui_event_mm_terminal" in txt:
        return
    anchor = "    static void ui_event_LogoButton(lv_event_t *e);"
    if anchor not in txt:
        print(f"[patch-tftview] anchor not found in {header_path} — skip injection")
        return
    inject = (anchor +
              "\n    static void ui_event_mm_terminal(lv_event_t *e);" +
              "\n    static void ui_event_mm_terminal_input(lv_event_t *e);")
    txt = txt.replace(anchor, inject, 1)
    with open(header_path, "w") as f:
        f.write(txt)
    print(f"[patch-tftview] injected mm_terminal decls into {header_path}")


neutralize_bsec2_exit(libdeps_dir)
inject_tftview_mm_terminal_methods(
    os.path.join(lib_root_dir, "include", "graphics", "view", "TFT", "TFTView_320x240.h"))

if not os.path.isdir(patches_src):
    print(f"[patch-device-ui] no patches dir at {patches_src} — skipping")
elif not os.path.isdir(lib_root_dir):
    print(f"[patch-device-ui] lib not yet present at {lib_root_dir} — will retry on next build")
else:
    copied = 0
    overwrites = 0
    for name in os.listdir(patches_src):
        src = os.path.join(patches_src, name)
        if not os.path.isfile(src):
            continue
        matches = []
        # Walk the entire lib tree and copy to every file of the same name.
        # Some headers exist in both include/ (public API) and source/ (impl);
        # both need to be patched or the public API will diverge from impl.
        for root, _, files in os.walk(lib_root_dir):
            if name in files:
                matches.append(os.path.join(root, name))
        if not matches:
            # New file — drop it in source/ root
            matches = [os.path.join(lib_source_fallback, name)]
        for m in matches:
            shutil.copyfile(src, m)
        copied += 1
        overwrites += len(matches) - 1
    print(f"[patch-device-ui] applied {copied} patch(es), {overwrites} extra header copies, to {lib_root_dir}")
