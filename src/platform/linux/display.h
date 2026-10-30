#ifndef FALLOUT_LINUX_DISPLAY_H_
#define FALLOUT_LINUX_DISPLAY_H_
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace fallout {
// Prefer connector names; indices are explicit overrides, never an automatic
// assumption that the second enumerated display is the built-in panel.
inline int linuxDisplay(const char* selector)
{
    if (!selector || !*selector) return -1;
    if (std::strncmp(selector, "bounds:", 7) == 0) {
        int x, y, w, h, consumed = 0;
        if (std::sscanf(selector + 7, "%d,%d,%d,%d%n", &x, &y, &w, &h, &consumed) != 4
            || selector[7 + consumed] != 0 || w <= 0 || h <= 0) return -1;
        for (int index = 0; index < SDL_GetNumVideoDisplays(); ++index) {
            SDL_Rect rect {};
            if (SDL_GetDisplayBounds(index, &rect) == 0 && rect.x == x && rect.y == y && rect.w == w && rect.h == h) return index;
        }
        return -1;
    }
    for (int index = 0; index < SDL_GetNumVideoDisplays(); ++index) {
        const char* name = SDL_GetDisplayName(index);
        if (name && std::strcmp(name, selector) == 0) return index;
    }
    char* end = nullptr;
    long value = std::strtol(selector, &end, 10);
    return end != selector && !*end && value >= 0 && value < SDL_GetNumVideoDisplays() ? int(value) : -1;
}
// Select by connector identity before attempting KScreen bounds. SDL2 and
// SDL2-compat disagree about whether sizes are logical or physical on KDE.
inline int linuxConfiguredDisplay(const char* screen, const char* fallback)
{
    char key[64];
    std::snprintf(key, sizeof(key), "FALLOUT_%s_OUTPUT", screen);
    const char* selected = std::getenv(key);
    int display = linuxDisplay(selected ? selected : fallback);
    if (display >= 0) return display;
    for (const char* suffix : { "BOUNDS", "PIXEL_BOUNDS" }) {
        std::snprintf(key, sizeof(key), "FALLOUT_%s_%s", screen, suffix);
        display = linuxDisplay(std::getenv(key));
        if (display >= 0) return display;
    }
    return -1;
}
inline float linuxDensity(const char* variable, float fallback)
{
    const char* value = std::getenv(variable);
    if (!value) return fallback;
    char* end = nullptr;
    float density = std::strtof(value, &end);
    return end != value && !*end && density >= 0.5f && density <= 6.0f ? density : fallback;
}
} // namespace fallout
#endif
