#ifndef TINYOS_TEST_SUPPORT_HPP
#define TINYOS_TEST_SUPPORT_HPP

#include <exception>
#include <iostream>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace tinyos_test {

struct Failure {};

inline bool check(bool condition, std::string_view message,
                  std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << " at " << location.file_name()
                  << ':' << location.line() << '\n';
    }
    return condition;
}

template <typename Actual, typename Expected>
bool values_equal(const Actual &actual, const Expected &expected) {
    if constexpr (std::is_integral_v<Actual> && std::is_integral_v<Expected> &&
                  !std::is_same_v<Actual, bool> && !std::is_same_v<Expected, bool>) {
        return std::cmp_equal(actual, expected);
    } else {
        return actual == expected;
    }
}

template <typename Actual, typename Expected>
bool check_equal(const Actual &actual, const Expected &expected,
                 std::string_view message = "values must be equal",
                 std::source_location location = std::source_location::current()) {
    if (values_equal(actual, expected)) {
        return true;
    }
    check(false, message, location);
    if constexpr (requires { std::cerr << actual << expected; }) {
        std::cerr << "    actual: " << actual << "\n    expected: " << expected << '\n';
    }
    return false;
}

inline void require(bool condition, std::string_view message,
                    std::source_location location = std::source_location::current()) {
    if (!check(condition, message, location)) {
        throw Failure{};
    }
}

template <typename Actual, typename Expected>
void require_equal(const Actual &actual, const Expected &expected,
                   std::string_view message = "values must be equal",
                   std::source_location location = std::source_location::current()) {
    if (!check_equal(actual, expected, message, location)) {
        throw Failure{};
    }
}

template <typename State, typename Result>
struct Observation {
    State before;
    Result result;
    State after;
};

// Take both snapshots around exactly one execution of the operation.
template <typename Capture, typename Operation>
auto observe(Capture capture, Operation operation) {
    auto before = capture();
    auto result = operation();
    auto after = capture();
    return Observation{std::move(before), std::move(result), std::move(after)};
}

template <typename Capture, typename Operation, typename Expected>
void expect_no_change(Capture capture, Operation operation, const Expected &expected,
                      std::source_location location = std::source_location::current()) {
    const auto observation = observe(capture, operation);
    require_equal(observation.result, expected, "operation return value", location);
    require_equal(observation.after, observation.before,
                  "operation must preserve state and call counts", location);
}

template <typename Test>
bool run_case(std::string_view name, Test test) {
    try {
        test();
        std::cout << "[PASS] " << name << '\n';
        return true;
    } catch (const Failure &) {
        std::cerr << "[FAIL] case: " << name << '\n';
        return false;
    } catch (const std::exception &error) {
        std::cerr << "[FAIL] case: " << name << ": " << error.what() << '\n';
        return false;
    }
}

} // namespace tinyos_test

#endif
