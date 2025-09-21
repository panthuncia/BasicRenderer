#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <optional>

#include <rhi.h>

enum class IndirectKind : uint8_t { Dispatch, DispatchMesh };

// Where the executed-count comes from
struct CountPolicy {
    // Execute up to this many commands (no counter)
    static CountPolicy Fixed(uint32_t maxCount) { return { Mode::Fixed, maxCount, false }; }
    // Use the buffer's internal UAV counter as the count source (common for culling)
    static CountPolicy FromCounter() { return { Mode::FromCounter, 0, true }; }
    enum class Mode : uint8_t { Fixed, FromCounter };
    Mode mode;
    uint32_t maxCount;     // only used for Fixed
    bool useInternalCounter;
};

class IndirectWorkload {
public:
    struct Desc {
        std::wstring       debugName;
        IndirectKind       kind;                 // DrawIndexed / Dispatch / DispatchMesh
        uint32_t           initialCapacity = 1024;
        uint32_t           growthIncrement = 1024;
        bool               useInternalCounterAsCount = true; // common path
        bool               createResetHelper = true;         // tiny upload/reset helper
    };

	static std::unique_ptr<IndirectWorkload> CreateUnique(Desc const& desc);

    void EnsureCapacity(uint32_t minCommands);

    void Execute(rhi::CommandList& cmd,
        const CountPolicy& count,
        uint32_t firstCommand = 0,
        const std::optional<rhi::PipelineHandle> pipeline); // Override PSO if set

    rhi::Resource& ArgsBuffer();
    uint64_t              GetCounterOffsetBytes() const;
    uint32_t              GetCapacity() const { return m_capacity; }
    IndirectKind          GetKind() const { return m_kind; }

private:
    uint64_t                             m_viewID = 0;
    IndirectKind                         m_kind = IndirectKind::DispatchMesh;
    uint32_t                             m_capacity = 0;
    uint32_t                             m_increment = 1024;
}