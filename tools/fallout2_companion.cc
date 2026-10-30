// Frame-only client of Armada's bottom compositor (or KDE's second output).
#include "platform/linux/companion_protocol.h"
#include "platform/linux/display.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace fallout::companion;
namespace {
bool sendMessage(Message m)
{
    m.timestamp = monotonicMs();
    // Never leave a finger pressed after a full socket queue: disconnecting
    // causes the engine to cancel all companion input without clicks.
    return send(kSocketFd, &m, sizeof(m), MSG_NOSIGNAL | MSG_DONTWAIT) == sizeof(m);
}
}
int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        puts("fallout2-companion: launched by fallout2-armada; requires inherited IPC descriptors.");
        return 0;
    }
    // Even if SDL blocks during video initialization, an engine crash or
    // forced termination must not leave a graphics helper behind.
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() == 1) return 1;
    struct stat info {};
    int socketType = 0;
    socklen_t typeLength = sizeof(socketType);
    if (fstat(kMemoryFd, &info) || info.st_size != sizeof(SharedFrames)
        || getsockopt(kSocketFd, SOL_SOCKET, SO_TYPE, &socketType, &typeLength) || socketType != SOCK_SEQPACKET) {
        fputs("companion: missing frame memory or control socket; use fallout2-armada\n", stderr);
        return 1;
    }
    auto* frames = static_cast<SharedFrames*>(mmap(nullptr, sizeof(SharedFrames), PROT_READ | PROT_WRITE, MAP_SHARED, kMemoryFd, 0));
    if (frames == MAP_FAILED) return 1;
    close(kMemoryFd);
    if (frames->version != kVersion) return 1;
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_APP_NAME, "Fallout 2 Companion");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    // Steam's keyboard belongs to the primary Gamescope/Steam connection.
    // The helper accepts text but must never open or dismiss Steam's overlay.
    SDL_SetHintWithPriority("SDL_ENABLE_STEAM_SCREEN_KEYBOARD", "0", SDL_HINT_OVERRIDE);
    // The companion must not open controllers: Steam/InputPlumber's virtual
    // gamepad belongs to the main process, regardless of window focus.
    fprintf(stderr, "companion: initializing video\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "companion: SDL: %s\n", SDL_GetError()); return 1;
    }
    SDL_StopTextInput(); // SDL2 enables text by default; suppress startup OSKs.
    fprintf(stderr, "companion: video driver %s\n", SDL_GetCurrentVideoDriver());
    const char* output = getenv("FALLOUT_BOTTOM_OUTPUT");
    bool gameMode = getenv("FALLOUT_BOTTOM_COMPOSITOR") != nullptr;
    int display = gameMode ? 0 : fallout::linuxConfiguredDisplay("BOTTOM", "DSI-1");
    if (display < 0) {
        fprintf(stderr, "companion: output %s unavailable (logical %s, physical %s)\n",
            output ? output : "DSI-1", getenv("FALLOUT_BOTTOM_BOUNDS") ? getenv("FALLOUT_BOTTOM_BOUNDS") : "unset",
            getenv("FALLOUT_BOTTOM_PIXEL_BOUNDS") ? getenv("FALLOUT_BOTTOM_PIXEL_BOUNDS") : "unset");
        for (int index = 0; index < SDL_GetNumVideoDisplays(); ++index) {
            SDL_Rect bounds {};
            SDL_GetDisplayBounds(index, &bounds);
            fprintf(stderr, "companion: display %d %s bounds=%d,%d,%d,%d\n", index, SDL_GetDisplayName(index), bounds.x, bounds.y, bounds.w, bounds.h);
        }
        SDL_Quit(); return 1;
    }
    SDL_DisplayMode mode {};
    if (SDL_GetDesktopDisplayMode(display, &mode) != 0 || !validSize(mode.w, mode.h)) {
        fputs("companion: unsupported output dimensions\n", stderr); SDL_Quit(); return 1;
    }
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
    const char* windowed = getenv("FALLOUT_COMPANION_WINDOWED");
    if (!windowed || strcmp(windowed, "1") != 0) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    SDL_Window* window = SDL_CreateWindow("Fallout 2 Companion", SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display), mode.w, mode.h, flags);
    fprintf(stderr, "companion: window %s\n", window ? "created" : SDL_GetError());
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED) : nullptr;
    if (!renderer && window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "companion: renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    bool textInputActive = false;
    int width = 0, height = 0;
    float density = fallout::linuxDensity("FALLOUT_BOTTOM_DENSITY", 2.6f);
    auto ready = [&]() {
        SDL_GetRendererOutputSize(renderer, &width, &height);
        if (!validSize(width, height)) return false;
        Message m; m.type = Type::Ready; m.a = width; m.b = height; m.x = density;
        return sendMessage(m);
    };
    bool running = ready();
    Message focus; focus.type = Type::Focus; focus.a = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    running = running && sendMessage(focus);
    fprintf(stderr, "companion: output %s, %dx%d, density %.2f, driver %s\n", SDL_GetDisplayName(display), width, height, density, SDL_GetCurrentVideoDriver());
    std::map<std::pair<SDL_TouchID, SDL_FingerID>, int> fingers;
    std::array<bool, 32> used {};
    SDL_Texture* texture = nullptr;
    uint32_t sequence = 0;
    int textureWidth = 0, textureHeight = 0;
    Uint32 heartbeat = 0;
    bool mouseDown = false;
    bool captured = false;
    bool redraw = true;
    auto cancel = [&]() {
        fingers.clear(); used.fill(false); mouseDown = false;
        Message m; m.type = Type::Cancel; return sendMessage(m);
    };
    while (running) {
        Message command;
        ssize_t received = recv(kSocketFd, &command, sizeof(command), MSG_DONTWAIT | MSG_TRUNC);
        if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) break;
        if (received > 0 && (received != sizeof(command) || !validMessage(command) || command.type == Type::Stop)) break;
        if (received > 0 && command.type == Type::TextInput) {
            if (textInputActive && command.a) SDL_StopTextInput();
            textInputActive = command.a != 0;
            if (textInputActive) SDL_StartTextInput();
            else SDL_StopTextInput();
        }
        SDL_Event event;
        // Bounded polling ensures continuous input cannot starve heartbeats
        // or frame presentation.
        for (int n = 0; n < 128 && running && SDL_PollEvent(&event); ++n) {
            if (textInputActive && event.type == SDL_TEXTINPUT) {
                Message m; m.type = Type::Text;
                memcpy(m.text, event.text.text, sizeof(m.text)); m.text[31] = 0;
                running = sendMessage(m);
            }
            if (textInputActive && (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)) {
                int code = -1;
                if (event.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) code = 0;
                if (event.key.keysym.scancode == SDL_SCANCODE_RETURN || event.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) code = 1;
                if (event.key.keysym.scancode == SDL_SCANCODE_DELETE) code = 2;
                if (code >= 0) {
                    Message m; m.type = Type::EditKey; m.a = code; m.b = event.type == SDL_KEYDOWN;
                    running = sendMessage(m);
                }
            }
            if (event.type == SDL_QUIT) { fputs("companion: quit event\n", stderr); running = false; break; }
            if (event.type == SDL_WINDOWEVENT) {
                switch (event.window.event) {
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_RESTORED:
                case SDL_WINDOWEVENT_SHOWN:
                    running = cancel() && ready(); redraw = true; break;
                case SDL_WINDOWEVENT_HIDDEN:
                case SDL_WINDOWEVENT_MINIMIZED:
                    // Gamescope can hide the window while the leased panel
                    // wakes or focus moves. Keep the connection and cancel
                    // captures; killing/restarting here makes activation
                    // impossible because each new window is hidden again.
                    running = cancel();
                    { Message m; m.type = Type::Visibility; m.a = 0; running = running && sendMessage(m); }
                    redraw = true;
                    break;
                case SDL_WINDOWEVENT_FOCUS_GAINED:
                case SDL_WINDOWEVENT_FOCUS_LOST: {
                    Message m; m.type = Type::Focus; m.a = event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
                    running = sendMessage(m);
                    if (!m.a) running = running && cancel();
                    break;
                }
                default: break;
                }
            }
            if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) {
                auto key = std::make_pair(event.tfinger.touchId, event.tfinger.fingerId);
                auto it = fingers.find(key);
                if (it == fingers.end()) {
                    if (event.type != SDL_FINGERDOWN) continue;
                    int slot = 0;
                    while (slot < 31 && used[slot]) ++slot;
                    if (slot == 31) continue; // 31 is reserved for desktop mouse testing
                    used[slot] = true;
                    it = fingers.emplace(key, slot).first;
                }
                Message m; m.type = Type::Touch;
                m.a = event.type == SDL_FINGERDOWN ? 0 : event.type == SDL_FINGERMOTION ? 1 : 2;
                m.b = it->second;
                m.x = std::clamp(event.tfinger.x, 0.0f, 1.0f); m.y = std::clamp(event.tfinger.y, 0.0f, 1.0f);
                running = sendMessage(m);
                if (m.a == 2) { used[it->second] = false; fingers.erase(it); }
            }
            // Explicit real-mouse support in the presentation helper also
            // makes development possible without a touchscreen. SDL's
            // touch-generated mouse events remain suppressed.
            if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP || event.type == SDL_MOUSEMOTION)
                && event.motion.which != SDL_TOUCH_MOUSEID) {
                bool motion = event.type == SDL_MOUSEMOTION;
                if (!motion && event.button.button != SDL_BUTTON_LEFT) continue;
                if (motion && !mouseDown) continue;
                if (!motion) mouseDown = event.type == SDL_MOUSEBUTTONDOWN;
                int w, h; SDL_GetWindowSize(window, &w, &h);
                Message m; m.type = Type::Touch; m.b = 31;
                m.a = motion ? 1 : mouseDown ? 0 : 2;
                m.x = std::clamp(float(motion ? event.motion.x : event.button.x) / std::max(w, 1), 0.0f, 1.0f);
                m.y = std::clamp(float(motion ? event.motion.y : event.button.y) / std::max(h, 1), 0.0f, 1.0f);
                running = sendMessage(m);
            }
        }
        // Two slots maximum. Compare sequence numbers so a newer frame can
        // never be overwritten by an older slot arriving out of order.
        for (int n = 0; n < 2; ++n) {
            Slot* slot = acquireRead(frames);
            if (!slot) break;
            if (validSize(slot->width, slot->height) && int32_t(slot->sequence - sequence) > 0) {
                if (!texture || textureWidth != int(slot->width) || textureHeight != int(slot->height)) {
                    SDL_DestroyTexture(texture);
                    textureWidth = slot->width; textureHeight = slot->height;
                    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, textureWidth, textureHeight);
                }
                if (!texture || SDL_UpdateTexture(texture, nullptr, slot->pixels, slot->width * 4) != 0) {
                    fprintf(stderr, "companion: texture upload failed: %s\n", SDL_GetError()); running = false;
                }
                sequence = slot->sequence;
                redraw = true;
            }
            slot->state.store(uint32_t(SlotState::Free), std::memory_order_release);
        }
        if (redraw) {
            SDL_SetRenderDrawColor(renderer, 4, 9, 5, 255);
            SDL_RenderClear(renderer);
            if (texture) SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            if (texture && !captured && getenv("FALLOUT_COMPANION_CAPTURE")) {
                SDL_Surface* capture = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
                if (capture && SDL_RenderReadPixels(renderer, nullptr, capture->format->format, capture->pixels, capture->pitch) == 0) {
                    SDL_SaveBMP(capture, getenv("FALLOUT_COMPANION_CAPTURE"));
                    captured = true;
                }
                SDL_FreeSurface(capture);
            }
            SDL_RenderPresent(renderer);
            redraw = false;
        }
        if (SDL_GetTicks() - heartbeat >= 250) {
            heartbeat = SDL_GetTicks();
            running = running && sendMessage(Message {});
        }
        SDL_Delay(8);
    }
    cancel();
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    SDL_Quit(); munmap(frames, sizeof(SharedFrames)); close(kSocketFd);
    return 0;
}
