#include "Game/IPlayerController.h"

// The global lives here rather than in PlayerController.cpp so that the handle
// does not belong to the first-person translation unit. Every controller
// implementation assigns to it; none of them owns it.
std::unique_ptr<IPlayerController> g_PlayerController;
