// ── IpcServer — Unix domain socket server for terminal UI ────────────────────

#include "IpcServer.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

IpcServer::IpcServer() {
    memset(rxBuf_, 0, sizeof(rxBuf_));
}

IpcServer::~IpcServer() {
    stop();
}

bool IpcServer::start(const char *sockPath) {
    stop();

    strncpy(sockPath_, sockPath, sizeof(sockPath_) - 1);
    sockPath_[sizeof(sockPath_) - 1] = '\0';

    // Remove stale socket file
    unlink(sockPath);

    listenFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("IpcServer: socket() failed: %s", strerror(errno));
        return false;
    }

    // Set non-blocking
    int flags = fcntl(listenFd_, F_GETFL, 0);
    fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);

    // Ignore SIGPIPE — we handle write errors explicitly
    signal(SIGPIPE, SIG_IGN);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path) - 1);

    if (bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("IpcServer: bind(%s) failed: %s", sockPath, strerror(errno));
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (listen(listenFd_, 1) < 0) {
        LOG_ERROR("IpcServer: listen() failed: %s", strerror(errno));
        close(listenFd_);
        listenFd_ = -1;
        unlink(sockPath);
        return false;
    }

    LOG_INFO("IpcServer: listening on %s", sockPath);
    return true;
}

void IpcServer::stop() {
    disconnectClient();

    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
        unlink(sockPath_);
        LOG_INFO("IpcServer: stopped");
    }
}

void IpcServer::push(const std::string &json) {
    if (clientFd_ < 0) return;

    // Ensure the message ends with \n
    std::string msg = json;
    if (msg.empty() || msg.back() != '\n') msg += '\n';

    const char *buf = msg.c_str();
    size_t len = msg.size();
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = write(clientFd_, buf + sent, len - sent);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            // EPIPE or other error — client disconnected
            LOG_INFO("IpcServer: client disconnected on write");
            disconnectClient();
            return;
        }
        sent += (size_t)n;
    }
}

bool IpcServer::poll() {
    if (listenFd_ < 0) return false;

    bool activity = false;

    // Build fd_set
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(listenFd_, &readfds);
    int maxFd = listenFd_;

    if (clientFd_ >= 0) {
        FD_SET(clientFd_, &readfds);
        if (clientFd_ > maxFd) maxFd = clientFd_;
    }

    struct timeval tv = {0, 0};  // non-blocking
    int ret = select(maxFd + 1, &readfds, nullptr, nullptr, &tv);
    if (ret <= 0) return false;

    // New connection?
    if (FD_ISSET(listenFd_, &readfds)) {
        acceptClient();
        activity = true;
    }

    // Data from client?
    if (clientFd_ >= 0 && FD_ISSET(clientFd_, &readfds)) {
        readClient();
        activity = true;
    }

    return activity;
}

void IpcServer::acceptClient() {
    int newFd = accept(listenFd_, nullptr, nullptr);
    if (newFd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_WARN("IpcServer: accept() failed: %s", strerror(errno));
        }
        return;
    }

    // Disconnect existing client if any
    if (clientFd_ >= 0) {
        LOG_INFO("IpcServer: replacing existing client");
        disconnectClient();
    }

    // Set non-blocking
    int flags = fcntl(newFd, F_GETFL, 0);
    fcntl(newFd, F_SETFL, flags | O_NONBLOCK);

    clientFd_ = newFd;
    rxLen_ = 0;
    LOG_INFO("IpcServer: client connected");

    // Send a welcome push so client knows we're alive
    push("{\"type\":\"HELLO\",\"daemon\":\"mmd\"}\n");
}

void IpcServer::readClient() {
    if (clientFd_ < 0) return;

    int space = (int)sizeof(rxBuf_) - rxLen_ - 1;
    if (space <= 0) {
        // Buffer overflow — discard
        rxLen_ = 0;
        return;
    }

    ssize_t n = read(clientFd_, rxBuf_ + rxLen_, space);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        LOG_INFO("IpcServer: client read error: %s", strerror(errno));
        disconnectClient();
        return;
    }
    if (n == 0) {
        LOG_INFO("IpcServer: client disconnected (EOF)");
        disconnectClient();
        return;
    }

    rxLen_ += (int)n;
    rxBuf_[rxLen_] = '\0';

    // Process complete lines
    while (true) {
        char *nl = (char *)memchr(rxBuf_, '\n', rxLen_);
        if (!nl) break;

        *nl = '\0';
        std::string line(rxBuf_);

        // Shift buffer
        int lineLen = (int)(nl - rxBuf_) + 1;
        memmove(rxBuf_, nl + 1, rxLen_ - lineLen);
        rxLen_ -= lineLen;
        rxBuf_[rxLen_] = '\0';

        if (!line.empty() && msgCb_) {
            msgCb_(line);
        }
    }
}

void IpcServer::disconnectClient() {
    if (clientFd_ >= 0) {
        close(clientFd_);
        clientFd_ = -1;
        rxLen_ = 0;
        LOG_INFO("IpcServer: client disconnected");
    }
}
