#pragma once

enum class PassBuilderKind { Render, Compute };

struct IPassBuilder {
    virtual ~IPassBuilder() = default;
    virtual PassBuilderKind Kind() const noexcept = 0;

    // What the graph needs from the pass builder
    virtual IResourceProvider* ResourceProvider() noexcept = 0;
    virtual void Finalize() = 0;
    virtual void Reset() = 0;
};