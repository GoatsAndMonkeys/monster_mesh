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
// GPI Case 2W reports D-pad as gamepad codes, not keyboard arrows.
#ifndef BTN_DPAD_UP
#define BTN_DPAD_UP     0x220
#endif
#ifndef BTN_DPAD_DOWN
#define BTN_DPAD_DOWN   0x221
#endif
#ifndef BTN_DPAD_LEFT
#define BTN_DPAD_LEFT   0x222
#endif
#ifndef BTN_DPAD_RIGHT
#define BTN_DPAD_RIGHT  0x223
#endif
#ifndef BTN_GAMEPAD
#define BTN_GAMEPAD     0x130
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
        case BTN_DPAD_UP:
        case KEY_UP:        return GpiButton::UP;
        case BTN_DPAD_DOWN:
        case KEY_DOWN:      return GpiButton::DOWN;
        case BTN_DPAD_LEFT:
        case KEY_LEFT:      return GpiButton::LEFT;
        case BTN_DPAD_RIGHT:
        case KEY_RIGHT:     return GpiButton::RIGHT;
        // GPI Case 2W: physical A button (right) reports as BTN_SOUTH (Xbox
        // "A" position), physical B button (left) reports as BTN_EAST (Xbox
        // "B" position).  Match the case's labels, not the underlying Xbox
        // semantic naming.
        case BTN_SOUTH:
        case KEY_ENTER:     return GpiButton::A;
        case BTN_EAST:
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

    // Track hat-switch state so we can emit press/release transitions
    // (the kernel reports axis position, not edge events).  GPI Case 2W's
    // RetroFlag controller exposes its D-pad as ABS_HAT0X / ABS_HAT0Y on
    // a Microsoft X-Box 360 pad emulation, not as discrete BTN_DPAD_*.
    static int8_t lastHatX_ = 0;
    static int8_t lastHatY_ = 0;

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

        // ── Buttons (EV_KEY) — A/B/Start/Select ─────────────────────────
        if (ev.type == EV_KEY) {
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
            continue;
        }

        // ── D-pad as hat-switch (EV_ABS, ABS_HAT0X / ABS_HAT0Y) ─────────
        // Value is -1 (left/up), 0 (released), or +1 (right/down).  We
        // synthesize press/release events on transitions through 0.
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_HAT0X) {
                int8_t v = (int8_t)ev.value;
                if (v != lastHatX_ && cb_) {
                    // Release whichever direction was active.
                    if (lastHatX_ < 0) { ButtonEvent r{GpiButton::LEFT,  false}; cb_(r); count++; }
                    if (lastHatX_ > 0) { ButtonEvent r{GpiButton::RIGHT, false}; cb_(r); count++; }
                    // Press the new direction (if any).
                    if (v < 0) { ButtonEvent p{GpiButton::LEFT,  true};  cb_(p); count++; }
                    if (v > 0) { ButtonEvent p{GpiButton::RIGHT, true};  cb_(p); count++; }
                    lastHatX_ = v;
                }
            } else if (ev.code == ABS_HAT0Y) {
                int8_t v = (int8_t)ev.value;
                if (v != lastHatY_ && cb_) {
                    if (lastHatY_ < 0) { ButtonEvent r{GpiButton::UP,   false}; cb_(r); count++; }
                    if (lastHatY_ > 0) { ButtonEvent r{GpiButton::DOWN, false}; cb_(r); count++; }
                    if (v < 0) { ButtonEvent p{GpiButton::UP,   true}; cb_(p); count++; }
                    if (v > 0) { ButtonEvent p{GpiButton::DOWN, true}; cb_(p); count++; }
                    lastHatY_ = v;
                }
            }
            continue;
        }
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
        // Accept any device that looks like keyboard arrows, BTN_DPAD_* gamepad,
        // or BTN_SOUTH (A button on any modern gamepad — Xbox360, Switch Pro,
        // PS4/5, the RetroFlag X360-emulated controller in the GPI Case 2W).
        bool hasKeyUp     = (keyBits[KEY_UP     / 8] >> (KEY_UP     % 8)) & 1;
        bool hasDpadUp    = (keyBits[BTN_DPAD_UP/ 8] >> (BTN_DPAD_UP% 8)) & 1;
        bool hasGamepadBtn= (keyBits[BTN_SOUTH  / 8] >> (BTN_SOUTH  % 8)) & 1;

        // Also peek at EV_ABS — Xbox-style controllers report the D-pad as
        // ABS_HAT0X/ABS_HAT0Y rather than discrete buttons.  If the device
        // has a hat axis, that's a strong gamepad signal even if it doesn't
        // expose BTN_SOUTH for some reason.
        uint8_t absBits[ABS_MAX / 8 + 1] = {};
        bool hasHat = false;
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0) {
            hasHat = (absBits[ABS_HAT0X / 8] >> (ABS_HAT0X % 8)) & 1;
        }
        ::close(fd);

        if (hasKeyUp || hasDpadUp || hasGamepadBtn || hasHat) { found = g.gl_pathv[i]; break; }
    }

    globfree(&g);
    return found;
#else
    return "";
#endif
}
