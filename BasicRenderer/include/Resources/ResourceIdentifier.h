#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <cassert>
#include <functional>

#include "Resources/ResourceStateTracker.h"

struct ResourceIdentifier {
    // e.g. {"Builtin","GBuffer","Normals"}
    std::vector<std::string> segments;

    ResourceIdentifier() = default;

    // parse "A::B::C"
    ResourceIdentifier(std::string_view s) {
        size_t start = 0;
        while (start < s.size()) {
            auto pos = s.find("::", start);
            if (pos == std::string_view::npos) pos = s.size();
            segments.emplace_back(s.substr(start, pos - start));
            start = pos + 2;
        }
    }

    // String constructor
	ResourceIdentifier(const std::string& s) : ResourceIdentifier(std::string_view(s)) {}

    // direct-from-literal ctor:
    ResourceIdentifier(char const* s) : ResourceIdentifier(std::string_view{ s }){}

    // join back into "A::B::C"
    std::string ToString() const {
        std::string out;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i) out += "::";
            out += segments[i];
        }
        return out;
    }

    bool operator==(ResourceIdentifier const& o) const noexcept {
        return segments == o.segments;
    }
    bool operator!=(ResourceIdentifier const& o) const noexcept {
        return !(*this == o);
    }

    // does this id start with prefix P?  (i.e. P is a namespace
    // under which *this* lives)
    bool hasPrefix(ResourceIdentifier const& p) const noexcept {
        if (p.segments.size() > segments.size()) return false;
        return std::equal(p.segments.begin(), p.segments.end(),
            segments.begin());
    }

    struct Hasher {
        size_t operator()(ResourceIdentifier const& id) const noexcept {
            size_t h = 0;
            for (auto& seg : id.segments)
                h = h * 31 + std::hash<std::string>()(seg);
            return h;
        }
    };
};

struct ResourceIdentifierAndRange {
    ResourceIdentifierAndRange(const ResourceIdentifier& resource) : identifier(resource) {
        range = {}; // Full range
    }
	ResourceIdentifierAndRange(const ResourceIdentifier& resource, const RangeSpec& range) : identifier(resource), range(range) {}
    ResourceIdentifier identifier;
    RangeSpec range;
};