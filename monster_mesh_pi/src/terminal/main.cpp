// main.cpp — mmterm: MonsterMesh Pi terminal UI entry point

#include "TerminalUI.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>

static TerminalUI *g_ui = nullptr;
static void sigHandler(int) { if (g_ui) g_ui->requestQuit(); }

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    // Redirect stderr to a log file so any LOG_WARN / LOG_ERROR calls don't
    // smear across the ncurses display. Look at /tmp/mmterm.log to debug.
    freopen("/tmp/mmterm.log", "w", stderr);

    TerminalUI ui;
    g_ui = &ui;

    if (!ui.init()) {
        fprintf(stderr, "Failed to initialize terminal UI\n");
        return 1;
    }

    ui.run();   // blocks until quit
    ui.shutdown();
    return 0;
}
