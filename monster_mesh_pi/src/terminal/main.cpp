// main.cpp — mmterm: MonsterMesh Pi terminal UI entry point

#include "TerminalUI.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static TerminalUI *g_ui = nullptr;
static void sigHandler(int) { if (g_ui) g_ui->requestQuit(); }

int main(int argc, char *argv[]) {
    // The MonsterMesh RetroPie system loads "ROMs" by passing the .mm file
    // path to launch.sh, which forwards it (or --pentest) to us.  A ROM whose
    // name mentions pentest/pikachu boots straight into the Pentest Pikachu
    // battle screen instead of the normal terminal menu.
    bool pentest = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!a) continue;
        if (strstr(a, "pentest") || strstr(a, "Pentest") ||
            strstr(a, "pikachu") || strstr(a, "Pikachu") ||
            strstr(a, "PENTEST") || strstr(a, "PIKACHU"))
            pentest = true;
    }

    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    // Redirect stderr to a log file so any LOG_WARN / LOG_ERROR calls don't
    // smear across the ncurses display. Look at /tmp/mmterm.log to debug.
    freopen("/tmp/mmterm.log", "w", stderr);

    TerminalUI ui;
    g_ui = &ui;
    ui.setPentestMode(pentest);

    if (!ui.init()) {
        fprintf(stderr, "Failed to initialize terminal UI\n");
        return 1;
    }

    ui.run();   // blocks until quit
    ui.shutdown();
    return 0;
}
