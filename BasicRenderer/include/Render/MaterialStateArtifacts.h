#pragma once

#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "Render/AsyncStateGraph.h"
#include "Materials/TechniqueDescriptor.h"
#include "Render/RasterBucketFlags.h"
#include "ShaderBuffers.h"

class MaterialManager;
class TextureAsset;
namespace org { class Resource; }

namespace br::render {

struct PublishedGpuBufferVersion;
struct MaterialRowInput {
    std::uint32_t materialID = 0, materialSlot = 0; std::uint64_t sourceRevision = 0;
    PerMaterialCB base{}; PerMaterialEvalCB evaluation{}; PerMaterialOpenPBRCB openPbr{};
};
struct MaterialRowArtifact {
    std::uint32_t materialID = 0, materialSlot = 0; std::uint64_t sourceRevision = 0;
    PerMaterialCB base{}; PerMaterialEvalCB evaluation{}; PerMaterialOpenPBRCB openPbr{};
};

inline constexpr std::uint64_t kMaterialBaseTableVariant = 1;
inline constexpr std::uint64_t kMaterialEvalTableVariant = 2;
inline constexpr std::uint64_t kMaterialOpenPbrTableVariant = 3;

struct MaterialCompileFlagEntryDTO {
    MaterialCompileFlags flags{};
    std::uint32_t slot = 0;
    auto operator<=>(const MaterialCompileFlagEntryDTO&) const = default;
};

struct MaterialTextureBindingDependencyDTO {
    std::uint32_t streamingTextureID = 0;
    std::uint64_t bindingRevision = 0;
    std::uint32_t imageDescriptorIndex = 0;
    std::uint32_t samplerDescriptorIndex = 0;
};

struct MaterialStateBuildInput {
    std::uint64_t sourceFingerprint = 0;
    std::uint64_t materialRowsRevision = 0;
    std::uint64_t materialRowCount = 0;
    std::uint32_t slotsUsed = 0;
    std::vector<MaterialCompileFlagEntryDTO> activeCompileFlags;
    // Dense shader-visible raster-bucket table. Index is the stable bucket ID;
    // unused/released entries retain MaterialRasterFlagsNone until a successor
    // publication assigns them again.
    std::vector<MaterialRasterFlags> rasterBucketFlags;
    ArtifactKey baseTableKey{ ArtifactKind::BufferVersion, 0, kMaterialBaseTableVariant };
    ArtifactKey evalTableKey{ ArtifactKind::BufferVersion, 0, kMaterialEvalTableVariant };
    ArtifactKey openPbrTableKey{ ArtifactKind::BufferVersion, 0, kMaterialOpenPbrTableVariant };
};

struct PublishedMaterialState {
    std::uint64_t sourceFingerprint = 0;
    std::uint32_t compileFlagSlotsUsed = 0;
    std::vector<MaterialCompileFlags> activeCompileFlags;
    std::vector<std::uint32_t> activeCompileFlagSlots;
    std::vector<MaterialRasterFlags> rasterBucketFlags;
    std::shared_ptr<const PublishedGpuBufferVersion> baseTable;
    std::shared_ptr<const PublishedGpuBufferVersion> evalTable;
    std::shared_ptr<const PublishedGpuBufferVersion> openPbrTable;

    [[nodiscard]] bool TryGetCompileFlagsSlot(
        MaterialCompileFlags flags, std::uint32_t& slot) const noexcept {
        for (std::size_t index = 0; index < activeCompileFlags.size(); ++index) {
            if (activeCompileFlags[index] == flags) {
                if (index >= activeCompileFlagSlots.size()) return false;
                slot = activeCompileFlagSlots[index];
                return true;
            }
        }
        return false;
    }
};

struct MaterialUsageBatchEntry {
    std::uint32_t materialID = 0;
    std::uint32_t count = 0;
    PerMaterialCB base{};
    PerMaterialEvalCB evaluation{};
    PerMaterialOpenPBRCB openPbr{};
    MaterialCompileFlags compileFlags{};
    std::vector<std::shared_ptr<TextureAsset>> textureServiceInputs;
    std::vector<std::shared_ptr<org::Resource>> retainedTextureResources;
    std::vector<MaterialTextureBindingDependencyDTO> textureBindings;
};

class MaterialUsageReservation {
public:
    using ResolveFn = std::function<bool(bool commit)>;
    explicit MaterialUsageReservation(ResolveFn resolve) : m_resolve(std::move(resolve)) {}
    ~MaterialUsageReservation() { (void)Resolve(false); }
    MaterialUsageReservation(const MaterialUsageReservation&) = delete;
    MaterialUsageReservation& operator=(const MaterialUsageReservation&) = delete;
    [[nodiscard]] bool Commit() const { return Resolve(true); }

private:
    bool Resolve(bool commit) const {
        std::scoped_lock lock(m_resolveMutex);
        if (m_resolved) return m_committed;
        m_resolved = true;
        m_committed = m_resolve ? m_resolve(commit) : !commit;
        return m_committed;
    }
    ResolveFn m_resolve;
    mutable std::mutex m_resolveMutex;
    mutable bool m_resolved = false;
    mutable bool m_committed = false;
};

struct MaterialUsageBatchBuildInput {
    std::uint64_t sourceFingerprint = 0;
    // Requests a refresh through the material storage's configured texture
    // service. Producer payloads never borrow the host's TextureFactory.
    bool refreshTextureBindings = false;
    std::vector<MaterialUsageBatchEntry> entries;
    std::shared_ptr<const MaterialUsageReservation> reservation;
};

struct PublishedMaterialUsageBatch {
    std::uint64_t sourceFingerprint = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> materialSlots;
};

// Serialized mutable storage boundary used while material rows and slot
// allocation are extracted from MaterialManager. Artifact producers depend on
// this narrow contract rather than on the renderer's broad manager facade.
class IMaterialStateStorage {
public:
    virtual ~IMaterialStateStorage() = default;
    virtual bool ApplyRow(const MaterialRowArtifact& row) = 0;
    virtual std::shared_ptr<const PublishedMaterialUsageBatch> ApplyUsageBatch(
        const MaterialUsageBatchBuildInput& input) = 0;
};

void RegisterMaterialStateProducer(AsyncStateGraph& graph);
// Reserved for the generation-safe Latest-successor cutover. Not registered by
// the renderer until internal rebuild generations stop consuming source revisions.
void RegisterMaterialRowProducer(AsyncStateGraph& graph, IMaterialStateStorage& storage);
void RegisterMaterialUsageBatchProducer(AsyncStateGraph& graph, IMaterialStateStorage& storage);

} // namespace br::render
