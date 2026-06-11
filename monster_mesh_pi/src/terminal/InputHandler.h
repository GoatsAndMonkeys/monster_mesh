#pragma once
#include "../shared/platform.h"
#include <functional>
#include <string>

// GPI Case button mapping (Xbox 360 layout on GPI Case 2W)
enum class GpiButton : uint8_t {
    UP = 0, DOWN, LEFT, RIGHT,
    A,      // bottom face button (BTN_SOUTH) — confirm/select
    B,      // right face button  (BTN_EAST)  — back/cancel
    X,      // left face button   (BTN_WEST)  — screen off
    Y,      // top face button    (BTN_NORTH) — boss key
    START,  // open command menu
    SELECT, // toggle help
    NONE
};

// Button event
struct ButtonEvent {
    GpiButton button;
    bool      pressed;  // true=press, false=release
};

class InputHandler {
public:
    using ButtonCallback = std::function<void(const ButtonEvent &ev)>;

    InputHandler();
    ~InputHandler();

    // Open the GPI Case input device. If path is nullptr, auto-detect.
    // Returns true on success.
    bool open(const char *devicePath = nullptr);
    void close();
    bool isOpen() const { return fd_ >= 0; }

    void setButtonCallback(ButtonCallback cb) { cb_ = cb; }

    // Poll for button events. Returns number of events processed.
    int poll();

    // Auto-detect the GPI Case input device
    static std::string autoDetect();

private:
    int fd_ = -1;
    ButtonCallback cb_;

    // Map Linux key code -> GpiButton
    static GpiButton mapKeyCode(int code);
};
