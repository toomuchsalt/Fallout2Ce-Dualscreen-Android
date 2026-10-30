// Device smoke test: exercise the real backend/compositor without game data.
#include "platform/linux/companion.h"
#include "bottom_screen.h"
#include "mui.h"
#include <SDL.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace fallout {
int debugPrint(const char* format, ...) { va_list args; va_start(args, format); int rc = vfprintf(stderr, format, args); va_end(args); return rc; }
void muiHandleTextInput(const char*) {}
void muiCancelSurface(MuiSurface) {}
bool muiHandleFingerEvent(const SDL_Event*) { return true; }
}
int main()
{
    using namespace fallout;
    SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS);
    linuxCompanionInit();
    Uint32 start = SDL_GetTicks();
    while (!bottomScreenAvailable() && SDL_GetTicks() - start < 20000) { linuxCompanionPoll(); SDL_Delay(5); }
    if (!bottomScreenAvailable()) { fputs("FAIL: helper did not become ready\n", stderr); linuxCompanionShutdown(); return 1; }
    int w, h; bottomScreenGetSize(&w, &h);
    printf("Companion ready: %dx%d density %.2f\n", w, h, bottomScreenGetDensity());
    std::vector<unsigned char> pixels(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        size_t offset = (size_t(y) * w + x) * 4;
        pixels[offset] = x < w / 3 ? 220 : 10;
        pixels[offset + 1] = x >= w / 3 && x < w * 2 / 3 ? 220 : 10;
        pixels[offset + 2] = x >= w * 2 / 3 ? 220 : 10;
        if ((x / 64 + y / 64) % 2) for (int c = 0; c < 3; ++c) pixels[offset + c] /= 2;
        pixels[offset + 3] = 255;
    }
    start = SDL_GetTicks();
    int count = 0;
    while (SDL_GetTicks() - start < 3000) {
        linuxCompanionPoll(); bottomScreenPostFrame(pixels.data(), w, h, w * 4); ++count; SDL_Delay(33);
    }
    bool success = bottomScreenAvailable();
    linuxCompanionShutdown(); SDL_Quit();
    printf("%s: %d frames posted\n", success ? "PASS" : "FAIL", count);
    return success ? 0 : 1;
}
