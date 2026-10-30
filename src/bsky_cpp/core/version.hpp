/// \file version.hpp
/// \brief Compile-time and linked-library version reporting.
#pragma once

#include <cstdint>

namespace bsky_cpp {

/// Describes the public library version.
struct Version {
    /// Major release component.
    std::uint32_t major;
    /// Minor release component.
    std::uint32_t minor;
    /// Patch release component.
    std::uint32_t patch;
};

/// Returns the version represented by the public headers.
/// \return The header version.
constexpr Version version() noexcept {
    return Version{0, 1, 0};
}

/// Returns the version represented by the linked library.
///
/// \return The linked library version.
/// \par Thread safety
/// Thread-safe.
Version linked_version() noexcept;

}  // namespace bsky_cpp
