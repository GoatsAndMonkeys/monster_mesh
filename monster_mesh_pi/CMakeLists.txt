cmake_minimum_required(VERSION 3.16)
project(MonsterMeshPi C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Platform detection ────────────────────────────────────────────────────────
# CURSES_NEED_WIDE asks find_package(Curses) for ncursesw (wide-char) instead
# of the 8-bit ncurses.  Wide ncurses emits UTF-8 multi-byte sequences for
# ACS_* macros — required for box() / ACS_HLINE to render as proper ┌─┐│└┘
# on terminals like the Linux framebuffer console (TERM=linux) that would
# otherwise fall back to ASCII letter substitutes.
set(CURSES_NEED_WIDE TRUE)

if(APPLE)
    message(STATUS "Building for macOS (dev mode)")
    find_package(Curses REQUIRED)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    # Homebrew SDL2 lives under /opt/homebrew on Apple Silicon.  Help
    # find_package locate it without forcing the user to set env vars.
    list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/sdl2)
elseif(UNIX)
    message(STATUS "Building for Linux")
    find_package(Curses REQUIRED)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# SDL2 powers the graphical battle window the terminal pops up on BATTLE
# entry.  Required for mmterm.
#
# Debian/Raspbian's libsdl2-dev doesn't ship SDL2Config.cmake — only sdl2.pc
# (pkg-config) and the sdl2-config shell script.  Homebrew on macOS also
# ships sdl2.pc.  pkg-config is the lowest-common-denominator path that
# works on both, so prefer it; fall back to find_package(SDL2) only if
# pkg-config isn't around.
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 REQUIRED sdl2)
else()
    find_package(SDL2 REQUIRED)
endif()

include_directories(src/shared src/battle)

# ── Phase 0: Standalone battle engine test (no ncurses, no IPC, no serial) ───
add_executable(test_battle
    tests/test_battle.cpp
    src/battle/Gen1BattleEngine.cpp
)
target_include_directories(test_battle PRIVATE src/shared src/battle)
target_link_libraries(test_battle m)

# ── Breeding app (Phase 6): roster + Mendelian breeding, no ncurses/SDL ───────
# Runnable breeding module + Monte-Carlo genetics verifier. Mirrors the firmware
# PentestGenetics genotype so caught mons transfer to the deck and breed here.
add_executable(mmbreed
    tests/test_breeding.cpp
    src/shared/BreedingApp.cpp
)
target_include_directories(mmbreed PRIVATE src/shared)
target_link_libraries(mmbreed m)

# ── Pentest Pikachu catch prototype: warwalk → catch → box → breed (Pi) ────────
# In-process mirror of the firmware catching genetics; caught mons drop straight
# into the breeding roster (no ESP32, no mesh transfer). `mmpoke --selftest`
# verifies BSSID→species+skin determinism and the catch math.
add_executable(mmpoke
    tests/pentest_catch.cpp
    src/shared/BreedingApp.cpp
)
target_include_directories(mmpoke PRIVATE src/shared)
target_link_libraries(mmpoke m)

# ── Daemon ────────────────────────────────────────────────────────────────────
add_executable(mmd
    src/daemon/main.cpp
    src/daemon/MonsterMeshDaemon.cpp
    src/daemon/MeshSerial.cpp
    src/daemon/SaveWatcher.cpp
    src/daemon/IpcServer.cpp
    src/daemon/PokemonDaycare.cpp
    src/daemon/DaycareEventGen.cpp
    src/battle/Gen1BattleEngine.cpp
)
target_include_directories(mmd PRIVATE src/daemon src/shared src/battle)
target_link_libraries(mmd m)

# ── Terminal ──────────────────────────────────────────────────────────────────
add_executable(mmterm
    src/terminal/main.cpp
    src/terminal/TerminalUI.cpp
    src/terminal/InputHandler.cpp
    src/terminal/IpcClient.cpp
    src/terminal/SpriteRender.cpp
    src/terminal/BattleWindow.cpp
    src/terminal/BitmapFont.cpp
    src/terminal/Gen2SpriteCache.cpp
    src/terminal/puff.c
    src/battle/Gen1BattleEngine.cpp
    src/battle/LordGyms.cpp
    src/battle/LordLogic.cpp
    src/battle/LordE4.cpp
    src/battle/LordSave.cpp
    src/shared/BreedingApp.cpp
)
target_include_directories(mmterm PRIVATE src/terminal src/shared src/battle)
target_include_directories(mmterm PRIVATE ${CURSES_INCLUDE_DIR} ${SDL2_INCLUDE_DIRS})
# pkg_check_modules puts the lib *names* in SDL2_LIBRARIES but the -L search
# path in SDL2_LIBRARY_DIRS — without the latter the linker can't find
# libSDL2 under Homebrew's /opt/homebrew/lib (or Debian's multiarch dir).
target_link_directories(mmterm PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_libraries(mmterm ${CURSES_LIBRARIES} ${SDL2_LIBRARIES} m)

# ── Pi Zero cross-compile ─────────────────────────────────────────────────────
# cmake -DCMAKE_TOOLCHAIN_FILE=../retropie/toolchain-arm.cmake ..
