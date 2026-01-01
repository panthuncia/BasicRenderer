#pragma once
#include <cstdint>
#include <vector>

#include <rhi.h>

#include "Resources/ResourceStateTracker.h"
#include "ImmediateBytecode.h"

namespace rg::imm
{
    struct Access {
        uint64_t     id = 0;
        RangeSpec    range{};
        ResourceState desired{};
        Queue        queue{};
    };

    class AccessLog {
    public:
        void Reset() { a_.clear(); }
        void Reserve(size_t n) { a_.reserve(n); }
        void Push(const Access& a) { a_.push_back(a); }

        std::span<const Access> Items() const { return a_; }

    private:
        std::vector<Access> a_;
    };
}
