// IpcClient.cpp — Unix socket IPC client for mmterm
// Connects to the MonsterMesh daemon, sends JSON commands, receives JSON messages.

#include "IpcClient.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

IpcClient::IpcClient()
{
    memset(rxBuf_, 0, sizeof(rxBuf_));
}

IpcClient::~IpcClient()
{
    disconnect();
}

bool IpcClient::connect(const char *sockPath)
{
    if (fd_ >= 0) disconnect();

    fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd_ < 0) {
        LOG_ERROR("IpcClient: socket() failed: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sockPath, sizeof(addr.sun_path) - 1);

    if (::connect(fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        // Silent — "Connection refused" while waiting for the daemon to
        // start is normal, and the warning would smear across the ncurses
        // panel each tick. The UI shows [offline] in the status bar when
        // we're not connected; that's the user-facing signal.
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Set non-blocking after connect
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0) flags = 0;
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

    rxLen_ = 0;
    LOG_INFO("IpcClient: connected to %s", sockPath);
    return true;
}

void IpcClient::disconnect()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        rxLen_ = 0;
        LOG_INFO("IpcClient: disconnected");
    }
}

bool IpcClient::send(const std::string &json)
{
    if (fd_ < 0) return false;

    std::string msg = json + "\n";
    ssize_t total = 0;
    ssize_t len   = (ssize_t)msg.size();
    const char *ptr = msg.c_str();

    while (total < len) {
        ssize_t n = ::write(fd_, ptr + total, (size_t)(len - total));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Non-blocking would block — skip for now (caller can retry)
                return total > 0;
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                LOG_WARN("IpcClient: send failed (broken pipe), disconnecting");
                disconnect();
                return false;
            }
            LOG_WARN("IpcClient: write error: %s", strerror(errno));
            disconnect();
            return false;
        }
        total += n;
    }
    return true;
}

bool IpcClient::poll()
{
    if (fd_ < 0) return false;

    bool gotAny = false;

    while (true) {
        // How much space remains in the rx buffer
        int space = (int)sizeof(rxBuf_) - rxLen_ - 1;
        if (space <= 0) {
            // Buffer full — scan for a newline so we can flush
            // If there's none, drop the oldest half and hope for the best
            LOG_WARN("IpcClient: rx buffer full, flushing");
            rxLen_ = 0;
            break;
        }

        ssize_t n = ::read(fd_, rxBuf_ + rxLen_, (size_t)space);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No data available — normal for non-blocking
            }
            LOG_WARN("IpcClient: read error: %s", strerror(errno));
            disconnect();
            break;
        }
        if (n == 0) {
            // EOF — daemon closed connection
            LOG_INFO("IpcClient: daemon closed connection");
            disconnect();
            break;
        }

        rxLen_ += (int)n;

        // Process all complete lines in the buffer
        while (true) {
            char *nl = (char *)memchr(rxBuf_, '\n', rxLen_);
            if (!nl) break;

            // Terminate and dispatch the line
            *nl = '\0';
            if (nl > rxBuf_ && msgCb_) {
                msgCb_(std::string(rxBuf_, nl - rxBuf_));
                gotAny = true;
            }

            // Shift remaining bytes forward
            int consumed = (int)(nl - rxBuf_) + 1;
            int remaining = rxLen_ - consumed;
            if (remaining > 0) {
                memmove(rxBuf_, rxBuf_ + consumed, remaining);
            }
            rxLen_ = remaining;
        }
    }

    return gotAny;
}
