#include "MonsterMeshDaemon.h"
#include "../shared/IpcProtocol.h"
#include "../shared/platform.h"
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static void sigHandler(int) { MonsterMeshDaemon::shouldStop = true; }

int main(int argc, char *argv[]) {
    signal(SIGINT,  sigHandler);
    signal(SIGTERM, sigHandler);

    const char *serialPort = MMD_SERIAL_PORT_DEFAULT;
    const char *saveDir    = RETROPIE_SAVE_DIR;
    const char *relayScript = nullptr;

    // Arg parsing:
    //   mmd [serial_port] [save_dir]
    //   mmd --relay path/to/mesh_relay.py [serial_port] [save_dir]
    int argIdx = 1;
    if (argc > argIdx && strcmp(argv[argIdx], "--relay") == 0) {
        argIdx++;
        if (argc > argIdx) relayScript = argv[argIdx++];
    }
    if (argc > argIdx) serialPort = argv[argIdx++];
    if (argc > argIdx) saveDir    = argv[argIdx++];

    LOG_INFO("MonsterMesh Daemon starting...");
    if (relayScript)
        LOG_INFO("Mode: Python relay (%s) via %s", relayScript, serialPort);
    else
        LOG_INFO("Serial port: %s", serialPort);
    LOG_INFO("Save dir: %s", saveDir);

    MonsterMeshDaemon daemon;
    if (relayScript) daemon.setRelayScript(relayScript);
    if (!daemon.init(serialPort, saveDir)) {
        LOG_ERROR("Daemon init failed");
        return 1;
    }

    LOG_INFO("Daemon running. IPC socket: %s", MMD_SOCK_PATH);

    while (!MonsterMeshDaemon::shouldStop) {
        daemon.tick();
        usleep(10000); // 10ms tick
    }

    LOG_INFO("Daemon shutting down.");
    return 0;
}
