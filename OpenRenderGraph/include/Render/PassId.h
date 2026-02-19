#pragma once
#include <cstdint>

struct PassId {
    uint32_t index{ UINT32_MAX };
    explicit operator bool() const noexcept { return index != UINT32_MAX; }
    bool operator==(const PassId& o) const noexcept { return index == o.index; }
    bool operator!=(const PassId& o) const noexcept { return !(*this == o); }
};

using PassUID = uint64_t;

// FNV-1a hash
constexpr PassUID Hash64(std::string_view s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}