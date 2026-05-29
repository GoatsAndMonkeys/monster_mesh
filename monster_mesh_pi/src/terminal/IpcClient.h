#pragma once
#include "../shared/platform.h"
#include <functional>
#include <string>

class IpcClient {
public:
    using MessageCallback = std::function<void(const std::string &msg)>;

    IpcClient();
    ~IpcClient();

    // Connect to the daemon socket. Returns true on success.
    bool connect(const char *sockPath);
    void disconnect();
    bool isConnected() const { return fd_ >= 0; }

    void setMessageCallback(MessageCallback cb) { msgCb_ = cb; }

    // Send a JSON command to the daemon
    bool send(const std::string &json);

    // Poll for incoming messages. Returns true if any received.
    bool poll();

private:
    int fd_ = -1;
    char rxBuf_[8192];
    int rxLen_ = 0;
    MessageCallback msgCb_;
};
