#include "Mesh/VoxelGroupBuilder.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

namespace
{
	// Helpers

	struct Float3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;
		Float3() = default;
		Float3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
		Float3 operator+(const Float3& o) const { return { x + o.x, y + o.y, z + o.z }; }
		Float3 operator-(const Float3& o) const { return { x - o.x, y - o.y, z - o.z }; }
		Float3 operator*(float s) const { return { x * s, y * s, z * s }; }
		float  dot(const Float3& o) const { return x * o.x + y * o.y + z * o.z; }
		Float3 cross(const Float3& o) const
		{
			return { y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x };
		}
		float lengthSq() const { return dot(*this); }
		float length() const { return std::sqrt(lengthSq()); }
		Float3 normalized() const
		{
			float len = length();
			return len > 1e-12f ? (*this) * (1.0f / len) : Float3(0.0f, 0.0f, 1.0f);
		}
	};

	Float3 ReadPosition(const std::vector<std::byte>& vertices, size_t stride, uint32_t index)
	{
		Float3 p;
		const size_t offset = static_cast<size_t>(index) * stride;
		std::memcpy(&p.x, vertices.data() + offset, sizeof(float));
		std::memcpy(&p.y, vertices.data() + offset + sizeof(float), sizeof(float));
		std::memcpy(&p.z, vertices.data() + offset + sizeof(float) * 2, sizeof(float));
		return p;
	}

	Float3 ToFloat3(const DirectX::XMFLOAT3& v) { return { v.x, v.y, v.z }; }
	DirectX::XMFLOAT3 ToXM(const Float3& v) { return { v.x, v.y, v.z }; }

	// Cell-key packing (supports resolutions up to 65535)
	uint64_t PackCell(uint32_t cx, uint32_t cy, uint32_t cz)
	{
		return static_cast<uint64_t>(cx) |
			(static_cast<uint64_t>(cy) << 16) |
			(static_cast<uint64_t>(cz) << 32);
	}

	void UnpackCell(uint64_t key, uint32_t& cx, uint32_t& cy, uint32_t& cz)
	{
		cx = static_cast<uint32_t>(key & 0xFFFF);
		cy = static_cast<uint32_t>((key >> 16) & 0xFFFF);
		cz = static_cast<uint32_t>((key >> 32) & 0xFFFF);
	}

	// Grid coordinate helpers
	uint32_t ToCellCoord(float val, float minVal, float invCellSize, uint32_t res)
	{
		int32_t c = static_cast<int32_t>(std::floor((val - minVal) * invCellSize));
		return static_cast<uint32_t>(std::max(0, std::min(c, static_cast<int32_t>(res) - 1)));
	}

	// Triangle-AABB overlap test (Separating Axis Theorem — Akenine-Möller method)
	bool TriangleAABBOverlap(const Float3& v0, const Float3& v1, const Float3& v2,
		const Float3& boxCenter, const Float3& boxHalfSize)
	{
		const Float3 a = v0 - boxCenter;
		const Float3 b = v1 - boxCenter;
		const Float3 c = v2 - boxCenter;

		const Float3 edges[3] = { b - a, c - b, a - c };

		const Float3 boxAxes[3] = { {1,0,0}, {0,1,0}, {0,0,1} };
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 3; ++j)
			{
				Float3 axis = edges[i].cross(boxAxes[j]);
				if (axis.lengthSq() < 1e-12f)
					continue;
				float pa = a.dot(axis), pb = b.dot(axis), pc = c.dot(axis);
				float triMin = std::min({ pa, pb, pc });
				float triMax = std::max({ pa, pb, pc });
				float r = boxHalfSize.x * std::abs(axis.x) +
					boxHalfSize.y * std::abs(axis.y) +
					boxHalfSize.z * std::abs(axis.z);
				if (triMin > r || triMax < -r)
					return false;
			}
		}

		if (std::min({ a.x, b.x, c.x }) > boxHalfSize.x || std::max({ a.x, b.x, c.x }) < -boxHalfSize.x) return false;
		if (std::min({ a.y, b.y, c.y }) > boxHalfSize.y || std::max({ a.y, b.y, c.y }) < -boxHalfSize.y) return false;
		if (std::min({ a.z, b.z, c.z }) > boxHalfSize.z || std::max({ a.z, b.z, c.z }) < -boxHalfSize.z) return false;

		Float3 triNormal = edges[0].cross(edges[1]);
		float d = triNormal.dot(a);
		float r = boxHalfSize.x * std::abs(triNormal.x) +
			boxHalfSize.y * std::abs(triNormal.y) +
			boxHalfSize.z * std::abs(triNormal.z);
		if (d > r || d < -r)
			return false;

		return true;
	}

	// AABB-AABB overlap test (for box-to-grid rasterization)
	bool AABBOverlap(const Float3& aMin, const Float3& aMax,
		const Float3& bMin, const Float3& bMax)
	{
		if (aMax.x < bMin.x || aMin.x > bMax.x) return false;
		if (aMax.y < bMin.y || aMin.y > bMax.y) return false;
		if (aMax.z < bMin.z || aMin.z > bMax.z) return false;
		return true;
	}

	// Möller–Trumbore ray-triangle intersection
	bool RayTriangleIntersect(const Float3& origin, const Float3& dir,
		const Float3& v0, const Float3& v1, const Float3& v2,
		float tMax)
	{
		constexpr float kEpsilon = 1e-8f;
		Float3 e1 = v1 - v0;
		Float3 e2 = v2 - v0;
		Float3 h = dir.cross(e2);
		float a = e1.dot(h);
		if (std::abs(a) < kEpsilon)
			return false;

		float f = 1.0f / a;
		Float3 s = origin - v0;
		float u = f * s.dot(h);
		if (u < 0.0f || u > 1.0f)
			return false;

		Float3 q = s.cross(e1);
		float v = f * dir.dot(q);
		if (v < 0.0f || u + v > 1.0f)
			return false;

		float t = f * e2.dot(q);
		return (t > kEpsilon && t < tMax);
	}

	// Ray-AABB slab intersection (for voxel-to-voxel ray tracing)
	bool RayAABBIntersect(const Float3& origin, const Float3& invDir,
		const Float3& boxMin, const Float3& boxMax,
		float tMax)
	{
		float t1 = (boxMin.x - origin.x) * invDir.x;
		float t2 = (boxMax.x - origin.x) * invDir.x;
		float tmin = std::min(t1, t2);
		float tmax = std::max(t1, t2);

		t1 = (boxMin.y - origin.y) * invDir.y;
		t2 = (boxMax.y - origin.y) * invDir.y;
		tmin = std::max(tmin, std::min(t1, t2));
		tmax = std::min(tmax, std::max(t1, t2));

		t1 = (boxMin.z - origin.z) * invDir.z;
		t2 = (boxMax.z - origin.z) * invDir.z;
		tmin = std::max(tmin, std::min(t1, t2));
		tmax = std::min(tmax, std::max(t1, t2));

		return tmax >= std::max(tmin, 1e-8f) && tmin < tMax;
	}

	// Deterministic ray set generation for a unit cube [0,1]^3
	struct Ray
	{
		Float3 origin;
		Float3 direction;
	};

	std::vector<Ray> GenerateCellRays(uint32_t rayCount, uint32_t seed)
	{
		std::mt19937 rng(seed);
		std::uniform_real_distribution<float> dist(0.0f, 1.0f);

		std::vector<Ray> rays;
		rays.reserve(rayCount);

		const uint32_t raysPerAxis = std::max(1u, rayCount / 3u);
		const uint32_t axisRayCounts[3] = {
			raysPerAxis,
			raysPerAxis,
			rayCount - 2u * raysPerAxis
		};

		for (uint32_t axis = 0; axis < 3; ++axis)
		{
			for (uint32_t i = 0; i < axisRayCounts[axis]; ++i)
			{
				Float3 origin{};
				Float3 direction{};

				float u = dist(rng);
				float v = dist(rng);

				switch (axis)
				{
				case 0:
					origin = { 0.0f, u, v };
					direction = { 1.0f, 0.0f, 0.0f };
					break;
				case 1:
					origin = { u, 0.0f, v };
					direction = { 0.0f, 1.0f, 0.0f };
					break;
				case 2:
					origin = { u, v, 0.0f };
					direction = { 0.0f, 0.0f, 1.0f };
					break;
				}

				rays.push_back({ origin, direction });
			}
		}

		return rays;
	}

	// Map ray from unit-cube [0,1]^3 to world-space cell and return world dir + tMax.
	void MapRayToWorldCell(const Ray& ray, const Float3& cellMin, const Float3& cellExtent,
		Float3& outOrigin, Float3& outDir, float& outTMax)
	{
		outOrigin = {
			cellMin.x + ray.origin.x * cellExtent.x,
			cellMin.y + ray.origin.y * cellExtent.y,
			cellMin.z + ray.origin.z * cellExtent.z
		};
		outDir = {
			ray.direction.x * cellExtent.x,
			ray.direction.y * cellExtent.y,
			ray.direction.z * cellExtent.z
		};
		outTMax = outDir.length();
		if (outTMax > 1e-12f)
			outDir = outDir * (1.0f / outTMax);
	}

	// Rasterize triangles into a grid, returning per-cell triangle index lists.
	struct CellTriEntry
	{
		uint64_t cellKey = 0;
		std::vector<uint32_t> triangleIndices; // indices into the flat triangle array (triangle index, not vertex index)
	};

	std::unordered_map<uint64_t, std::vector<uint32_t>> RasterizeTrianglesToGrid(
		const std::vector<std::byte>& vertices,
		size_t vertexStrideBytes,
		const std::vector<uint32_t>& triangleIndices,
		const Float3& aabbMin,
		const Float3& aabbMax,
		uint32_t resolution)
	{
		const uint32_t triangleCount = static_cast<uint32_t>(triangleIndices.size() / 3);
		std::unordered_map<uint64_t, std::vector<uint32_t>> cellTriMap;
		if (triangleCount == 0 || resolution == 0)
			return cellTriMap;

		const Float3 extent = aabbMax - aabbMin;
		const Float3 cellSize = {
			extent.x / static_cast<float>(resolution),
			extent.y / static_cast<float>(resolution),
			extent.z / static_cast<float>(resolution)
		};

		if (cellSize.x <= 0.0f || cellSize.y <= 0.0f || cellSize.z <= 0.0f)
			return cellTriMap;

		const Float3 invCellSize = {
			1.0f / cellSize.x,
			1.0f / cellSize.y,
			1.0f / cellSize.z
		};
		const Float3 halfCell = cellSize * 0.5f;

		cellTriMap.reserve(triangleCount * 2);

		for (uint32_t triIdx = 0; triIdx < triangleCount; ++triIdx)
		{
			const uint32_t i0 = triangleIndices[triIdx * 3 + 0];
			const uint32_t i1 = triangleIndices[triIdx * 3 + 1];
			const uint32_t i2 = triangleIndices[triIdx * 3 + 2];

			const Float3 v0 = ReadPosition(vertices, vertexStrideBytes, i0);
			const Float3 v1 = ReadPosition(vertices, vertexStrideBytes, i1);
			const Float3 v2 = ReadPosition(vertices, vertexStrideBytes, i2);

			Float3 triMin = {
				std::min({ v0.x, v1.x, v2.x }),
				std::min({ v0.y, v1.y, v2.y }),
				std::min({ v0.z, v1.z, v2.z })
			};
			Float3 triMax = {
				std::max({ v0.x, v1.x, v2.x }),
				std::max({ v0.y, v1.y, v2.y }),
				std::max({ v0.z, v1.z, v2.z })
			};

			const uint32_t cxMin = ToCellCoord(triMin.x, aabbMin.x, invCellSize.x, resolution);
			const uint32_t cyMin = ToCellCoord(triMin.y, aabbMin.y, invCellSize.y, resolution);
			const uint32_t czMin = ToCellCoord(triMin.z, aabbMin.z, invCellSize.z, resolution);
			const uint32_t cxMax = ToCellCoord(triMax.x, aabbMin.x, invCellSize.x, resolution);
			const uint32_t cyMax = ToCellCoord(triMax.y, aabbMin.y, invCellSize.y, resolution);
			const uint32_t czMax = ToCellCoord(triMax.z, aabbMin.z, invCellSize.z, resolution);

			for (uint32_t cz = czMin; cz <= czMax; ++cz)
			{
				for (uint32_t cy = cyMin; cy <= cyMax; ++cy)
				{
					for (uint32_t cx = cxMin; cx <= cxMax; ++cx)
					{
						Float3 center = {
							aabbMin.x + (static_cast<float>(cx) + 0.5f) * cellSize.x,
							aabbMin.y + (static_cast<float>(cy) + 0.5f) * cellSize.y,
							aabbMin.z + (static_cast<float>(cz) + 0.5f) * cellSize.z
						};

						if (TriangleAABBOverlap(v0, v1, v2, center, halfCell))
						{
							cellTriMap[PackCell(cx, cy, cz)].push_back(triIdx);
						}
					}
				}
			}
		}

		return cellTriMap;
	}

	// Rasterize child voxel cells (as boxes) into a parent grid.
	// Returns per-parent-cell list of (childIndex, cellIndex) pairs.
	struct ChildCellRef
	{
		uint32_t childIndex = 0;  // Index into children array
		uint32_t cellIndex = 0;   // Index into that child's activeCells
	};

	std::unordered_map<uint64_t, std::vector<ChildCellRef>> RasterizeVoxelsToGrid(
		const std::vector<const VoxelGroupPayload*>& children,
		const Float3& parentAabbMin,
		const Float3& parentAabbMax,
		uint32_t parentResolution)
	{
		std::unordered_map<uint64_t, std::vector<ChildCellRef>> cellRefMap;
		if (children.empty() || parentResolution == 0)
			return cellRefMap;

		const Float3 extent = parentAabbMax - parentAabbMin;
		const Float3 cellSize = {
			extent.x / static_cast<float>(parentResolution),
			extent.y / static_cast<float>(parentResolution),
			extent.z / static_cast<float>(parentResolution)
		};

		if (cellSize.x <= 0.0f || cellSize.y <= 0.0f || cellSize.z <= 0.0f)
			return cellRefMap;

		const Float3 invCellSize = {
			1.0f / cellSize.x,
			1.0f / cellSize.y,
			1.0f / cellSize.z
		};

		for (uint32_t ci = 0; ci < static_cast<uint32_t>(children.size()); ++ci)
		{
			const VoxelGroupPayload* child = children[ci];
			if (!child || child->activeCells.empty() || child->resolution == 0)
				continue;

			const Float3 childAabbMin = ToFloat3(child->aabbMin);
			const Float3 childExtent = ToFloat3(child->aabbMax) - childAabbMin;
			const float childCellSizeX = childExtent.x / static_cast<float>(child->resolution);
			const float childCellSizeY = childExtent.y / static_cast<float>(child->resolution);
			const float childCellSizeZ = childExtent.z / static_cast<float>(child->resolution);

			for (uint32_t cellIdx = 0; cellIdx < static_cast<uint32_t>(child->activeCells.size()); ++cellIdx)
			{
				const VoxelCell& vc = child->activeCells[cellIdx];

				// Skip near-empty cells
				if (vc.opacity < 1e-6f)
					continue;

				// Child cell world-space AABB
				const Float3 childCellMin = {
					childAabbMin.x + static_cast<float>(vc.x) * childCellSizeX,
					childAabbMin.y + static_cast<float>(vc.y) * childCellSizeY,
					childAabbMin.z + static_cast<float>(vc.z) * childCellSizeZ
				};
				const Float3 childCellMax = {
					childCellMin.x + childCellSizeX,
					childCellMin.y + childCellSizeY,
					childCellMin.z + childCellSizeZ
				};

				// Find parent cell range that this child cell can overlap
				const uint32_t pcxMin = ToCellCoord(childCellMin.x, parentAabbMin.x, invCellSize.x, parentResolution);
				const uint32_t pcyMin = ToCellCoord(childCellMin.y, parentAabbMin.y, invCellSize.y, parentResolution);
				const uint32_t pczMin = ToCellCoord(childCellMin.z, parentAabbMin.z, invCellSize.z, parentResolution);
				const uint32_t pcxMax = ToCellCoord(childCellMax.x, parentAabbMin.x, invCellSize.x, parentResolution);
				const uint32_t pcyMax = ToCellCoord(childCellMax.y, parentAabbMin.y, invCellSize.y, parentResolution);
				const uint32_t pczMax = ToCellCoord(childCellMax.z, parentAabbMin.z, invCellSize.z, parentResolution);

				for (uint32_t pz = pczMin; pz <= pczMax; ++pz)
				{
					for (uint32_t py = pcyMin; py <= pcyMax; ++py)
					{
						for (uint32_t px = pcxMin; px <= pcxMax; ++px)
						{
							// Parent cell AABB
							const Float3 pCellMin = {
								parentAabbMin.x + static_cast<float>(px) * cellSize.x,
								parentAabbMin.y + static_cast<float>(py) * cellSize.y,
								parentAabbMin.z + static_cast<float>(pz) * cellSize.z
							};
							const Float3 pCellMax = {
								pCellMin.x + cellSize.x,
								pCellMin.y + cellSize.y,
								pCellMin.z + cellSize.z
							};

							if (AABBOverlap(childCellMin, childCellMax, pCellMin, pCellMax))
							{
								cellRefMap[PackCell(px, py, pz)].push_back({ ci, cellIdx });
							}
						}
					}
				}
			}
		}

		return cellRefMap;
	}

	// Per-cell opacity sampling via ray tracing against triangles.
	float SampleCellOpacityTriangles(
		const std::vector<std::byte>& vertices,
		size_t vertexStrideBytes,
		const std::vector<uint32_t>& meshTriangleIndices,
		const std::vector<uint32_t>& cellTriangleIndices,
		const Float3& cellWorldMin,
		const Float3& cellWorldMax,
		const std::vector<Ray>& rays)
	{
		if (rays.empty() || cellTriangleIndices.empty())
			return 0.0f;

		const Float3 cellExtent = cellWorldMax - cellWorldMin;
		uint32_t hitCount = 0;

		for (const Ray& ray : rays)
		{
			Float3 origin, dir;
			float tMax;
			MapRayToWorldCell(ray, cellWorldMin, cellExtent, origin, dir, tMax);
			if (tMax < 1e-12f)
				continue;

			bool hit = false;
			for (uint32_t triLocalIdx : cellTriangleIndices)
			{
				const uint32_t i0 = meshTriangleIndices[triLocalIdx * 3 + 0];
				const uint32_t i1 = meshTriangleIndices[triLocalIdx * 3 + 1];
				const uint32_t i2 = meshTriangleIndices[triLocalIdx * 3 + 2];

				const Float3 v0 = ReadPosition(vertices, vertexStrideBytes, i0);
				const Float3 v1 = ReadPosition(vertices, vertexStrideBytes, i1);
				const Float3 v2 = ReadPosition(vertices, vertexStrideBytes, i2);

				if (RayTriangleIntersect(origin, dir, v0, v1, v2, tMax))
				{
					hit = true;
					break;
				}
			}

			if (hit)
				++hitCount;
		}

		return static_cast<float>(hitCount) / static_cast<float>(rays.size());
	}

	// Per-cell opacity sampling via ray tracing against child voxel boxes.
	// Each child cell is an axis-aligned box; we test ray-AABB intersection.
	// A child cell with opacity < 1 is treated probabilistically:
	// the ray "hits" with probability = child opacity.
	// For determinism, we use opacity-weighted hit counting.
	float SampleCellOpacityVoxels(
		const std::vector<const VoxelGroupPayload*>& children,
		const std::vector<ChildCellRef>& cellRefs,
		const Float3& cellWorldMin,
		const Float3& cellWorldMax,
		const std::vector<Ray>& rays)
	{
		if (rays.empty() || cellRefs.empty())
			return 0.0f;

		const Float3 cellExtent = cellWorldMax - cellWorldMin;

		// Pre-compute child cell world-space AABBs for all refs in this parent cell.
		struct ChildCellBox
		{
			Float3 boxMin{};
			Float3 boxMax{};
			float opacity = 0.0f;
		};

		std::vector<ChildCellBox> boxes;
		boxes.reserve(cellRefs.size());

		for (const ChildCellRef& ref : cellRefs)
		{
			const VoxelGroupPayload* child = children[ref.childIndex];
			const Float3 childAabbMin = ToFloat3(child->aabbMin);
			const Float3 childExtent = ToFloat3(child->aabbMax) - childAabbMin;
			const float childCellSizeX = childExtent.x / static_cast<float>(child->resolution);
			const float childCellSizeY = childExtent.y / static_cast<float>(child->resolution);
			const float childCellSizeZ = childExtent.z / static_cast<float>(child->resolution);

			const VoxelCell& vc = child->activeCells[ref.cellIndex];

			ChildCellBox box;
			box.boxMin = {
				childAabbMin.x + static_cast<float>(vc.x) * childCellSizeX,
				childAabbMin.y + static_cast<float>(vc.y) * childCellSizeY,
				childAabbMin.z + static_cast<float>(vc.z) * childCellSizeZ
			};
			box.boxMax = {
				box.boxMin.x + childCellSizeX,
				box.boxMin.y + childCellSizeY,
				box.boxMin.z + childCellSizeZ
			};
			box.opacity = vc.opacity;
			boxes.push_back(box);
		}

		float weightedHits = 0.0f;

		for (const Ray& ray : rays)
		{
			Float3 origin, dir;
			float tMax;
			MapRayToWorldCell(ray, cellWorldMin, cellExtent, origin, dir, tMax);
			if (tMax < 1e-12f)
				continue;

			// Compute inverse direction for slab test (handle near-zero axes)
			constexpr float kHuge = 1e30f;
			Float3 invDir = {
				std::abs(dir.x) > 1e-12f ? 1.0f / dir.x : (dir.x >= 0.0f ? kHuge : -kHuge),
				std::abs(dir.y) > 1e-12f ? 1.0f / dir.y : (dir.y >= 0.0f ? kHuge : -kHuge),
				std::abs(dir.z) > 1e-12f ? 1.0f / dir.z : (dir.z >= 0.0f ? kHuge : -kHuge)
			};

			// Find the maximum opacity among all hit child boxes.
			// This is a conservative estimate: if a ray passes through multiple
			// partially-opaque child cells, we take the densest one.
			float maxHitOpacity = 0.0f;

			for (const ChildCellBox& box : boxes)
			{
				if (RayAABBIntersect(origin, invDir, box.boxMin, box.boxMax, tMax))
				{
					maxHitOpacity = std::max(maxHitOpacity, box.opacity);
					if (maxHitOpacity >= 1.0f)
						break;
				}
			}

			weightedHits += maxHitOpacity;
		}

		return weightedHits / static_cast<float>(rays.size());
	}

	// Morton code: 10-bit per axis -> 30-bit interleaved
	uint32_t ExpandBits10(uint32_t v)
	{
		// Spread 10 bits across 30 bits: --9--8--7--6--5--4--3--2--1--0
		v = (v | (v << 16)) & 0x030000FFu;
		v = (v | (v << 8)) & 0x0300F00Fu;
		v = (v | (v << 4)) & 0x030C30C3u;
		v = (v | (v << 2)) & 0x09249249u;
		return v;
	}

	uint32_t Morton3D(uint32_t x, uint32_t y, uint32_t z)
	{
		return (ExpandBits10(x) << 2) | (ExpandBits10(y) << 1) | ExpandBits10(z);
	}
}

// Public API: VoxelizeTriangles
VoxelGroupPayload VoxelizeTriangles(const VoxelizeTrianglesInput& input)
{
	VoxelGroupPayload result{};

	if (!input.vertices || input.vertexStrideBytes < sizeof(float) * 3)
		return result;

	if (!input.triangleIndices || input.triangleIndices->empty() || (input.triangleIndices->size() % 3) != 0)
		return result;

	if (input.resolution < 2)
		return result;

	const Float3 aabbMin = ToFloat3(input.aabbMin);
	const Float3 aabbMax = ToFloat3(input.aabbMax);

	if (aabbMax.x - aabbMin.x <= 0.0f ||
		aabbMax.y - aabbMin.y <= 0.0f ||
		aabbMax.z - aabbMin.z <= 0.0f)
	{
		spdlog::warn("VoxelGroupBuilder: degenerate AABB, skipping triangle voxelization");
		return result;
	}

	result.resolution = input.resolution;
	result.aabbMin = input.aabbMin;
	result.aabbMax = input.aabbMax;

	auto cellTriMap = RasterizeTrianglesToGrid(
		*input.vertices, input.vertexStrideBytes,
		*input.triangleIndices,
		aabbMin, aabbMax,
		input.resolution);

	if (cellTriMap.empty())
		return result;

	const std::vector<Ray> rays = GenerateCellRays(input.raysPerCell, input.resolution * 2654435761u);

	const Float3 extent = aabbMax - aabbMin;
	const Float3 cellSize = {
		extent.x / static_cast<float>(input.resolution),
		extent.y / static_cast<float>(input.resolution),
		extent.z / static_cast<float>(input.resolution)
	};

	result.activeCells.reserve(cellTriMap.size());

	for (auto& [key, triIndices] : cellTriMap)
	{
		uint32_t cx, cy, cz;
		UnpackCell(key, cx, cy, cz);

		Float3 cellMin = {
			aabbMin.x + static_cast<float>(cx) * cellSize.x,
			aabbMin.y + static_cast<float>(cy) * cellSize.y,
			aabbMin.z + static_cast<float>(cz) * cellSize.z
		};
		Float3 cellMax = {
			cellMin.x + cellSize.x,
			cellMin.y + cellSize.y,
			cellMin.z + cellSize.z
		};

		float opacity = SampleCellOpacityTriangles(
			*input.vertices, input.vertexStrideBytes,
			*input.triangleIndices,
			triIndices,
			cellMin, cellMax,
			rays);

		VoxelCell vc{};
		vc.x = cx;
		vc.y = cy;
		vc.z = cz;
		vc.opacity = opacity;

		result.activeCells.push_back(vc);
	}

	return result;
}

// Public API: VoxelizeVoxels
VoxelGroupPayload VoxelizeVoxels(const VoxelizeVoxelsInput& input)
{
	VoxelGroupPayload result{};

	if (!input.children || input.children->empty())
		return result;

	if (input.resolution < 2)
		return result;

	const Float3 aabbMin = ToFloat3(input.aabbMin);
	const Float3 aabbMax = ToFloat3(input.aabbMax);

	if (aabbMax.x - aabbMin.x <= 0.0f ||
		aabbMax.y - aabbMin.y <= 0.0f ||
		aabbMax.z - aabbMin.z <= 0.0f)
	{
		spdlog::warn("VoxelGroupBuilder: degenerate AABB, skipping voxel-to-voxel reduction");
		return result;
	}

	result.resolution = input.resolution;
	result.aabbMin = input.aabbMin;
	result.aabbMax = input.aabbMax;

	auto cellRefMap = RasterizeVoxelsToGrid(
		*input.children,
		aabbMin, aabbMax,
		input.resolution);

	if (cellRefMap.empty())
		return result;

	const std::vector<Ray> rays = GenerateCellRays(input.raysPerCell, input.resolution * 2654435761u);

	const Float3 extent = aabbMax - aabbMin;
	const Float3 cellSize = {
		extent.x / static_cast<float>(input.resolution),
		extent.y / static_cast<float>(input.resolution),
		extent.z / static_cast<float>(input.resolution)
	};

	result.activeCells.reserve(cellRefMap.size());

	for (auto& [key, refs] : cellRefMap)
	{
		uint32_t cx, cy, cz;
		UnpackCell(key, cx, cy, cz);

		Float3 cellMin = {
			aabbMin.x + static_cast<float>(cx) * cellSize.x,
			aabbMin.y + static_cast<float>(cy) * cellSize.y,
			aabbMin.z + static_cast<float>(cz) * cellSize.z
		};
		Float3 cellMax = {
			cellMin.x + cellSize.x,
			cellMin.y + cellSize.y,
			cellMin.z + cellSize.z
		};

		float opacity = SampleCellOpacityVoxels(
			*input.children,
			refs,
			cellMin, cellMax,
			rays);

		if (opacity < 1e-6f)
			continue;

		VoxelCell vc{};
		vc.x = cx;
		vc.y = cy;
		vc.z = cz;
		vc.opacity = opacity;

		result.activeCells.push_back(vc);
	}

	return result;
}

// Public API: MortonSort
std::vector<uint32_t> MortonSort(
	const DirectX::XMFLOAT3* positions,
	uint32_t count,
	const DirectX::XMFLOAT3& aabbMin,
	const DirectX::XMFLOAT3& aabbMax)
{
	std::vector<uint32_t> permutation(count);
	std::iota(permutation.begin(), permutation.end(), 0u);

	if (count <= 1)
		return permutation;

	const float extX = aabbMax.x - aabbMin.x;
	const float extY = aabbMax.y - aabbMin.y;
	const float extZ = aabbMax.z - aabbMin.z;

	const float invExtX = extX > 1e-12f ? 1.0f / extX : 0.0f;
	const float invExtY = extY > 1e-12f ? 1.0f / extY : 0.0f;
	const float invExtZ = extZ > 1e-12f ? 1.0f / extZ : 0.0f;

	// Compute 30-bit Morton codes
	std::vector<uint32_t> mortonCodes(count);
	for (uint32_t i = 0; i < count; ++i)
	{
		float nx = (positions[i].x - aabbMin.x) * invExtX;
		float ny = (positions[i].y - aabbMin.y) * invExtY;
		float nz = (positions[i].z - aabbMin.z) * invExtZ;

		nx = std::max(0.0f, std::min(nx, 1.0f));
		ny = std::max(0.0f, std::min(ny, 1.0f));
		nz = std::max(0.0f, std::min(nz, 1.0f));

		const uint32_t ix = std::min(static_cast<uint32_t>(nx * 1023.0f), 1023u);
		const uint32_t iy = std::min(static_cast<uint32_t>(ny * 1023.0f), 1023u);
		const uint32_t iz = std::min(static_cast<uint32_t>(nz * 1023.0f), 1023u);

		mortonCodes[i] = Morton3D(ix, iy, iz);
	}

	// Sort permutation by Morton code
	std::sort(permutation.begin(), permutation.end(),
		[&mortonCodes](uint32_t a, uint32_t b)
		{
			return mortonCodes[a] < mortonCodes[b];
		});

	return permutation;
}

// Public API: MergeGroupsSpatial
std::vector<std::vector<uint32_t>> MergeGroupsSpatial(
	const std::vector<uint32_t>& sortedGroupIndices,
	uint32_t maxFanout)
{
	std::vector<std::vector<uint32_t>> batches;

	if (sortedGroupIndices.empty() || maxFanout == 0)
		return batches;

	maxFanout = std::max(1u, maxFanout);

	std::vector<uint32_t> currentBatch;
	currentBatch.reserve(maxFanout);

	for (uint32_t idx : sortedGroupIndices)
	{
		currentBatch.push_back(idx);
		if (static_cast<uint32_t>(currentBatch.size()) >= maxFanout)
		{
			batches.push_back(std::move(currentBatch));
			currentBatch = {};
			currentBatch.reserve(maxFanout);
		}
	}

	if (!currentBatch.empty())
	{
		batches.push_back(std::move(currentBatch));
	}

	return batches;
}
