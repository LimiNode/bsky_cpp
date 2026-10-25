#pragma once

#include <cstdint>

namespace bsky_cpp {

/// Describes the public library version.
struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;
};

/// Returns the version represented by the public headers.
constexpr Version version() noexcept {
    return Version{0, 1, 0};
}

/// Returns the version represented by the linked library.
///
/// \return The linked library version.
/// \thread_safety Thread-safe.
Version linked_version() noexcept;

}  // namespace bsky_cpp
