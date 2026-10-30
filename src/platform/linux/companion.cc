#include "companion.h"
#include "companion_protocol.h"
#include "bottom_screen.h"
#include "mui.h"
#include "debug.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <new>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

extern char** environ;
namespace fallout {
namespace {
using namespace companion;
int gSocket = -1;
pid_t gChild = -1;
SharedFrames* gFrames = nullptr;
int gWidth = 0, gHeight = 0;
float gDensity = 2.6f;
bool gEnabled = false, gFocused = false, gVisible = false;
Uint32 gRetry = 0, gLastSeen = 0;
uint32_t gSequence = 0;
std::vector<unsigned char> gCapturedFrame;
std::array<SDL_FingerID, 32> gDown {};
std::map<std::pair<SDL_TouchID, SDL_FingerID>, SDL_FingerID> gFingerIds;
SDL_FingerID gNextFinger = 1;

void disconnected()
{
    linuxCompanionCancel();
    if (gSocket != -1) close(gSocket);
    gSocket = -1;
    gWidth = gHeight = 0;
    gCapturedFrame.clear();
    gFocused = gVisible = false;
    if (gFrames) munmap(gFrames, sizeof(SharedFrames));
    gFrames = nullptr;
    if (gChild > 0) {
        // This frame-only child owns no persistent data. SDL can hang in
        // compositor initialization and trap SIGTERM, so forcibly retire it
        // after EOF/timeout rather than leave an orphan or block the engine.
        if (waitpid(gChild, nullptr, WNOHANG) == 0) kill(gChild, SIGKILL);
        else gChild = -1;
    }
    gRetry = SDL_GetTicks() + 2000;
}
void launch()
{
    if (gChild > 0) {
        if (waitpid(gChild, nullptr, WNOHANG) == 0) return;
        gChild = -1;
    }
    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) != 0) return;
    const char* runtime = getenv("XDG_RUNTIME_DIR");
    std::string path = std::string(runtime ? runtime : "/tmp") + "/fallout2-frames-XXXXXX";
    std::vector<char> name(path.begin(), path.end());
    name.push_back(0);
    int memory = mkstemp(name.data());
    if (memory != -1) unlink(name.data());
    if (memory == -1 || ftruncate(memory, sizeof(SharedFrames)) != 0) {
        if (memory != -1) close(memory);
        close(sockets[0]); close(sockets[1]); return;
    }
    fchmod(memory, 0600);
    fcntl(memory, F_SETFD, FD_CLOEXEC);
    void* mapped = mmap(nullptr, sizeof(SharedFrames), PROT_READ | PROT_WRITE, MAP_SHARED, memory, 0);
    if (mapped == MAP_FAILED) {
        close(memory); close(sockets[0]); close(sockets[1]); return;
    }
    // Initialize the control fields only; touching 50 MB of pixel pages
    // here would needlessly stall the game on each helper restart.
    auto* frames = static_cast<SharedFrames*>(mapped);
    frames->version = kVersion;
    for (auto& slot : frames->slots) new (&slot.state) std::atomic<uint32_t>(uint32_t(SlotState::Free));
    const char* command = getenv("FALLOUT_COMPANION_LAUNCHER");
    std::string defaultCommand;
    if (!command) {
        char* base = SDL_GetBasePath();
        defaultCommand = std::string(base ? base : "./") + "fallout2-companion";
        SDL_free(base);
        command = defaultCommand.c_str();
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, sockets[1], kSocketFd);
    posix_spawn_file_actions_adddup2(&actions, memory, kMemoryFd);
    posix_spawn_file_actions_addclose(&actions, sockets[0]);
    char* args[] = { const_cast<char*>(command), nullptr };
    pid_t child;
    int result = posix_spawn(&child, command, &actions, nullptr, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(memory); close(sockets[1]);
    if (result != 0) {
        debugPrint("companion: launch failed: %s\n", strerror(result));
        close(sockets[0]); munmap(mapped, sizeof(SharedFrames)); return;
    }
    gChild = child;
    gSocket = sockets[0];
    fcntl(gSocket, F_SETFL, fcntl(gSocket, F_GETFL) | O_NONBLOCK);
    gFrames = frames;
    gLastSeen = SDL_GetTicks();
    debugPrint("companion: presentation helper started (pid %d)\n", int(child));
}
}
void linuxNormalizeFinger(SDL_TouchFingerEvent& event, Uint32 type)
{
    auto key = std::make_pair(event.touchId, event.fingerId);
    auto it = gFingerIds.find(key);
    if (it == gFingerIds.end()) {
        if (type != SDL_FINGERDOWN) { event.fingerId = 0; return; }
        if (gFingerIds.size() >= 64) { event.fingerId = 0; return; }
        it = gFingerIds.emplace(key, gNextFinger++).first;
    }
    event.fingerId = it->second;
    // Retain ended physical fingers until SDL's queued events are drained;
    // the lost-finger check may need the mapping for a preceding DOWN.
    if (type == SDL_FINGERUP && event.touchId == kMuiBottomTouchId) gFingerIds.erase(it);
}
void linuxCompanionInit()
{
    const char* value = getenv("FALLOUT_COMPANION");
    gEnabled = !value || strcmp(value, "0") != 0;
    if (gEnabled) { launch(); gRetry = SDL_GetTicks() + 2000; }
}
void linuxCompanionCancel()
{
    // Reset captures without manufacturing releases at a valid widget.
    muiCancelSurface(MuiSurface::Bottom);
    for (auto& id : gDown) id = 0;
    for (auto it = gFingerIds.begin(); it != gFingerIds.end();) {
        if (it->first.first == kMuiBottomTouchId) it = gFingerIds.erase(it);
        else ++it;
    }
}
void linuxCompanionPoll()
{
    if (gSocket == -1) {
        if (gEnabled && Sint32(SDL_GetTicks() - gRetry) >= 0) {
            gRetry = SDL_GetTicks() + 2000;
            launch();
        }
        return;
    }
    // Bounded work in modal loops too. The helper sends a complete packet
    // or closes the connection, so an UP can never silently disappear.
    for (int count = 0; count < 128; ++count) {
        Message m;
        ssize_t length = recv(gSocket, &m, sizeof(m), MSG_DONTWAIT | MSG_TRUNC);
        if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (length != sizeof(m) || !validMessage(m)) { disconnected(); return; }
        gLastSeen = SDL_GetTicks();
        if (m.type == Type::Ready) {
            if (gWidth != m.a || gHeight != m.b) { linuxCompanionCancel(); gCapturedFrame.clear(); }
            gWidth = m.a; gHeight = m.b; gDensity = m.x; gVisible = true;
        } else if (m.type == Type::Visibility) {
            gVisible = m.a != 0;
            if (!gVisible) linuxCompanionCancel();
        } else if (m.type == Type::Cancel) {
            linuxCompanionCancel();
        } else if (m.type == Type::Focus) {
            gFocused = m.a != 0;
        } else if ((m.type == Type::Text || m.type == Type::EditKey) && SDL_IsTextInputActive()) {
            uint64_t now = monotonicMs();
            if (m.timestamp > now || now - m.timestamp > 400) continue;
            if (m.type == Type::Text) muiHandleTextInput(m.text);
            else {
                SDL_Event e {};
                e.type = m.b ? SDL_KEYDOWN : SDL_KEYUP;
                e.key.state = m.b ? SDL_PRESSED : SDL_RELEASED;
                e.key.keysym.scancode = m.a == 0 ? SDL_SCANCODE_BACKSPACE : m.a == 1 ? SDL_SCANCODE_RETURN : SDL_SCANCODE_DELETE;
                SDL_PushEvent(&e);
            }
        } else if (m.type == Type::Touch && gWidth > 0) {
            uint64_t now = monotonicMs();
            if (m.timestamp > now || now - m.timestamp > 400) {
                linuxCompanionCancel();
                continue;
            }
            SDL_Event e {};
            e.type = m.a == 0 ? SDL_FINGERDOWN : m.a == 1 ? SDL_FINGERMOTION : SDL_FINGERUP;
            e.tfinger.type = e.type;
            e.tfinger.touchId = kMuiBottomTouchId;
            e.tfinger.fingerId = m.b;
            e.tfinger.timestamp = SDL_GetTicks();
            e.tfinger.x = m.x; e.tfinger.y = m.y;
            e.tfinger.pressure = m.a == 2 ? 0.0f : 1.0f;
            linuxNormalizeFinger(e.tfinger, e.type);
            if (e.tfinger.fingerId == 0) continue;
            gDown[m.b] = m.a == 2 ? 0 : e.tfinger.fingerId;
            // Main thread: direct UI dispatch preserves cancellation order
            // and doesn't pretend these fingers belong to SDL's registry.
            muiHandleFingerEvent(&e);
        }
    }
    if (SDL_GetTicks() - gLastSeen > (gWidth > 0 ? 3000u : 15000u)) disconnected();
}
void linuxClearFingerIds()
{
    gFingerIds.clear();
    gDown.fill(0);
}
void linuxPruneFingerIds()
{
    for (auto it = gFingerIds.begin(); it != gFingerIds.end();) {
        bool found = it->first.first == kMuiBottomTouchId;
        if (!found) {
            int count = SDL_GetNumTouchFingers(it->first.first);
            for (int index = 0; index < count; ++index) {
                SDL_Finger* finger = SDL_GetTouchFinger(it->first.first, index);
                if (finger && finger->id == it->first.second) { found = true; break; }
            }
        }
        if (!found) it = gFingerIds.erase(it);
        else ++it;
    }
}
int linuxCompanionFingers(SDL_FingerID* ids, int capacity)
{
    int count = 0;
    for (const auto& finger : gFingerIds) {
        if (finger.first.first != kMuiBottomTouchId) continue;
        if (count < capacity) ids[count++] = finger.second;
    }
    return count;
}
bool linuxCompanionFocused() { return gFocused; }
void linuxCompanionShutdown()
{
    gEnabled = false;
    disconnected();
    if (gChild > 0) { waitpid(gChild, nullptr, WNOHANG); gChild = -1; }
}
bool bottomScreenAvailable() { return gSocket != -1 && gVisible && gWidth > 0 && gHeight > 0; }
void bottomScreenGetSize(int* w, int* h) { *w = gWidth; *h = gHeight; }
float bottomScreenGetDensity() { return gDensity; }
void bottomScreenPostFrame(const unsigned char* pixels, int width, int height, int pitch)
{
    if (!bottomScreenAvailable() || width != gWidth || height != gHeight || !companion::validSize(width, height) || pitch < width * 4) return;
    auto* slot = companion::acquireWrite(gFrames);
    if (!slot) return;
    slot->width = width; slot->height = height; slot->sequence = ++gSequence;
    for (int y = 0; y < height; ++y) memcpy(slot->pixels + size_t(y) * width * 4, pixels + size_t(y) * pitch, size_t(width) * 4);
    if (getenv("FALLOUT_CAPTURE_BOTTOM")) gCapturedFrame.assign(slot->pixels, slot->pixels + size_t(width) * height * 4);
    slot->state.store(uint32_t(companion::SlotState::Ready), std::memory_order_release);
}
// Test backend remains a separate CMake configuration, not a runtime device.
void bottomScreenSetTest(int, int, float) {}
const unsigned char* bottomScreenTestFrame() { return gCapturedFrame.empty() ? nullptr : gCapturedFrame.data(); }
}
