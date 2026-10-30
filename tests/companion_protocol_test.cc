#include "platform/linux/companion_protocol.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace fallout::companion;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (false)
int main()
{
    Message message;
    CHECK(validMessage(message));
    message.type = Type::Ready; message.a = 1240; message.b = 1080; message.x = 2.6f;
    CHECK(validMessage(message));
    message.a = -1; CHECK(!validMessage(message));
    message.a = kMaxWidth + 1; CHECK(!validMessage(message));
    message.a = 1240; message.x = std::numeric_limits<float>::quiet_NaN(); CHECK(!validMessage(message));
    message.type = Type::Touch; message.a = 0; message.b = 31; message.x = .5f; message.y = .5f;
    CHECK(validMessage(message));
    message.b = 32; CHECK(!validMessage(message));
    message.b = 0; message.x = -1; CHECK(!validMessage(message));
    message.x = .5f; message.version++; CHECK(!validMessage(message));
    auto* shared = static_cast<SharedFrames*>(mmap(nullptr, sizeof(SharedFrames), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    CHECK(shared != MAP_FAILED);
    for (auto& slot : shared->slots) new (&slot.state) std::atomic<uint32_t>(uint32_t(SlotState::Free));
    Slot* first = acquireWrite(shared);
    Slot* second = acquireWrite(shared);
    CHECK(first && second && first != second && !acquireWrite(shared));
    first->state.store(uint32_t(SlotState::Ready), std::memory_order_release);
    CHECK(acquireRead(shared) == first);
    CHECK(!acquireWrite(shared)); // a consumer-owned frame cannot be reclaimed
    first->state.store(uint32_t(SlotState::Free), std::memory_order_release);
    second->state.store(uint32_t(SlotState::Free), std::memory_order_release);
    // Exercise the actual process-shared release/acquire boundary, not just
    // the functions in a single thread. Pixel data must never tear.
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        unsigned highest = 0;
        while (highest < 1000) {
            Slot* slot = acquireRead(shared);
            if (!slot) { usleep(50); continue; }
            CHECK(slot->width == 8 && slot->height == 8);
            unsigned sequence = slot->sequence;
            for (int i = 0; i < 256; ++i) CHECK(slot->pixels[i] == sequence % 251);
            if (sequence > highest) highest = sequence;
            slot->state.store(uint32_t(SlotState::Free), std::memory_order_release);
        }
        _exit(0);
    }
    for (unsigned seq = 1; seq <= 1000; ++seq) {
        Slot* slot;
        while (!(slot = acquireWrite(shared))) usleep(50);
        slot->width = 8; slot->height = 8; slot->sequence = seq;
        for (int i = 0; i < 256; ++i) slot->pixels[i] = seq % 251;
        slot->state.store(uint32_t(SlotState::Ready), std::memory_order_release);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    munmap(shared, sizeof(SharedFrames));
    puts("PASS: message validation, bounded backpressure, process-shared frames");
}
