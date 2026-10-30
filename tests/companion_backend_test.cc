#include "platform/linux/companion.h"
#include "platform/linux/companion_protocol.h"
#include "bottom_screen.h"
#include "mui.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace fallout;
using namespace fallout::companion;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (false)
namespace {
std::vector<SDL_Event> events;
int cancellations = 0;
std::vector<std::string> texts;
void sendPacket(Message m) { m.timestamp = monotonicMs(); CHECK(send(kSocketFd, &m, sizeof(m), MSG_NOSIGNAL) == sizeof(m)); }
int fakeHelper()
{
    auto* frames = static_cast<SharedFrames*>(mmap(nullptr, sizeof(SharedFrames), PROT_READ | PROT_WRITE, MAP_SHARED, kMemoryFd, 0));
    CHECK(frames != MAP_FAILED);
    Message m; m.type = Type::Ready; m.a = 8; m.b = 8; m.x = 2.6f; sendPacket(m);
    m = Message {}; m.type = Type::Touch; m.x = .5f; m.y = .5f; sendPacket(m);
    Uint32 start = SDL_GetTicks();
    bool sawInactive = false, sawActive = false, sawStop = false;
    while (!sawStop && SDL_GetTicks() - start < 5000) {
        Message command;
        ssize_t n = recv(kSocketFd, &command, sizeof(command), MSG_DONTWAIT);
        if (n < 0) { SDL_Delay(1); continue; }
        CHECK(n == sizeof(command) && validMessage(command) && command.type == Type::TextInput);
        if (!command.a && !sawActive) sawInactive = true;
        if (command.a) {
            CHECK(sawInactive);
            sawActive = true;
            Message text; text.type = Type::Text; strcpy(text.text, "test"); sendPacket(text);
        } else if (sawActive) {
            sawStop = true;
            Message text; text.type = Type::Text; strcpy(text.text, "late text"); sendPacket(text);
        }
    }
    CHECK(sawInactive && sawActive && sawStop);
    bool read = false;
    while (SDL_GetTicks() - start < 5000) {
        auto* slot = acquireRead(frames);
        if (!slot) { SDL_Delay(1); continue; }
        CHECK(slot->width == 8 && slot->height == 8);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 32; ++x) CHECK(slot->pixels[y * 32 + x] == y);
        slot->state.store(uint32_t(SlotState::Free), std::memory_order_release);
        read = true; break;
    }
    CHECK(read);
    m.a = 2; sendPacket(m);
    // A stale tap must be canceled without reaching a widget.
    m.a = 0; m.timestamp = monotonicMs() - 1000;
    CHECK(send(kSocketFd, &m, sizeof(m), MSG_NOSIGNAL) == sizeof(m));
    m.type = Type::Cancel; sendPacket(m);
    // The resized companion must invalidate old captures.
    m.type = Type::Ready; m.a = 16; m.b = 16; m.x = 2.6f; sendPacket(m);
    // A new Ready (resize/reconnect) must replay the current editing state.
    bool replayed = false;
    while (!replayed && SDL_GetTicks() - start < 5000) {
        Message command;
        ssize_t n = recv(kSocketFd, &command, sizeof(command), MSG_DONTWAIT);
        if (n < 0) { SDL_Delay(1); continue; }
        CHECK(n == sizeof(command) && validMessage(command));
        CHECK(command.type == Type::TextInput && command.a == 0);
        replayed = true;
    }
    CHECK(replayed);
    return 0;
}
}
namespace fallout {
int debugPrint(const char*, ...) { return 0; }
void muiHandleTextInput(const char* text) { texts.emplace_back(text); }
void muiCancelSurface(MuiSurface surface) { CHECK(surface == MuiSurface::Bottom); ++cancellations; }
bool muiHandleFingerEvent(const SDL_Event* event) { events.push_back(*event); return true; }
}
int main(int argc, char** argv)
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    CHECK(SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS | SDL_INIT_VIDEO) == 0);
    SDL_StopTextInput();
    if (getenv("FALLOUT_TEST_HELPER")) return fakeHelper();
    setenv("FALLOUT_TEST_HELPER", "1", 1);
    setenv("FALLOUT_COMPANION_LAUNCHER", argv[0], 1);
    SDL_TouchFingerEvent top {}; top.touchId = 5; top.fingerId = 0;
    linuxNormalizeFinger(top, SDL_FINGERDOWN);
    CHECK(top.fingerId > 0);
    SDL_TouchFingerEvent other {}; other.touchId = 6; other.fingerId = 0;
    linuxNormalizeFinger(other, SDL_FINGERDOWN);
    CHECK(other.fingerId != top.fingerId);
    linuxCompanionInit();
    Uint32 start = SDL_GetTicks();
    while (events.empty() && SDL_GetTicks() - start < 5000) { linuxCompanionPoll(); SDL_Delay(1); }
    CHECK(bottomScreenAvailable()); CHECK(events.size() == 1);
    CHECK(events[0].type == SDL_FINGERDOWN && events[0].tfinger.touchId == kMuiBottomTouchId);
    CHECK(events[0].tfinger.fingerId != top.fingerId && events[0].tfinger.fingerId != other.fingerId);
    SDL_FingerID down[32]; CHECK(linuxCompanionFingers(down, 32) == 1);
    SDL_StartTextInput();
    linuxCompanionSetTextInput(true);
    while (texts.empty() && SDL_GetTicks() - start < 5000) { linuxCompanionPoll(); SDL_Delay(1); }
    CHECK(texts == std::vector<std::string> { "test" });
    SDL_StopTextInput();
    linuxCompanionSetTextInput(false);
    // Padded input pitch must be copied correctly into tightly packed frames.
    unsigned char pixels[8 * 40];
    for (int y = 0; y < 8; ++y) memset(pixels + y * 40, y, 40);
    bottomScreenPostFrame(pixels, 8, 8, 40);
    while (bottomScreenAvailable() && SDL_GetTicks() - start < 5000) { linuxCompanionPoll(); SDL_Delay(1); }
    CHECK(!bottomScreenAvailable()); CHECK(events.size() == 2);
    CHECK(events[1].type == SDL_FINGERUP && events[1].tfinger.fingerId == events[0].tfinger.fingerId);
    CHECK(texts == std::vector<std::string> { "test" });
    CHECK(cancellations >= 3); CHECK(linuxCompanionFingers(down, 32) == 0);
    linuxCompanionShutdown(); SDL_Quit();
    puts("PASS: child transport, editing lifecycle, late-text rejection, state replay, frames, touch cancellation");
    return 0;
}
