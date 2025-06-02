#pragma once
#include <variant>
#include <string>
#include <string_view>
#include <optional>
#include <cassert>
#include <functional>
#include <memory>

#include "BuiltinResources.h"
#include "Resources/ResourceStateTracker.h"

class Resource;

class ResourceIdentifier {
public:
    constexpr ResourceIdentifier(BuiltinResource key) noexcept
        : _id(key), _custom() 
    {}

    explicit ResourceIdentifier(std::string_view name) 
        : _id(), _custom()
    {
        if (auto built = BuiltinResourceFromString(name)) {
            _id = *built;
        } else {
            _custom = std::string{name};
        }
    }

    explicit ResourceIdentifier(std::string name)
        : _id(), _custom(std::move(name))
    {
        auto maybeBuilt = BuiltinResourceFromString(_custom.value());
        if (maybeBuilt) {
            _id = *maybeBuilt;
            _custom.reset();
        }
    }

    ResourceIdentifier(const ResourceIdentifier&) = default;
    ResourceIdentifier(ResourceIdentifier&&) noexcept = default;
    ResourceIdentifier& operator=(const ResourceIdentifier&) = default;
    ResourceIdentifier& operator=(ResourceIdentifier&&) noexcept = default;

    [[nodiscard]] bool IsBuiltin() const noexcept {
        return std::holds_alternative<BuiltinResource>(_id);
    }
    [[nodiscard]] bool IsCustom() const noexcept {
        return _custom.has_value();
    }

    [[nodiscard]] BuiltinResource AsBuiltin() const {
        assert(IsBuiltin());
        return std::get<BuiltinResource>(_id);
    }
    [[nodiscard]] const std::string& AsCustom() const {
        assert(IsCustom());
        return _custom.value();
    }

    [[nodiscard]] std::string ToString() const {
        if (IsBuiltin()) {
            return std::string{ BuiltinResourceToString(std::get<BuiltinResource>(_id)) };
        } else {
            return _custom.value();
        }
    }

    bool operator==(ResourceIdentifier const& o) const noexcept {
        if (IsBuiltin() && o.IsBuiltin()) {
            return std::get<BuiltinResource>(_id) == std::get<BuiltinResource>(o._id);
        }
        if (IsCustom() && o.IsCustom()) {
            return _custom.value() == o._custom.value();
        }
        return false;
    }
    bool operator!=(ResourceIdentifier const& o) const noexcept {
        return !(*this == o);
    }

    struct Hasher {
        size_t operator()(ResourceIdentifier const& r) const noexcept {
            if (r.IsBuiltin()) {
                return std::hash<uint16_t>()(
                    static_cast<uint16_t>(std::get<BuiltinResource>(r._id))
                    );
            } else {
                return std::hash<std::string>()(r._custom.value());
            }
        }
    };

private:
    std::variant<BuiltinResource> _id;        // holds a BuiltinResource if IsBuiltin()==true
    std::optional<std::string>   _custom;     // holds a string if IsCustom()==true
};

struct ResourceIdentifierAndRange {
    ResourceIdentifierAndRange(const ResourceIdentifier& resource) : identifier(resource) {
        range = {}; // Full range
    }
	ResourceIdentifierAndRange(const ResourceIdentifier& resource, const RangeSpec& range) : identifier(resource), range(range) {}
    ResourceIdentifier identifier;
    RangeSpec range;
};