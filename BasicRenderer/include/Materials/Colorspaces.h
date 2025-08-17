#pragma once

#include <string>

enum class ColorSpaces {
	linear,
	Rec709,
};

struct ColorSpace {
	float r[2];
	float g[2];
	float b[2];
	float D65[2];
};

static constexpr ColorSpace Rec709 = {
	{ 0.64f, 0.33f }, // Red
	{ 0.30f, 0.60f }, // Green
	{ 0.15f, 0.06f }, // Blue
	{ 0.3127f, 0.3290f } // D65 white point
};

static ColorSpaces StringToColorSpace(const std::string& str) {
	auto lowerStr = ToLower(str);
	if (lowerStr == "linear") {
		return ColorSpaces::linear;
	} else if (lowerStr == "rec709" || lowerStr == "srgb") {
		return ColorSpaces::Rec709;
	} else {
		throw std::invalid_argument("Unknown color space: " + str);
	}
}