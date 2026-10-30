#ifndef FALLOUT_COMPANION_PROTOCOL_H_
#define FALLOUT_COMPANION_PROTOCOL_H_

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>

namespace fallout::companion {
constexpr uint32_t kVersion = 2;
constexpr uint32_t kMaxWidth = 2560;
constexpr uint32_t kMaxHeight = 2560;
constexpr size_t kFrameBytes = size_t(kMaxWidth) * kMaxHeight * 4;
constexpr int kSocketFd = 198;
constexpr int kMemoryFd = 199;
enum class Type : uint32_t { Ready = 1, Touch, Cancel, Heartbeat, Stop, Focus, Text, EditKey, Visibility, TextInput };
enum class SlotState : uint32_t { Free, Writing, Ready, Reading };
struct Message {
    uint32_t version = kVersion;
    Type type = Type::Heartbeat;
    int32_t a = 0;
    int32_t b = 0;
    float x = 0;
    float y = 0;
    char text[32] {};
    uint64_t timestamp = 0; // CLOCK_MONOTONIC ms, shared across processes
};
struct Slot {
    std::atomic<uint32_t> state { uint32_t(SlotState::Free) };
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sequence = 0;
    alignas(64) unsigned char pixels[kFrameBytes];
};
struct SharedFrames {
    uint32_t version = kVersion;
    Slot slots[2];
};
static_assert(std::atomic<uint32_t>::is_always_lock_free, "Shared frames require lock-free atomics");
inline uint64_t monotonicMs()
{
    timespec value {};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return uint64_t(value.tv_sec) * 1000 + value.tv_nsec / 1000000;
}
inline bool validSize(int width, int height)
{
    return width > 0 && height > 0 && width <= int(kMaxWidth) && height <= int(kMaxHeight);
}
inline bool validMessage(const Message& m)
{
    if (m.version != kVersion) return false;
    switch (m.type) {
    case Type::Ready:
        return validSize(m.a, m.b) && std::isfinite(m.x) && m.x >= 0.5f && m.x <= 6.0f;
    case Type::Touch:
        return m.a >= 0 && m.a <= 2 && m.b >= 0 && m.b < 32
            && std::isfinite(m.x) && std::isfinite(m.y)
            && m.x >= 0 && m.x <= 1 && m.y >= 0 && m.y <= 1;
    case Type::Cancel:
    case Type::Heartbeat:
    case Type::Stop:
        return true;
    case Type::Text:
        return m.text[31] == 0;
    case Type::EditKey:
        return m.a >= 0 && m.a <= 2 && (m.b == 0 || m.b == 1);
    case Type::TextInput:
    case Type::Visibility:
    case Type::Focus:
        return m.a == 0 || m.a == 1;
    }
    return false;
}
inline Slot* acquireWrite(SharedFrames* shared)
{
    for (auto& slot : shared->slots) {
        uint32_t expected = uint32_t(SlotState::Free);
        if (slot.state.compare_exchange_strong(expected, uint32_t(SlotState::Writing), std::memory_order_acquire)) return &slot;
    }
    // The producer never waits for a slow compositor. Only Ready slots can
    // be reclaimed; the consumer holds Reading until its texture upload ends.
    for (auto& slot : shared->slots) {
        uint32_t expected = uint32_t(SlotState::Ready);
        if (slot.state.compare_exchange_strong(expected, uint32_t(SlotState::Writing), std::memory_order_acquire)) return &slot;
    }
    return nullptr;
}
inline Slot* acquireRead(SharedFrames* shared)
{
    for (auto& slot : shared->slots) {
        uint32_t expected = uint32_t(SlotState::Ready);
        if (slot.state.compare_exchange_strong(expected, uint32_t(SlotState::Reading), std::memory_order_acquire)) return &slot;
    }
    return nullptr;
}
} // namespace fallout::companion
#endif
