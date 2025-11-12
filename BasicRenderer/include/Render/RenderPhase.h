#pragma once
#include <string>
#include <vector>
#include <string_view>
#include <cassert>
#include <functional>

struct RenderPhase {
    // e.g. {"Builtin","GBuffer","Normals"}
    std::vector<std::string> segments;
	size_t hash = 0;
    std::string name;
    RenderPhase() = default;

    // parse "A::B::C"
    RenderPhase(std::string_view s) {
        size_t start = 0;
        while (start < s.size()) {
            auto pos = s.find("::", start);
            if (pos == std::string_view::npos) pos = s.size();
            segments.emplace_back(s.substr(start, pos - start));
            start = pos + 2;
        }
		hash = Hasher{}(*this);
        name = s;
    }

    // String constructor
    RenderPhase(const std::string& s) : RenderPhase(std::string_view(s)) {}

    // direct-from-literal ctor:
    RenderPhase(char const* s) : RenderPhase(std::string_view{ s }){}

    // join back into "A::B::C"
    std::string ToString() const {
        std::string out;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i) out += "::";
            out += segments[i];
        }
        return out;
    }

    bool operator==(RenderPhase const& o) const noexcept {
        return segments == o.segments;
    }
    bool operator!=(RenderPhase const& o) const noexcept {
        return !(*this == o);
    }

    struct Hasher {
        size_t operator()(RenderPhase const& id) const noexcept {
            size_t h = 0;
            for (auto& seg : id.segments)
                h = h * 31 + std::hash<std::string>()(seg);
            return h;
        }
    };
};

namespace std {
    template<>
    struct hash<RenderPhase> {
        size_t operator()(RenderPhase const& id) const noexcept {
            return RenderPhase::Hasher{}(id);
        }
    };
}