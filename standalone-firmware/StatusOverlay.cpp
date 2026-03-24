#include "StatusOverlay.h"
#include "Lobby.h"

bool StatusOverlay::lobbyIsOpen() const {
    return lobby_ && lobby_->isOpen();
}
