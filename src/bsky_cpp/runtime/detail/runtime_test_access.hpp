#pragma once

#include <cstddef>
#include <functional>

namespace bsky_cpp::runtime {
class Runtime;
namespace detail {
enum class RuntimeFaultPoint {
    Terminalization,
    WorkError,
    DeliveryRejectionError,
    SubmitRejectionError
};
// Private integration fixture. Close the executor only on an idle external
// consumer; this exercises actual upstream post-admission queue rejection.
struct RuntimeTestAccess {
    static void close_executor(Runtime& runtime);
    static std::size_t registered_operations(Runtime& runtime);
    static void set_startup_hook(Runtime& runtime, std::function<void()> hook);
    static void set_post_initialize_hook(Runtime& runtime, std::function<void()> hook);
    static void set_admission_hook(Runtime& runtime, std::function<void()> hook);
    static void set_external_pass_hook(Runtime& runtime, std::function<void()> hook);
    static void inject_fault(Runtime& runtime, RuntimeFaultPoint point);
};
}
}
