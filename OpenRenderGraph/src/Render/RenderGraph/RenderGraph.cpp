#include "Render/RenderGraph/RenderGraph.h"

#include <span>
#include <algorithm>
#include <limits>
#include <numeric>
#include <sstream>
#include <rhi_helpers.h>
#include <rhi_debug.h>

#include "Render/PassExecutionContext.h"
#include "Utilities/ORGUtilities.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Managers/Singletons/DeletionManager.h"
#include "Render/PassBuilders.h"
#include "Resources/ResourceGroup.h"
#include "Managers/CommandRecordingManager.h"
#include "Interfaces/IHasMemoryMetadata.h"
#include "Interfaces/IDynamicDeclaredResources.h"
#include "Resources/PixelBuffer.h"
#include "Resources/MemoryStatisticsComponents.h"

RenderGraph::PassView RenderGraph::GetPassView(AnyPassAndResources& pr) {
	PassView v{};
	if (pr.type == PassType::Compute) {
		auto& p = std::get<ComputePassAndResources>(pr.pass);
		v.reqs = &p.resources.frameResourceRequirements;
		v.internalTransitions = &p.resources.internalTransitions;
	}
	else {
		auto& p = std::get<RenderPassAndResources>(pr.pass);
		v.reqs = &p.resources.frameResourceRequirements;
		v.internalTransitions = &p.resources.internalTransitions;
	}
	return v;
}

std::vector<RenderGraph::Node> RenderGraph::BuildNodes(RenderGraph& rg, std::vector<AnyPassAndResources>& passes) {

	std::vector<Node> nodes;
	nodes.resize(passes.size());

	for (size_t i = 0; i < passes.size(); ++i) {
		Node n{};
		n.passIndex = i;
		n.isCompute = (passes[i].type == PassType::Compute);
		n.originalOrder = static_cast<uint32_t>(i);

		PassView view = GetPassView(passes[i]);

		std::unordered_set<uint64_t> touched;
		std::unordered_set<uint64_t> uavs;

		auto mark = [&](uint64_t rid, AccessKind k, bool isUav) {
			touched.insert(rid);
			if (isUav) uavs.insert(rid);

			auto it = n.accessByID.find(rid);
			if (it == n.accessByID.end()) {
				n.accessByID.emplace(rid, k);
			}
			else {
				// Write dominates
				if (k == AccessKind::Write) it->second = AccessKind::Write;
			}
			};

		// resource requirements
		for (auto& req : *view.reqs) {
			uint64_t base = req.resourceHandleAndRange.resource.GetGlobalResourceID();
			bool write = AccessTypeIsWriteType(req.state.access);
			bool isUav = IsUAVState(req.state);

			for (uint64_t rid : rg.m_aliasingSubsystem.GetSchedulingEquivalentIDs(base, rg.aliasPlacementRangesByID)) {
				mark(rid, write ? AccessKind::Write : AccessKind::Read, isUav);
			}
		}

		// internal transitions: treat as "write" for scheduling conservatism
		for (auto& tr : *view.internalTransitions) {
			uint64_t base = tr.first.resource.GetGlobalResourceID();
			for (uint64_t rid : rg.m_aliasingSubsystem.GetSchedulingEquivalentIDs(base, rg.aliasPlacementRangesByID)) {
				mark(rid, AccessKind::Write, /*isUav=*/false);
			}
		}

		n.touchedIDs.assign(touched.begin(), touched.end());
		n.uavIDs.assign(uavs.begin(), uavs.end());

		nodes[i] = std::move(n);
	}

	return nodes;
}

bool RenderGraph::AddEdgeDedup(
	size_t from, size_t to,
	std::vector<Node>& nodes,
	std::unordered_set<uint64_t>& edgeSet)
{
	if (from == to) return false;
	uint64_t key = (uint64_t(from) << 32) | uint64_t(to);
	if (!edgeSet.insert(key).second) return false;

	nodes[from].out.push_back(to);
	nodes[to].in.push_back(from);
	nodes[to].indegree++;
	return true;
}

bool RenderGraph::BuildDependencyGraph(
	std::vector<Node>& nodes)
{
	return BuildDependencyGraph(nodes, {});
}

bool RenderGraph::BuildDependencyGraph(
	std::vector<Node>& nodes,
	std::span<const std::pair<size_t, size_t>> explicitEdges)
{
	std::unordered_map<uint64_t, SeqState> seq;
	seq.reserve(4096);

	std::unordered_set<uint64_t> edgeSet;
	edgeSet.reserve(nodes.size() * 8);

	// build deps in ORIGINAL order
	for (size_t i = 0; i < nodes.size(); ++i) {
		auto& node = nodes[i];

		for (auto& [rid, kind] : node.accessByID) {
			auto& s = seq[rid];

			if (kind == AccessKind::Read) {
				if (s.lastWriter) AddEdgeDedup(*s.lastWriter, i, nodes, edgeSet);
				s.readsSinceWrite.push_back(i);
			}
			else { // Write
				if (s.lastWriter) AddEdgeDedup(*s.lastWriter, i, nodes, edgeSet);
				for (size_t r : s.readsSinceWrite)
					AddEdgeDedup(r, i, nodes, edgeSet);
				s.readsSinceWrite.clear();
				s.lastWriter = i;
			}
		}
	}

	// Apply explicit edges (e.g. "After(passName)")
	for (auto const& e : explicitEdges) {
		if (e.first >= nodes.size() || e.second >= nodes.size()) continue;
		AddEdgeDedup(e.first, e.second, nodes, edgeSet);
	}

	// topo + criticality (longest path)
	std::vector<uint32_t> indeg(nodes.size());
	for (size_t i = 0; i < nodes.size(); ++i) indeg[i] = nodes[i].indegree;

	std::vector<size_t> q;
	q.reserve(nodes.size());
	for (size_t i = 0; i < nodes.size(); ++i)
		if (indeg[i] == 0) q.push_back(i);

	std::vector<size_t> topo;
	topo.reserve(nodes.size());

	for (size_t head = 0; head < q.size(); ++head) {
		size_t u = q[head];
		topo.push_back(u);
		for (size_t v : nodes[u].out) {
			if (--indeg[v] == 0) q.push_back(v);
		}
	}

	if (topo.size() != nodes.size()) {
		// cycle: invalid graph
		return false;
	}

	// reverse topo DP
	for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
		size_t u = *it;
		uint32_t best = 0;
		for (size_t v : nodes[u].out)
			best = std::max(best, uint32_t(1 + nodes[v].criticality));
		nodes[u].criticality = best;
	}

	return true;
}

void RenderGraph::CommitPassToBatch(
	RenderGraph& rg,
	AnyPassAndResources& pr,
	const Node& node,

	unsigned int currentBatchIndex,
	PassBatch& currentBatch,

	std::unordered_set<uint64_t>& computeUAVs,
	std::unordered_set<uint64_t>& renderUAVs,

	std::unordered_map<uint64_t, unsigned int>& batchOfLastRenderQueueTransition,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastComputeQueueTransition,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastRenderQueueProducer,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastComputeQueueProducer,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastRenderQueueUsage,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastComputeQueueUsage)
{
	bool isCompute = (pr.type == PassType::Compute);
	std::unordered_set<uint64_t> resourcesTransitionedThisPass;

	if (isCompute) {
		auto& pass = std::get<ComputePassAndResources>(pr.pass);

		rg.ProcessResourceRequirements(
			/*isCompute=*/true,
			pass.resources.frameResourceRequirements,
			batchOfLastRenderQueueUsage,
			batchOfLastComputeQueueTransition,
			currentBatchIndex,
			currentBatch,
			resourcesTransitionedThisPass);

		currentBatch.computePasses.push_back(pass);

		for (auto& exit : pass.resources.internalTransitions) {
			std::vector<ResourceTransition> _;
			auto pRes = _registry.Resolve(exit.first.resource);
			auto& compileTracker = GetOrCreateCompileTracker(pRes, exit.first.resource.GetGlobalResourceID());
			compileTracker.Apply(
				exit.first.range, nullptr, exit.second, _); // TODO: Do we really need the ptr?
			currentBatch.internallyTransitionedResources.insert(exit.first.resource.GetGlobalResourceID());
		}

		for (auto& req : pass.resources.frameResourceRequirements) {
			uint64_t id = req.resourceHandleAndRange.resource.GetGlobalResourceID();
			currentBatch.allResources.insert(id);
			batchOfLastComputeQueueUsage[id] = currentBatchIndex;
		}

		// NEW: track UAV usage for cross-queue "same batch" rejection
		computeUAVs.insert(node.uavIDs.begin(), node.uavIDs.end());

		rg.applySynchronization(
			/*isComputePass=*/true,
			currentBatch,
			currentBatchIndex,
			std::get<ComputePassAndResources>(pr.pass),
			batchOfLastRenderQueueTransition,
			batchOfLastRenderQueueProducer,
			batchOfLastRenderQueueUsage,
			resourcesTransitionedThisPass);

	}
	else {
		auto& pass = std::get<RenderPassAndResources>(pr.pass);

		rg.ProcessResourceRequirements(
			/*isCompute=*/false,
			pass.resources.frameResourceRequirements,
			batchOfLastRenderQueueUsage,
			batchOfLastRenderQueueTransition,
			currentBatchIndex,
			currentBatch,
			resourcesTransitionedThisPass);

		currentBatch.renderPasses.push_back(pass);

		for (auto& exit : pass.resources.internalTransitions) {
			std::vector<ResourceTransition> _;
			auto pRes = _registry.Resolve(exit.first.resource);
			auto& compileTracker = GetOrCreateCompileTracker(pRes, exit.first.resource.GetGlobalResourceID());
			compileTracker.Apply(
				exit.first.range, nullptr, exit.second, _);
			currentBatch.internallyTransitionedResources.insert(exit.first.resource.GetGlobalResourceID());
		}

		for (auto& req : pass.resources.frameResourceRequirements) {
			uint64_t id = req.resourceHandleAndRange.resource.GetGlobalResourceID();
			currentBatch.allResources.insert(id);
			batchOfLastRenderQueueUsage[id] = currentBatchIndex;
		}

		renderUAVs.insert(node.uavIDs.begin(), node.uavIDs.end());

		rg.applySynchronization(
			/*isComputePass=*/false,
			currentBatch,
			currentBatchIndex,
			std::get<RenderPassAndResources>(pr.pass),
			batchOfLastComputeQueueTransition,
			batchOfLastComputeQueueProducer,
			batchOfLastComputeQueueUsage,
			resourcesTransitionedThisPass);
	}
}

void RenderGraph::AutoScheduleAndBuildBatches(
	RenderGraph& rg,
	std::vector<AnyPassAndResources>& passes,
	std::vector<Node>& nodes)
{
	std::vector<int32_t> rejectedInBatch(nodes.size(), -1);

	// Working indegrees
	std::vector<uint32_t> indeg(nodes.size());
	for (size_t i = 0; i < nodes.size(); ++i) indeg[i] = nodes[i].indegree;

	std::vector<size_t> ready;
	ready.reserve(nodes.size());
	for (size_t i = 0; i < nodes.size(); ++i)
		if (indeg[i] == 0) ready.push_back(i);

	std::vector<uint8_t> inBatch(nodes.size(), 0);
	std::vector<size_t>  batchMembers;
	batchMembers.reserve(nodes.size());

	auto openNewBatch = [&]() -> PassBatch {
		PassBatch b;
		b.renderTransitionFenceValue = rg.GetNextGraphicsQueueFenceValue();
		b.renderCompletionFenceValue = rg.GetNextGraphicsQueueFenceValue();
		b.computeTransitionFenceValue = rg.GetNextComputeQueueFenceValue();
		b.computeCompletionFenceValue = rg.GetNextComputeQueueFenceValue();
		return b;
		};

	PassBatch currentBatch = openNewBatch();
	unsigned int currentBatchIndex = 1; // Start at batch 1- batch 0 is reserved for inserting transitions before first batch

	std::unordered_set<uint64_t> computeUAVs;
	std::unordered_set<uint64_t> renderUAVs;

	std::unordered_map<uint64_t, unsigned int> batchOfLastRenderQueueTransition;
	std::unordered_map<uint64_t, unsigned int> batchOfLastComputeQueueTransition;
	std::unordered_map<uint64_t, unsigned int> batchOfLastRenderQueueProducer;
	std::unordered_map<uint64_t, unsigned int> batchOfLastComputeQueueProducer;
	std::unordered_map<uint64_t, unsigned int> batchOfLastRenderQueueUsage;
	std::unordered_map<uint64_t, unsigned int> batchOfLastComputeQueueUsage;

	auto closeBatch = [&]() {
		// clear inBatch marks for members
		for (size_t p : batchMembers) inBatch[p] = 0;
		batchMembers.clear();

		rg.batches.push_back(std::move(currentBatch));
		currentBatch = openNewBatch();
		computeUAVs.clear();
		renderUAVs.clear();
		++currentBatchIndex;
		};

	size_t remaining = nodes.size();

	while (remaining > 0) {
		// Collect "fits" and pick best by heuristic
		int bestIdxInReady = -1;
		double bestScore = -1e300;

		bool batchHasCompute = !currentBatch.computePasses.empty();
		bool batchHasRender = !currentBatch.renderPasses.empty();

		for (int ri = 0; ri < (int)ready.size(); ++ri) {
			size_t ni = ready[ri];

			if (rejectedInBatch[ni] == static_cast<int32_t>(currentBatchIndex)) {
				continue;
			}

			auto& n = nodes[ni];

			PassView view = GetPassView(passes[n.passIndex]);

			// Extra constraint: disallow Render->Compute deps within same batch
			if (n.isCompute && batchHasRender) {
				bool hasRenderPredInBatch = false;
				for (size_t pred : n.in) {
					if (inBatch[pred] && !nodes[pred].isCompute) {
						hasRenderPredInBatch = true;
						break;
					}
				}
				if (hasRenderPredInBatch) continue;
			}

			const auto& otherUAVs = n.isCompute ? renderUAVs : computeUAVs;

			if (rg.IsNewBatchNeeded(
				*view.reqs,
				*view.internalTransitions,
				currentBatch.passBatchTrackers,
				currentBatch.internallyTransitionedResources,
				currentBatch.allResources,
				otherUAVs))
			{
				rejectedInBatch[ni] = static_cast<int32_t>(currentBatchIndex);
				continue;
			}

			// Score: pack by reusing resources already in batch, and encourage overlap
			int reuse = 0, fresh = 0;
			for (uint64_t rid : n.touchedIDs) {
				if (currentBatch.allResources.contains(rid)) ++reuse;
				else ++fresh;
			}

			double score = 3.0 * reuse - 1.0 * fresh;

			// Encourage having both queues represented (more overlap opportunity)
			if (n.isCompute && !batchHasCompute) score += 2.0;
			if (!n.isCompute && !batchHasRender) score += 2.0;

			// Tie-break
			score += 0.05 * double(n.criticality);

			// Deterministic tie-break: prefer earlier original order slightly
			score += 1e-6 * double(nodes.size() - n.originalOrder);

			if (score > bestScore) {
				bestScore = score;
				bestIdxInReady = ri;
			}
		}

		if (bestIdxInReady < 0) {
			// Nothing ready fits: must end batch
			if (!currentBatch.computePasses.empty() || !currentBatch.renderPasses.empty()) {
				closeBatch();
				continue;
			}
			else {
				// Should be rare; fall back by forcing one ready pass in.
				// If this happens, IsNewBatchNeeded is likely too strict on empty batch.
				size_t ni = ready.front();
				auto& n = nodes[ni];
				CommitPassToBatch(
					rg, passes[n.passIndex], n,
					currentBatchIndex, currentBatch,
					computeUAVs, renderUAVs,
					batchOfLastRenderQueueTransition,
					batchOfLastComputeQueueTransition,
					batchOfLastRenderQueueProducer,
					batchOfLastComputeQueueProducer,
					batchOfLastRenderQueueUsage,
					batchOfLastComputeQueueUsage);

				inBatch[ni] = 1;
				batchMembers.push_back(ni);

				// Pop from ready
				ready[0] = ready.back();
				ready.pop_back();

				for (size_t v : nodes[ni].out) {
					if (--indeg[v] == 0) ready.push_back(v);
				}
				--remaining;
				continue;
			}
		}

		// Commit chosen pass
		size_t chosenNodeIndex = ready[bestIdxInReady];
		auto& chosen = nodes[chosenNodeIndex];

		CommitPassToBatch(
			rg, passes[chosen.passIndex], chosen,
			currentBatchIndex, currentBatch,
			computeUAVs, renderUAVs,
			batchOfLastRenderQueueTransition,
			batchOfLastComputeQueueTransition,
			batchOfLastRenderQueueProducer,
			batchOfLastComputeQueueProducer,
			batchOfLastRenderQueueUsage,
			batchOfLastComputeQueueUsage);

		inBatch[chosenNodeIndex] = 1;
		batchMembers.push_back(chosenNodeIndex);

		// Remove from ready
		ready[bestIdxInReady] = ready.back();
		ready.pop_back();

		// Release successors
		for (size_t v : chosen.out) {
			if (--indeg[v] == 0) ready.push_back(v);
		}

		--remaining;
	}

	// Final batch
	if (!currentBatch.computePasses.empty() || !currentBatch.renderPasses.empty()) {
		rg.batches.push_back(std::move(currentBatch));
	}

	rg.m_compiledLastRenderProducerBatchByResource = std::move(batchOfLastRenderQueueProducer);
	rg.m_compiledLastComputeProducerBatchByResource = std::move(batchOfLastComputeQueueProducer);
}


// Factory for the transition lambda
void RenderGraph::AddTransition(
	std::unordered_map<uint64_t, unsigned int>& batchOfLastRenderQueueUsage,
	unsigned int batchIndex,
	PassBatch& currentBatch,
	bool isComputePass,
	const ResourceRequirement& r,
	std::unordered_set<uint64_t>& outTransitionedResourceIDs)
{

	auto& resource = r.resourceHandleAndRange.resource;

	// If this triggers, you're probably queueing an operation on an external/ephemeral resource, and then discarding it before the graph can use it.
	if (!resource.IsEphemeral() && !_registry.IsValid(resource)) {
		spdlog::error("Invalid resource handle");
		throw (std::runtime_error("Invalid resource handle in RenderGraph::AddTransition"));
	}
	std::vector<ResourceTransition> transitions;
	auto pRes = _registry.Resolve(resource); // TODO: Can we get rid of pRes in transitions?
	auto& compileTracker = GetOrCreateCompileTracker(pRes, resource.GetGlobalResourceID());

	if (aliasActivationPending.find(resource.GetGlobalResourceID()) != aliasActivationPending.end()) {
		const bool firstUseIsWrite = AccessTypeIsWriteType(r.state.access);
		const bool firstUseIsCommon = r.state.access == rhi::ResourceAccessType::Common;
		// Common counts as write for alias activation, as this is generally used to indicate that the resource will be
		// transitioned internally by an external system that still uses legacy barriers. Don't abuse this.
		if (firstUseIsWrite || firstUseIsCommon) { 
			const uint64_t id = resource.GetGlobalResourceID();
			auto itSig = aliasPlacementSignatureByID.find(id);
			spdlog::info(
				"RG alias activate: id={} name='{}' signature={} accessAfter={} layoutAfter={} syncAfter={} discard=1",
				id,
				pRes ? pRes->GetName() : std::string("<null>"),
				itSig != aliasPlacementSignatureByID.end() ? itSig->second : 0ull,
				static_cast<uint32_t>(r.state.access),
				static_cast<uint32_t>(r.state.layout),
				static_cast<uint32_t>(r.state.sync));
			transitions.emplace_back(
				pRes,
				r.resourceHandleAndRange.range,
				rhi::ResourceAccessType::None,
				r.state.access,
				rhi::ResourceLayout::Undefined,
				r.state.layout,
				rhi::ResourceSyncState::None,
				r.state.sync,
				true);
		}
		else {
			throw std::runtime_error("Alias activation requires first use to be a write when explicit initialization is disabled");
		}
		std::vector<ResourceTransition> ignored;
		compileTracker.Apply(r.resourceHandleAndRange.range, pRes, r.state, ignored);
		aliasActivationPending.erase(resource.GetGlobalResourceID());
	}
	else {
		compileTracker.Apply(r.resourceHandleAndRange.range, pRes, r.state, transitions);
	}

	if (!transitions.empty()) {
		outTransitionedResourceIDs.insert(resource.GetGlobalResourceID());
	}

	currentBatch.passBatchTrackers[resource.GetGlobalResourceID()] = &compileTracker; // We will need to check subsequent passes against this

	bool oldSyncHasNonComputeSyncState = false;
	for (auto& transition : transitions) {
		if (ResourceSyncStateIsNotComputeSyncState(transition.prevSyncState)) {
			oldSyncHasNonComputeSyncState = true;
		}
	}
	if (isComputePass && oldSyncHasNonComputeSyncState) { // We need to place transitions on render queue
		for (auto& transition : transitions) {
			// Resource groups will pass through their child ptrs in the transition
			const auto id = transition.pResource ? transition.pResource->GetGlobalResourceID() : resource.GetGlobalResourceID();
			unsigned int gfxBatch = batchOfLastRenderQueueUsage[id];
			batchOfLastRenderQueueUsage[id] = gfxBatch; // Can this cause transition overlaps?
			batches[gfxBatch].batchEndTransitions.push_back(transition);
		}
	}
	else {
		if (isComputePass) {
			for (auto& transition : transitions) {
				//context.transHistCompute[transition.pResource->GetGlobalResourceID()] = batchIndex;
				currentBatch.computeTransitions.push_back(transition);
			}
		}
		else {
			for (auto& transition : transitions) {
				//context.transHistRender[transition.pResource->GetGlobalResourceID()] = batchIndex;
				currentBatch.renderTransitions.push_back(transition);
			}
		}
	}
}

void RenderGraph::ProcessResourceRequirements(
	bool isCompute,
	std::vector<ResourceRequirement>& resourceRequirements,
	std::unordered_map<uint64_t, unsigned int>& batchOfLastRenderQueueUsage,
	std::unordered_map<uint64_t, unsigned int>& producerHistory,
	unsigned int batchIndex,
	PassBatch& currentBatch, std::unordered_set<uint64_t>& outTransitionedResourceIDs) {

	for (auto& resourceRequirement : resourceRequirements) {

		//if (!resourcesByID.contains(resourceRequirement.resourceHandleAndRange.resource.GetGlobalResourceID())) {
		//	spdlog::error("Resource referenced by pass is not managed by this graph");
		//	throw(std::runtime_error("Resource referenced is not managed by this graph"));
		//}

		const auto& id = resourceRequirement.resourceHandleAndRange.resource.GetGlobalResourceID();

		AddTransition(batchOfLastRenderQueueUsage, batchIndex, currentBatch, isCompute, resourceRequirement, outTransitionedResourceIDs);

		if (AccessTypeIsWriteType(resourceRequirement.state.access)) {
			producerHistory[id] = batchIndex;
		}
	}
}

bool ResolveFirstMipSlice(ResourceRegistry::RegistryHandle r, RangeSpec range, uint32_t& outMip, uint32_t& outSlice) noexcept
{
	const uint32_t totalMips = r.GetNumMipLevels();
	const uint32_t totalSlices = r.GetArraySize();
	if (totalMips == 0 || totalSlices == 0) return false;

	SubresourceRange sr = ResolveRangeSpec(range, totalMips, totalSlices);
	if (sr.isEmpty()) return false;

	outMip = sr.firstMip;
	outSlice = sr.firstSlice;
	return true;
}

RenderGraph::RenderGraph(rhi::Device device) {
	DeviceManager::GetInstance().Initialize(device);

	auto MakeDefaultImmediateDispatch = [&]() noexcept -> rg::imm::ImmediateDispatch
		{
			rg::imm::ImmediateDispatch d{};
			d.user = this;

			d.GetResourceHandle = [](RenderGraph* user, ResourceRegistry::RegistryHandle r) noexcept -> rhi::ResourceHandle {
				Resource* ptr;
				if (r.IsEphemeral()) {
					ptr = r.GetEphemeralPtr();
				}
				else {
					ptr = user->_registry.Resolve(r);
				}
				return ptr ? ptr->GetAPIResource().GetHandle() : rhi::ResourceHandle{};
				};

			d.GetRTV = +[](RenderGraph* user, ResourceRegistry::RegistryHandle r, RangeSpec range) noexcept -> rhi::DescriptorSlot {
				auto* gir = dynamic_cast<GloballyIndexedResource*>(user->_registry.Resolve(r));
				if (!gir || !gir->HasRTV()) return {};

				uint32_t mip = 0, slice = 0;
				if (!ResolveFirstMipSlice(r, range, mip, slice)) return {};

				return gir->GetRTVInfo(mip, slice).slot;
				};

			d.GetDSV = +[](RenderGraph* user, ResourceRegistry::RegistryHandle r, RangeSpec range) noexcept -> rhi::DescriptorSlot {
				auto* gir = dynamic_cast<GloballyIndexedResource*>(user->_registry.Resolve(r));
				if (!gir || !gir->HasDSV()) return {};

				uint32_t mip = 0, slice = 0;
				if (!ResolveFirstMipSlice(r, range, mip, slice)) return {};

				return gir->GetDSVInfo(mip, slice).slot;
				};

			d.GetUavClearInfo = +[](RenderGraph* user, ResourceRegistry::RegistryHandle r, RangeSpec range, rhi::UavClearInfo& out) noexcept -> bool {
				auto* gir = dynamic_cast<GloballyIndexedResource*>(user->_registry.Resolve(r));

				// DX12 path requires both a shader-visible and CPU-visible UAV descriptor.
				if (!gir || !gir->HasUAVShaderVisible() || !gir->HasUAVNonShaderVisible()) return false;

				uint32_t mip = 0, slice = 0;
				if (!ResolveFirstMipSlice(r, range, mip, slice)) return false;

				out.shaderVisible = gir->GetUAVShaderVisibleInfo(mip, slice).slot;
				out.cpuVisible = gir->GetUAVNonShaderVisibleInfo(mip, slice).slot;

				out.resource = gir->GetAPIResource();

				return true;
				};

			return d;
		};

	m_immediateDispatch = MakeDefaultImmediateDispatch();
	if (!m_statisticsService) {
		m_statisticsService = rg::runtime::CreateDefaultStatisticsService();
	}
	if (!m_uploadService) {
		m_uploadService = rg::runtime::CreateDefaultUploadService();
	}
	if (!m_readbackService) {
		m_readbackService = rg::runtime::CreateDefaultReadbackService();
	}
	if (!m_descriptorService) {
		m_descriptorService = rg::runtime::CreateDefaultDescriptorService();
	}
	if (!m_renderGraphSettingsService) {
		m_renderGraphSettingsService = rg::runtime::CreateDefaultRenderGraphSettingsService();
	}
}

RenderGraph::~RenderGraph() {
	m_pCommandRecordingManager->ShutdownThreadLocal(); // Clears thread-local storage
	DeletionManager::GetInstance().Cleanup();
	DeviceManager::GetInstance().Cleanup();
}

SymbolicTracker& RenderGraph::GetOrCreateCompileTracker(Resource* resource, uint64_t resourceID) {
	auto it = compileTrackers.find(resourceID);
	if (it != compileTrackers.end()) {
		return it->second;
	}

	SymbolicTracker seed;
	if (resource) {
		bool hasLiveBacking = true;
		if (auto* texture = dynamic_cast<PixelBuffer*>(resource)) {
			hasLiveBacking = texture->IsMaterialized();
		}
		else if (auto* buffer = dynamic_cast<Buffer*>(resource)) {
			hasLiveBacking = buffer->IsMaterialized();
		}

		if (hasLiveBacking) {
			seed = *resource->GetStateTracker();
		}
	}

	auto [insertedIt, _] = compileTrackers.emplace(resourceID, std::move(seed));
	return insertedIt->second;
}

void RenderGraph::MaterializeReferencedResources(
	const std::vector<ResourceRequirement>& resourceRequirements,
	const std::vector<std::pair<ResourceHandleAndRange, ResourceState>>& internalTransitions)
{
	auto materializeIfNeeded = [&](const ResourceRegistry::RegistryHandle& handle) {
		if (handle.IsEphemeral()) {
			return;
		}

		auto resource = _registry.Resolve(handle);
		if (!resource) {
			return;
		}

		auto texture = dynamic_cast<PixelBuffer*>(resource);
		if (texture) {
			if (texture->IsMaterialized()) {
				return;
			}

			if (texture->GetDescription().allowAlias) {
				// Alias placement is frame-dependent and produced later in CompileFrame.
				// Defer materialization until RenderGraph::MaterializeUnmaterializedResources,
				// after BuildAliasPlanAfterDag has produced placement options.
				return;
			}

			texture->Materialize();
			resourceBackingGenerationByID[handle.GetGlobalResourceID()] = texture->GetBackingGeneration();
			return;
		}

		auto buffer = dynamic_cast<Buffer*>(resource);
		if (buffer) {
			if (buffer->IsMaterialized()) {
				return;
			}

			if (buffer->IsAliasingAllowed()) {
				return;
			}

			buffer->Materialize();
			resourceBackingGenerationByID[handle.GetGlobalResourceID()] = buffer->GetBackingGeneration();
		}
	};

	for (auto const& req : resourceRequirements) {
		materializeIfNeeded(req.resourceHandleAndRange.resource);
	}

	for (auto const& transition : internalTransitions) {
		materializeIfNeeded(transition.first.resource);
	}
}

std::unordered_set<uint64_t> RenderGraph::CollectFrameResourceIDs() const {
	std::unordered_set<uint64_t> used;
	used.reserve(m_framePasses.size() * 4);

	for (auto const& pr : m_framePasses) {
		if (pr.type == PassType::Compute) {
			auto const& p = std::get<ComputePassAndResources>(pr.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				used.insert(req.resourceHandleAndRange.resource.GetGlobalResourceID());
			}
			for (auto const& t : p.resources.internalTransitions) {
				used.insert(t.first.resource.GetGlobalResourceID());
			}
		}
		else if (pr.type == PassType::Render) {
			auto const& p = std::get<RenderPassAndResources>(pr.pass);
			for (auto const& req : p.resources.frameResourceRequirements) {
				used.insert(req.resourceHandleAndRange.resource.GetGlobalResourceID());
			}
			for (auto const& t : p.resources.internalTransitions) {
				used.insert(t.first.resource.GetGlobalResourceID());
			}
		}
	}

	return used;
}

void RenderGraph::ApplyIdleDematerializationPolicy(const std::unordered_set<uint64_t>& usedResourceIDs) {
	for (auto& [id, resource] : resourcesByID) {
		if (!resource) {
			continue;
		}

		auto texture = std::dynamic_pointer_cast<PixelBuffer>(resource);
		if (!texture || !texture->IsIdleDematerializationEnabled()) {
			continue;
		}

		if (usedResourceIDs.find(id) != usedResourceIDs.end()) {
			resourceIdleFrameCounts[id] = 0;
			continue;
		}

		auto& idleFrames = resourceIdleFrameCounts[id];
		idleFrames++;

		if (texture->IsMaterialized() && idleFrames >= texture->GetIdleDematerializationThreshold()) {
			texture->Dematerialize();
			resourceBackingGenerationByID[id] = texture->GetBackingGeneration();
		}
	}
}

void RenderGraph::SnapshotCompiledResourceGenerations(const std::unordered_set<uint64_t>& usedResourceIDs) {
	compiledResourceGenerationByID.clear();
	compiledResourceGenerationByID.reserve(usedResourceIDs.size());

	for (uint64_t id : usedResourceIDs) {
		auto it = resourcesByID.find(id);
		if (it == resourcesByID.end() || !it->second) {
			continue;
		}

		auto texture = std::dynamic_pointer_cast<PixelBuffer>(it->second);
		if (texture) {
			compiledResourceGenerationByID[id] = texture->GetBackingGeneration();
			continue;
		}

		auto buffer = std::dynamic_pointer_cast<Buffer>(it->second);
		if (buffer) {
			compiledResourceGenerationByID[id] = buffer->GetBackingGeneration();
		}
	}
}

void RenderGraph::ValidateCompiledResourceGenerations() const {
	for (auto const& [id, compiledGeneration] : compiledResourceGenerationByID) {
		auto it = resourcesByID.find(id);
		if (it == resourcesByID.end() || !it->second) {
			continue;
		}

		auto texture = std::dynamic_pointer_cast<PixelBuffer>(it->second);
		if (texture) {
			const uint64_t currentGeneration = texture->GetBackingGeneration();
			if (currentGeneration != compiledGeneration) {
				throw std::runtime_error("Resource backing generation changed after compile and before execute. Resource ID: " + std::to_string(id));
			}
			continue;
		}

		auto buffer = std::dynamic_pointer_cast<Buffer>(it->second);
		if (buffer) {
			const uint64_t currentGeneration = buffer->GetBackingGeneration();
			if (currentGeneration != compiledGeneration) {
				throw std::runtime_error("Resource backing generation changed after compile and before execute. Resource ID: " + std::to_string(id));
			}
		}
	}
}


void RenderGraph::RegisterExtension(std::unique_ptr<IRenderGraphExtension> ext) {
	if (!ext) return;

	const auto& incomingType = typeid(*ext);
	for (const auto& existing : m_extensions) {
		if (existing && typeid(*existing) == incomingType) {
			spdlog::error("Duplicate RenderGraph extension registration: {}", incomingType.name());
			throw std::runtime_error("Duplicate RenderGraph extension registration");
		}
	}

	// Let the extension see the current registry immediately.
	ext->OnRegistryReset(&_registry);
	m_extensions.push_back(std::move(ext));
}

void RenderGraph::ResetForRebuild()
{

	//std::vector<IResourceProvider*> _providers;
	//ResourceRegistry _registry;
	//std::unordered_map<ResourceIdentifier, IResourceProvider*, ResourceIdentifier::Hasher> _providerMap;

	//std::vector<IPassBuilder*> m_passBuilderOrder;
	//std::unordered_map<std::string, std::unique_ptr<IPassBuilder>> m_passBuildersByName;
	//std::unordered_set<std::string> m_passNamesSeenThisReset;

	//std::vector<AnyPassAndResources> passes;
	//std::unordered_map<std::string, std::shared_ptr<RenderPass>> renderPassesByName;
	//std::unordered_map<std::string, std::shared_ptr<ComputePass>> computePassesByName;
	//std::unordered_map<std::string, std::shared_ptr<Resource>> resourcesByName;
	//std::unordered_map<uint64_t, std::shared_ptr<Resource>> resourcesByID;
	//std::unordered_map<uint64_t, uint64_t> independantlyManagedResourceToGroup;
	//std::vector<std::shared_ptr<ResourceGroup>> resourceGroups;

	//std::unordered_map<uint64_t, std::unordered_set<uint64_t>> aliasedResources; // Tracks resources that use the same memory
	//std::unordered_map<uint64_t, size_t> resourceToAliasGroup;
	//std::vector<std::vector<uint64_t>>   aliasGroups;
	//std::vector<std::unordered_map<UINT, uint64_t>> lastActiveSubresourceInAliasGroup;

	//// Sometimes, we have a resource group that has children that are also managed independently by this graph. If so, we need to handle their transitions separately
	//std::unordered_map<uint64_t, std::vector<uint64_t>> resourcesFromGroupToManageIndependantly;

	//std::unordered_map<uint64_t, ResourceTransition> initialTransitions; // Transitions needed to reach the initial state of the resources before executing the first batch. Executed on graph setup.
	//std::vector<PassBatch> batches;
	//std::unordered_map<uint64_t, SymbolicTracker*> trackers; // Tracks the state of resources in the graph.

	// Clear any existing compile state
	m_masterPassList.clear();
	batches.clear();
	trackers.clear();

	// Clear resources
	resourcesByID.clear();
	resourcesByName.clear();
	resourceBackingGenerationByID.clear();
	resourceIdleFrameCounts.clear();
	compiledResourceGenerationByID.clear();
	m_aliasingSubsystem.ResetPersistentState(*this);
	compileTrackers.clear();
	m_lastProducerByResourceAcrossFrames.clear();
	m_lastAliasPlacementProducersByPoolAcrossFrames.clear();
	m_compiledLastRenderProducerBatchByResource.clear();
	m_compiledLastComputeProducerBatchByResource.clear();
	m_hasPendingFrameStartComputeWaitOnRender = false;
	m_pendingFrameStartComputeWaitOnRenderFenceValue = 0;
	m_hasPendingFrameStartRenderWaitOnCompute = false;
	m_pendingFrameStartRenderWaitOnComputeFenceValue = 0;

	// Clear providers
	_providerMap.clear();
	_providers.clear();
	_resolverMap.clear();
	_registry = ResourceRegistry();

	// Notify extensions that the registry was replaced
	for (auto& ext : m_extensions) {
		if (ext) ext->OnRegistryReset(&_registry);
	}
	// clear pass ordering
	m_passBuilderOrder.clear();
	m_passNamesSeenThisReset.clear();

}

void RenderGraph::ResetForFrame() {
	batches.clear();
	compiledResourceGenerationByID.clear();
	m_aliasingSubsystem.ResetPerFrameState(*this);
	compileTrackers.clear();
	m_compiledLastRenderProducerBatchByResource.clear();
	m_compiledLastComputeProducerBatchByResource.clear();
	m_hasPendingFrameStartComputeWaitOnRender = false;
	m_pendingFrameStartComputeWaitOnRenderFenceValue = 0;
	m_hasPendingFrameStartRenderWaitOnCompute = false;
	m_pendingFrameStartRenderWaitOnComputeFenceValue = 0;
	// reset pass builders
	for (auto& [name, builder] : m_passBuildersByName) {
		builder->Reset();
	}
}

void RenderGraph::CompileStructural() {
	// Register resource providers from pass builders

	std::vector<unsigned int> empty;
	// Go backwards to build skip list
	for (int i = static_cast<int>(m_passBuilderOrder.size()) - 1; i >= 0; i--) {
		auto ptr = m_passBuilderOrder[i];
		auto prov = ptr->ResourceProvider();
		if (!prov) {
			empty.push_back(i); // This pass was not built
			continue;
		}
		RegisterProvider(prov);
	}
	unsigned int i = 0;
	for (auto ptr : m_passBuilderOrder) {
		if (!empty.empty() && empty.back() == i) {
			empty.pop_back();
			continue;
		}
		ptr->Finalize();
		i++;
	}

	batches.clear();

	struct Pending {
		AnyPassAndResources pr;

		std::string anchorKey;     // internal unique key used for anchoring/emission

		ExternalInsertPoint where; // concrete
		bool chained = false;      // true if where was synthesized from "natural ordering"
		int chainOrder = 0;        // preserves extension-local order among chained followers

		int priority = 0;
		size_t order = 0;          // global stable order across all gathered externals
	};

	auto makeAny = [&](ExternalPassDesc const& d) -> AnyPassAndResources {
		AnyPassAndResources any;
		any.type = d.type;
		any.name = d.name;

		if (d.type == PassType::Render) {
			auto rp = std::get<std::shared_ptr<RenderPass>>(d.pass);
			RenderPassAndResources par;
			par.pass = std::move(rp);
			par.name = d.name;
			{
				RenderPassBuilder b(this, d.name);
				b.pass = par.pass;
				b.built_ = true;
				b.params = {};
				b.params.isGeometryPass = d.isGeometryPass;
				b._declaredIds.clear();
				par.pass->DeclareResourceUsages(&b);
				par.resources.staticResourceRequirements = b.GatherResourceRequirements();
				par.resources.frameResourceRequirements = par.resources.staticResourceRequirements;
				par.resources.internalTransitions = b.params.internalTransitions;
				par.resources.identifierSet = b.DeclaredResourceIds();
				par.resources.isGeometryPass = b.params.isGeometryPass;
				MaterializeReferencedResources(par.resources.staticResourceRequirements, par.resources.internalTransitions);
			}
 			any.pass = std::move(par);
 		}
 		else if (d.type == PassType::Compute) {
 			auto cp = std::get<std::shared_ptr<ComputePass>>(d.pass);
 			ComputePassAndResources par;
 			par.pass = std::move(cp);
 			par.name = d.name;
			{
				ComputePassBuilder b(this, d.name);
				b.pass = par.pass;
				b.built_ = true;
				b.params = {};
				b._declaredIds.clear();
				par.pass->DeclareResourceUsages(&b);
				par.resources.staticResourceRequirements = b.GatherResourceRequirements();
				par.resources.frameResourceRequirements = par.resources.staticResourceRequirements;
				par.resources.internalTransitions = b.params.internalTransitions;
				par.resources.identifierSet = b.DeclaredResourceIds();
				MaterializeReferencedResources(par.resources.staticResourceRequirements, par.resources.internalTransitions);
			}
 			any.pass = std::move(par);
 		}
 		return any;
 		};

	auto registerName = [&](ExternalPassDesc const& d, AnyPassAndResources& any) {
		if (!d.registerName) return;
		if (d.type == PassType::Render) {
			auto& rp = std::get<RenderPassAndResources>(any.pass);
			if (!d.name.empty()) renderPassesByName[d.name] = rp.pass;
		}
		else if (d.type == PassType::Compute) {
			auto& cp = std::get<ComputePassAndResources>(any.pass);
			if (!d.name.empty()) computePassesByName[d.name] = cp.pass;
		}
		};

	// Sentinels (must not collide with real pass names)
	static constexpr const char* kBeginKey = "__rg_begin__";
	static constexpr const char* kAfterBaseKey = "__rg_after_base__";
	static constexpr const char* kEndKey = "__rg_end__"; // legacy alias for end-of-base anchor
	static constexpr const char* kFirstBaseKey = "__rg_first_base__"; // optional helper token

	struct ExtItem {
		AnyPassAndResources pr;
		std::string key;                 // unique key for anchoring
		ExternalInsertPoint where;        // constraints
		int priority = 0;
		size_t order = 0;                // global stable order
		int extIndex = 0;                // which extension emitted it
		int extLocalOrder = 0;           // order within that extension
	};

	struct MergeNode {
		std::string key;
		bool hasPass = false;            // sentinel nodes have no pass payload
		AnyPassAndResources pass{};      // valid iff hasPass
		int priority = 0;
		size_t order = 0;
		std::vector<size_t> out;
		uint32_t indeg = 0;
	};

	// Keep base passes
	auto base = std::move(m_masterPassList);
	m_masterPassList.clear();

	// Gather extension passes into ExtItem list
	std::vector<ExtItem> extItems;
	extItems.reserve(64);

	size_t globalOrder = 0;

	// helper: stable synthetic key for unnamed passes
	auto makeSyntheticKey = [](size_t n) -> std::string {
		return "__rg_ext_" + std::to_string(n);
		};

	for (int ei = 0; ei < (int)m_extensions.size(); ++ei) {
		auto& ext = m_extensions[ei];
		if (!ext) continue;

		std::vector<ExternalPassDesc> local;
		local.reserve(16);
		ext->GatherStructuralPasses(*this, local);

		std::optional<std::string> prevKey; // for extension-local chaining
		int localOrder = 0;

		for (auto& d : local) {
			if (d.type == PassType::Unknown) continue;
			if (std::holds_alternative<std::monostate>(d.pass)) continue;

			ExtItem it;
			it.pr = makeAny(d);
			registerName(d, it.pr);

			it.order = globalOrder++;
			it.key = !d.name.empty() ? d.name : makeSyntheticKey(it.order);

			if (d.where.has_value()) {
				it.where = *d.where;
			}
			else {
				it.where = ExternalInsertPoint{};
				it.where.keepExtensionOrder = true;

				// Only the *first* unconstrained pass gets anchored after base.
				// Followers will be ordered by chain edges.
				if (!prevKey.has_value()) {
					it.where.after.push_back(kAfterBaseKey);
				}
			}

			it.priority = it.where.priority;
			it.extIndex = ei;
			it.extLocalOrder = localOrder++;

			// Store prevKey for later chaining edges (we apply chaining even if explicit where)
			// (We don't add edges here because we haven't built node indices yet.)
			// We'll reconstruct chaining using extIndex/extLocalOrder + keepExtensionOrder below.
			extItems.push_back(std::move(it));
			prevKey = extItems.back().key;
		}
	}

	// Build nodes list: sentinels + base + externals
	std::vector<MergeNode> nodes;
	nodes.reserve(2 + base.size() + extItems.size());

	auto addNode = [&](std::string key, bool hasPass, AnyPassAndResources&& pass, int prio, size_t ord) -> size_t {
		MergeNode n;
		n.key = std::move(key);
		n.hasPass = hasPass;
		if (hasPass) n.pass = std::move(pass);
		n.priority = prio;
		n.order = ord;
		nodes.push_back(std::move(n));
		return nodes.size() - 1;
		};

	const size_t beginIdx = addNode(std::string(kBeginKey), false, AnyPassAndResources{}, INT_MIN, 0);
	const size_t afterBaseIdx = addNode(std::string(kAfterBaseKey), false, AnyPassAndResources{}, INT_MAX, 1);

	// Map key->node index (detect collisions early)
	std::unordered_map<std::string, size_t> keyToIdx;
	keyToIdx.reserve(2 + base.size() + extItems.size());
	keyToIdx.emplace(nodes[beginIdx].key, beginIdx);
	keyToIdx.emplace(nodes[afterBaseIdx].key, afterBaseIdx);
	keyToIdx.emplace(kEndKey, afterBaseIdx);

	// Add base pass nodes (preserve current base order deterministically)
	std::vector<size_t> baseIdx;
	baseIdx.reserve(base.size());

	for (size_t bi = 0; bi < base.size(); ++bi) {
		AnyPassAndResources bp = std::move(base[bi]);

		// Anchor keys for base: use name if present, else synthetic (not anchorable by user)
		std::string key = !bp.name.empty() ? bp.name : ("__rg_base_" + std::to_string(bi));

		if (keyToIdx.contains(key)) {
			throw std::runtime_error("Pass name/key collision during structural merge: " + key);
		}

		size_t idx = addNode(key, true, std::move(bp), /*prio=*/0, /*ord=*/1000 + bi);
		keyToIdx.emplace(nodes[idx].key, idx);
		baseIdx.push_back(idx);
	}

	// Add external pass nodes
	std::vector<size_t> extIdx;
	extIdx.reserve(extItems.size());

	for (size_t i = 0; i < extItems.size(); ++i) {
		auto& e = extItems[i];

		if (keyToIdx.contains(e.key)) {
			spdlog::error("External pass name/key collision during structural merge: {}", e.key);
			throw std::runtime_error("External pass name/key collision during structural merge: " + e.key);
		}

		size_t idx = addNode(e.key, true, std::move(e.pr), e.priority, /*ord=*/2000 + e.order);
		keyToIdx.emplace(nodes[idx].key, idx);
		extIdx.push_back(idx);
	}

	// Edge helper (dedup)
	std::unordered_set<uint64_t> edgeSet;
	edgeSet.reserve(nodes.size() * 8);

	auto addEdge = [&](size_t from, size_t to) {
		if (from == to) return;
		uint64_t k = (uint64_t(from) << 32) | uint64_t(to);
		if (!edgeSet.insert(k).second) return;
		nodes[from].out.push_back(to);
		nodes[to].indeg++;
		};

	// Base order edges: BEGIN -> base0 -> base1 -> ... -> lastBase -> AFTER_BASE
	if (!baseIdx.empty()) {
		addEdge(beginIdx, baseIdx.front());
		for (size_t i = 0; i + 1 < baseIdx.size(); ++i) addEdge(baseIdx[i], baseIdx[i + 1]);
		addEdge(baseIdx.back(), afterBaseIdx);
	}
	else {
		addEdge(beginIdx, afterBaseIdx);
	}

	// Apply external constraints + extension chaining
	// Build per-extension ordering list (by extLocalOrder)
	std::unordered_map<int, std::vector<std::pair<int, size_t>>> extOrder; // extIndex -> [(localOrder, nodeIdx)]
	extOrder.reserve(m_extensions.size());

	for (size_t i = 0; i < extItems.size(); ++i) {
		extOrder[extItems[i].extIndex].push_back({ extItems[i].extLocalOrder, extIdx[i] });
	}

	for (auto& [ei, v] : extOrder) {
		std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first < b.first; });
	}

	// Now attach constraints and chain edges
	for (size_t i = 0; i < extItems.size(); ++i) {
		auto& e = extItems[i];
		const size_t passNode = extIdx[i];

		// Helper: resolve special token for "first base"
		auto resolveAnchor = [&](std::string const& anchor) -> std::optional<size_t> {
			if (anchor == kFirstBaseKey) {
				if (!baseIdx.empty()) return baseIdx.front();
				return afterBaseIdx;
			}
			auto it = keyToIdx.find(anchor);
			if (it == keyToIdx.end()) return std::nullopt;
			return it->second;
			};

		bool anyConstraint = false;

		// after[] : anchor -> pass
		for (auto const& a : e.where.after) {
			auto idxOpt = resolveAnchor(a);
			if (!idxOpt) {
				spdlog::warn("External pass '{}' requested After('{}') but anchor not found; ignoring.", e.key, a);
				continue;
			}
			addEdge(*idxOpt, passNode);
			anyConstraint = true;
		}

		// before[] : pass -> anchor
		for (auto const& b : e.where.before) {
			auto idxOpt = resolveAnchor(b);
			if (!idxOpt) {
				spdlog::warn("External pass '{}' requested Before('{}') but anchor not found; ignoring.", e.key, b);
				continue;
			}
			addEdge(passNode, *idxOpt);
			anyConstraint = true;
		}
	}

	// Extension chaining edges: prev -> next (if keepExtensionOrder on the *next* pass)
	for (auto& [ei, v] : extOrder) {
		for (size_t j = 1; j < v.size(); ++j) {
			// Find the extItems entry for this node to check keepExtensionOrder
			// (We can check by key because keys are unique.)
			const size_t prevNode = v[j - 1].second;
			const size_t nextNode = v[j].second;

			const std::string& nextKey = nodes[nextNode].key;

			// locate corresponding ext item (small N; linear is fine)
			bool keep = true;
			for (auto& e : extItems) {
				if (e.key == nextKey) { keep = e.where.keepExtensionOrder; break; }
			}
			if (keep) addEdge(prevNode, nextNode);
		}
	}

	// Topological sort (stable by priority then order)
	std::vector<uint32_t> indeg(nodes.size());
	for (size_t n = 0; n < nodes.size(); ++n) indeg[n] = nodes[n].indeg;

	std::vector<size_t> ready;
	ready.reserve(nodes.size());
	for (size_t n = 0; n < nodes.size(); ++n) if (indeg[n] == 0) ready.push_back(n);

	auto better = [&](size_t a, size_t b) {
		if (nodes[a].priority != nodes[b].priority) return nodes[a].priority < nodes[b].priority;
		return nodes[a].order < nodes[b].order;
		};

	std::vector<size_t> topo;
	topo.reserve(nodes.size());

	while (!ready.empty()) {
		auto it = std::min_element(ready.begin(), ready.end(), [&](size_t a, size_t b) { return better(a, b); });
		size_t u = *it;
		ready.erase(it);

		topo.push_back(u);
		for (size_t vtx : nodes[u].out) {
			if (--indeg[vtx] == 0) ready.push_back(vtx);
		}
	}

	if (topo.size() != nodes.size()) {
		spdlog::error("Structural merge has a cycle (extension anchors/chains impossible).");
		throw std::runtime_error("RenderGraph structural merge cycle");
	}

	// Emit final m_masterPassList in topo order (skip sentinels)
	m_masterPassList.clear();
	m_masterPassList.reserve(baseIdx.size() + extIdx.size());

	for (size_t u : topo) {
		if (!nodes[u].hasPass) {
			continue;
		}
		m_masterPassList.push_back(std::move(nodes[u].pass));
	}
}


static ResourceRegistry::RegistryHandle ResolveByIdThunk(void* user, ResourceIdentifier const& id, bool allowFailure) {
	return static_cast<RenderGraph*>(user)->RequestResourceHandle(id, allowFailure);
}

static ResourceRegistry::RegistryHandle ResolveByPtrThunk(void* user, Resource* ptr, bool allowFailure) {
	return static_cast<RenderGraph*>(user)->RequestResourceHandle(ptr, allowFailure);
}

static bool Overlap(SubresourceRange a, SubresourceRange b) {
	auto aMipEnd = a.firstMip + a.mipCount;
	auto bMipEnd = b.firstMip + b.mipCount;
	auto aSlEnd = a.firstSlice + a.sliceCount;
	auto bSlEnd = b.firstSlice + b.sliceCount;
	return (a.firstMip < bMipEnd && b.firstMip < aMipEnd) &&
		(a.firstSlice < bSlEnd && b.firstSlice < aSlEnd);
}

static bool RequirementsConflict(
	std::vector<ResourceRequirement> const& retained,
	std::vector<ResourceRequirement> const& immediate)
{
	// group by resource ID for convenience
	// TODO: This is O(N^2), could be optimized
	for (auto const& ra : retained) {
		auto res = ra.resourceHandleAndRange.resource;
		uint64_t rid = res.GetGlobalResourceID();
		auto a = ResolveRangeSpec(ra.resourceHandleAndRange.range, res.GetNumMipLevels(), res.GetArraySize());
		if (a.isEmpty()) continue;

		for (auto const& ib : immediate) {
			if (ib.resourceHandleAndRange.resource.GetGlobalResourceID() != rid) continue;

			auto b = ResolveRangeSpec(ib.resourceHandleAndRange.range, res.GetNumMipLevels(), res.GetArraySize());
			if (b.isEmpty()) continue;

			if (Overlap(a, b) && !(ra.state == ib.state)) {
				return true;
			}
		}
	}
	return false;
}


void RenderGraph::RefreshRetainedDeclarationsForFrame(RenderPassAndResources& p, uint8_t frameIndex)
{
	RenderPassBuilder b(this, p.name);

	// Make it look like a normal builder enough for any pass code that queries ResourceProvider()
	b.pass = p.pass;
	b.built_ = true;

	// Clear any previous declarations
	b.params = {};
	b._declaredIds.clear();

	// Let the pass declare based on current per-frame state (queued mip jobs etc.)
	p.pass->DeclareResourceUsages(&b);

	// Update the frame view used by scheduling
	p.resources.staticResourceRequirements = b.GatherResourceRequirements();

	// Internal transitions also affect scheduling
	p.resources.internalTransitions = b.params.internalTransitions;

	p.resources.identifierSet = b.DeclaredResourceIds();
	MaterializeReferencedResources(p.resources.staticResourceRequirements, p.resources.internalTransitions);

	// Ensure the pass's view matches the refreshed identifier set
	p.pass->SetResourceRegistryView(
		std::make_unique<ResourceRegistryView>(_registry, p.resources.identifierSet)
	);
	p.pass->Setup();
}

void RenderGraph::RefreshRetainedDeclarationsForFrame(ComputePassAndResources& p, uint8_t frameIndex)
{
	ComputePassBuilder b(this, p.name);
	b.pass = p.pass;
	b.built_ = true;

	b.params = {};
	b._declaredIds.clear();

	p.pass->DeclareResourceUsages(&b);

	p.resources.staticResourceRequirements = b.GatherResourceRequirements();
	p.resources.internalTransitions = b.params.internalTransitions;
	p.resources.identifierSet = b.DeclaredResourceIds();
	MaterializeReferencedResources(p.resources.staticResourceRequirements, p.resources.internalTransitions);

	p.pass->SetResourceRegistryView(
		std::make_unique<ResourceRegistryView>(_registry, p.resources.identifierSet)
	);

	p.pass->Setup();
}

void RenderGraph::CompileFrame(rhi::Device device, uint8_t frameIndex, const IHostExecutionData* hostData) {
	if (m_statisticsService) {
		m_statisticsService->BeginFrame();
	}
	compileTrackers.clear();
	m_aliasingSubsystem.ResetPerFrameState(*this);

	auto needsRefresh = [&](auto& p) -> bool {
		auto* iFace = dynamic_cast<IDynamicDeclaredResources*>(p.pass.get());
		if (!iFace) {
			// if pass doesn't opt-in, assume no change
			return false;
		}

		return iFace->DeclaredResourcesChanged();
		};

	// First, refresh all retained declarations for this frame
	for (auto& pr : m_masterPassList) {
		if (pr.type == PassType::Compute) {
			auto& p = std::get<ComputePassAndResources>(pr.pass);
			p.immediateBytecode.clear();
			p.resources.frameResourceRequirements = {};// p.resources.staticResourceRequirements;
			if (needsRefresh(p)) {
				RefreshRetainedDeclarationsForFrame(p, frameIndex);
			}
		}
		else {
			auto& p = std::get<RenderPassAndResources>(pr.pass);
			p.immediateBytecode.clear();
			p.resources.frameResourceRequirements = {};// p.resources.staticResourceRequirements;
			if (needsRefresh(p)) {
				RefreshRetainedDeclarationsForFrame(p, frameIndex);
			}
		}
	}

	batches.clear();
	batches.push_back({}); // Dummy batch 0 for pre-first-pass transitions
	m_framePasses.clear(); // Combined retained + immediate-mode passes for this frame

	// Record immediate-mode commands + access for each pass and fold into per-frame requirements
	for (auto& pr : m_masterPassList) {

		if (pr.type == PassType::Compute) {
			auto& p = std::get<ComputePassAndResources>(pr.pass);

			// reset per-frame
			p.immediateBytecode.clear();
			p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;

			ImmediateExecutionContext c{ device,
				{/*isRenderPass=*/false,
				m_immediateDispatch,
				&ResolveByIdThunk,
				&ResolveByPtrThunk,
				this},
				frameIndex,
				hostData
			};

			// Record immediate-mode commands
			p.pass->ExecuteImmediate(c);

			auto immediateFrameData = c.list.Finalize();
			// If there is a conflict between retained and immediate requirements, split the pass
			bool conflict = RequirementsConflict(
				p.resources.frameResourceRequirements,   // baseline retained for this frame
				immediateFrameData.requirements);
			if (conflict) {
				// Create new PassAndResources for the immediate requirements
				ComputePassAndResources immediatePassAndResources;
				immediatePassAndResources.pass = p.pass;
				immediatePassAndResources.resources.staticResourceRequirements = immediateFrameData.requirements;
				immediatePassAndResources.resources.frameResourceRequirements = immediateFrameData.requirements;
				immediatePassAndResources.immediateBytecode = std::move(immediateFrameData.bytecode);
				immediatePassAndResources.immediateKeepAlive = std::move(p.immediateKeepAlive);
				immediatePassAndResources.run = PassRunMask::Immediate;
				AnyPassAndResources immediateAnyPassAndResources;
				immediateAnyPassAndResources.type = PassType::Compute;
				immediateAnyPassAndResources.pass = immediatePassAndResources;
				m_framePasses.push_back(immediateAnyPassAndResources);
				p.run = PassRunMask::Retained;
				m_framePasses.push_back(pr); // Retained pass
			}
			else {
				p.immediateBytecode = std::move(immediateFrameData.bytecode);
				p.immediateKeepAlive = std::move(immediateFrameData.keepAlive);
				p.resources.frameResourceRequirements.insert(
					p.resources.frameResourceRequirements.end(),
					immediateFrameData.requirements.begin(),
					immediateFrameData.requirements.end());
				p.run = p.immediateBytecode.empty() ? PassRunMask::Retained : PassRunMask::Both;
				m_framePasses.push_back(pr);
			}
		}
		else {
			auto& p = std::get<RenderPassAndResources>(pr.pass);

			p.immediateBytecode.clear();
			p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;

			ImmediateExecutionContext c{ device,
				{/*isRenderPass=*/true,
				m_immediateDispatch,
				&ResolveByIdThunk,
				&ResolveByPtrThunk,
				this},
				frameIndex,
				hostData
			};
			p.pass->ExecuteImmediate(c);
			auto immediateFrameData = c.list.Finalize();

			bool conflict = RequirementsConflict(
				p.resources.frameResourceRequirements,   // baseline retained for this frame
				immediateFrameData.requirements);

			if (conflict) {
				// Create new PassAndResources for the immediate requirements
				RenderPassAndResources immediatePassAndResources;
				immediatePassAndResources.pass = p.pass;
				immediatePassAndResources.resources.staticResourceRequirements = immediateFrameData.requirements;
				immediatePassAndResources.resources.frameResourceRequirements = immediateFrameData.requirements;
				immediatePassAndResources.immediateBytecode = std::move(immediateFrameData.bytecode);
				immediatePassAndResources.immediateKeepAlive = std::move(p.immediateKeepAlive);
				immediatePassAndResources.run = PassRunMask::Immediate;
				AnyPassAndResources immediateAnyPassAndResources;
				immediateAnyPassAndResources.type = PassType::Render;
				immediateAnyPassAndResources.pass = immediatePassAndResources;
				m_framePasses.push_back(immediateAnyPassAndResources);
				p.run = PassRunMask::Retained;
				m_framePasses.push_back(pr); // Retained pass
			}
			else {
				p.immediateBytecode = std::move(immediateFrameData.bytecode);
				p.immediateKeepAlive = std::move(immediateFrameData.keepAlive);
				p.resources.frameResourceRequirements.insert(
					p.resources.frameResourceRequirements.end(),
					immediateFrameData.requirements.begin(),
					immediateFrameData.requirements.end());
				p.run = p.immediateBytecode.empty() ? PassRunMask::Retained : PassRunMask::Both;
				m_framePasses.push_back(pr);
			}
		}
	}

	// --- Per-frame extension passes (ephemeral) ---
	// These are injected into the per-frame pass list (not m_masterPassList) so they do not accumulate.
	std::vector<ExternalPassDesc> frameExt;
	frameExt.reserve(16);
	for (auto& ext : m_extensions) {
		if (!ext) continue;
		ext->GatherFramePasses(*this, frameExt);
	}

	// explicit After(anchor) edges (anchorName -> injectedName)
	std::vector<std::pair<std::string, std::string>> explicitAfterByName;
	explicitAfterByName.reserve(frameExt.size());

	if (!frameExt.empty()) {
		auto makeAny = [&](ExternalPassDesc const& d) -> AnyPassAndResources {
			AnyPassAndResources any;
			any.type = d.type;
			any.name = d.name;

			if (d.type == PassType::Render) {
				auto rp = std::get<std::shared_ptr<RenderPass>>(d.pass);
				RenderPassAndResources par;
				par.pass = std::move(rp);
				par.name = d.name;
				{
					RenderPassBuilder b(this, d.name);
					b.pass = par.pass;
					b.built_ = true;
					b.params = {};
					b._declaredIds.clear();
					par.pass->DeclareResourceUsages(&b);
					par.resources.staticResourceRequirements = b.GatherResourceRequirements();
					par.resources.frameResourceRequirements = par.resources.staticResourceRequirements;
					par.resources.internalTransitions = b.params.internalTransitions;
					par.resources.identifierSet = b.DeclaredResourceIds();
					par.resources.isGeometryPass = b.params.isGeometryPass;
				}

				par.pass->SetResourceRegistryView(
					std::make_unique<ResourceRegistryView>(_registry, par.resources.identifierSet)
				);
				par.pass->Setup();
				any.pass = std::move(par);
			}
			else if (d.type == PassType::Compute) {
				auto cp = std::get<std::shared_ptr<ComputePass>>(d.pass);
				ComputePassAndResources par;
				par.pass = std::move(cp);
				par.name = d.name;
				{
					ComputePassBuilder b(this, d.name);
					b.pass = par.pass;
					b.built_ = true;
					b.params = {};
					b._declaredIds.clear();
					par.pass->DeclareResourceUsages(&b);
					par.resources.staticResourceRequirements = b.GatherResourceRequirements();
					par.resources.frameResourceRequirements = par.resources.staticResourceRequirements;
					par.resources.internalTransitions = b.params.internalTransitions;
					par.resources.identifierSet = b.DeclaredResourceIds();
				}

				par.pass->SetResourceRegistryView(
					std::make_unique<ResourceRegistryView>(_registry, par.resources.identifierSet)
				);
				par.pass->Setup();
				any.pass = std::move(par);
			}

			return any;
		};

		auto recordImmediate = [&](AnyPassAndResources& pr) {
			if (pr.type == PassType::Compute) {
				auto& p = std::get<ComputePassAndResources>(pr.pass);
				p.immediateBytecode.clear();
				p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;

				ImmediateExecutionContext c{ device,
					{/*isRenderPass=*/false,
					m_immediateDispatch,
					&ResolveByIdThunk,
					&ResolveByPtrThunk,
					this},
					frameIndex,
					hostData
				};

				p.pass->ExecuteImmediate(c);
				auto immediateFrameData = c.list.Finalize();
				p.immediateBytecode = std::move(immediateFrameData.bytecode);
				p.immediateKeepAlive = std::move(immediateFrameData.keepAlive);
				p.resources.frameResourceRequirements.insert(
					p.resources.frameResourceRequirements.end(),
					immediateFrameData.requirements.begin(),
					immediateFrameData.requirements.end());
				p.run = p.immediateBytecode.empty() ? PassRunMask::Retained : PassRunMask::Both;
			}
			else {
				auto& p = std::get<RenderPassAndResources>(pr.pass);
				p.immediateBytecode.clear();
				p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;

				ImmediateExecutionContext c{ device,
					{/*isRenderPass=*/true,
					m_immediateDispatch,
					&ResolveByIdThunk,
					&ResolveByPtrThunk,
					this},
					frameIndex,
					hostData
				};

				p.pass->ExecuteImmediate(c);
				auto immediateFrameData = c.list.Finalize();
				p.immediateBytecode = std::move(immediateFrameData.bytecode);
				p.immediateKeepAlive = std::move(immediateFrameData.keepAlive);
				p.resources.frameResourceRequirements.insert(
					p.resources.frameResourceRequirements.end(),
					immediateFrameData.requirements.begin(),
					immediateFrameData.requirements.end());
				p.run = p.immediateBytecode.empty() ? PassRunMask::Retained : PassRunMask::Both;
			}
		};

		auto findPassIndexByName = [&](const std::string& name) -> std::optional<size_t> {
			if (name.empty()) return std::nullopt;
			for (size_t i = 0; i < m_framePasses.size(); ++i) {
				if (m_framePasses[i].name == name) return i;
			}
			return std::nullopt;
		};

		std::unordered_map<std::string, uint32_t> insertedAfterCount;
		insertedAfterCount.reserve(frameExt.size());

		for (auto& d : frameExt) {
			if (d.type == PassType::Unknown) continue;
			if (std::holds_alternative<std::monostate>(d.pass)) continue;
			if (d.name.empty()) {
				spdlog::warn("Frame extension emitted a pass with empty name; skipping.");
				continue;
			}

			AnyPassAndResources any = makeAny(d);
			recordImmediate(any);

			// Default insertion: append
			size_t insertPos = m_framePasses.size();
			std::string anchorName;

			if (d.where.has_value()) {
				for (auto const& a : d.where->after) {
					auto idxOpt = findPassIndexByName(a);
					if (idxOpt.has_value()) {
						anchorName = a;
						uint32_t offset = insertedAfterCount[anchorName]++;
						insertPos = std::min(*idxOpt + 1 + (size_t)offset, m_framePasses.size());
						break;
					}
				}
			}

			if (!anchorName.empty()) {
				explicitAfterByName.push_back({ anchorName, any.name });
			}

			m_framePasses.insert(m_framePasses.begin() + insertPos, std::move(any));
		}
	}

	// Register/refresh pass statistics indices for this frame's concrete pass list.
	// This supports transient passes and per-frame retained/immediate splits.
	if (m_statisticsService) {
		for (size_t i = 0; i < m_framePasses.size(); ++i) {
			auto& any = m_framePasses[i];
			if (any.type == PassType::Render) {
				auto& p = std::get<RenderPassAndResources>(any.pass);
				if (p.name.empty()) {
					p.name = "RenderPass#" + std::to_string(i);
				}
				any.name = p.name;
				p.statisticsIndex = static_cast<int>(m_statisticsService->RegisterPass(p.name, p.resources.isGeometryPass));
			}
			else if (any.type == PassType::Compute) {
				auto& p = std::get<ComputePassAndResources>(any.pass);
				if (p.name.empty()) {
					p.name = "ComputePass#" + std::to_string(i);
				}
				any.name = p.name;
				p.statisticsIndex = static_cast<int>(m_statisticsService->RegisterPass(p.name, false));
			}
		}

		m_statisticsService->SetupQueryHeap();
	}

	auto usedResourceIDs = CollectFrameResourceIDs();
	ApplyIdleDematerializationPolicy(usedResourceIDs);

	auto currentBatch = PassBatch();
	currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
	currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
	currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
	currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();

	std::unordered_set<uint64_t> computeUAVs;
	std::unordered_set<uint64_t> renderUAVs;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueTransition;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueTransition;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueProducer;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueProducer;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueUsage;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueUsage;

	// Convert explicit After(anchorName)->(passName) constraints into node-index edges.
	std::vector<std::pair<size_t, size_t>> explicitEdges;
	explicitEdges.reserve(explicitAfterByName.size());
	if (!explicitAfterByName.empty()) {
		std::unordered_map<std::string, size_t> nameToIndex;
		nameToIndex.reserve(m_framePasses.size());
		for (size_t i = 0; i < m_framePasses.size(); ++i) {
			if (!m_framePasses[i].name.empty()) {
				nameToIndex[m_framePasses[i].name] = i;
			}
		}
		for (auto const& e : explicitAfterByName) {
			auto itA = nameToIndex.find(e.first);
			auto itB = nameToIndex.find(e.second);
			if (itA == nameToIndex.end() || itB == nameToIndex.end()) {
				spdlog::warn("Explicit After edge dropped (anchor='{}', pass='{}'): name not found in frame pass list.", e.first, e.second);
				continue;
			}
			explicitEdges.push_back({ itA->second, itB->second });
		}
	}

	auto nodes = BuildNodes(*this, m_framePasses);
	if (!BuildDependencyGraph(nodes, explicitEdges)) {
		// Cycle detected
		spdlog::error("Render graph contains a dependency cycle! Render graph compilation failed.");
		throw std::runtime_error("Render graph contains a dependency cycle");
	}

	std::vector<rg::alias::AliasSchedulingNode> aliasNodes;
	aliasNodes.reserve(nodes.size());
	for (const auto& node : nodes) {
		aliasNodes.push_back(rg::alias::AliasSchedulingNode{
			.passIndex = node.passIndex,
			.originalOrder = node.originalOrder,
			.indegree = node.indegree,
			.criticality = node.criticality,
			.out = node.out,
		});
	}

	m_aliasingSubsystem.AutoAssignAliasingPools(*this, aliasNodes);
	m_aliasingSubsystem.BuildAliasPlanAfterDag(*this, aliasNodes);
	MaterializeUnmaterializedResources(&usedResourceIDs);
	SnapshotCompiledResourceGenerations(usedResourceIDs);

	AutoScheduleAndBuildBatches(*this, m_framePasses, nodes);
	m_aliasingSubsystem.ApplyAliasQueueSynchronization(*this);

	m_hasPendingFrameStartComputeWaitOnRender = false;
	m_pendingFrameStartComputeWaitOnRenderFenceValue = 0;
	m_hasPendingFrameStartRenderWaitOnCompute = false;
	m_pendingFrameStartRenderWaitOnComputeFenceValue = 0;
	uint32_t overlapTriggeredComputeWaitCount = 0;
	uint32_t overlapTriggeredRenderWaitCount = 0;
	uint64_t overlapSampleCurrentForComputeWait = 0;
	uint64_t overlapSamplePreviousForComputeWait = 0;
	uint64_t overlapSampleCurrentForRenderWait = 0;
	uint64_t overlapSamplePreviousForRenderWait = 0;

	auto accumulateCrossFrameWaitForHandle = [&](bool passIsCompute, const ResourceRegistry::RegistryHandle& handle) {
		if (handle.IsEphemeral()) {
			return;
		}

		const uint64_t id = handle.GetGlobalResourceID();
		for (uint64_t rid : m_aliasingSubsystem.GetSchedulingEquivalentIDs(id, aliasPlacementRangesByID)) {
			auto it = m_lastProducerByResourceAcrossFrames.find(rid);
			if (it == m_lastProducerByResourceAcrossFrames.end()) {
				// no-op
			}
			else {
				if (passIsCompute && it->second.queue == CommandQueueType::Graphics) {
					m_hasPendingFrameStartComputeWaitOnRender = true;
					m_pendingFrameStartComputeWaitOnRenderFenceValue =
						std::max(m_pendingFrameStartComputeWaitOnRenderFenceValue, it->second.fenceValue);
				}
				else if (!passIsCompute && it->second.queue == CommandQueueType::Compute) {
					m_hasPendingFrameStartRenderWaitOnCompute = true;
					m_pendingFrameStartRenderWaitOnComputeFenceValue =
						std::max(m_pendingFrameStartRenderWaitOnComputeFenceValue, it->second.fenceValue);
				}
			}

			auto itCurPlacement = aliasPlacementRangesByID.find(rid);
			if (itCurPlacement == aliasPlacementRangesByID.end()) {
				continue;
			}

			auto itPoolState = persistentAliasPools.find(itCurPlacement->second.poolID);
			if (itPoolState == persistentAliasPools.end()) {
				continue;
			}

			auto itPrevPool = m_lastAliasPlacementProducersByPoolAcrossFrames.find(itCurPlacement->second.poolID);
			if (itPrevPool == m_lastAliasPlacementProducersByPoolAcrossFrames.end()) {
				continue;
			}

			const uint64_t curStart = itCurPlacement->second.startByte;
			const uint64_t curEnd = itCurPlacement->second.endByte;
			const uint64_t curPoolGeneration = itPoolState->second.generation;

			for (const auto& prevPlacementProducer : itPrevPool->second) {
				if (prevPlacementProducer.poolGeneration != curPoolGeneration) {
					continue;
				}

				const uint64_t overlapStart = std::max(curStart, prevPlacementProducer.startByte);
				const uint64_t overlapEnd = std::min(curEnd, prevPlacementProducer.endByte);
				if (overlapStart >= overlapEnd) {
					continue;
				}

				if (passIsCompute && prevPlacementProducer.producer.queue == CommandQueueType::Graphics) {
					m_hasPendingFrameStartComputeWaitOnRender = true;
					m_pendingFrameStartComputeWaitOnRenderFenceValue =
						std::max(
							m_pendingFrameStartComputeWaitOnRenderFenceValue,
							prevPlacementProducer.producer.fenceValue);
					overlapTriggeredComputeWaitCount++;
					if (overlapSampleCurrentForComputeWait == 0) {
						overlapSampleCurrentForComputeWait = rid;
						overlapSamplePreviousForComputeWait = prevPlacementProducer.resourceID;
					}
				}
				else if (!passIsCompute && prevPlacementProducer.producer.queue == CommandQueueType::Compute) {
					m_hasPendingFrameStartRenderWaitOnCompute = true;
					m_pendingFrameStartRenderWaitOnComputeFenceValue =
						std::max(
							m_pendingFrameStartRenderWaitOnComputeFenceValue,
							prevPlacementProducer.producer.fenceValue);
					overlapTriggeredRenderWaitCount++;
					if (overlapSampleCurrentForRenderWait == 0) {
						overlapSampleCurrentForRenderWait = rid;
						overlapSamplePreviousForRenderWait = prevPlacementProducer.resourceID;
					}
				}
			}
		}
	};

	for (const auto& pr : m_framePasses) {
		if (pr.type == PassType::Compute) {
			auto const& pass = std::get<ComputePassAndResources>(pr.pass);
			for (auto const& req : pass.resources.frameResourceRequirements) {
				accumulateCrossFrameWaitForHandle(true, req.resourceHandleAndRange.resource);
			}
			for (auto const& tr : pass.resources.internalTransitions) {
				accumulateCrossFrameWaitForHandle(true, tr.first.resource);
			}
		}
		else if (pr.type == PassType::Render) {
			auto const& pass = std::get<RenderPassAndResources>(pr.pass);
			for (auto const& req : pass.resources.frameResourceRequirements) {
				accumulateCrossFrameWaitForHandle(false, req.resourceHandleAndRange.resource);
			}
			for (auto const& tr : pass.resources.internalTransitions) {
				accumulateCrossFrameWaitForHandle(false, tr.first.resource);
			}
		}
	}

	if (overlapTriggeredComputeWaitCount > 0) {
		spdlog::info(
			"RG cross-frame overlap wait: compute waits on graphics; hits={} waitFence={} sampleCurrentResourceId={} samplePreviousResourceId={}",
			overlapTriggeredComputeWaitCount,
			m_pendingFrameStartComputeWaitOnRenderFenceValue,
			overlapSampleCurrentForComputeWait,
			overlapSamplePreviousForComputeWait);
	}

	if (overlapTriggeredRenderWaitCount > 0) {
		spdlog::info(
			"RG cross-frame overlap wait: graphics waits on compute; hits={} waitFence={} sampleCurrentResourceId={} samplePreviousResourceId={}",
			overlapTriggeredRenderWaitCount,
			m_pendingFrameStartRenderWaitOnComputeFenceValue,
			overlapSampleCurrentForRenderWait,
			overlapSamplePreviousForRenderWait);
	}

	// Insert transitions to loop resources back to their initial states
	//ComputeResourceLoops();

	// Cut out repeat waits on the same fence
	uint64_t lastRenderWaitFenceValue = 0;
	uint64_t lastComputeWaitFenceValue = 0;
	for (auto& batch : batches) {
		if (batch.computeQueueWaitOnRenderQueueBeforeTransition) {
			if (batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue <= lastComputeWaitFenceValue) {
				batch.computeQueueWaitOnRenderQueueBeforeTransition = false;
			}
			else {
				lastComputeWaitFenceValue = batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue;
			}
		}
		if (batch.computeQueueWaitOnRenderQueueBeforeExecution) {
			if (batch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue <= lastComputeWaitFenceValue) {
				batch.computeQueueWaitOnRenderQueueBeforeExecution = false;
			}
			else {
				lastComputeWaitFenceValue = batch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue;
			}
		}
		if (batch.renderQueueWaitOnComputeQueueBeforeTransition) {
			if (batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue <= lastRenderWaitFenceValue) {
				batch.renderQueueWaitOnComputeQueueBeforeTransition = false;
			}
			else {
				lastRenderWaitFenceValue = batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue;
			}
		}
		if (batch.renderQueueWaitOnComputeQueueBeforeExecution) {
			if (batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue <= lastRenderWaitFenceValue) {
				batch.renderQueueWaitOnComputeQueueBeforeExecution = false;
			}
			else {
				lastRenderWaitFenceValue = batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue;
			}
		}
	}

#if BUILD_TYPE == BUILD_TYPE_DEBUG
	// Sanity checks:
	// 1. No conflicting resource transitions in a batch

	// Build one vector of transitions per batch
	for (size_t bi = 0; bi < batches.size(); bi++) {
		std::vector<ResourceTransition> allTransitions;
		auto& batch = batches[bi];
		allTransitions.insert(
			allTransitions.end(),
			batch.renderTransitions.begin(),
			batch.renderTransitions.end());
		allTransitions.insert(
			allTransitions.end(),
			batch.computeTransitions.begin(),
			batch.computeTransitions.end());

		// Validate
		TransitionConflict out;
		if (bool ok = ValidateNoConflictingTransitions(allTransitions, &out); !ok) {
			spdlog::error("Render graph has conflicting resource transitions!");
			throw std::runtime_error("Render graph has conflicting resource transitions!");
		}
	}

#endif
}

std::tuple<int, int, int> RenderGraph::GetBatchesToWaitOn(
	const ComputePassAndResources& pass,
	std::unordered_map<uint64_t, unsigned int> const& transitionHistory,
	std::unordered_map<uint64_t, unsigned int> const& producerHistory,
	std::unordered_map<uint64_t, unsigned int> const& usageHistory,
	std::unordered_set<uint64_t> const& resourcesTransitionedThisPass)
{
	int latestTransition = -1, latestProducer = -1, latestUsage = -1;

	auto processResource = [&](ResourceRegistry::RegistryHandle const& res) {
		uint64_t id = res.GetGlobalResourceID();
		auto ids = m_aliasingSubsystem.GetSchedulingEquivalentIDs(id, aliasPlacementRangesByID);
		for (auto rid : ids) {
			auto itT = transitionHistory.find(rid);
			if (itT != transitionHistory.end())
				latestTransition = std::max(latestTransition, (int)itT->second);

			auto itP = producerHistory.find(rid);
			if (itP != producerHistory.end())
				latestProducer = std::max(latestProducer, (int)itP->second);
		}
		};

	for (auto const& req : pass.resources.frameResourceRequirements)
		processResource(req.resourceHandleAndRange.resource);

	for (auto& transitionID : resourcesTransitionedThisPass) { // We only need to wait on the latest usage for resources that will be transitioned in this batch
		for (auto rid : m_aliasingSubsystem.GetSchedulingEquivalentIDs(transitionID, aliasPlacementRangesByID)) {
			if (usageHistory.contains(rid)) {
				latestUsage = std::max(latestUsage, (int)usageHistory.at(rid));
			}
		}
	}

	return { latestTransition, latestProducer, latestUsage };
}

std::tuple<int, int, int> RenderGraph::GetBatchesToWaitOn(
	const RenderPassAndResources& pass,
	std::unordered_map<uint64_t, unsigned int> const& transitionHistory,
	std::unordered_map<uint64_t, unsigned int> const& producerHistory,
	std::unordered_map<uint64_t, unsigned int> const& usageHistory,
	std::unordered_set<uint64_t> const& resourcesTransitionedThisPass)
{
	int latestTransition = -1, latestProducer = -1, latestUsage = -1;

	auto processResource = [&](ResourceRegistry::RegistryHandle const& res) {
		uint64_t id = res.GetGlobalResourceID();
		for (auto rid : m_aliasingSubsystem.GetSchedulingEquivalentIDs(id, aliasPlacementRangesByID)) {
			auto itT = transitionHistory.find(rid);
			if (itT != transitionHistory.end())
				latestTransition = std::max(latestTransition, (int)itT->second);

			auto itP = producerHistory.find(rid);
			if (itP != producerHistory.end())
				latestProducer = std::max(latestProducer, (int)itP->second);
		}
		};

	for (auto const& req : pass.resources.frameResourceRequirements)
		processResource(req.resourceHandleAndRange.resource);

	for (auto& transitionID : resourcesTransitionedThisPass) { // We only need to wait on the latest usage for resources that will be transitioned in this batch
		for (auto rid : m_aliasingSubsystem.GetSchedulingEquivalentIDs(transitionID, aliasPlacementRangesByID)) {
			if (usageHistory.contains(rid)) {
				latestUsage = std::max(latestUsage, (int)usageHistory.at(rid));
			}
		}
	}

	return { latestTransition, latestProducer, latestUsage };
}

void RenderGraph::MaterializeUnmaterializedResources(const std::unordered_set<uint64_t>* onlyResourceIDs) {
	for (auto& [id, resource] : resourcesByID) {
		if (onlyResourceIDs && onlyResourceIDs->find(id) == onlyResourceIDs->end()) {
			continue;
		}
		if (!resource) {
			continue;
		}

		auto texture = std::dynamic_pointer_cast<PixelBuffer>(resource);
		if (texture) {
			if (!texture->IsMaterialized()) {
				auto itAlias = aliasMaterializeOptionsByID.find(id);
				if (itAlias != aliasMaterializeOptionsByID.end()) {
					if (std::holds_alternative<PixelBuffer::MaterializeOptions>(itAlias->second)) {
						auto& options = std::get<PixelBuffer::MaterializeOptions>(itAlias->second);
						if (options.aliasPlacement.has_value()) {
							const auto& ap = options.aliasPlacement.value();
							spdlog::info(
								"RG alias materialize: id={} name='{}' pool={} offset={}",
								id,
								resource->GetName(),
								ap.poolID.has_value() ? ap.poolID.value() : 0ull,
								ap.offset);
						}
						texture->Materialize(&options);
					}
				}
				else {
					if (texture->GetDescription().allowAlias) {
						const bool hasManualAliasPool = texture->GetDescription().aliasingPoolID.has_value();
						const bool hasAutoAliasPool = autoAliasPoolByID.find(id) != autoAliasPoolByID.end();
						const bool aliasPlacementRequiredThisFrame = hasManualAliasPool || hasAutoAliasPool;

						if (onlyResourceIDs && aliasPlacementRequiredThisFrame) {
							throw std::runtime_error(
								"Aliasing placement missing for used aliased resource during frame materialization. Resource ID: " + std::to_string(id));
						}

						if (onlyResourceIDs && !aliasPlacementRequiredThisFrame) {
							spdlog::debug(
								"RG alias fallback materialize: id={} name='{}' allowAlias=1 but no pool assignment this frame; materializing standalone",
								id,
								resource->GetName());
						}

						// Setup-time eager materialization happens before alias planning.
						// Defer aliased resources until compile-time placement is available.
						if (!onlyResourceIDs) {
							continue;
						}
					}
					texture->Materialize();
				}
			}

			resourceBackingGenerationByID[id] = texture->GetBackingGeneration();
			continue;
		}

		auto buffer = std::dynamic_pointer_cast<Buffer>(resource);
		if (!buffer) {
			continue;
		}

		if (!buffer->IsMaterialized()) {
			auto itAlias = aliasMaterializeOptionsByID.find(id);
			if (itAlias != aliasMaterializeOptionsByID.end()) {
				if (std::holds_alternative<BufferBase::MaterializeOptions>(itAlias->second)) {
					auto& options = std::get<BufferBase::MaterializeOptions>(itAlias->second);
					if (options.aliasPlacement.has_value()) {
						const auto& ap = options.aliasPlacement.value();
						spdlog::info(
							"RG alias materialize (buffer): id={} name='{}' pool={} offset={}",
							id,
							resource->GetName(),
							ap.poolID.has_value() ? ap.poolID.value() : 0ull,
							ap.offset);
					}
					buffer->Materialize(&options);
				}
			}
			else {
				if (buffer->IsAliasingAllowed()) {
					const bool hasManualAliasPool = buffer->GetAliasingPoolHint().has_value();
					const bool hasAutoAliasPool = autoAliasPoolByID.find(id) != autoAliasPoolByID.end();
					const bool aliasPlacementRequiredThisFrame = hasManualAliasPool || hasAutoAliasPool;

					if (onlyResourceIDs && aliasPlacementRequiredThisFrame) {
						throw std::runtime_error(
							"Aliasing placement missing for used aliased buffer during frame materialization. Resource ID: " + std::to_string(id));
					}

					if (onlyResourceIDs && !aliasPlacementRequiredThisFrame) {
						spdlog::debug(
							"RG alias fallback materialize (buffer): id={} name='{}' allowAlias=1 but no pool assignment this frame; materializing standalone",
							id,
							resource->GetName());
					}

					if (!onlyResourceIDs) {
						continue;
					}
				}
				buffer->Materialize();
			}
		}

		resourceBackingGenerationByID[id] = buffer->GetBackingGeneration();
	}
}

void RenderGraph::Setup() {
	DeletionManager::GetInstance().Initialize();

	// Setup the statistics manager
	if (m_statisticsService) {
		m_statisticsService->ClearAll();
	}
	auto& manager = DeviceManager::GetInstance();
	if (m_statisticsService) {
		m_statisticsService->RegisterQueue(manager.GetGraphicsQueue().GetKind());
		m_statisticsService->RegisterQueue(manager.GetComputeQueue().GetKind());
		m_statisticsService->SetupQueryHeap();
	}

	auto device = DeviceManager::GetInstance().GetDevice();

	m_graphicsCommandListPool = std::make_unique<CommandListPool>(device, rhi::QueueKind::Graphics);
	m_computeCommandListPool = std::make_unique<CommandListPool>(device, rhi::QueueKind::Compute);
	m_copyCommandListPool = std::make_unique<CommandListPool>(device, rhi::QueueKind::Copy);

	auto result = device.CreateTimeline(m_graphicsQueueFence);
	result = device.CreateTimeline(m_computeQueueFence);
	result = device.CreateTimeline(m_copyQueueFence);
	result = device.CreateTimeline(m_frameStartSyncFence);

	m_getUseAsyncCompute = [this]() {
		return m_renderGraphSettingsService ? m_renderGraphSettingsService->GetUseAsyncCompute() : false;
	};
	m_getAutoAliasMode = [this]() {
		const auto mode = m_renderGraphSettingsService
			? m_renderGraphSettingsService->GetAutoAliasMode()
			: static_cast<uint8_t>(AutoAliasMode::Off);
		return static_cast<AutoAliasMode>(mode);
	};
	m_getAutoAliasPackingStrategy = [this]() {
		const auto strategy = m_renderGraphSettingsService
			? m_renderGraphSettingsService->GetAutoAliasPackingStrategy()
			: static_cast<uint8_t>(AutoAliasPackingStrategy::GreedySweepLine);
		return static_cast<AutoAliasPackingStrategy>(strategy);
	};
	m_getAutoAliasLogExclusionReasons = [this]() {
		return m_renderGraphSettingsService ? m_renderGraphSettingsService->GetAutoAliasLogExclusionReasons() : false;
	};
	m_getAutoAliasPoolRetireIdleFrames = [this]() {
		return m_renderGraphSettingsService ? m_renderGraphSettingsService->GetAutoAliasPoolRetireIdleFrames() : 120u;
	};
	m_getAutoAliasPoolGrowthHeadroom = [this]() {
		return m_renderGraphSettingsService ? m_renderGraphSettingsService->GetAutoAliasPoolGrowthHeadroom() : 1.5f;
	};
	MaterializeUnmaterializedResources();

	// Run pass setup to collect static resource requirements
	for (auto& pass : m_masterPassList) {
		switch (pass.type) {
		case PassType::Render: {
			auto& renderPass = std::get<RenderPassAndResources>(pass.pass);
			renderPass.pass->SetResourceRegistryView(std::make_unique<ResourceRegistryView>(_registry, renderPass.resources.identifierSet));
			renderPass.pass->Setup();
			break;
		}
		case PassType::Compute: {
			auto& computePass = std::get<ComputePassAndResources>(pass.pass);
			computePass.pass->SetResourceRegistryView(std::make_unique<ResourceRegistryView>(_registry, computePass.resources.identifierSet));
			computePass.pass->Setup();
			break;
		}
		}
	}
}

void RenderGraph::AddRenderPass(std::shared_ptr<RenderPass> pass, RenderPassParameters& resources, std::string name) {
	RenderPassAndResources passAndResources;
	passAndResources.pass = pass;
	passAndResources.resources = resources;
	passAndResources.name = name;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Render;
	passAndResourcesAny.pass = passAndResources;
	passAndResourcesAny.name = name;
	m_masterPassList.push_back(passAndResourcesAny);
	if (name != "") {
		renderPassesByName[name] = pass;
	}
}

void RenderGraph::AddComputePass(std::shared_ptr<ComputePass> pass, ComputePassParameters& resources, std::string name) {
	ComputePassAndResources passAndResources;
	passAndResources.pass = pass;
	passAndResources.resources = resources;
	passAndResources.name = name;
	AnyPassAndResources passAndResourcesAny;
	passAndResourcesAny.type = PassType::Compute;
	passAndResourcesAny.pass = passAndResources;
	passAndResourcesAny.name = name;
	m_masterPassList.push_back(passAndResourcesAny);
	if (name != "") {
		computePassesByName[name] = pass;
	}
}

void RenderGraph::AddResource(std::shared_ptr<Resource> resource, bool transition) {
	if (resourcesByID.contains(resource->GetGlobalResourceID())) {
		return; // Resource already added
	}
	auto& name = resource->GetName();

#ifdef _DEBUG
	//if (name == L"") {
	//	throw std::runtime_error("Resource name cannot be empty");
	//}
	//else if (resourcesByName.find(name) != resourcesByName.end()) {
	//	throw std::runtime_error("Resource with name " + ws2s(name) + " already exists");
	//}
#endif

	resourcesByName[name] = resource;
	resourcesByID[resource->GetGlobalResourceID()] = resource;

	if (auto texture = std::dynamic_pointer_cast<PixelBuffer>(resource)) {
		texture->EnsureVirtualDescriptorSlotsAllocated();
	}
	if (auto buffer = std::dynamic_pointer_cast<Buffer>(resource)) {
		buffer->EnsureVirtualDescriptorSlotsAllocated();
	}
	/*if (transition) {
		initialResourceStates[resource->GetGlobalResourceID()] = initialState;
	}*/
}

std::shared_ptr<Resource> RenderGraph::GetResourceByName(const std::string& name) {
	return resourcesByName[name];
}

std::shared_ptr<Resource> RenderGraph::GetResourceByID(const uint64_t id) {
	return resourcesByID[id];
}
std::shared_ptr<RenderPass> RenderGraph::GetRenderPassByName(const std::string& name) {
	if (renderPassesByName.find(name) != renderPassesByName.end()) {
		return renderPassesByName[name];
	}
	else {
		return nullptr;
	}
}

std::shared_ptr<ComputePass> RenderGraph::GetComputePassByName(const std::string& name) {
	if (computePassesByName.find(name) != computePassesByName.end()) {
		return computePassesByName[name];
	}
	else {
		return nullptr;
	}
}

void RenderGraph::Update(const UpdateExecutionContext& context, rhi::Device device) {
	ResetForFrame();

	for (auto& pr : m_masterPassList) {	
		// Resolve into type and update
		std::visit([&](auto& obj) {
			using T = std::decay_t<decltype(obj)>;
			if constexpr (std::is_same_v<T, std::monostate>) {
				// no-op
			}
			else {
				obj.pass->Update(context);
			}
			}, pr.pass);
	}

	CompileFrame(device, context.frameIndex, context.hostData);
}

#define IFDEBUG(x) 

namespace {
	void ExecuteTransitions(std::vector<ResourceTransition>& transitions,
		CommandRecordingManager* crm,
		QueueKind queueKind,
		rhi::CommandList& commandList) {
		rhi::helpers::OwnedBarrierBatch batch;
		for (auto& transition : transitions) {
			std::vector<ResourceTransition> dummy;
			transition.pResource->GetStateTracker()->Apply(
				transition.range, transition.pResource,
				{ transition.newAccessType, transition.newLayout, transition.newSyncState }, dummy);
			auto bg = transition.pResource->GetEnhancedBarrierGroup(
				transition.range, transition.prevAccessType, transition.newAccessType,
				transition.prevLayout, transition.newLayout,
				transition.prevSyncState, transition.newSyncState);
			const size_t textureStart = batch.textures.size();
			batch.Append(bg);
			if (transition.discard) {
				for (size_t i = textureStart; i < batch.textures.size(); ++i) {
					batch.textures[i].discard = true;
				}
			}
		}
		if (!batch.Empty()) {
			commandList.Barriers(batch.View());
		}
	}

	template<typename PassT>
	void ExecutePasses(std::vector<PassT>& passes,
		CommandRecordingManager* crm,
		rhi::Queue& queue,
		QueueKind queueKind,
		rhi::CommandList& commandList,
		UINT64 fenceOffset,
		bool fenceSignal,
		UINT64 fenceValue,
		PassExecutionContext& context,
		rg::runtime::IStatisticsService* statisticsService) {
		std::vector<PassReturn> externalFences;
		context.commandList = commandList;
		for (auto& pr : passes) {
			if (pr.pass->IsInvalidated()) {
				rhi::debug::Scope scope(commandList, rhi::colors::Mint, pr.name.c_str());

				if (!pr.immediateBytecode.empty()) {
					rg::imm::Replay(pr.immediateBytecode, commandList);
				}

				if (statisticsService) {
					statisticsService->BeginQuery(pr.statisticsIndex, context.frameIndex, queue, commandList);
				}
				if ((pr.run & PassRunMask::Immediate) != PassRunMask::None) {
					rg::imm::Replay(pr.immediateBytecode, commandList); // Replay immediate-mode commands
				}

				// Drop immediate-mode keep-alive
				pr.immediateKeepAlive.reset();

				if ((pr.run & PassRunMask::Retained) != PassRunMask::None) {
					auto passReturn = pr.pass->Execute(context); // Execute retained-mode commands
					if (passReturn.fence) {
						externalFences.push_back(passReturn);
					}
				}
				if (statisticsService) {
					statisticsService->EndQuery(pr.statisticsIndex, context.frameIndex, queue, commandList);
				}
			}
		}
		if (statisticsService) {
			statisticsService->ResolveQueries(context.frameIndex, queue, commandList);
		}
		if (externalFences.size() > 0) {
			for (auto& fr : externalFences) {
				if (!fr.fence.has_value()) {
					spdlog::warn("Pass returned an external fence without a value. This should not happen.");
				}
				else {
					queue.Signal({ fr.fence.value().GetHandle(), fr.fenceValue });
				}
			}
		}
	}
} // namespace

void RenderGraph::Execute(PassExecutionContext& context) {
	ValidateCompiledResourceGenerations();

	bool useAsyncCompute = m_getUseAsyncCompute();
	auto& manager = DeviceManager::GetInstance();
	CommandRecordingManager::Init init{
		.graphicsQ = &manager.GetGraphicsQueue(),
		.graphicsF = &m_graphicsQueueFence.Get(),
		.graphicsPool = m_graphicsCommandListPool.get(),

		.computeQ = useAsyncCompute ? &manager.GetComputeQueue() : &manager.GetGraphicsQueue(),
		.computeF = useAsyncCompute ? &m_computeQueueFence.Get() : &m_graphicsQueueFence.Get(),
		.computePool = useAsyncCompute ? m_computeCommandListPool.get() : m_graphicsCommandListPool.get(),

		.copyQ = &manager.GetCopyQueue(),
		.copyF = &m_copyQueueFence.Get(),
		.copyPool = m_copyCommandListPool.get(),
		.computeMode = useAsyncCompute ? ComputeMode::Async : ComputeMode::AliasToGraphics
	};

	m_pCommandRecordingManager = std::make_unique<CommandRecordingManager>(init);
	auto crm = m_pCommandRecordingManager.get();

	auto graphicsQueue = crm->Queue(QueueKind::Graphics);
	auto computeQueue = crm->Queue(QueueKind::Compute);

	const bool alias = (computeQueue == graphicsQueue);
	auto WaitIfDistinct = [&](rhi::Queue* dstQ, rhi::Timeline& fence, UINT64 val) {
		if (!alias) dstQ->Wait({ fence.GetHandle(), val });
		};

	if (m_hasPendingFrameStartComputeWaitOnRender) {
		WaitIfDistinct(computeQueue, m_graphicsQueueFence.Get(), m_pendingFrameStartComputeWaitOnRenderFenceValue);
	}
	if (m_hasPendingFrameStartRenderWaitOnCompute) {
		WaitIfDistinct(graphicsQueue, m_computeQueueFence.Get(), m_pendingFrameStartRenderWaitOnComputeFenceValue);
	}

	UINT64 currentGraphicsQueueFenceOffset = m_graphicsQueueFenceValue * context.frameFenceValue;
	UINT64 currentComputeQueueFenceOffset = m_computeQueueFenceValue * context.frameFenceValue;

	for (const auto& [resourceID, producerBatch] : m_compiledLastRenderProducerBatchByResource) {
		(void)resourceID;
		if (producerBatch < batches.size()) {
			batches[producerBatch].renderCompletionSignal = true;
		}
	}

	if (alias) {
		for (const auto& [resourceID, producerBatch] : m_compiledLastComputeProducerBatchByResource) {
			(void)resourceID;
			if (producerBatch < batches.size()) {
				batches[producerBatch].renderCompletionSignal = true;
			}
		}
	}
	else {
		for (const auto& [resourceID, producerBatch] : m_compiledLastComputeProducerBatchByResource) {
			(void)resourceID;
			if (producerBatch < batches.size()) {
				batches[producerBatch].computeCompletionSignal = true;
			}
		}
	}

	auto nextLastProducerByResourceAcrossFrames = m_lastProducerByResourceAcrossFrames;
	auto nextLastAliasPlacementProducersByPoolAcrossFrames = m_lastAliasPlacementProducersByPoolAcrossFrames;

	auto removeResourceFromLastAliasPlacementCache = [&](uint64_t resourceID) {
		for (auto& [poolID, producers] : nextLastAliasPlacementProducersByPoolAcrossFrames) {
			(void)poolID;
			producers.erase(
				std::remove_if(
					producers.begin(),
					producers.end(),
					[&](const LastAliasPlacementProducerAcrossFrames& p) {
						return p.resourceID == resourceID;
					}),
				producers.end());
		}
	};

	auto publishAliasPlacementProducer = [&](uint64_t resourceID, LastProducerAcrossFrames producer) {
		removeResourceFromLastAliasPlacementCache(resourceID);

		auto itPlacement = aliasPlacementRangesByID.find(resourceID);
		if (itPlacement == aliasPlacementRangesByID.end()) {
			return;
		}

		auto itPoolState = persistentAliasPools.find(itPlacement->second.poolID);
		if (itPoolState == persistentAliasPools.end()) {
			return;
		}

		nextLastAliasPlacementProducersByPoolAcrossFrames[itPlacement->second.poolID].push_back(
			LastAliasPlacementProducerAcrossFrames{
				.resourceID = resourceID,
				.poolID = itPlacement->second.poolID,
				.poolGeneration = itPoolState->second.generation,
				.startByte = itPlacement->second.startByte,
				.endByte = itPlacement->second.endByte,
				.producer = producer,
			});
	};

	auto* statisticsService = m_statisticsService.get();

	unsigned int batchIndex = 0;
	for (auto& batch : batches) {

		if (batch.computeQueueWaitOnRenderQueueBeforeTransition) {
			WaitIfDistinct(computeQueue, m_graphicsQueueFence.Get(),
				currentGraphicsQueueFenceOffset +
				batch.computeQueueWaitOnRenderQueueBeforeTransitionFenceValue);
		}

		auto computeCommandList = crm->EnsureOpen(QueueKind::Compute, context.frameIndex); // TODO: Frame index or frame #?

		ExecuteTransitions(batch.computeTransitions,
			crm,
			QueueKind::Compute,
			computeCommandList);

		if (batch.computeQueueWaitOnRenderQueueBeforeExecution) {
			WaitIfDistinct(computeQueue, m_graphicsQueueFence.Get(),
				currentGraphicsQueueFenceOffset +
				batch.computeQueueWaitOnRenderQueueBeforeExecutionFenceValue);
		}

		if (batch.computeTransitionSignal && !alias) {
			UINT64 signalValue = currentComputeQueueFenceOffset + batch.computeTransitionFenceValue;
			crm->Flush(QueueKind::Compute, { true, signalValue });
		}

		ExecutePasses(batch.computePasses,
			crm,
			*computeQueue,
			QueueKind::Compute,
			computeCommandList,
			currentComputeQueueFenceOffset,
			batch.computeCompletionSignal,
			batch.computeCompletionFenceValue,
			context,
			statisticsService);

		if (batch.computeCompletionSignal && !alias) {
			UINT64 signalValue = currentComputeQueueFenceOffset + batch.computeCompletionFenceValue;
			crm->Flush(QueueKind::Compute, { true, signalValue });
		}

		if (batch.renderQueueWaitOnComputeQueueBeforeTransition) {
			WaitIfDistinct(graphicsQueue, m_computeQueueFence.Get(),
				currentComputeQueueFenceOffset +
				batch.renderQueueWaitOnComputeQueueBeforeTransitionFenceValue);
		}

		auto graphicsCommandList = crm->EnsureOpen(QueueKind::Graphics, context.frameIndex);

		ExecuteTransitions(batch.renderTransitions,
			crm,
			QueueKind::Graphics,
			graphicsCommandList);

		if (batch.renderTransitionSignal && !alias) {
			UINT64 signalValue = currentGraphicsQueueFenceOffset + batch.renderTransitionFenceValue;
			crm->Flush(QueueKind::Graphics, { true, signalValue });
		}

		if (batch.renderQueueWaitOnComputeQueueBeforeExecution) {
			WaitIfDistinct(graphicsQueue, m_computeQueueFence.Get(),
				currentComputeQueueFenceOffset +
				batch.renderQueueWaitOnComputeQueueBeforeExecutionFenceValue);
		}

		bool signalNow = batch.batchEndTransitions.size() == 0 && batch.renderCompletionSignal ? true : false;

		ExecutePasses(batch.renderPasses,
			crm,
			*graphicsQueue,
			QueueKind::Graphics,
			graphicsCommandList,
			currentGraphicsQueueFenceOffset,
			signalNow,
			batch.renderCompletionFenceValue,
			context,
			statisticsService);

		if (batch.renderCompletionSignal && signalNow) {
			UINT64 signalValue = currentGraphicsQueueFenceOffset + batch.renderCompletionFenceValue;
			crm->Flush(QueueKind::Graphics, { true, signalValue });
		}

		if (batch.batchEndTransitions.size() > 0) {
			ExecuteTransitions(batch.batchEndTransitions,
				crm,
				QueueKind::Graphics,
				graphicsCommandList);

			if (batch.renderCompletionSignal) {
				UINT64 signalValue = currentGraphicsQueueFenceOffset + batch.renderCompletionFenceValue;
				crm->Flush(QueueKind::Graphics, { true, signalValue });
			}
		}
		++batchIndex;
	}

	for (const auto& [resourceID, producerBatch] : m_compiledLastRenderProducerBatchByResource) {
		if (producerBatch >= batches.size()) {
			continue;
		}

		const uint64_t fenceValue = currentGraphicsQueueFenceOffset + batches[producerBatch].renderCompletionFenceValue;
		LastProducerAcrossFrames producer{
			.queue = CommandQueueType::Graphics,
			.fenceValue = fenceValue,
		};
		nextLastProducerByResourceAcrossFrames[resourceID] = producer;
		publishAliasPlacementProducer(resourceID, producer);
	}

	if (alias) {
		for (const auto& [resourceID, producerBatch] : m_compiledLastComputeProducerBatchByResource) {
			if (producerBatch >= batches.size()) {
				continue;
			}

			const uint64_t fenceValue = currentGraphicsQueueFenceOffset + batches[producerBatch].renderCompletionFenceValue;
			LastProducerAcrossFrames producer{
				.queue = CommandQueueType::Graphics,
				.fenceValue = fenceValue,
			};
			nextLastProducerByResourceAcrossFrames[resourceID] = producer;
			publishAliasPlacementProducer(resourceID, producer);
		}
	}
	else {
		for (const auto& [resourceID, producerBatch] : m_compiledLastComputeProducerBatchByResource) {
			if (producerBatch >= batches.size()) {
				continue;
			}

			const uint64_t fenceValue = currentComputeQueueFenceOffset + batches[producerBatch].computeCompletionFenceValue;
			LastProducerAcrossFrames producer{
				.queue = CommandQueueType::Compute,
				.fenceValue = fenceValue,
			};
			nextLastProducerByResourceAcrossFrames[resourceID] = producer;
			publishAliasPlacementProducer(resourceID, producer);
		}
	}

	m_lastProducerByResourceAcrossFrames = std::move(nextLastProducerByResourceAcrossFrames);
	m_lastAliasPlacementProducersByPoolAcrossFrames = std::move(nextLastAliasPlacementProducersByPoolAcrossFrames);
	DeletionManager::GetInstance().ProcessDeletions();
	crm->Flush(QueueKind::Graphics, { false, 0 });
	crm->Flush(QueueKind::Compute, { false, 0 });
	crm->EndFrame();
}

bool RenderGraph::IsNewBatchNeeded(
	const std::vector<ResourceRequirement>& reqs,
	const std::vector<std::pair<ResourceHandleAndRange, ResourceState>> passInternalTransitions,
	const std::unordered_map<uint64_t, SymbolicTracker*>& passBatchTrackers,
	const std::unordered_set<uint64_t>& currentBatchInternallyTransitionedResources,
	const std::unordered_set<uint64_t>& currentBatchAllResources,
	const std::unordered_set<uint64_t>& otherQueueUAVs)
{
	// For each internally modified resource
	for (auto const& r : passInternalTransitions) {
		auto id = r.first.resource.GetGlobalResourceID();
		// If this resource is used in the current batch, we need a new one
		if (currentBatchAllResources.contains(id)) {
			return true;
		}
	}

	// For each subresource requirement in this pass:
	for (auto const& r : reqs) {

		uint64_t id = r.resourceHandleAndRange.resource.GetGlobalResourceID();

		// If this resource is internally modified in the current batch, we need a new one
		if (currentBatchInternallyTransitionedResources.contains(id)) {
			return true;
		}

		ResourceState wantState{ r.state.access, r.state.layout, r.state.sync };

		// Changing state?
		auto it = passBatchTrackers.find(id);
		if (it != passBatchTrackers.end()) {
			if (it->second->WouldModify(r.resourceHandleAndRange.range, wantState))
				return true;
		}
		// first-use in this batch never forces a split.

		// Cross-queue UAV hazard?
		if ((r.state.access & rhi::ResourceAccessType::UnorderedAccess)
			&& otherQueueUAVs.contains(id))
			return true;
		if (r.state.layout == rhi::ResourceLayout::UnorderedAccess
			&& otherQueueUAVs.contains(id))
			return true;
	}
	return false;
}

//void RenderGraph::ComputeResourceLoops() {
//	PassBatch loopBatch;
//
//	RangeSpec whole{};
//
//	constexpr ResourceState flushState{
//		rhi::ResourceAccessType::Common,
//		rhi::ResourceLayout::Common,
//		rhi::ResourceSyncState::All
//	};
//
//	for (auto& [id, tracker] : trackers) {
//		auto itRes = resourcesByID.find(id);
//		if (itRes == resourcesByID.end())
//			continue;  // no pointer for this ID? skip
//
//		auto const& pRes = itRes->second;
//
//		tracker->Apply(
//			whole, // covers all mips & slices
//			pRes.get(),
//			flushState,    // the state were flushing to
//			loopBatch.renderTransitions            // collects all transitions
//		);
//	}
//	batches.push_back(std::move(loopBatch));
//}

void RenderGraph::RegisterProvider(IResourceProvider* prov) {
	auto keys = prov->GetSupportedKeys();
	for (const auto& key : keys) {
		if (_providerMap.find(key) != _providerMap.end()) {
			std::string_view name = key.ToString();
			throw std::runtime_error("Resource provider already registered for key: " + std::string(name));
		}
		_providerMap[key] = prov;
	}
	_providers.push_back(prov);

	for (const auto& key : prov->GetSupportedKeys()) {
		auto resource = prov->ProvideResource(key);
		if (resource) {
			RegisterResource(key, resource, prov);
		}
		else {
			spdlog::warn("Provider returned null for advertised key: {}", key.ToString());
		}
	}

	// Register resolvers from this provider
	for (const auto& key : prov->GetSupportedResolverKeys()) {
		if (const auto resolver = prov->ProvideResolver(key); resolver) {
			RegisterResolver(key, resolver);
		}
		else {
			spdlog::warn("Provider returned null resolver for advertised key: {}", key.ToString());
		}
	}
}

void RenderGraph::RegisterResolver(ResourceIdentifier id, const std::shared_ptr<IResourceResolver>& resolver) {
	if (_resolverMap.contains(id)) {
		throw std::runtime_error("Resolver already registered for key: " + id.ToString());
	}
	// Resolve it and register its resources
	for (const auto& resource : resolver->Resolve()) {
		if (resource) {
			resourcesByID[resource->GetGlobalResourceID()] = resource;
			// Anonymous registration
			_registry.RegisterAnonymous(resource);
		}
	}
	_resolverMap[id] = resolver;
	_registry.RegisterResolver(id, resolver);
}

std::shared_ptr<IResourceResolver> RenderGraph::RequestResolver(ResourceIdentifier const& rid, bool allowFailure) {
	if (auto it = _resolverMap.find(rid); it != _resolverMap.end()) {
		return it->second;
	}

	if (allowFailure) return nullptr;
	throw std::runtime_error("No resolver registered for key: " + rid.ToString());
}

void RenderGraph::RegisterResource(ResourceIdentifier id, std::shared_ptr<Resource> resource,
	IResourceProvider* provider) {

	auto key = _registry.RegisterOrUpdate(id, resource);
	AddResource(resource);
	if (provider) {
		_providerMap[id] = provider;
	}

	// If resource can be cast to IHasMemoryMetadata, tag it with this ResouceIdentifier
	if (const auto hasMemoryMetadata = std::dynamic_pointer_cast<IHasMemoryMetadata>(resource); hasMemoryMetadata) {
		hasMemoryMetadata->ApplyMetadataComponentBundle(EntityComponentBundle().Set<ResourceIdentifier>(id));
	}
}

std::shared_ptr<Resource> RenderGraph::RequestResourcePtr(ResourceIdentifier const& rid, bool allowFailure) {
	// If it's already in our registry, return it
	auto cached = _registry.RequestShared(rid);
	if (cached) {
		return cached;
	}

	// We don't have it in our registry, check if we have a provider for it
	auto providerIt = _providerMap.find(rid);
	if (providerIt != _providerMap.end()) {
		// If we have a provider for this key, use it to provide the resource
		auto provider = providerIt->second;
		if (provider) {
			auto resource = provider->ProvideResource(rid);
			if (resource) {
				// Register the resource in our registry
				_registry.RegisterOrUpdate(rid, resource);
				AddResource(resource);
				return resource;
			}
			else {
				throw std::runtime_error("Provider returned null for key: " + rid.ToString());
			}
		}
	}

	// No provider registered for this key
	if (allowFailure) {
		// If we are allowed to fail, return nullptr
		return nullptr;
	}
	throw std::runtime_error("No resource provider registered for key: " + rid.ToString());
}

ResourceRegistry::RegistryHandle RenderGraph::RequestResourceHandle(ResourceIdentifier const& rid, bool allowFailure) {
	// If it's already in our registry, return it
	auto cached = _registry.GetHandleFor(rid);
	if (cached.has_value()) {
		return cached.value();
	}

	// We don't have it in our registry, check if we have a provider for it
	auto providerIt = _providerMap.find(rid);
	if (providerIt != _providerMap.end()) {
		// If we have a provider for this key, use it to provide the resource
		auto provider = providerIt->second;
		if (provider) {
			auto resource = provider->ProvideResource(rid);
			if (resource) {
				// Register the resource in our registry
				_registry.RegisterOrUpdate(rid, resource);
				AddResource(resource);
				return _registry.GetHandleFor(rid).value();
			}
			else {
				throw std::runtime_error("Provider returned null for key: " + rid.ToString());
			}
		}
	}

	// No provider registered for this key
	if (allowFailure) {
		// If we are allowed to fail, return nullptr
		return {};
	}
	throw std::runtime_error("No resource provider registered for key: " + rid.ToString());
}

ResourceRegistry::RegistryHandle RenderGraph::RequestResourceHandle(Resource* const& pResource, bool allowFailure) {
	if (!pResource) {
		if (allowFailure) {
			return {};
		}
		throw std::runtime_error("Null resource pointer passed to RequestResourceHandle(Resource*)");
	}

	auto ensureTrackedInGraph = [&]() {
		if (resourcesByID.contains(pResource->GetGlobalResourceID())) {
			return;
		}

		auto shared = pResource->weak_from_this().lock();
		if (shared) {
			AddResource(std::move(shared));
		}
	};

	// If it's already in our registry, return it
	auto cached = _registry.GetHandleFor(pResource);
	if (cached.has_value()) {
		if (_registry.IsValid(cached.value())) {
			ensureTrackedInGraph();
			return cached.value();
		}

		spdlog::warn(
			"Stale cached registry handle for resource '{}' (id={}) detected; reminting anonymous handle.",
			pResource ? pResource->GetName() : std::string("<null>"),
			pResource ? pResource->GetGlobalResourceID() : 0ull);

		// Fall through and remint a fresh handle for this live resource pointer.
		// This can happen if a resource was replaced but an old reverse-map entry remained.
		const auto reminted = _registry.RegisterAnonymousWeak(pResource->weak_from_this());
		ensureTrackedInGraph();
		return reminted;
	}

	if (allowFailure) {
		return {};
	}

	// Register anonymous resource
	const auto handle = _registry.RegisterAnonymousWeak(pResource->weak_from_this());
	ensureTrackedInGraph();

	return handle;
}


ComputePassBuilder& RenderGraph::BuildComputePass(std::string const& name) {
	if (auto it = m_passBuildersByName.find(name); it != m_passBuildersByName.end()) {
		if (m_passNamesSeenThisReset.contains(name)) {
			throw std::runtime_error("Pass names must be unique.");
		}
		if (it->second->Kind() != PassBuilderKind::Compute) {
			throw std::runtime_error("Pass builder name collision (render vs compute): " + name);
		}
		m_passBuilderOrder.push_back(it->second.get());
		return static_cast<ComputePassBuilder&>(*(it->second));
	}
	m_passNamesSeenThisReset.insert(name);
	auto ptr = std::unique_ptr<ComputePassBuilder>(new ComputePassBuilder(this, name));
	m_passBuilderOrder.push_back(ptr.get());
	m_passBuildersByName.emplace(name, std::move(ptr));
	return static_cast<ComputePassBuilder&>(*(m_passBuildersByName[name]));
}
RenderPassBuilder& RenderGraph::BuildRenderPass(std::string const& name) {
	if (auto it = m_passBuildersByName.find(name); it != m_passBuildersByName.end()) {
		if (m_passNamesSeenThisReset.contains(name)) {
			throw std::runtime_error("Pass names must be unique.");
		}
		if (it->second->Kind() != PassBuilderKind::Render) {
			throw std::runtime_error("Pass builder name collision (render vs compute): " + name);
		}
		m_passBuilderOrder.push_back(it->second.get());
		return static_cast<RenderPassBuilder&>(*(it->second));
	}
	m_passNamesSeenThisReset.insert(name);
	auto ptr = std::unique_ptr<RenderPassBuilder>(new RenderPassBuilder(this, name));
	m_passBuilderOrder.push_back(ptr.get());
	m_passBuildersByName.emplace(name, std::move(ptr));
	return static_cast<RenderPassBuilder&>(*(m_passBuildersByName[name]));
}

//void RenderGraph::RegisterPassBuilder(RenderPassBuilder&& builder) {
//	m_passBuildersByName[builder.passName] = std::move(builder);
//}
//void RenderGraph::RegisterPassBuilder(ComputePassBuilder&& builder) {
//	m_passBuildersByName[builder.passName] = std::move(builder);
//}