#pragma once
#include "../shared/platform.h"
#include <functional>
#include <string>
#include <vector>

class IpcServer {
public:
    using MessageCallback = std::function<void(const std::string &msg)>;

    IpcServer();
    ~IpcServer();

    // Start listening on the Unix socket
    bool start(const char *sockPath);
    void stop();

    void setMessageCallback(MessageCallback cb) { msgCb_ = cb; }

    // Push a JSON message to the connected terminal (if any)
    void push(const std::string &json);

    // Poll for incoming connections/data. Returns true if activity.
    bool poll();

    bool hasClient() const { return clientFd_ >= 0; }

private:
    int listenFd_ = -1;
    int clientFd_ = -1;
    char sockPath_[256] = {};
    MessageCallback msgCb_;

    // Line buffer for client
    char rxBuf_[4096];
    int rxLen_ = 0;

    void acceptClient();
    void readClient();
    void disconnectClient();
};
