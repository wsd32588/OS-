#include "console/console_device.hpp"

#include <algorithm>
#include <array>

#include "console/console_service.hpp"
#include "user_memory_fixture.hpp"

namespace {
using tinyos::console::ConsoleDevice;
using tinyos::console::ConsoleIo;
using namespace tinyos_test;

struct IoCapture {
    std::array<int, 512> input{};
    std::array<unsigned char, 512> output{};
    size_t input_size{};
    size_t input_position{};
    size_t read_calls{};
    size_t output_size{};
    size_t write_calls{};
    bool overflow{};

    static int read(void* context) noexcept {
        auto& capture = *static_cast<IoCapture*>(context);
        ++capture.read_calls;
        if (capture.input_position == capture.input_size) {
            return -1;
        }
        return capture.input[capture.input_position++];
    }

    static void write(void* context, std::span<const unsigned char> source) noexcept {
        auto& capture = *static_cast<IoCapture*>(context);
        ++capture.write_calls;
        if (source.size() > capture.output.size() - capture.output_size) {
            capture.overflow = true;
            return;
        }
        for (unsigned char byte : source) {
            capture.output[capture.output_size++] = byte;
        }
    }

    ConsoleIo io() noexcept { return {this, read, write}; }
};

int null_context_read(void*) noexcept { return -1; }
void null_context_write(void*, std::span<const unsigned char>) noexcept {}

void test_unconfigured_and_atomic_configuration() {
    ConsoleDevice device{};
    const ConsoleDevice& view = device;
    std::array<unsigned char, 3> destination{7, 8, 9};
    const std::array<unsigned char, 2> source{0, 255};
    require(!view.ready(), "a new console device is not configured");
    require_equal(device.poll_input(), -1L, "unconfigured input polling is rejected");
    require_equal(view.read_current(destination), -1L, "unconfigured reads are rejected");
    require_equal(view.read_current({}), -1L, "unconfigured zero-length reads are also rejected");
    require_equal(device.commit_read(0), -1, "unconfigured zero-byte commits are rejected");
    require_equal(view.write(source), -1L, "unconfigured output is rejected");
    require_equal(view.write({}), -1L, "unconfigured empty output is rejected");
    require(destination == std::array<unsigned char, 3>{7, 8, 9},
            "unconfigured reads do not touch destination bytes");

    IoCapture capture{};
    require_equal(device.configure({}), -1, "configuration requires both callback pointers");
    require_equal(device.configure({&capture, nullptr, IoCapture::write}), -1,
                  "a missing receive callback is rejected");
    require_equal(device.configure({&capture, IoCapture::read, nullptr}), -1,
                  "a missing output callback is rejected");
    require(!device.ready() && capture.read_calls == 0 && capture.write_calls == 0,
            "failed configuration publishes no state and invokes no callbacks");
    require_equal(device.configure({nullptr, null_context_read, null_context_write}), 0,
                  "a null context is legal when the callbacks do not need state");
    require(device.ready(), "valid configuration publishes readiness");
    require_equal(device.poll_input(), 0L, "a configured null-context input callback can report no input");
    require_equal(device.commit_read(0), 0, "configured zero-byte commit succeeds");
    require_equal(device.configure(capture.io()), -1, "a configured device cannot be rebound");
    require_equal(device.poll_input(), 0L, "the original null-context callback remains installed");
    require_equal(capture.read_calls, size_t{0}, "rejected configuration does not replace the old context");
}

void test_reconfiguration_preserves_pending_input() {
    IoCapture original{};
    original.input[0] = 'a';
    original.input[1] = 'b';
    original.input_size = 2;
    IoCapture replacement{};
    replacement.input[0] = 'x';
    replacement.input_size = 1;
    ConsoleDevice device{};
    require_equal(device.configure(original.io()), 0, "configure the original I/O provider");
    require_equal(device.poll_input(), 1L, "one poll receives exactly one byte");
    require(original.input_position == 1 && original.read_calls == 1,
            "polling does not drain the complete hardware input source");
    require_equal(device.configure(replacement.io()), -1, "reconfiguration fails with pending input");
    std::array<unsigned char, 4> output{9, 9, 9, 9};
    require_equal(device.read_current(output), 1L, "pending input survives rejected reconfiguration");
    require_equal(output[0], static_cast<unsigned char>('a'), "the old queued prefix is preserved");
    require_equal(device.poll_input(), 1L, "the next poll still calls the original provider");
    require_equal(device.read_current(output), 2L, "the original stream continues after rejected binding");
    require(output == std::array<unsigned char, 4>{'a', 'b', 9, 9} &&
            replacement.read_calls == 0 && replacement.write_calls == 0,
            "reconfiguration changes neither provider identity nor pending byte order");
    require_equal(device.write({output.data(), 2}), 2L, "output also retains the original provider");
    require(original.write_calls == 1 && original.output_size == 2 &&
            original.output[0] == 'a' && original.output[1] == 'b',
            "output uses the configured context and callback");
}

void test_poll_capacity_and_wrapped_fifo() {
    IoCapture capture{};
    for (size_t i = 0; i < tinyos::console::InputQueue::kCapacity; ++i) {
        capture.input[i] = static_cast<int>(i);
    }
    capture.input[256] = 42;
    capture.input[257] = 43;
    capture.input_size = 258;
    ConsoleDevice device{};
    require_equal(device.configure(capture.io()), 0, "configure a finite binary input source");
    for (size_t i = 0; i < 256; ++i) {
        require_equal(device.poll_input(), 1L, "each successful poll receives one binary byte");
        require_equal(capture.read_calls, i + 1, "each poll calls the receive provider once");
    }
    require_equal(device.poll_input(), 0L, "a full software queue reports no new byte received");
    require(capture.read_calls == 256 && capture.input_position == 256,
            "a full queue never consumes another hardware byte");
    std::array<unsigned char, 256> all{};
    require_equal(device.read_current(all), 256L, "all 256 slots including NUL and ff are available");
    for (size_t i = 0; i < all.size(); ++i) {
        require_equal(all[i], static_cast<unsigned char>(i), "the device preserves binary FIFO input");
    }
    require_equal(device.commit_read(250), 0, "consume only the delivered prefix");
    require_equal(device.poll_input(), 1L, "input resumes when capacity becomes available");
    require_equal(device.poll_input(), 1L, "append a second byte across the queue boundary");
    std::array<unsigned char, 8> wrapped{};
    require_equal(device.read_current(wrapped), 8L, "read the remaining wrapped byte sequence");
    require(wrapped == std::array<unsigned char, 8>{250, 251, 252, 253, 254, 255, 42, 43},
            "device delegation preserves FIFO across physical array wrap");
    require_equal(device.commit_read(wrapped.size()), 0, "consume the wrapped remainder");
    require_equal(device.poll_input(), 0L, "hardware -1 becomes no new input rather than a byte");
    require_equal(device.read_current(wrapped), 0L, "temporary input absence leaves an empty queue");
    require(capture.read_calls == 259 && capture.input_position == 258,
            "an empty hardware poll calls the provider once without advancing its data cursor");
}

void test_peek_commit_and_synchronous_output() {
    IoCapture capture{};
    capture.input[0] = 0;
    capture.input[1] = 255;
    capture.input[2] = 12;
    capture.input_size = 3;
    ConsoleDevice device{};
    require_equal(device.configure(capture.io()), 0, "configure the transactional input device");
    for (size_t i = 0; i < capture.input_size; ++i) {
        require_equal(device.poll_input(), 1L, "receive the finite transaction source");
    }
    std::array<unsigned char, 6> destination{};
    destination.fill(0xa5);
    require_equal(device.read_current({}), 0L, "an empty destination leaves queued input intact");
    require_equal(device.read_current({destination.data() + 1, 4}), 3L,
                  "read only available input into a bounded guarded destination");
    require(destination == std::array<unsigned char, 6>{0xa5, 0, 255, 12, 0xa5, 0xa5},
            "device reads preserve guards and the unused destination tail");
    std::array<unsigned char, 2> retry{};
    require_equal(device.read_current(retry), 2L, "abandoned delivery can peek again");
    require(retry == std::array<unsigned char, 2>{0, 255}, "a peek does not consume the pending prefix");
    require_equal(device.commit_read(4), -1, "commit beyond available input is rejected");
    require_equal(device.commit_read(0), 0, "zero-byte commit preserves pending input");
    require_equal(device.read_current(retry), 2L, "rejected commits preserve pending bytes");
    require(retry == std::array<unsigned char, 2>{0, 255}, "retry starts from the same pending prefix");
    require_equal(device.commit_read(1), 0, "commit only the successfully delivered byte");
    require_equal(device.read_current(retry), 2L, "read after a partial successful delivery");
    require(retry == std::array<unsigned char, 2>{255, 12}, "partial commit advances the device queue correctly");

    require_equal(device.write({}), 0L, "empty output succeeds without invoking the provider");
    require_equal(capture.write_calls, size_t{0}, "empty output has no callback side effect");
    std::array<unsigned char, 4> source{0, 255, 'A', '\n'};
    require_equal(device.write(source), 4L, "nonempty output reports the full synchronous byte count");
    source.fill(7);
    require(capture.write_calls == 1 && capture.output_size == 4 && !capture.overflow &&
            std::equal(capture.output.begin(), capture.output.begin() + 4,
                       std::array<unsigned char, 4>{0, 255, 'A', '\n'}.begin()),
            "one output callback synchronously consumes the bytes before the source changes");
}

void test_service_single_configuration_and_forwarding() {
    namespace service = tinyos::console;
    // The service keeps this context for the rest of the host process.
    static IoCapture capture{};
    require(!service::console_ready(), "the private service device begins unconfigured");
    unsigned char byte = 7;
    require_equal(service::poll_console_input(), -1L, "unconfigured service polling forwards failure");
    require_equal(service::read_console_current({&byte, 1}), -1L, "unconfigured service read forwards failure");
    require_equal(service::commit_console_read(0), -1, "unconfigured service commit forwards failure");
    require_equal(service::write_console({}), -1L, "unconfigured service output forwards failure");
    require_equal(byte, static_cast<unsigned char>(7), "an unconfigured service does not touch destination storage");
    require_equal(service::configure_console({}), -1, "the service rejects an incomplete provider");
    require(!service::console_ready(), "invalid service binding does not publish readiness");
    capture.input[0] = 255;
    capture.input[1] = 0;
    capture.input_size = 2;
    require_equal(service::configure_console(capture.io()), 0, "bind the long-lived service provider once");
    require(service::console_ready(), "service readiness reflects the configured private device");
    require_equal(service::poll_console_input(), 1L, "service polling receives through the configured provider");
    require_equal(service::configure_console({nullptr, null_context_read, null_context_write}), -1,
                  "the service rejects replacement even with pending input");
    require_equal(service::read_console_current({&byte, 1}), 1L, "service forwarding retains pending input");
    require_equal(byte, static_cast<unsigned char>(255), "rejected rebinding does not clear queued input");
    require_equal(service::commit_console_read(2), -1, "service forwarding preserves rejected-commit behavior");
    require_equal(service::commit_console_read(1), 0, "service forwarding commits the delivered input");
    require_equal(service::poll_console_input(), 1L, "service continues using its original input provider");
    require_equal(service::read_console_current({&byte, 1}), 1L, "the service reads the next original byte");
    require_equal(byte, static_cast<unsigned char>(0), "the service preserves binary NUL input");
    require_equal(service::write_console({&byte, 1}), 1L, "service output forwards through the same provider");
    require(capture.write_calls == 1 && capture.output_size == 1 && capture.output[0] == 0,
            "service output synchronously writes the configured byte");
    require_equal(service::commit_console_read(1), 0, "leave the service input drained after the test");
    require_equal(service::read_console_current({&byte, 1}), 0L, "service temporary emptiness reports zero bytes");
}

bool run_checked(const char* name, void (*test)()) {
    return run_case(name, [test] {
        PagePool<1> pages;
        const auto allocations = host_allocation_calls();
        const auto calls = host_call_counts();
        const auto available = pmm_available_pages();
        test();
        require_equal(host_allocation_calls(), allocations, "console devices allocate no physical pages");
        require_equal(host_call_counts(), calls, "console devices do not free, unmap or flush pages");
        require_equal(pmm_available_pages(), available, "console device operations preserve available pages");
    });
}
}

int test_console_device() {
    bool passed = true;
    passed = run_checked("console device: unconfigured refusal and atomic configuration", test_unconfigured_and_atomic_configuration) && passed;
    passed = run_checked("console device: reconfiguration preserves provider and pending input", test_reconfiguration_preserves_pending_input) && passed;
    passed = run_checked("console device: single-byte polling, full backpressure and wrapped FIFO", test_poll_capacity_and_wrapped_fifo) && passed;
    passed = run_checked("console device: peek/commit and synchronous binary output", test_peek_commit_and_synchronous_output) && passed;
    passed = run_checked("console service: single long-lived binding and forwarding", test_service_single_configuration_and_forwarding) && passed;
    return passed ? 0 : -1;
}
