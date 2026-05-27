#pragma once
#include "configuration.h"

#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU

#include "MeshModule.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "Observer.h"
#include "game.h"   // for GameState enum + gameState extern

// ── PocketPikachuModule ──────────────────────────────────────────────────────
// One carousel frame (game + sidebar).
//
// Navigation:
//   Short press (USER_PRESS) — normally advances carousel; intercepted when
//                              the internal menu is open to cycle clock→status.
//   Long press (SELECT)      — enters the Pikachu menu from game, or exits
//                              back to game from clock/status.
//
// Mesh traffic:
//   Every received packet (broadcast OR encrypted DM) counts as one step via
//   game_on_mesh_message(). No port filtering — all traffic is counted.
//
// Install in Modules.cpp setupModules():
//   pikachuModule = new PocketPikachuModule();

class PocketPikachuModule : public MeshModule,
                            public Observable<const UIFrameEvent *>,
                            public concurrency::OSThread
{
  public:
    PocketPikachuModule();

  protected:
    // Receive every packet — broadcast + encrypted DMs
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override { return true; }
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

#if HAS_SCREEN
    // One carousel frame
    virtual bool wantUIFrame() override { return true; }
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state,
                           int16_t x, int16_t y) override;

    // Block carousel navigation while a Pikachu menu is open. Driven by the
    // live gameState so it always reflects reality (e.g. when the POW
    // animation auto-times-out from GS_GIFT_MENU → GS_HAPPY → GS_STAND).
    virtual bool interceptingKeyboardInput() override {
        return gameState == GS_GIFT_MENU ||
               gameState == GS_CLOCK_MENU ||
               gameState == GS_STATUS_MENU;
    }
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
#endif

    // OSThread — game logic ~20 fps
    virtual int32_t runOnce() override;

  private:
    uint32_t lastDrawMs_  = 0;   // millis() of last drawFrame call

    int handleInputEvent(const InputEvent *event);
    CallbackObserver<PocketPikachuModule, const InputEvent *> inputObserver_ =
        CallbackObserver<PocketPikachuModule, const InputEvent *>(
            this, &PocketPikachuModule::handleInputEvent);

    static void drawGameArea(OLEDDisplay *d, int16_t ox, int16_t oy);
    static void drawSidebar(OLEDDisplay *d, int16_t ox, int16_t oy);
    static void drawClockScreen(OLEDDisplay *d, int16_t ox, int16_t oy);
    static void drawStatusScreen(OLEDDisplay *d, int16_t ox, int16_t oy);
    static void drawGiftMenu(OLEDDisplay *d, int16_t ox, int16_t oy);
};

extern PocketPikachuModule *pikachuModule;

#endif // HAS_SCREEN && !MESHTASTIC_EXCLUDE_POCKETPIKACHU
