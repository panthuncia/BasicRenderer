#pragma once

#include <unordered_map>
#include <string>

#include "AnimationClip.h"

class Animation {
public:
	Animation(std::string name) : name(name) {
	}
	std::string name;
	std::unordered_map<uint32_t, std::shared_ptr<AnimationClip>> nodesMap;

};