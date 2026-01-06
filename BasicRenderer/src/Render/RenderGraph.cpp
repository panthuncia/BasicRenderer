#include "Render/RenderGraph.h"

#include <span>
#include <rhi_helpers.h>
#include <rhi_debug.h>

#include "Render/RenderContext.h"
#include "Utilities/Utilities.h"
#include "Managers/Singletons/SettingsManager.h"
#include "Managers/Singletons/ReadbackManager.h"
#include "Managers/Singletons/DeviceManager.h"
#include "Render/PassBuilders.h"
#include "Resources/ResourceGroup.h"
#include "Managers/Singletons/StatisticsManager.h"
#include "Managers/CommandRecordingManager.h"
#include "Interfaces/IHasMemoryMetadata.h"

// BFS over alias and group/child relationships to get all relevant IDs
std::vector<uint64_t> RenderGraph::ExpandSchedulingIDs(uint64_t id) const {
	std::vector<uint64_t> out;
	out.reserve(8);

	std::unordered_set<uint64_t> visited;
	std::vector<uint64_t> stack;
	stack.push_back(id);

	auto push = [&](uint64_t x) {
		if (visited.insert(x).second) stack.push_back(x);
		};

	while (!stack.empty()) {
		uint64_t cur = stack.back();
		stack.pop_back();

		// alias closure
		for (uint64_t a : GetAllAliasIDs(cur)) {
			if (visited.insert(a).second) {
				out.push_back(a);
			}
		}
	}

	return out;
}

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

			for (uint64_t rid : rg.ExpandSchedulingIDs(base)) {
				mark(rid, write ? AccessKind::Write : AccessKind::Read, isUav);
			}
		}

		// internal transitions: treat as "write" for scheduling conservatism
		for (auto& tr : *view.internalTransitions) {
			uint64_t base = tr.first.resource.GetGlobalResourceID();
			for (uint64_t rid : rg.ExpandSchedulingIDs(base)) {
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
	std::unordered_map<uint64_t, SeqState> seq;
	seq.reserve(4096);

	std::unordered_set<uint64_t> edgeSet;
	edgeSet.reserve(nodes.size() * 8);

	// IMPORTANT: build deps in ORIGINAL order
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
			exit.first.resource.GetStateTracker()->Apply(
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
			exit.first.resource.GetStateTracker()->Apply(
				exit.first.range, nullptr, exit.second, _);
			currentBatch.internallyTransitionedResources.insert(exit.first.resource.GetGlobalResourceID());
		}

		for (auto& req : pass.resources.frameResourceRequirements) {
			uint64_t id = req.resourceHandleAndRange.resource.GetGlobalResourceID();
			currentBatch.allResources.insert(id);
			batchOfLastRenderQueueUsage[id] = currentBatchIndex;
		}

		// NEW:
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
	unsigned int currentBatchIndex = 0;

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

			// Critical path tie-break
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
			// (Avoid pushing empty batches if that can happen)
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
}


// Factory for the transition lambda
void RenderGraph::AddTransition(
	std::unordered_map<uint64_t, unsigned int>&  batchOfLastRenderQueueUsage,
	unsigned int batchIndex,
	PassBatch& currentBatch,
	bool isComputePass, 
	const ResourceRequirement& r,
	std::unordered_set<uint64_t>& outTransitionedResourceIDs)
{

	auto& resource = r.resourceHandleAndRange.resource;
	std::vector<ResourceTransition> transitions;
	auto pRes = _registry.Resolve(resource); // TODO: Can we get rid of pRes in transitions?

	resource.GetStateTracker()->Apply(r.resourceHandleAndRange.range, pRes, r.state, transitions);

	if (!transitions.empty()) {
		outTransitionedResourceIDs.insert(resource.GetGlobalResourceID());
	}

	currentBatch.passBatchTrackers[resource.GetGlobalResourceID()] = resource.GetStateTracker(); // We will need to chack subsequent passes against this

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

RenderGraph::RenderGraph() {
	UploadManager::GetInstance().SetUploadResolveContext({ &_registry });

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
}

RenderGraph::~RenderGraph() {
	m_pCommandRecordingManager->ShutdownThreadLocal(); // Clears thread-local storage
}

void RenderGraph::ResetForRecompile()
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
	aliasGroups.clear();
	resourceToAliasGroup.clear();
	aliasedResources.clear();
	lastActiveSubresourceInAliasGroup.clear();
	trackers.clear();

	// Clear resources
	resourcesByID.clear();
	resourcesByName.clear();

	// Clear providers
	_providerMap.clear();
	_providers.clear();
	_resolverMap.clear();
	_registry = ResourceRegistry();

	// Register new registry with upload manager
	UploadManager::GetInstance().SetUploadResolveContext({&_registry});

	// reset pass builders and clear pass ordering
	for (auto& [name, builder] : m_passBuildersByName) {
		builder->Reset();
	}
	m_passBuilderOrder.clear();
	m_passNamesSeenThisReset.clear();

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

	// Manage aliased resources 

	// Mark resources that use the same memory as each other, as they need aliasing barriers
	for (const auto& resource : resourcesByID) {
		auto& r = resource.second;
		for (auto& alias : r->GetAliasedResources()) {
			if (!aliasedResources.contains(r->GetGlobalResourceID())) {
				aliasedResources[r->GetGlobalResourceID()] = {};
			}
			aliasedResources[r->GetGlobalResourceID()].insert(alias->GetGlobalResourceID());
		}
	}
	// Assemble alias groups
	{
		std::unordered_set<uint64_t> visited;
		for (auto& [rID, aliases] : aliasedResources) {
			if (visited.count(rID)) continue;
			// BFS-connected component
			std::vector<uint64_t> group;
			std::queue<uint64_t>  queue;
			queue.push(rID);
			visited.insert(rID);
			while (!queue.empty()) {
				uint64_t cur = queue.front(); queue.pop();
				group.push_back(cur);
				for (uint64_t other : aliasedResources[cur]) {
					if (!visited.contains(other)) {
						visited.insert(other);
						queue.push(other);
					}
				}
			}
			size_t idx = aliasGroups.size();
			for (uint64_t member : group)
				resourceToAliasGroup[member] = idx;
			aliasGroups.push_back(std::move(group));
		}
	}

	// Upload pass inserted at front
	if (const auto uploadPass = UploadManager::GetInstance().GetUploadPass(); uploadPass) {
		auto uploadBatch = PassBatch();
		RenderPassAndResources uploadPassAndResources;
		uploadPassAndResources.pass = uploadPass;
		AnyPassAndResources uploadAnyPassAndResources;
		uploadAnyPassAndResources.type = PassType::Render;
		uploadAnyPassAndResources.pass = uploadPassAndResources;
		m_masterPassList.insert(m_masterPassList.begin(), uploadAnyPassAndResources);
	}

	// Readback pass inserted at end
	if (const auto readbackPass = ReadbackManager::GetInstance().GetReadbackPass(); readbackPass) { // This pass uses the immediate-mode API to perform readbacks
		auto readbackBatch = PassBatch();
		RenderPassAndResources readbackPassAndResources;
		readbackPassAndResources.pass = readbackPass;
		AnyPassAndResources readbackAnyPassAndResources;
		readbackAnyPassAndResources.type = PassType::Render;
		readbackAnyPassAndResources.pass = readbackPassAndResources;
		m_masterPassList.push_back(readbackAnyPassAndResources);
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


void RenderGraph::CompileFrame(rhi::Device device, uint8_t frameIndex) {
	batches.clear();
	m_framePasses.clear(); // Combined retained + immediate-mode passes for this frame
	// initialize frame requirements to the retained requirements
	for (auto& pr : m_masterPassList) {
		if (pr.type == PassType::Compute) {
			auto& p = std::get<ComputePassAndResources>(pr.pass);
			p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;
			p.immediateBytecode.clear();
		}
		else {
			auto& p = std::get<RenderPassAndResources>(pr.pass);
			p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;
			p.immediateBytecode.clear();
		}
	}

	// Record immediate-mode commands + access for each pass and fold into per-frame requirements
	for (auto& pr : m_masterPassList) {

		if (pr.type == PassType::Compute) {
			auto& p = std::get<ComputePassAndResources>(pr.pass);

			// reset per-frame
			p.immediateBytecode.clear();
			p.resources.frameResourceRequirements = p.resources.staticResourceRequirements;

			ImmediateContext c{ device, 
				{/*isRenderPass=*/false,
				m_immediateDispatch,
				&ResolveByIdThunk,
				&ResolveByPtrThunk,
				this},
				frameIndex
			};

			// Record immediate-mode commands
			p.pass->ExecuteImmediate(c);

			auto immediateFrameData = c.list.Finalize();
			// If there is a conflict between retained and immediate requirements, split the pass
			bool conflict = RequirementsConflict(
				p.resources.staticResourceRequirements,
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

			ImmediateContext c{ device, 
				{/*isRenderPass=*/true,
				m_immediateDispatch,
				&ResolveByIdThunk,
				&ResolveByPtrThunk,
				this},
				frameIndex
			};
			p.pass->ExecuteImmediate(c);
			auto immediateFrameData = c.list.Finalize();

			bool conflict = RequirementsConflict(
				p.resources.staticResourceRequirements,
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

	lastActiveSubresourceInAliasGroup.clear();
	lastActiveSubresourceInAliasGroup.resize(aliasGroups.size());

	auto currentBatch = PassBatch();
	currentBatch.renderTransitionFenceValue = GetNextGraphicsQueueFenceValue();
	currentBatch.renderCompletionFenceValue = GetNextGraphicsQueueFenceValue();
	currentBatch.computeTransitionFenceValue = GetNextComputeQueueFenceValue();
	currentBatch.computeCompletionFenceValue = GetNextComputeQueueFenceValue();
	//std::unordered_map<std::wstring, ResourceState> previousBatchResourceStates;
 //   std::unordered_map<uint64_t, ResourceAccessType> finalResourceAccessTypes;
	//std::unordered_map<uint64_t, ResourceLayout> finalResourceLayouts;
	//std::unordered_map<uint64_t, ResourceSyncState> finalResourceSyncStates;

	//std::unordered_map<uint64_t, ResourceAccessType> firstResourceAccessTypes;
	//std::unordered_map<uint64_t, ResourceLayout> firstResourceLayouts;
	//std::unordered_map<uint64_t, ResourceSyncState> firstResourceSyncStates;

	std::unordered_set<uint64_t> computeUAVs;
	std::unordered_set<uint64_t> renderUAVs;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueTransition;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueTransition;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueProducer;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueProducer;

	std::unordered_map<uint64_t, unsigned int>  batchOfLastRenderQueueUsage;
	std::unordered_map<uint64_t, unsigned int>  batchOfLastComputeQueueUsage;

	auto nodes = BuildNodes(*this, m_framePasses);
	if (!BuildDependencyGraph(nodes)) {
		// Cycle detected
		spdlog::error("Render graph contains a dependency cycle! Render graph compilation failed.");
		throw std::runtime_error("Render graph contains a dependency cycle");
	}
	else {
		AutoScheduleAndBuildBatches(*this, m_framePasses, nodes);
	}

	// Insert transitions to loop resources back to their initial states
	ComputeResourceLoops();

	//// Readback pass in its own batch
	//auto readbackPass = ReadbackManager::GetInstance().GetReadbackPass();
	//if (readbackPass) {
	//	auto readbackBatch = PassBatch();
	//	RenderPassAndResources readbackPassAndResources; // ReadbackPass is a special-case pass which transitions resources internally
	//	readbackPassAndResources.pass = readbackPass;
	//	readbackBatch.renderPasses.push_back(readbackPassAndResources);
	//	batches.push_back(readbackBatch);
	//}

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
		// get this ID plus any aliases
		auto ids = GetAllAliasIDs(id);
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
		for (auto rid : GetAllAliasIDs(transitionID)) {
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
		for (auto rid : GetAllAliasIDs(id)) {
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
		for (auto rid : GetAllAliasIDs(transitionID)) {
			if (usageHistory.contains(rid)) {
				latestUsage = std::max(latestUsage, (int)usageHistory.at(rid));
			}
		}
	}

	return { latestTransition, latestProducer, latestUsage };
}

void RenderGraph::Setup() {
	// Setup the statistics manager
	auto& statisticsManager = StatisticsManager::GetInstance();
	statisticsManager.ClearAll();
	auto& manager = DeviceManager::GetInstance();
	for (auto& batch : batches) {
		for (auto& pass : batch.renderPasses) {
			if (pass.statisticsIndex == -1) {
				pass.statisticsIndex = statisticsManager.RegisterPass(pass.name);
				if (pass.resources.isGeometryPass) {
					statisticsManager.MarkGeometryPass(pass.name);
				}
			}
		}
		for (auto& pass : batch.computePasses) {
			if (pass.statisticsIndex == -1) {
				pass.statisticsIndex = statisticsManager.RegisterPass(pass.name);
			}
		}
	}
	statisticsManager.RegisterQueue(manager.GetGraphicsQueue().GetKind());
	statisticsManager.RegisterQueue(manager.GetComputeQueue().GetKind());
	statisticsManager.SetupQueryHeap();

	auto device = DeviceManager::GetInstance().GetDevice();

	m_graphicsCommandListPool = std::make_unique<CommandListPool>(device, rhi::QueueKind::Graphics);
	m_computeCommandListPool = std::make_unique<CommandListPool>(device, rhi::QueueKind::Compute);
	m_copyCommandListPool = std::make_unique<CommandListPool>(device, rhi::QueueKind::Copy);

	auto result = device.CreateTimeline(m_graphicsQueueFence);
	result = device.CreateTimeline(m_computeQueueFence);
	result = device.CreateTimeline(m_copyQueueFence);
	result = device.CreateTimeline(m_frameStartSyncFence);

	m_getUseAsyncCompute = SettingsManager::GetInstance().getSettingGetter<bool>("useAsyncCompute");

	std::vector<rhi::CommandList> emptyLists;
	for (auto& pass : m_masterPassList) {
		switch (pass.type) {
		case PassType::Render: {
			auto& renderPass = std::get<RenderPassAndResources>(pass.pass);
			renderPass.pass->SetResourceRegistryView(std::make_unique<ResourceRegistryView>(_registry, renderPass.resources.identifierSet));
			renderPass.pass->Setup();
			renderPass.pass->RegisterCommandLists(emptyLists);
			break;
		}
		case PassType::Compute: {
			auto& computePass = std::get<ComputePassAndResources>(pass.pass);
			computePass.pass->SetResourceRegistryView(std::make_unique<ResourceRegistryView>(_registry, computePass.resources.identifierSet));
			computePass.pass->Setup();
			computePass.pass->RegisterCommandLists(emptyLists);
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
	trackers[resource->GetGlobalResourceID()] = resource->GetStateTracker();
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
    if (renderPassesByName.find(name)!= renderPassesByName.end()) {
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

void RenderGraph::Update() {
    for (auto& batch : batches) {
        for (auto& passAndResources : batch.renderPasses) {
            passAndResources.pass->Update();
        }
        for (auto& passAndResources : batch.computePasses) {
            passAndResources.pass->Update();
        }
    }
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
			batch.Append(bg);
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
		RenderContext& context,
		StatisticsManager& statisticsManager) {
		std::vector<PassReturn> externalFences;
		context.commandList = commandList;
		for (auto& pr : passes) {
			if (pr.pass->IsInvalidated()) {
				rhi::debug::Scope scope(commandList, rhi::colors::Mint, pr.name.c_str());

				if (!pr.immediateBytecode.empty()) {
					rg::imm::Replay(pr.immediateBytecode, commandList);
				}

				statisticsManager.BeginQuery(pr.statisticsIndex, context.frameIndex, queue, commandList);
				rg::imm::Replay(pr.immediateBytecode, commandList); // Replay immediate-mode commands

				// Drop immediate-mode keep-alive
				pr.immediateKeepAlive.reset();

				auto passReturn = pr.pass->Execute(context); // Execute retained-mode commands
				statisticsManager.EndQuery(pr.statisticsIndex, context.frameIndex, queue, commandList);
				if (passReturn.fence) {
					externalFences.push_back(passReturn);
				}
			}
		}
		statisticsManager.ResolveQueries(context.frameIndex, queue, commandList);
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

void RenderGraph::Execute(RenderContext& context) {

	CompileFrame(context.device, context.frameIndex);

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
		if (!alias) dstQ->Wait({ fence.GetHandle(), val});
		};

	UINT64 currentGraphicsQueueFenceOffset = m_graphicsQueueFenceValue * context.frameFenceValue;
	UINT64 currentComputeQueueFenceOffset = m_computeQueueFenceValue * context.frameFenceValue;

	if (!alias) { // TODO: This is needed to sync with earlier UploadManager copies. We should really include UploadManager copies as part of the graph.
		graphicsQueue->Signal({ m_frameStartSyncFence->GetHandle(), context.frameFenceValue });
		computeQueue->Wait({ m_frameStartSyncFence->GetHandle(), context.frameFenceValue });
	}

	auto& statisticsManager = StatisticsManager::GetInstance();

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
			statisticsManager);

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
			statisticsManager);

		if (batch.renderCompletionSignal && signalNow && !alias) {
			UINT64 signalValue = currentGraphicsQueueFenceOffset + batch.renderCompletionFenceValue;
			crm->Flush(QueueKind::Graphics, { true, signalValue });
		}

		if (batch.batchEndTransitions.size() > 0) {
			ExecuteTransitions(batch.batchEndTransitions, 
				crm,
				QueueKind::Graphics,
				graphicsCommandList);

			if (!alias) {
				UINT64 signalValue = currentGraphicsQueueFenceOffset + batch.renderCompletionFenceValue;
				crm->Flush(QueueKind::Graphics, { true, signalValue });
			}
		}
		++batchIndex;
	}
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
	for (auto const &r : reqs) {

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


void RenderGraph::ComputeResourceLoops() {
	PassBatch loopBatch;

	RangeSpec whole{};  

	constexpr ResourceState flushState {
		rhi::ResourceAccessType::Common,
		rhi::ResourceLayout::Common,
		rhi::ResourceSyncState::All
	};

	for (auto& [id, tracker] : trackers) {
		auto itRes = resourcesByID.find(id);
		if (itRes == resourcesByID.end())
			continue;  // no pointer for this ID? skip

		auto const& pRes = itRes->second;

		tracker->Apply(
			whole, // covers all mips & slices
			pRes.get(),
			flushState,    // the state were flushing to
			loopBatch.renderTransitions            // collects all transitions
		);
	}
	batches.push_back(std::move(loopBatch));
}

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
	// If it's already in our registry, return it
	auto cached = _registry.GetHandleFor(pResource);
	if (cached.has_value()) {
		return cached.value();
	}

	// Register anonymous resource
	const auto handle = _registry.RegisterAnonymous(pResource->shared_from_this());
	 
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