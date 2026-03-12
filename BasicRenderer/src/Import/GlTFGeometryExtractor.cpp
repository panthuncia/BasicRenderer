#include "Import/GlTFGeometryExtractor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Import/CLodCacheLoader.h"
#include "Managers/Singletons/TaskSchedulerManager.h"
#include "Mesh/ClusterLODTypes.h"
#include "Mesh/VertexFlags.h"
#include "Utilities/CachePathUtilities.h"

using nlohmann::json;
using namespace DirectX;

namespace {

// Internal types

struct GLBHeader {
	uint32_t magic = 0;
	uint32_t version = 0;
	uint32_t length = 0;
};

struct GLBChunkSpan {
	uint32_t type = 0;
	uint32_t length = 0;
	uint64_t dataOffset = 0;
};

struct BufferViewInfo {
	size_t bufferIndex = 0;
	size_t byteOffset = 0;
	size_t byteLength = 0;
	size_t byteStride = 0;
};

struct AccessorInfo {
	size_t bufferViewIndex = 0;
	size_t byteOffset = 0;
	size_t count = 0;
	int componentType = 0;
	std::string type;
};

enum class BufferBacking {
	FileSpan,
	DataUri,
};

struct BufferSource {
	BufferBacking backing = BufferBacking::FileSpan;
	std::filesystem::path filePath;
	uint64_t fileOffset = 0;
	uint64_t fileLength = 0;
	std::string dataUri;
};

struct ParsedDocument {
	json gltf;
	std::vector<BufferSource> buffers;
};

// Constants

constexpr uint32_t kGlbMagic = 0x46546C67;
constexpr uint32_t kJsonChunkType = 0x4E4F534A;
constexpr uint32_t kBinChunkType = 0x004E4942;
constexpr int kTrianglesMode = 4;

// File helpers

uint64_t GetFileSize(const std::filesystem::path& path) {
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec) {
		throw std::runtime_error("Failed to query file size: " + path.string());
	}
	return static_cast<uint64_t>(size);
}

std::vector<uint8_t> ReadFileRange(const std::filesystem::path& path, uint64_t offset, uint64_t size) {
	std::vector<uint8_t> out;
	TaskSchedulerManager::GetInstance().RunIoTask([&]() {
		std::ifstream file(path, std::ios::binary);
		if (!file) {
			throw std::runtime_error("Failed to open file: " + path.string());
		}

		const uint64_t fileSize = GetFileSize(path);
		if (offset > fileSize || size > fileSize - offset) {
			throw std::runtime_error("Read range out of file bounds: " + path.string());
		}

		file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		out.resize(static_cast<size_t>(size));
		if (size > 0) {
			file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
			if (!file) {
				throw std::runtime_error("Failed to read file range: " + path.string());
			}
		}
		});

	return out;
}

uint32_t ReadU32LE(const std::filesystem::path& path, uint64_t offset) {
	auto bytes = ReadFileRange(path, offset, sizeof(uint32_t));
	uint32_t value = 0;
	std::memcpy(&value, bytes.data(), sizeof(uint32_t));
	return value;
}

// GLB parsing

GLBHeader ReadGLBHeader(const std::filesystem::path& path) {
	auto bytes = ReadFileRange(path, 0, 12);
	GLBHeader header;
	std::memcpy(&header.magic, bytes.data(), sizeof(uint32_t));
	std::memcpy(&header.version, bytes.data() + 4, sizeof(uint32_t));
	std::memcpy(&header.length, bytes.data() + 8, sizeof(uint32_t));

	if (header.magic != kGlbMagic) {
		throw std::runtime_error("Invalid GLB magic for: " + path.string());
	}
	if (header.version != 2) {
		throw std::runtime_error("Unsupported GLB version for: " + path.string());
	}

	return header;
}

std::vector<GLBChunkSpan> ReadGLBChunkSpans(const std::filesystem::path& path, uint64_t fileSize) {
	std::vector<GLBChunkSpan> chunks;
	uint64_t offset = 12;

	while (offset < fileSize) {
		if (offset + 8 > fileSize) {
			throw std::runtime_error("Invalid GLB chunk header range");
		}

		const uint32_t chunkLength = ReadU32LE(path, offset);
		const uint32_t chunkType = ReadU32LE(path, offset + 4);
		offset += 8;

		if (offset + chunkLength > fileSize) {
			throw std::runtime_error("Invalid GLB chunk length");
		}

		GLBChunkSpan chunk;
		chunk.type = chunkType;
		chunk.length = chunkLength;
		chunk.dataOffset = offset;
		chunks.push_back(chunk);

		offset += chunkLength;
	}

	return chunks;
}

// Base64 / data URI

int Base64CharToValue(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	if (c == '=') return -2;
	return -1;
}

std::vector<uint8_t> DecodeBase64(const std::string& encoded) {
	std::vector<uint8_t> decoded;
	decoded.reserve((encoded.size() * 3) / 4);

	int val = 0;
	int valb = -8;

	for (unsigned char c : encoded) {
		if (std::isspace(c) != 0) {
			continue;
		}

		int d = Base64CharToValue(c);
		if (d == -1) {
			throw std::runtime_error("Invalid base64 data");
		}
		if (d == -2) {
			break;
		}

		val = (val << 6) + d;
		valb += 6;
		if (valb >= 0) {
			decoded.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
			valb -= 8;
		}
	}

	return decoded;
}

std::vector<uint8_t> DecodeDataUri(const std::string& uri) {
	const auto commaPos = uri.find(',');
	if (commaPos == std::string::npos) {
		throw std::runtime_error("Invalid data URI");
	}

	const std::string metadata = uri.substr(0, commaPos);
	const std::string payload = uri.substr(commaPos + 1);

	if (metadata.find(";base64") == std::string::npos) {
		throw std::runtime_error("Only base64 data URIs are supported");
	}

	return DecodeBase64(payload);
}

// Accessor helpers

size_t NumComponentsForType(const std::string& type) {
	if (type == "SCALAR") return 1;
	if (type == "VEC2") return 2;
	if (type == "VEC3") return 3;
	if (type == "VEC4") return 4;
	if (type == "MAT2") return 4;
	if (type == "MAT3") return 9;
	if (type == "MAT4") return 16;
	throw std::runtime_error("Unsupported accessor type: " + type);
}

size_t BytesPerComponent(int componentType) {
	switch (componentType) {
	case 5120:
	case 5121:
		return 1;
	case 5122:
	case 5123:
		return 2;
	case 5125:
	case 5126:
		return 4;
	default:
		throw std::runtime_error("Unsupported component type");
	}
}

AccessorInfo GetAccessorInfo(const json& gltf, size_t accessorIndex) {
	const auto& accessors = gltf.at("accessors");
	if (accessorIndex >= accessors.size()) {
		throw std::runtime_error("Accessor index out of range");
	}

	const auto& accessor = accessors[accessorIndex];
	if (!accessor.contains("bufferView")) {
		throw std::runtime_error("Sparse accessors are not yet supported");
	}

	AccessorInfo info;
	info.bufferViewIndex = accessor.at("bufferView").get<size_t>();
	info.byteOffset = accessor.value<size_t>("byteOffset", static_cast<size_t>(0));
	info.count = accessor.at("count").get<size_t>();
	info.componentType = accessor.at("componentType").get<int>();
	info.type = accessor.at("type").get<std::string>();
	return info;
}

BufferViewInfo GetBufferViewInfo(const json& gltf, size_t bufferViewIndex) {
	const auto& bufferViews = gltf.at("bufferViews");
	if (bufferViewIndex >= bufferViews.size()) {
		throw std::runtime_error("BufferView index out of range");
	}

	const auto& bufferView = bufferViews[bufferViewIndex];
	BufferViewInfo info;
	info.bufferIndex = bufferView.at("buffer").get<size_t>();
	info.byteOffset = bufferView.value<size_t>("byteOffset", static_cast<size_t>(0));
	info.byteLength = bufferView.at("byteLength").get<size_t>();
	info.byteStride = bufferView.value<size_t>("byteStride", static_cast<size_t>(0));
	return info;
}

template <typename T>
T ReadTyped(const std::vector<uint8_t>& buffer, size_t byteOffset) {
	if (byteOffset + sizeof(T) > buffer.size()) {
		throw std::runtime_error("Buffer read out of bounds");
	}

	T value{};
	std::memcpy(&value, buffer.data() + byteOffset, sizeof(T));
	return value;
}

double ReadComponentAsDouble(const std::vector<uint8_t>& source, int componentType, size_t byteOffset) {
	switch (componentType) {
	case 5120: return static_cast<double>(ReadTyped<int8_t>(source, byteOffset));
	case 5121: return static_cast<double>(ReadTyped<uint8_t>(source, byteOffset));
	case 5122: return static_cast<double>(ReadTyped<int16_t>(source, byteOffset));
	case 5123: return static_cast<double>(ReadTyped<uint16_t>(source, byteOffset));
	case 5125: return static_cast<double>(ReadTyped<uint32_t>(source, byteOffset));
	case 5126: return static_cast<double>(ReadTyped<float>(source, byteOffset));
	default:
		throw std::runtime_error("Unsupported accessor component type");
	}
}

// Buffer reading

std::vector<uint8_t> ReadFromSource(const BufferSource& source, uint64_t offset, uint64_t size) {
	if (source.backing == BufferBacking::DataUri) {
		const auto bytes = DecodeDataUri(source.dataUri);
		if (offset > bytes.size() || size > bytes.size() - offset) {
			throw std::runtime_error("Data URI range out of bounds");
		}

		std::vector<uint8_t> slice(static_cast<size_t>(size));
		if (size > 0) {
			std::memcpy(slice.data(), bytes.data() + offset, static_cast<size_t>(size));
		}
		return slice;
	}

	if (offset > source.fileLength || size > source.fileLength - offset) {
		throw std::runtime_error("Buffer source range out of bounds: " + source.filePath.string());
	}

	return ReadFileRange(source.filePath, source.fileOffset + offset, size);
}

std::vector<uint8_t> ReadAccessorRawWindow(const ParsedDocument& doc, size_t accessorIndex, size_t firstElement, size_t elementCount, size_t* outStride, size_t* outComponentCount, int* outComponentType) {
	const AccessorInfo accessor = GetAccessorInfo(doc.gltf, accessorIndex);
	const BufferViewInfo view = GetBufferViewInfo(doc.gltf, accessor.bufferViewIndex);

	if (view.bufferIndex >= doc.buffers.size()) {
		throw std::runtime_error("Buffer index out of range");
	}

	const size_t componentCount = NumComponentsForType(accessor.type);
	const size_t componentBytes = BytesPerComponent(accessor.componentType);
	const size_t packedElementSize = componentCount * componentBytes;
	const size_t stride = view.byteStride == 0 ? packedElementSize : view.byteStride;

	if (elementCount == 0) {
		*outStride = stride;
		*outComponentCount = componentCount;
		*outComponentType = accessor.componentType;
		return {};
	}

	if (firstElement > accessor.count || elementCount > accessor.count - firstElement) {
		throw std::runtime_error("Accessor window out of bounds");
	}

	const uint64_t relativeStart = static_cast<uint64_t>(view.byteOffset + accessor.byteOffset + firstElement * stride);
	const uint64_t relativeEnd = relativeStart + static_cast<uint64_t>((elementCount - 1) * stride + packedElementSize);
	const uint64_t viewEnd = static_cast<uint64_t>(view.byteOffset + view.byteLength);

	if (relativeEnd > viewEnd) {
		throw std::runtime_error("Accessor range exceeds bufferView bounds");
	}

	*outStride = stride;
	*outComponentCount = componentCount;
	*outComponentType = accessor.componentType;

	return ReadFromSource(doc.buffers[view.bufferIndex], relativeStart, relativeEnd - relativeStart);
}

// Extension logging

void LogDeclaredExtensions(const json& gltf, const std::string& filePath) {
	auto readStringArray = [](const json& object, const char* key) {
		std::vector<std::string> values;
		if (!object.contains(key) || !object[key].is_array()) {
			return values;
		}

		const auto& array = object[key];
		values.reserve(array.size());
		for (const auto& entry : array) {
			if (entry.is_string()) {
				values.push_back(entry.get<std::string>());
			}
		}
		return values;
	};

	auto joinNames = [](const std::vector<std::string>& names) {
		std::string joined;
		for (size_t i = 0; i < names.size(); ++i) {
			if (i > 0) {
				joined += ", ";
			}
			joined += names[i];
		}
		return joined;
	};

	const auto extensionsUsed = readStringArray(gltf, "extensionsUsed");
	const auto extensionsRequired = readStringArray(gltf, "extensionsRequired");

	if (extensionsUsed.empty() && extensionsRequired.empty()) {
		spdlog::info("glTF extensions for {}: none declared", filePath);
		return;
	}

	if (!extensionsUsed.empty()) {
		spdlog::info("glTF extensionsUsed for {}: {}", filePath, joinNames(extensionsUsed));
	}
	else {
		spdlog::info("glTF extensionsUsed for {}: none", filePath);
	}

	if (!extensionsRequired.empty()) {
		spdlog::info("glTF extensionsRequired for {}: {}", filePath, joinNames(extensionsRequired));
	}
	else {
		spdlog::info("glTF extensionsRequired for {}: none", filePath);
	}
}

// Document parsing

ParsedDocument ParseDocument(const std::string& filePath) {
	const std::filesystem::path path(filePath);
	if (!std::filesystem::is_regular_file(path)) {
		throw std::runtime_error("glTF file not found: " + filePath);
	}

	ParsedDocument doc;
	const std::string extension = path.extension().string();

	std::optional<GLBChunkSpan> glbJsonChunk;
	std::optional<GLBChunkSpan> glbBinChunk;

	if (extension == ".glb" || extension == ".GLB") {
		const uint64_t fileSize = GetFileSize(path);
		const GLBHeader header = ReadGLBHeader(path);
		if (header.length != fileSize) {
			spdlog::warn("GLB length header ({}) differs from file size ({}) for {}",
				header.length, fileSize, filePath);
		}

		const auto chunks = ReadGLBChunkSpans(path, fileSize);
		for (const auto& chunk : chunks) {
			if (chunk.type == kJsonChunkType && !glbJsonChunk.has_value()) {
				glbJsonChunk = chunk;
			}
			else if (chunk.type == kBinChunkType && !glbBinChunk.has_value()) {
				glbBinChunk = chunk;
			}
		}

		if (!glbJsonChunk.has_value()) {
			throw std::runtime_error("GLB JSON chunk not found");
		}

		auto jsonBytes = ReadFileRange(path, glbJsonChunk->dataOffset, glbJsonChunk->length);
		doc.gltf = json::parse(jsonBytes.begin(), jsonBytes.end());
	}
	else {
		std::ifstream stream(path);
		if (!stream) {
			throw std::runtime_error("Failed to open glTF JSON: " + filePath);
		}
		stream >> doc.gltf;
	}

	if (!doc.gltf.contains("buffers") || doc.gltf["buffers"].empty()) {
		throw std::runtime_error("glTF contains no buffers");
	}

	const std::filesystem::path parent = path.parent_path();

	for (size_t bufferIndex = 0; bufferIndex < doc.gltf["buffers"].size(); ++bufferIndex) {
		const auto& buffer = doc.gltf["buffers"][bufferIndex];
		BufferSource source;

		if (buffer.contains("uri")) {
			const std::string uri = buffer["uri"].get<std::string>();
			if (uri.rfind("data:", 0) == 0) {
				source.backing = BufferBacking::DataUri;
				source.dataUri = uri;
				source.fileLength = buffer.value<uint64_t>("byteLength", static_cast<uint64_t>(0));
			}
			else {
				source.backing = BufferBacking::FileSpan;
				source.filePath = parent / std::filesystem::path(uri);
				source.fileOffset = 0;
				const uint64_t actualLength = GetFileSize(source.filePath);
				const uint64_t declaredLength = buffer.value<uint64_t>("byteLength", actualLength);
				if (declaredLength != actualLength) {
					spdlog::warn(
						"glTF buffer byteLength mismatch for {} (declared={}, actual={}); using actual size",
						source.filePath.string(), declaredLength, actualLength);
				}
				source.fileLength = actualLength;
			}
		}
		else {
			if (!(extension == ".glb" || extension == ".GLB")) {
				throw std::runtime_error("Non-GLB buffer missing URI");
			}
			if (!glbBinChunk.has_value()) {
				throw std::runtime_error("GLB binary chunk not found");
			}

			source.backing = BufferBacking::FileSpan;
			source.filePath = path;
			source.fileOffset = glbBinChunk->dataOffset;
			const uint64_t actualLength = static_cast<uint64_t>(glbBinChunk->length);
			const uint64_t declaredLength = buffer.value<uint64_t>("byteLength", actualLength);
			if (declaredLength != actualLength) {
				spdlog::warn(
					"GLB buffer byteLength mismatch in {} (declared={}, binChunk={}); using bin chunk size",
					filePath, declaredLength, actualLength);
			}
			source.fileLength = actualLength;
		}

		doc.buffers.push_back(std::move(source));
	}

	return doc;
}

// Index reading

std::vector<uint32_t> ReadAllIndices(const ParsedDocument& doc, const json& primitive, size_t vertexCount) {
	std::vector<uint32_t> indices;
	if (primitive.contains("indices")) {
		const size_t accessorIndex = primitive["indices"].get<size_t>();
		const AccessorInfo indexAccessor = GetAccessorInfo(doc.gltf, accessorIndex);
		if (NumComponentsForType(indexAccessor.type) != 1) {
			throw std::runtime_error("Index accessor must be SCALAR");
		}

		indices.reserve(indexAccessor.count);
		constexpr size_t kIndexChunkSize = 131072;
		for (size_t firstIndex = 0; firstIndex < indexAccessor.count; firstIndex += kIndexChunkSize) {
			const size_t chunkIndexCount = std::min(kIndexChunkSize, indexAccessor.count - firstIndex);
			size_t indexStride = 0;
			size_t indexComponents = 0;
			int indexComponentType = 0;
			const auto indexBytes = ReadAccessorRawWindow(doc, accessorIndex, firstIndex, chunkIndexCount, &indexStride, &indexComponents, &indexComponentType);
			if (indexComponents != 1) {
				throw std::runtime_error("Index accessor component mismatch");
			}

			for (size_t i = 0; i < chunkIndexCount; ++i) {
				const size_t indexBase = i * indexStride;
				indices.push_back(static_cast<uint32_t>(ReadComponentAsDouble(indexBytes, indexComponentType, indexBase)));
			}
		}
	}
	else {
		indices.reserve(vertexCount);
		for (size_t i = 0; i < vertexCount; ++i) {
			indices.push_back(static_cast<uint32_t>(i));
		}
	}
	return indices;
}

// Normal generation

std::vector<XMFLOAT3> ComputeSmoothNormals(
	const ParsedDocument& doc,
	size_t positionAccessorIndex,
	size_t vertexCount,
	const std::vector<uint32_t>& indices)
{
	// Read all positions
	std::vector<XMFLOAT3> positions(vertexCount);
	constexpr size_t kChunkSize = 32768;
	for (size_t first = 0; first < vertexCount; first += kChunkSize) {
		const size_t count = std::min(kChunkSize, vertexCount - first);
		size_t stride = 0, components = 0;
		int componentType = 0;
		const auto bytes = ReadAccessorRawWindow(doc, positionAccessorIndex, first, count, &stride, &components, &componentType);
		const size_t componentBytes = BytesPerComponent(componentType);
		for (size_t i = 0; i < count; ++i) {
			const size_t base = i * stride;
			positions[first + i] = XMFLOAT3(
				static_cast<float>(ReadComponentAsDouble(bytes, componentType, base + componentBytes * 0)),
				static_cast<float>(ReadComponentAsDouble(bytes, componentType, base + componentBytes * 1)),
				static_cast<float>(ReadComponentAsDouble(bytes, componentType, base + componentBytes * 2)));
		}
	}

	// Accumulate area-weighted face normals per vertex
	std::vector<XMFLOAT3> normals(vertexCount, XMFLOAT3(0.0f, 0.0f, 0.0f));
	for (size_t tri = 0; tri + 2 < indices.size(); tri += 3) {
		const uint32_t i0 = indices[tri + 0];
		const uint32_t i1 = indices[tri + 1];
		const uint32_t i2 = indices[tri + 2];
		if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
			continue;
		}

		const XMFLOAT3& p0 = positions[i0];
		const XMFLOAT3& p1 = positions[i1];
		const XMFLOAT3& p2 = positions[i2];
		const float e1x = p1.x - p0.x, e1y = p1.y - p0.y, e1z = p1.z - p0.z;
		const float e2x = p2.x - p0.x, e2y = p2.y - p0.y, e2z = p2.z - p0.z;

		const XMFLOAT3 faceNormal(
			e1y * e2z - e1z * e2y,
			e1z * e2x - e1x * e2z,
			e1x * e2y - e1y * e2x);

		const float lenSq = faceNormal.x * faceNormal.x + faceNormal.y * faceNormal.y + faceNormal.z * faceNormal.z;
		if (lenSq <= 1e-20f) {
			continue;
		}

		normals[i0].x += faceNormal.x; normals[i0].y += faceNormal.y; normals[i0].z += faceNormal.z;
		normals[i1].x += faceNormal.x; normals[i1].y += faceNormal.y; normals[i1].z += faceNormal.z;
		normals[i2].x += faceNormal.x; normals[i2].y += faceNormal.y; normals[i2].z += faceNormal.z;
	}

	// Normalize
	for (size_t i = 0; i < vertexCount; ++i) {
		const float lenSq = normals[i].x * normals[i].x + normals[i].y * normals[i].y + normals[i].z * normals[i].z;
		if (lenSq > 1e-20f) {
			const float invLen = 1.0f / std::sqrt(lenSq);
			normals[i].x *= invLen;
			normals[i].y *= invLen;
			normals[i].z *= invLen;
		}
	}

	return normals;
}

// Per-primitive Preprocessing

MeshPreprocessResult BuildPrimitivePreprocessData(
	const ParsedDocument& doc,
	const json& primitive,
	const std::string& sourceFilePath,
	size_t meshIndex,
	size_t primitiveIndex)
{
	const int primitiveMode = primitive.value("mode", kTrianglesMode);
	if (primitiveMode != kTrianglesMode) {
		throw std::runtime_error("Only TRIANGLES primitive mode is supported in v1");
	}

	if (!primitive.contains("attributes") || !primitive["attributes"].contains("POSITION")) {
		throw std::runtime_error("Primitive missing POSITION accessor");
	}

	const auto& attributes = primitive["attributes"];

	const size_t positionAccessorIndex = attributes["POSITION"].get<size_t>();
	const AccessorInfo positionAccessor = GetAccessorInfo(doc.gltf, positionAccessorIndex);
	if (NumComponentsForType(positionAccessor.type) != 3) {
		throw std::runtime_error("POSITION accessor must be VEC3");
	}

	const size_t vertexCount = positionAccessor.count;

	bool hasNormals = false;
	size_t normalAccessorIndex = 0;
	AccessorInfo normalAccessor;
	if (attributes.contains("NORMAL")) {
		normalAccessorIndex = attributes["NORMAL"].get<size_t>();
		normalAccessor = GetAccessorInfo(doc.gltf, normalAccessorIndex);
		if (normalAccessor.count != vertexCount || NumComponentsForType(normalAccessor.type) != 3) {
			throw std::runtime_error("NORMAL accessor size/type mismatch");
		}
		hasNormals = true;
	}

	bool hasTexcoords = false;
	size_t texcoordAccessorIndex = 0;
	AccessorInfo texcoordAccessor;
	if (attributes.contains("TEXCOORD_0")) {
		texcoordAccessorIndex = attributes["TEXCOORD_0"].get<size_t>();
		texcoordAccessor = GetAccessorInfo(doc.gltf, texcoordAccessorIndex);
		if (texcoordAccessor.count != vertexCount || NumComponentsForType(texcoordAccessor.type) != 2) {
			throw std::runtime_error("TEXCOORD_0 accessor size/type mismatch");
		}
		hasTexcoords = true;
	}

	unsigned int meshFlags = VertexFlags::VERTEX_NORMALS; // Always present: authored or generated
	if (hasTexcoords) {
		meshFlags |= VertexFlags::VERTEX_TEXCOORDS;
	}

	const uint8_t vertexSize = static_cast<uint8_t>(sizeof(XMFLOAT3) + sizeof(XMFLOAT3) + (hasTexcoords ? sizeof(XMFLOAT2) : 0));

	CLodCacheLoader::MeshCacheIdentity cacheIdentity{};
	cacheIdentity.sourceIdentifier = NormalizeCacheSourcePath(sourceFilePath);
	cacheIdentity.primPath = "/glTF/Mesh/" + std::to_string(meshIndex) + "/Primitive/" + std::to_string(primitiveIndex);
	cacheIdentity.subsetName = "";

	auto prebuiltData = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);

	MeshIngestBuilder ingest(vertexSize, 0, meshFlags);
	if (prebuiltData.has_value()) {
		return MeshPreprocessResult(std::move(ingest), std::move(cacheIdentity), std::move(prebuiltData));
	}

	// Read indices early (needed for smooth normal generation and ingest)
	std::vector<uint32_t> allIndices = ReadAllIndices(doc, primitive, vertexCount);

	// Generate smooth normals when the primitive lacks a NORMAL attribute
	std::vector<XMFLOAT3> generatedNormals;
	if (!hasNormals) {
		generatedNormals = ComputeSmoothNormals(doc, positionAccessorIndex, vertexCount, allIndices);
		spdlog::info("glTF: Generated smooth normals for mesh {} primitive {} ({} vertices)",
			meshIndex, primitiveIndex, vertexCount);
	}

	ingest.ReserveVertices(vertexCount);

	constexpr size_t kVertexChunkSize = 32768;
	for (size_t firstVertex = 0; firstVertex < vertexCount; firstVertex += kVertexChunkSize) {
		const size_t chunkVertexCount = std::min(kVertexChunkSize, vertexCount - firstVertex);

		size_t positionStride = 0;
		size_t positionComponents = 0;
		int positionComponentType = 0;
		const auto positionBytes = ReadAccessorRawWindow(doc, positionAccessorIndex, firstVertex, chunkVertexCount, &positionStride, &positionComponents, &positionComponentType);
		const size_t positionComponentBytes = BytesPerComponent(positionComponentType);

		size_t normalStride = 0;
		size_t normalComponents = 0;
		int normalComponentType = 0;
		std::vector<uint8_t> normalBytes;
		size_t normalComponentBytes = 0;
		if (hasNormals) {
			normalBytes = ReadAccessorRawWindow(doc, normalAccessorIndex, firstVertex, chunkVertexCount, &normalStride, &normalComponents, &normalComponentType);
			normalComponentBytes = BytesPerComponent(normalComponentType);
		}

		size_t texcoordStride = 0;
		size_t texcoordComponents = 0;
		int texcoordComponentType = 0;
		std::vector<uint8_t> texcoordBytes;
		size_t texcoordComponentBytes = 0;
		if (hasTexcoords) {
			texcoordBytes = ReadAccessorRawWindow(doc, texcoordAccessorIndex, firstVertex, chunkVertexCount, &texcoordStride, &texcoordComponents, &texcoordComponentType);
			texcoordComponentBytes = BytesPerComponent(texcoordComponentType);
		}

		for (size_t i = 0; i < chunkVertexCount; ++i) {
			const size_t positionBase = i * positionStride;
			const XMFLOAT3 pos(
				static_cast<float>(ReadComponentAsDouble(positionBytes, positionComponentType, positionBase + positionComponentBytes * 0)),
				static_cast<float>(ReadComponentAsDouble(positionBytes, positionComponentType, positionBase + positionComponentBytes * 1)),
				static_cast<float>(ReadComponentAsDouble(positionBytes, positionComponentType, positionBase + positionComponentBytes * 2)));

			XMFLOAT3 normal;
			if (hasNormals) {
				const size_t normalBase = i * normalStride;
				normal = XMFLOAT3(
					static_cast<float>(ReadComponentAsDouble(normalBytes, normalComponentType, normalBase + normalComponentBytes * 0)),
					static_cast<float>(ReadComponentAsDouble(normalBytes, normalComponentType, normalBase + normalComponentBytes * 1)),
					static_cast<float>(ReadComponentAsDouble(normalBytes, normalComponentType, normalBase + normalComponentBytes * 2))
				);
			}
			else {
				normal = generatedNormals[firstVertex + i];
			}

			std::array<std::byte, sizeof(XMFLOAT3) + sizeof(XMFLOAT3) + sizeof(XMFLOAT2)> packedVertex{};
			std::memcpy(packedVertex.data(), &pos, sizeof(XMFLOAT3));
			size_t offset = sizeof(XMFLOAT3);
			std::memcpy(packedVertex.data() + offset, &normal, sizeof(XMFLOAT3));
			offset += sizeof(XMFLOAT3);

			if (hasTexcoords) {
				const size_t texcoordBase = i * texcoordStride;
				const XMFLOAT2 uv(
					static_cast<float>(ReadComponentAsDouble(texcoordBytes, texcoordComponentType, texcoordBase + texcoordComponentBytes * 0)),
					static_cast<float>(ReadComponentAsDouble(texcoordBytes, texcoordComponentType, texcoordBase + texcoordComponentBytes * 1))
				);
				std::memcpy(packedVertex.data() + offset, &uv, sizeof(XMFLOAT2));
			}

			ingest.AppendVertexBytes(packedVertex.data(), vertexSize);
		}
	}

	ingest.ReserveIndices(allIndices.size());
	ingest.AppendIndices(allIndices.data(), allIndices.size());

	if (!prebuiltData.has_value()) {
		ClusterLODPrebuildArtifacts artifacts = ingest.BuildClusterLODArtifacts();

		const bool cacheSaved = CLodCacheLoader::SavePrebuiltLocked(cacheIdentity, artifacts.prebuiltData, artifacts.cacheBuildData.AsPayload());
		if (!cacheSaved) {
			spdlog::warn("Failed to save CLOD cache for {} (mesh {}, primitive {})", sourceFilePath, meshIndex, primitiveIndex);
			prebuiltData = std::move(artifacts.prebuiltData);
		}
		else {
			auto diskBackedPrebuilt = CLodCacheLoader::TryLoadPrebuilt(cacheIdentity);
			if (diskBackedPrebuilt.has_value()) {
				prebuiltData = std::move(diskBackedPrebuilt);
			}
			else {
				prebuiltData = std::move(artifacts.prebuiltData);
			}
		}
	}

	return MeshPreprocessResult(std::move(ingest), std::move(cacheIdentity), std::move(prebuiltData));
}

}

// Public API

namespace GlTFGeometryExtractor {

ExtractionResult ExtractAll(const std::string& filePath) {
	ParsedDocument doc = ParseDocument(filePath);
	LogDeclaredExtensions(doc.gltf, filePath);

	ExtractionResult result;
	result.gltf = std::move(doc.gltf);

	// Re-attach a const reference so helpers still work with the doc
	doc.gltf = result.gltf; // shallow copy of the json; buffers stay in doc

	if (!result.gltf.contains("meshes")) {
		return result;
	}

	struct PrimitiveWorkItem {
		size_t meshIndex = 0;
		size_t primitiveIndex = 0;
		const json* primitive = nullptr;
	};

	const auto& meshArray = result.gltf["meshes"];
	std::vector<PrimitiveWorkItem> workItems;
	std::vector<std::vector<std::optional<MeshPreprocessResult>>> preprocessed;
	preprocessed.resize(meshArray.size());

	for (size_t meshIndex = 0; meshIndex < meshArray.size(); ++meshIndex) {
		const auto& mesh = meshArray[meshIndex];
		if (!mesh.contains("primitives")) {
			continue;
		}

		const auto& primitiveArray = mesh["primitives"];
		preprocessed[meshIndex].resize(primitiveArray.size());

		for (size_t primitiveIndex = 0; primitiveIndex < primitiveArray.size(); ++primitiveIndex) {
			workItems.push_back(PrimitiveWorkItem{
				.meshIndex = meshIndex,
				.primitiveIndex = primitiveIndex,
				.primitive = &primitiveArray[primitiveIndex]
				});
		}
	}

	TaskSchedulerManager::GetInstance().ParallelFor(workItems.size(), [&](size_t workIndex) {
		const PrimitiveWorkItem& workItem = workItems[workIndex];
		preprocessed[workItem.meshIndex][workItem.primitiveIndex] = BuildPrimitivePreprocessData(
			doc,
			*workItem.primitive,
			filePath,
			workItem.meshIndex,
			workItem.primitiveIndex);
		});

	for (const PrimitiveWorkItem& workItem : workItems) {
		auto& preprocessSlot = preprocessed[workItem.meshIndex][workItem.primitiveIndex];
		if (!preprocessSlot.has_value()) {
			throw std::runtime_error("Missing preprocessed primitive data");
		}

		result.primitives.emplace_back(
			workItem.meshIndex,
			workItem.primitiveIndex,
			std::move(preprocessSlot.value()));
	}

	return result;
}

} // namespace GlTFGeometryExtractor
