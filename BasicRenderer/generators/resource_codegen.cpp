#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

static std::string recurse_ns(const json& node, const std::string& prefix) {
	std::string out;
	for (auto& [key, child] : node.items()) {
		std::string full = prefix + "::" + key;
		if (child.is_object() && !child.empty()) {
			out += "namespace " + key + " {\n"
				+ recurse_ns(child, full)
				+ "}\n";
		}
		else {
			out += "inline constexpr std::string_view "
				+ key
				+ " = \"" + full + "\";\n";
		}
	}
	return out;
}

int main(int argc, char* argv[]) {
	//__debugbreak();
	if (argc != 3) {
		std::cerr << "Usage: resource_codegen <data.json> <out.h>\n";
		return 1;
	}

	json data = json::parse(std::ifstream{ argv[1] });

	std::string result = "#pragma once\n\n#include <string>\n\n// GENERATED CODE, DO NOT EDIT\n\n";
	for (auto& [nsName, nsObj] : data.items()) {
		result += "namespace " + nsName + " {\n";
		result += recurse_ns(nsObj, nsName);
		result += "}\n\n";
	}

	std::cout
		<< "----- Generated Code -----\n"
		<< result
		<< "\n----- End Generated Code -----\n";

	const char* out_path = argv[2];
	std::ofstream ofs{ out_path };
	if (!ofs) {
		std::cerr << "Error: could not open output file for writing: "
			<< out_path << "\n";
		return 1;
	}
	ofs << result;
	ofs.close();

	std::cout << "Wrote generated header to: " << out_path << "\n";

	return 0;
}