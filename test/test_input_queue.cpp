#include "console/input_queue.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include "user_memory_fixture.hpp"

namespace {
using tinyos::console::InputQueue;
using namespace tinyos_test;

void test_empty_and_binary_bytes() {
    InputQueue queue{};
    const InputQueue& view = queue;
    std::array<unsigned char, 4> output{9, 9, 9, 9};
    require(view.empty() && !view.full() && view.size() == 0,
            "a new input queue is empty with all capacity available");
    require_equal(view.read_current(output), size_t{0}, "empty input returns zero bytes");
    require(output == std::array<unsigned char, 4>{9, 9, 9, 9},
            "an empty read leaves the complete destination unchanged");
    require_equal(queue.commit_read(0), 0, "empty zero-byte commit succeeds");
    require(queue.try_push(0) && queue.try_push(255), "NUL and ff are valid queued bytes");
    require_equal(view.read_current({}), size_t{0}, "an empty destination consumes nothing");
    require_equal(view.read_current(output), size_t{2}, "binary read reports the actual count");
    require(output == std::array<unsigned char, 4>{0, 255, 9, 9},
            "binary bytes retain their values and the unused destination tail stays intact");
    require(view.size() == 2 && !view.empty() && !view.full(),
            "reading does not consume binary input");
    require_equal(queue.commit_read(2), 0, "successfully delivered binary input can be consumed");
    require(view.empty() && view.size() == 0, "committing the actual count empties the queue");
}

void test_full_rejects_without_overwrite() {
    InputQueue queue{};
    std::array<unsigned char, InputQueue::kCapacity> expected{};
    for (size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<unsigned char>(i);
        require(queue.try_push(expected[i]), "all 256 input slots are usable");
    }
    require(queue.full() && !queue.empty() && queue.size() == expected.size(),
            "a full queue distinguishes capacity from emptiness");
    require(!queue.try_push(42), "a full queue rejects the next byte");
    std::array<unsigned char, InputQueue::kCapacity> output{};
    require_equal(queue.read_current(output), expected.size(), "all full-queue bytes remain readable");
    require(output == expected && queue.full(), "full rejection preserves the oldest input and size");
    require_equal(queue.commit_read(1), 0, "consuming one byte makes exactly one slot available");
    require(queue.try_push(42) && queue.full(), "a consumed slot can be reused immediately");
    require_equal(queue.read_current(output), expected.size(), "the reused queue remains full");
    require(std::equal(output.begin(), output.end() - 1, expected.begin() + 1) && output.back() == 42,
            "reusing a full queue appends at the logical tail rather than overwriting its head");
}

void test_wrapped_fifo_and_partial_commit() {
    InputQueue queue{};
    for (size_t i = 0; i < InputQueue::kCapacity; ++i) {
        require(queue.try_push(static_cast<unsigned char>(i)), "prepare a full queue for wrapping");
    }
    require_equal(queue.commit_read(250), 0, "advance the logical head close to the array end");
    for (unsigned char byte = 0; byte < 20; ++byte) {
        require(queue.try_push(static_cast<unsigned char>(100 + byte)), "append across the array end");
    }
    const std::array<unsigned char, 10> prefix{250, 251, 252, 253, 254, 255, 100, 101, 102, 103};
    std::array<unsigned char, 10> output{};
    require_equal(queue.read_current(output), output.size(), "a short destination bounds a wrapped read");
    require(output == prefix && queue.size() == 26, "wrapped reads retain exact FIFO order without consuming");
    require_equal(queue.commit_read(8), 0, "commit only the successfully handled prefix");
    std::array<unsigned char, 18> remainder{};
    require_equal(queue.read_current(remainder), remainder.size(), "the remaining input starts after the partial commit");
    for (size_t i = 0; i < remainder.size(); ++i) {
        require_equal(remainder[i], static_cast<unsigned char>(102 + i), "the wrapped remainder retains exact order");
    }
    require_equal(queue.commit_read(remainder.size()), 0, "consume the wrapped remainder");
    require(queue.empty() && queue.try_push(77), "an empty queue with a nonzero head accepts new input");
    unsigned char last{};
    require_equal(queue.read_current({&last, 1}), size_t{1}, "read after draining a wrapped queue");
    require_equal(last, static_cast<unsigned char>(77), "stale physical slots are not exposed");
}

void test_abandoned_delivery_and_rejected_commit() {
    InputQueue queue{};
    for (unsigned char byte : std::array<unsigned char, 4>{'a', 'b', 'c', 'd'}) {
        require(queue.try_push(byte), "prepare input for a delivery attempt");
    }
    std::array<unsigned char, 2> first{};
    std::array<unsigned char, 2> retry{};
    require_equal(queue.read_current(first), first.size(), "peek at input before delivery");
    // Abandon this delivery: no commit is made. No user-copy syscall is involved.
    require_equal(queue.read_current(retry), retry.size(), "retry the abandoned delivery");
    require(first == retry && first == std::array<unsigned char, 2>{'a', 'b'} && queue.size() == 4,
            "repeated peeks retain the same unconsumed prefix");
    require_equal(queue.commit_read(5), -1, "an oversized commit is rejected");
    require_equal(queue.commit_read(std::numeric_limits<size_t>::max()), -1,
                  "commit rejects a huge count before doing index arithmetic");
    require_equal(queue.commit_read(0), 0, "zero-byte commit leaves nonempty input intact");
    require_equal(queue.read_current(retry), retry.size(), "input remains readable after commit rejection");
    require(retry == first && queue.size() == 4, "rejected commits preserve the head and valid size");
    require_equal(queue.commit_read(1), 0, "retry commits only the actually handled byte");
    require_equal(queue.read_current(retry), retry.size(), "read the next prefix after a partial retry");
    require(retry == std::array<unsigned char, 2>{'b', 'c'} && queue.size() == 3,
            "partial commit does not consume the rest of the peeked buffer");
}

void test_destination_bounds_and_serial_append() {
    InputQueue queue{};
    require(queue.try_push(11) && queue.try_push(22), "prepare a short source");
    std::array<unsigned char, 7> guarded{};
    guarded.fill(0xa5);
    require_equal(queue.read_current({guarded.data() + 1, 5}), size_t{2},
                  "a long destination returns only the queued byte count");
    require(guarded == std::array<unsigned char, 7>{0xa5, 11, 22, 0xa5, 0xa5, 0xa5, 0xa5},
            "queue reads preserve both guards and every unused destination byte");
    require(queue.try_push(33), "a serialized append can occur after a peek");
    require_equal(queue.commit_read(2), 0, "consume the delivered prefix while retaining later input");
    require_equal(queue.read_current({guarded.data() + 1, 5}), size_t{1}, "the appended byte remains pending");
    require_equal(guarded[1], static_cast<unsigned char>(33), "serial append preserves the delivered prefix contract");
}

bool run_checked(const char* name, void (*test)()) {
    return run_case(name, [test] {
        PagePool<1> pages;
        const auto allocations = host_allocation_calls();
        const auto calls = host_call_counts();
        const auto available = pmm_available_pages();
        test();
        require_equal(host_allocation_calls(), allocations, "input queues allocate no physical pages");
        require_equal(host_call_counts(), calls, "input queues do not free, unmap or flush pages");
        require_equal(pmm_available_pages(), available, "input queues preserve available physical pages");
    });
}
}

int test_input_queue() {
    bool passed = true;
    passed = run_checked("input queue: empty and binary byte semantics", test_empty_and_binary_bytes) && passed;
    passed = run_checked("input queue: full rejection preserves all 256 bytes", test_full_rejects_without_overwrite) && passed;
    passed = run_checked("input queue: wrapped FIFO and partial consumption", test_wrapped_fifo_and_partial_commit) && passed;
    passed = run_checked("input queue: abandoned delivery, rejected commit and retry", test_abandoned_delivery_and_rejected_commit) && passed;
    passed = run_checked("input queue: destination guards and serialized append", test_destination_bounds_and_serial_append) && passed;
    return passed ? 0 : -1;
}
