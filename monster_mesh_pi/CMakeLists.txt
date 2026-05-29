cmake_minimum_required(VERSION 3.16)
project(MonsterMeshPi CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Platform detection ────────────────────────────────────────────────────────
if(APPLE)
    message(STATUS "Building for macOS (dev mode)")
    find_package(Curses REQUIRED)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
elseif(UNIX)
    message(STATUS "Building for Linux")
    find_package(Curses REQUIRED)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

include_directories(src/shared src/battle)

# ── Phase 0: Standalone battle engine test (no ncurses, no IPC, no serial) ───
add_executable(test_battle
    tests/test_battle.cpp
    src/battle/Gen1BattleEngine.cpp
)
target_include_directories(test_battle PRIVATE src/shared src/battle)
target_link_libraries(test_battle m)

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
    src/battle/Gen1BattleEngine.cpp
    src/battle/LordGyms.cpp
    src/battle/LordSave.cpp
)
target_include_directories(mmterm PRIVATE src/terminal src/shared src/battle)
target_include_directories(mmterm PRIVATE ${CURSES_INCLUDE_DIR})
target_link_libraries(mmterm ${CURSES_LIBRARIES} m)

# ── Pi Zero cross-compile ─────────────────────────────────────────────────────
# cmake -DCMAKE_TOOLCHAIN_FILE=../retropie/toolchain-arm.cmake ..
