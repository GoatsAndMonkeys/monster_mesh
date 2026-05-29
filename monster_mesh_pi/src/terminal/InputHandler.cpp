// InputHandler.cpp — GPI Case button input via /dev/input/eventN (Linux)
// On macOS: open() always returns false; TerminalUI ncurses keyboard handles input.

#include "InputHandler.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <string>

#ifdef __linux__
#include <glob.h>
#include <sys/ioctl.h>
#include <linux/input.h>

// Key codes for GPI Case (Game Boy-style layout)
#ifndef KEY_UP
#define KEY_UP          103
#endif
#ifndef KEY_DOWN
#define KEY_DOWN        108
#endif
#ifndef KEY_LEFT
#define KEY_LEFT        105
#endif
#ifndef KEY_RIGHT
#define KEY_RIGHT       106
#endif
#ifndef KEY_ENTER
#define KEY_ENTER        28
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE    14
#endif
#ifndef KEY_SPACE
#define KEY_SPACE        57
#endif
#ifndef KEY_LEFTSHIFT
#define KEY_LEFTSHIFT    42
#endif
#ifndef BTN_EAST
#define BTN_EAST        0x131
#endif
#ifndef BTN_SOUTH
#define BTN_SOUTH       0x130
#endif
#ifndef BTN_START
#define BTN_START       0x13b
#endif
#ifndef BTN_SELECT
#define BTN_SELECT      0x13a
#endif
#endif  // __linux__

InputHandler::InputHandler() {}
InputHandler::~InputHandler() { close(); }

bool InputHandler::open(const char *devicePath)
{
#ifdef __linux__
    if (fd_ >= 0) close();

    std::string path;
    if (devicePath) {
        path = devicePath;
    } else {
        path = autoDetect();
        if (path.empty()) {
            LOG_WARN("InputHandler: no GPI input device found — using keyboard fallback");
            return false;
        }
    }

    fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        LOG_WARN("InputHandler: open(%s) failed: %s", path.c_str(), strerror(errno));
        return false;
    }

    LOG_INFO("InputHandler: opened %s", path.c_str());
    return true;
#else
    (void)devicePath;
    LOG_INFO("InputHandler: macOS — using ncurses keyboard fallback (Z=A, X=B, Tab=Start, Space=Select)");
    return false;  // ncurses keyboard path handles input
#endif
}

void InputHandler::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

GpiButton InputHandler::mapKeyCode(int code)
{
#ifdef __linux__
    switch (code) {
        case KEY_UP:        return GpiButton::UP;
        case KEY_DOWN:      return GpiButton::DOWN;
        case KEY_LEFT:      return GpiButton::LEFT;
        case KEY_RIGHT:     return GpiButton::RIGHT;
        case BTN_EAST:
        case KEY_ENTER:     return GpiButton::A;
        case BTN_SOUTH:
        case KEY_BACKSPACE: return GpiButton::B;
        case BTN_START:
        case KEY_SPACE:     return GpiButton::START;
        case BTN_SELECT:
        case KEY_LEFTSHIFT: return GpiButton::SELECT;
        default:            return GpiButton::NONE;
    }
#else
    (void)code;
    return GpiButton::NONE;
#endif
}

int InputHandler::poll()
{
#ifdef __linux__
    if (fd_ < 0) return 0;

    int count = 0;
    struct input_event ev;

    while (true) {
        ssize_t n = ::read(fd_, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_WARN("InputHandler: read error: %s", strerror(errno));
            close();
            break;
        }
        if (n == 0) break;
        if (n < (ssize_t)sizeof(ev)) continue;

        if (ev.type != EV_KEY) continue;
        if (ev.value != 0 && ev.value != 1) continue;

        GpiButton btn = mapKeyCode(ev.code);
        if (btn == GpiButton::NONE) continue;

        if (cb_) {
            ButtonEvent bev;
            bev.button  = btn;
            bev.pressed = (ev.value == 1);
            cb_(bev);
        }
        count++;
    }

    return count;
#else
    return 0;   // macOS: ncurses handles input in TerminalUI::run()
#endif
}

std::string InputHandler::autoDetect()
{
#ifdef __linux__
    glob_t g;
    memset(&g, 0, sizeof(g));
    if (glob("/dev/input/event*", 0, nullptr, &g) != 0) {
        globfree(&g);
        return "";
    }

    std::string found;
    for (size_t i = 0; i < g.gl_pathc; ++i) {
        int fd = ::open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        uint8_t evBits[EV_MAX / 8 + 1] = {};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0) { ::close(fd); continue; }
        bool hasEvKey = (evBits[EV_KEY / 8] >> (EV_KEY % 8)) & 1;
        if (!hasEvKey) { ::close(fd); continue; }

        uint8_t keyBits[KEY_MAX / 8 + 1] = {};
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0) { ::close(fd); continue; }
        bool hasKeyUp = (keyBits[KEY_UP / 8] >> (KEY_UP % 8)) & 1;
        ::close(fd);

        if (hasKeyUp) { found = g.gl_pathv[i]; break; }
    }

    globfree(&g);
    return found;
#else
    return "";
#endif
}
