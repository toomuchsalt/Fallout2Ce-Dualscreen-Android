#ifndef FALLOUT_LINUX_COMPANION_H_
#define FALLOUT_LINUX_COMPANION_H_
#include <SDL.h>
namespace fallout {
void linuxCompanionInit();
void linuxCompanionPoll();
void linuxCompanionShutdown();
void linuxCompanionCancel();
// Synchronize the editing lifecycle with the window receiving bottom input.
void linuxCompanionSetTextInput(bool active);
bool linuxCompanionFocused();
// Canonical IDs include device identity, so top/bottom and multiple devices
// cannot collide even when the OS reuses pointer numbers.
void linuxNormalizeFinger(SDL_TouchFingerEvent& event, Uint32 type);
void linuxClearFingerIds();
void linuxPruneFingerIds();
int linuxCompanionFingers(SDL_FingerID* ids, int capacity);
} // namespace fallout
#endif
