#pragma once
#include <unordered_map>
#include <unordered_set>

enum class ImageFiletype {
	UNKNOWN,
	PNG,
	JPEG,
	BMP,
	DDS
};

enum class ImageLoader {
	UNKNOWN,
	STBImage,
	DirectXTex
};

static std::unordered_map<ImageFiletype, ImageLoader> imageFiletypeToLoader = {
	{ImageFiletype::PNG, ImageLoader::DirectXTex},
	{ImageFiletype::JPEG, ImageLoader::STBImage},
	{ImageFiletype::BMP, ImageLoader::STBImage},
	{ImageFiletype::DDS, ImageLoader::DirectXTex}
};

static std::unordered_map<std::string, ImageFiletype> extensionToFiletype = {
	{".png", ImageFiletype::PNG},
	{".jpg", ImageFiletype::JPEG},
	{".jpeg", ImageFiletype::JPEG},
	{".bmp", ImageFiletype::BMP},
	{".dds", ImageFiletype::DDS},
	{"png", ImageFiletype::PNG},
	{"jpg", ImageFiletype::JPEG},
	{"jpeg", ImageFiletype::JPEG},
	{"bmp", ImageFiletype::BMP},
	{"dds", ImageFiletype::DDS}
};

static std::unordered_set<ImageFiletype> DirectXTexSupportedFiletypes = {
	ImageFiletype::DDS
};

static std::unordered_set<ImageFiletype> STBImageSupportedFiletypes = {
	ImageFiletype::PNG,
	ImageFiletype::JPEG,
	ImageFiletype::BMP
};

enum class SceneFiletype {
	OTHER,
	USD,
};

enum class SceneLoader {
	UNKNOWN,
	Assimp,
	OpenUSD
};

static std::unordered_map<SceneFiletype, SceneLoader> sceneFiletypeToLoader = {
	{SceneFiletype::USD, SceneLoader::OpenUSD},
	{SceneFiletype::OTHER, SceneLoader::Assimp} // default loader
};

static std::unordered_set<std::string> usdFileExtensions = {
	".usd", ".usda", ".usdc", ".usdz",
	"usd", "usda", "usdc", "usdz"
};

inline bool isUsdExt(const std::string& ext) {
	return usdFileExtensions.contains(ext);
}

inline SceneFiletype GetSceneFiletype(const std::string& ext) {
	if (isUsdExt(ext)) {
		return SceneFiletype::USD;
	}
	return SceneFiletype::OTHER;
}

inline SceneLoader GetSceneLoader(const std::string& ext) {
	SceneFiletype filetype = GetSceneFiletype(ext);
	if (sceneFiletypeToLoader.contains(filetype)) {
		return sceneFiletypeToLoader[filetype];
	}
	return SceneLoader::Assimp; // default
}