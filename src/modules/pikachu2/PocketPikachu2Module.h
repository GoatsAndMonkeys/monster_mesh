#pragma once
#include "configuration.h"

// Header is always available; .cpp self-guards on the exclude flag so
// non-T114 builds don't actually compile the implementation.

#include "MeshModule.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "Observer.h"

class PocketPikachu2Module : public MeshModule, public concurrency::OSThread
{
  public:
    PocketPikachu2Module();

  protected:
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

#if HAS_SCREEN
    virtual bool wantUIFrame() override { return true; }
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                           int16_t x, int16_t y) override;
#endif

    virtual int32_t runOnce() override;

  private:
    int handleInputEvent(const InputEvent *event);
    CallbackObserver<PocketPikachu2Module, const InputEvent *> inputObserver_ =
        CallbackObserver<PocketPikachu2Module, const InputEvent *>(
            this, &PocketPikachu2Module::handleInputEvent);

    void showGiftMenu();
    static void drawGameArea(OLEDDisplay *d, int16_t ox, int16_t oy);
    static void drawSidebar(OLEDDisplay *d, int16_t ox, int16_t oy);
    static void drawPowScreen(OLEDDisplay *d, int16_t ox, int16_t oy);

    bool     isFrameActive_   = false;
    bool     menuIsOpen_      = false;
    bool     pendingGiftMenu_ = false;
    uint32_t lastDrawMs_      = 0;
};

extern PocketPikachu2Module *pikachuModule2;

// (no closing include-guard — header always exposes its declarations)
