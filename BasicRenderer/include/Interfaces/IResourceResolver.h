#pragma once

#include <vector>
#include <memory>

class IResourceResolver {
	public:
	virtual ~IResourceResolver() = default;
	virtual std::vector<std::shared_ptr<Resource>> Resolve() const = 0;

    template<typename T>
    std::vector<std::shared_ptr<T>> ResolveAs(bool require_all_casts = true) const {
        static_assert(std::is_base_of_v<Resource, T>, "T must derive from Resource");

        auto base = Resolve();
        std::vector<std::shared_ptr<T>> out;
        out.reserve(base.size());

        for (auto& p : base) {
            if (auto d = std::dynamic_pointer_cast<T>(p)) {
                out.push_back(std::move(d));
            }
            else if (require_all_casts) {
                assert(false && "Resource could not be cast to requested type");
            }
        }
        return out;
    }
};