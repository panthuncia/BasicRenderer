#pragma once
#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_hlsl();

struct Replacement {
    uint32_t startByte;
    uint32_t endByte;
    std::string replacement;
};

void BuildFunctionDefs(std::unordered_map<std::string, std::vector<TSNode>>& functionDefs, const char* preprocessedSource, const TSNode& root) {
    // Search through all children of root to find function_definition nodes
    uint32_t childCount = ts_node_child_count(root);
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode node = ts_node_child(root, i);
        if (std::string(ts_node_type(node)) == "function_definition") {
            // In tree-sitter-hlsl grammar, child #1 is the identifier
            TSNode topDecl = ts_node_child_by_field_name(node, "declarator", static_cast<uint32_t>(strlen("declarator")));
            if (ts_node_is_null(topDecl)) continue;

            // From that, get the inner declarator
            TSNode innerDecl = ts_node_child_by_field_name(topDecl, "declarator", static_cast<uint32_t>(strlen("declarator")));
            if (ts_node_is_null(innerDecl)) continue;

            // Now, find the actual identifier inside 'innerDecl'
            TSNode nameNode = {};
            // If this declarator *is* a bare identifier, use it directly:
            if (std::string(ts_node_type(innerDecl)) == "identifier") {
                nameNode = innerDecl;
            }
            // Otherwise, it's some sort of qualified or templated thing:
            else {
                nameNode = ts_node_child_by_field_name(
                    innerDecl,
                    "name",
                    static_cast<uint32_t>(strlen("name"))
                );
            }

            if (ts_node_is_null(nameNode)) {
                continue;
            }

            // Extract the text out of the source buffer:
            uint32_t start = ts_node_start_byte(nameNode);
            uint32_t end = ts_node_end_byte(nameNode);
            std::string fnName(preprocessedSource + start, end - start);

            TSNode bodyNode = ts_node_child_by_field_name(node, "body", 4);
            if (!functionDefs.contains(fnName)) {
                functionDefs[fnName] = std::vector<TSNode>();
            }
            functionDefs[fnName].push_back(bodyNode);
        }
    }
}

void ParseBRSLResourceIdentifiers(std::unordered_set<std::string>& outMandatoryIdentifiers, std::unordered_set<std::string>& outOptionalIdentifiers, const DxcBuffer* pBuffer, const std::string& entryPointName) {
    const char* preprocessedSource = static_cast<const char*>(pBuffer->Ptr);
    size_t sourceSize = pBuffer->Size;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());

    TSTree* tree = ts_parser_parse_string(parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> functionDefs;

    BuildFunctionDefs(functionDefs, preprocessedSource, root);

    // Prepare for call-graph walk
    std::unordered_set<std::string> visited;
    std::vector<std::string> worklist;
    worklist.push_back(entryPointName);

    // Wherever we discover ResourceDescriptorIndex, we record replacements
    std::vector<Replacement> replacements;
    std::unordered_map<std::string, std::string> indexMap;

    // Helper to process one function body
    auto processBody = [&](const TSNode& bodyNode, auto&& processBodyRef) -> void {
        // Walk the subtree of this body looking for:
        //  a) ResourceDescriptorIndex calls -> record replacements
        //  b) Other function calls -> enqueue them if unvisited
        std::function<void(TSNode)> walk = [&](TSNode node) {
            const char* type = ts_node_type(node);

            if (strcmp(type, "call_expression") == 0) {
                // Check for ResourceDescriptorIndex(...)
                TSNode functionNode =
                    ts_node_child_by_field_name(node, "function", static_cast<uint32_t>(strlen("function")));

                if (ts_node_is_null(functionNode)) return;

                // Pull out function name from source
                uint32_t start = ts_node_start_byte(functionNode);
                uint32_t end = ts_node_end_byte(functionNode);

                // Slice out the raw text
                std::string funcName(preprocessedSource + start, end - start);

                // trim whitespace:
                auto l = funcName.find_first_not_of(" \t\n\r");
                auto r = funcName.find_last_not_of(" \t\n\r");
                if (l != std::string::npos && r != std::string::npos)
                    funcName = funcName.substr(l, r - l + 1);

                auto parseBuiltin = [&]() -> std::string {
                    TSNode argList = ts_node_child_by_field_name(node, "arguments", 9);
                    if (ts_node_named_child_count(argList) == 1) {
                        TSNode argNode = ts_node_named_child(argList, 0);

                        uint32_t start = ts_node_start_byte(argNode);
                        uint32_t end = ts_node_end_byte(argNode);

                        std::string rawText(preprocessedSource + start, end - start);

                        // if it's a quoted string literal, strip the quotes
                        if (rawText.size() >= 2 && rawText.front() == '"' && rawText.back() == '"') {
                            rawText = rawText.substr(1, rawText.size() - 2);
                        }

                        return std::move(rawText);
                    }
                    else {
                        throw std::runtime_error("ResourceDescriptorIndex requires exactly one argument");
                    }
                    };

                if (funcName == "ResourceDescriptorIndex") {
                    outMandatoryIdentifiers.insert(parseBuiltin());
                }
                else if (funcName == "OptionalResourceDescriptorIndex") {
                    outOptionalIdentifiers.insert(parseBuiltin());
                }
                else {
                    // Otherwise, a normal function call:
                    // Enqueue it for later processing if we know its definition
                    if (functionDefs.count(funcName) && !visited.count(funcName)) {
                        visited.insert(funcName);
                        worklist.push_back(funcName);
                    }
                }
            }

            // recurse
            uint32_t n = ts_node_child_count(node);
            for (uint32_t i = 0; i < n; ++i) {
                walk(ts_node_child(node, i));
            }
            };

        walk(bodyNode);
        };

    // Traverse the call graph
    while (!worklist.empty()) {
        std::string fn = worklist.back();
        worklist.pop_back();

        auto it = functionDefs.find(fn);
        if (it == functionDefs.end())
            continue;   // no definition in this file

        for (const auto& bodyNode : it->second) {
            processBody(bodyNode, processBody);
        }
    }

    std::string sourceText(preprocessedSource, sourceSize);
    // Emit transformed code
    std::string output;
    size_t cursor = 0;
    for (const auto& r : replacements) {
        output += sourceText.substr(cursor, r.startByte - cursor);
        output += r.replacement;
        cursor = r.endByte;
    }
    output += sourceText.substr(cursor);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
}

std::string
rewriteResourceDescriptorCalls(const char* preprocessedSource,
    size_t       sourceSize,
    const std::string& entryPointName,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());
    TSTree* tree = ts_parser_parse_string(
        parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> functionDefs;

    BuildFunctionDefs(functionDefs, preprocessedSource, root);

    // Then do the same call-graph walk, but instead of collecting into a set,
    // build a vector<Replacement> by looking up replacementMap[id].
    // Finally, apply the text splice just as you already have:

    // [parse & build functionDefs, then BFS same as collect]

    std::vector<Replacement> replacements;
    std::unordered_set<std::string> visited;
    std::vector<std::string> worklist;
    worklist.push_back(entryPointName);

    auto processBody = [&](const TSNode& bodyNode, auto&& processBodyRef) -> void {
        // Walk the subtree of this body looking for:
        //  a) ResourceDescriptorIndex calls -> record replacements
        //  b) Other function calls -> enqueue them if unvisited
        std::function<void(TSNode)> walk = [&](TSNode node) {
            const char* type = ts_node_type(node);

            if (strcmp(type, "call_expression") == 0) {
                // Check for ResourceDescriptorIndex(...)
                TSNode functionNode =
                    ts_node_child_by_field_name(node, "function", static_cast<uint32_t>(strlen("function")));

                if (ts_node_is_null(functionNode)) return;

                // Pull out function name from source
                uint32_t start = ts_node_start_byte(functionNode);
                uint32_t end = ts_node_end_byte(functionNode);

                // Slice out the raw text
                std::string funcName(preprocessedSource + start, end - start);

                // trim whitespace:
                auto l = funcName.find_first_not_of(" \t\n\r");
                auto r = funcName.find_last_not_of(" \t\n\r");
                if (l != std::string::npos && r != std::string::npos)
                    funcName = funcName.substr(l, r - l + 1);


                if (funcName == "ResourceDescriptorIndex" || funcName == "OptionalResourceDescriptorIndex") {
                    TSNode argList = ts_node_child_by_field_name(node, "arguments", 9);
                    if (ts_node_named_child_count(argList) == 1) {
                        TSNode argNode = ts_node_named_child(argList, 0);

                        uint32_t start = ts_node_start_byte(argNode);
                        uint32_t end = ts_node_end_byte(argNode);

                        std::string rawText(preprocessedSource + start, end - start);

                        // if it's a quoted string literal, strip the quotes
                        if (rawText.size() >= 2 && rawText.front() == '"' && rawText.back() == '"') {
                            rawText = rawText.substr(1, rawText.size() - 2);
                        }

                        std::string identifier = std::move(rawText);
                        if (replacementMap.count(identifier)) {
                            replacements.push_back({
                            ts_node_start_byte(node),
                            ts_node_end_byte(node),
                            replacementMap.at(identifier)
                                });
                        }
                        else {
                            throw std::runtime_error(
                                "ResourceDescriptorIndex identifier does not have mapped replacement: " + identifier);
                        }
                    }
                }
                else {
                    // Otherwise, a normal function call:
                    // Enqueue it for later processing if we know its definition
                    if (functionDefs.count(funcName) && !visited.count(funcName)) {
                        visited.insert(funcName);
                        worklist.push_back(funcName);
                    }
                }
            }

            // recurse
            uint32_t n = ts_node_child_count(node);
            for (uint32_t i = 0; i < n; ++i) {
                walk(ts_node_child(node, i));
            }
            };

        walk(bodyNode);
        };

    // Traverse the call graph
    while (!worklist.empty()) {
        std::string fn = worklist.back();
        worklist.pop_back();

        auto it = functionDefs.find(fn);
        if (it == functionDefs.end())
            continue;   // no definition in this file

        for (const auto& bodyNode : it->second) {
            processBody(bodyNode, processBody);
        }
    }

    // Sort replacements by startByte
    std::sort(replacements.begin(), replacements.end(),
        [](const Replacement& a, const Replacement& b) {
            return a.startByte < b.startByte;
        });

    std::string source(preprocessedSource, sourceSize);
    std::string out;
    size_t cursor = 0;
    for (auto& r : replacements) {
        out.append(source, cursor, r.startByte - cursor);
        out += r.replacement;
        cursor = r.endByte;
    }
    out.append(source, cursor);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return out;
}

static uint32_t
shrinkToIncludeDecorators(const char* src, uint32_t origStart)
{
    uint32_t newStart = origStart;
    while (newStart > 0) {
        // find the '\n' that ends the *previous* line
        auto prevNL = std::string_view(src, newStart).rfind('\n');
        if (prevNL == std::string::npos) break;
        // prevNL points at the '\n' before the declaration line,
        // so the line we care about runs from (prevNL_of_that + 1) .. prevNL-1
        size_t lineEnd = prevNL;
        size_t lineBegin = std::string_view(src, prevNL).rfind('\n');
        if (lineBegin == std::string_view::npos) lineBegin = 0;
        else                                    lineBegin += 1;

        // extract that line and trim it
        auto   len = lineEnd - lineBegin;
        auto   sv = std::string_view(src + lineBegin, len);
        auto   first = sv.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            // blank line: skip it, but keep going up
            newStart = (uint32_t)lineBegin;
            continue;
        }
        if (sv[first] == '[') {
            // decorator: include this line too
            newStart = (uint32_t)lineBegin;
            continue;
        }
        // otherwise: stop looking
        break;
    }
    return newStart;
}

struct Range { uint32_t start, end; };
std::string
pruneUnusedCode(const char* preprocessedSource,
    size_t       sourceSize,
    const std::string& entryPointName,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());
    TSTree* tree = ts_parser_parse_string(parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> bodyMap, defMap;
    uint32_t topCount = ts_node_child_count(root);
    for (uint32_t i = 0; i < topCount; ++i) {
        TSNode node = ts_node_child(root, i);
        if (std::string(ts_node_type(node)) != "function_definition") continue;

        // extract the function name
        TSNode decl1 = ts_node_child_by_field_name(node, "declarator", static_cast<uint32_t>(strlen("declarator")));
        TSNode decl2 = !ts_node_is_null(decl1)
            ? ts_node_child_by_field_name(decl1, "declarator", static_cast<uint32_t>(strlen("declarator")))
            : TSNode{};
        TSNode nameNode = {};
        if (!ts_node_is_null(decl2) && std::string(ts_node_type(decl2)) == "identifier") {
            nameNode = decl2;
        }
        else if (!ts_node_is_null(decl2)) {
            nameNode = ts_node_child_by_field_name(decl2, "name", static_cast<uint32_t>(strlen("name")));
        }
        if (ts_node_is_null(nameNode)) continue;

        auto s = ts_node_start_byte(nameNode);
        auto e = ts_node_end_byte(nameNode);
        std::string fnName(preprocessedSource + s, e - s);

        // store the full def node and its body

        if (!defMap.contains(fnName)) {
            defMap[fnName] = {};
        }

        if (!bodyMap.contains(fnName)) {
            bodyMap[fnName] = {};
        }

        defMap[fnName].push_back(node);
        bodyMap[fnName].push_back(ts_node_child_by_field_name(node, "body", static_cast<uint32_t>(strlen("body"))));
    }

    // BFS from entryPointName to find reachable functions
    std::unordered_set<std::string> visited{ entryPointName };
    std::vector<std::string> work{ entryPointName };

    auto enqueueCalls = [&](TSNode body) {
        std::function<void(TSNode)> walk = [&](TSNode n) {
            if (ts_node_is_null(n)) return;
            if (std::string(ts_node_type(n)) == "call_expression") {
                TSNode fn = ts_node_child_by_field_name(n, "function", static_cast<uint32_t>(strlen("function")));
                if (!ts_node_is_null(fn)) {

                    TSNode args = ts_node_child_by_field_name(n, "arguments", static_cast<uint32_t>(strlen("arguments")));

                    uint32_t start = ts_node_start_byte(fn);
                    uint32_t end = ts_node_end_byte(args);    // include closing ')'

                    std::string callSig(
                        preprocessedSource + start,
                        end - start
                    );

                    uint32_t ss = ts_node_start_byte(fn), ee = ts_node_end_byte(fn);
                    std::string called(preprocessedSource + ss, ee - ss);
                    auto l = called.find_first_not_of(" \t\r\n"),
                        r = called.find_last_not_of(" \t\r\n");
                    if (l != std::string::npos && r != std::string::npos)
                        called = called.substr(l, r - l + 1);
                    if (bodyMap.count(called) && !visited.count(called)) {
                        visited.insert(called);
                        work.push_back(called);
                    }
                }
            }
            uint32_t c = ts_node_child_count(n);
            for (uint32_t i = 0; i < c; ++i) walk(ts_node_child(n, i));
            };
        walk(body);
        };

    while (!work.empty()) {
        auto fn = work.back(); work.pop_back();
        auto it = bodyMap.find(fn);
        if (it != bodyMap.end())
            for (auto& body : it->second) {
                enqueueCalls(body);
            }
    }

    std::vector<Range> removeRanges;
    for (auto const& kv : defMap) {
        if (!visited.count(kv.first)) {
            for (auto& defNode : kv.second) {
                uint32_t origStart = ts_node_start_byte(defNode);
                uint32_t origEnd = ts_node_end_byte(defNode);
                uint32_t adjStart = shrinkToIncludeDecorators( //TODO: Probably not needed with new grammar
                    preprocessedSource, origStart);
                removeRanges.push_back({ adjStart, origEnd });
            }
        }
    }

    // merge overlapping removeRanges
    std::sort(removeRanges.begin(), removeRanges.end(),
        [](auto& a, auto& b) { return a.start < b.start; });
    std::vector<Range> merged;
    for (auto& r : removeRanges) {
        if (merged.empty() || r.start > merged.back().end) {
            merged.push_back(r);
        }
        else {
            merged.back().end = std::max(merged.back().end, r.end);
        }
    }

    // keepRanges as complement of merged removeRanges
    std::vector<Range> keep;
    uint32_t lastEnd = 0;
    for (auto& r : merged) {
        if (lastEnd < r.start)
            keep.push_back({ lastEnd, r.start });
        lastEnd = std::max(lastEnd, r.end);
    }
    if (lastEnd < sourceSize)
        keep.push_back({ lastEnd, (uint32_t)sourceSize });

    // splice keepRanges together
    std::string out;
    out.reserve(sourceSize);
    for (auto& r : keep)
        out.append(preprocessedSource + r.start, r.end - r.start);

    // cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return out;
}

// SM 6.8 library parsing
struct ShaderEntryPointDesc
{
    std::string functionName;     // e.g. "MyNode"
    std::string shaderAttribute;  // e.g. "node", "compute", "pixel", etc. (value inside Shader("..."))
    uint32_t    functionStartByte = 0; // ts_node_start_byte(function_definition)
    uint32_t    functionEndByte = 0; // ts_node_end_byte(function_definition)
};

struct ShaderLibraryBRSLAnalysis
{
    std::vector<ShaderEntryPointDesc> entryPoints;
    std::unordered_set<std::string> mandatoryIdentifiers;
    std::unordered_set<std::string> optionalIdentifiers;
};

static inline bool IsIdentChar(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

static std::string TrimCopy(std::string_view sv)
{
    size_t l = 0;
    while (l < sv.size() && (sv[l] == ' ' || sv[l] == '\t' || sv[l] == '\r' || sv[l] == '\n')) ++l;
    size_t r = sv.size();
    while (r > l && (sv[r - 1] == ' ' || sv[r - 1] == '\t' || sv[r - 1] == '\r' || sv[r - 1] == '\n')) --r;
    return std::string(sv.substr(l, r - l));
}

static bool IEquals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

// Remove //... and /*...*/ from a small snippet (decorator block).
static std::string StripComments(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); )
    {
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/')
        {
            // line comment
            i += 2;
            while (i < s.size() && s[i] != '\n') ++i;
            continue;
        }
        if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '*')
        {
            // block comment
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
            if (i + 1 < s.size()) i += 2;
            continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}

static std::optional<std::string> ExtractFunctionName(
    const char* preprocessedSource,
    const TSNode& functionDefNode)
{
    TSNode topDecl = ts_node_child_by_field_name(
        functionDefNode, "declarator",
        static_cast<uint32_t>(strlen("declarator")));
    if (ts_node_is_null(topDecl)) return std::nullopt;

    TSNode innerDecl = ts_node_child_by_field_name(
        topDecl, "declarator",
        static_cast<uint32_t>(strlen("declarator")));
    if (ts_node_is_null(innerDecl)) return std::nullopt;

    TSNode nameNode = {};
    if (std::string(ts_node_type(innerDecl)) == "identifier")
    {
        nameNode = innerDecl;
    }
    else
    {
        nameNode = ts_node_child_by_field_name(
            innerDecl, "name",
            static_cast<uint32_t>(strlen("name")));
    }

    if (ts_node_is_null(nameNode)) return std::nullopt;

    uint32_t start = ts_node_start_byte(nameNode);
    uint32_t end = ts_node_end_byte(nameNode);
    return std::string(preprocessedSource + start, end - start);
}

// Returns the string inside Shader("...") if present; otherwise nullopt.
static std::optional<std::string> TryParseShaderDecorator(std::string_view decoratorBlockRaw)
{
    // Make false-positives less likely by removing comments first.
    std::string cleaned = StripComments(decoratorBlockRaw);
    std::string_view s(cleaned);

    // scan for word "shader" (case-insensitive), with identifier boundaries
    for (size_t i = 0; i + 6 <= s.size(); ++i)
    {
        if (!IEquals(s.substr(i, 6), "shader")) continue;

        // word boundary
        if (i > 0 && IsIdentChar(s[i - 1])) continue;
        if (i + 6 < s.size() && IsIdentChar(s[i + 6])) continue;

        size_t p = i + 6;

        // skip whitespace
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
        if (p >= s.size() || s[p] != '(') continue;
        ++p;

        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
        if (p >= s.size() || s[p] != '"') continue;

        // parse a basic string literal (supports \" escaping)
        ++p;
        std::string value;
        while (p < s.size())
        {
            char c = s[p++];
            if (c == '\\' && p < s.size())
            {
                char next = s[p++];
                // keep simple: accept \" and \\ and \n \t
                if (next == '"' || next == '\\') value.push_back(next);
                else if (next == 'n') value.push_back('\n');
                else if (next == 't') value.push_back('\t');
                else value.push_back(next);
                continue;
            }
            if (c == '"') break;
            value.push_back(c);
        }

        // We don’t strictly need to verify closing ) and ] here.
        if (!value.empty())
            return value;

        return std::string{}; // allow empty Shader("")
    }

    return std::nullopt;
}

static std::vector<ShaderEntryPointDesc> ExtractShaderLibraryEntryPoints(
    const char* preprocessedSource,
    size_t sourceSize,
    const TSNode& root)
{
    std::vector<ShaderEntryPointDesc> out;

    uint32_t topCount = ts_node_child_count(root);
    out.reserve(topCount / 4);

    for (uint32_t i = 0; i < topCount; ++i)
    {
        TSNode node = ts_node_child(root, i);
        if (std::string(ts_node_type(node)) != "function_definition") {
            continue;
        }

        auto nameOpt = ExtractFunctionName(preprocessedSource, node);
        if (!nameOpt.has_value()) {
            continue;
        }

        uint32_t fnStart = ts_node_start_byte(node);
        uint32_t fnEnd = ts_node_end_byte(node);

        // decorator expansion:
        std::string_view decorBlock(preprocessedSource + fnStart, fnEnd - fnStart);

        auto shaderAttr = TryParseShaderDecorator(decorBlock);
        if (!shaderAttr.has_value())
            continue; // not a library entrypoint

        ShaderEntryPointDesc ep;
        ep.functionName = std::move(*nameOpt);
        ep.shaderAttribute = std::move(*shaderAttr);
        ep.functionStartByte = fnStart;
        ep.functionEndByte = fnEnd;
        out.push_back(std::move(ep));
    }

    // stable ordering by source position
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) {
        return a.functionStartByte < b.functionStartByte;
        });

    // TODO: dedupe function names? This would probably break anyway

    return out;
}

static void CollectBRSLIdentifiersFromRoots(
    std::unordered_set<std::string>& outMandatory,
    std::unordered_set<std::string>& outOptional,
    const char* preprocessedSource,
    const std::unordered_map<std::string, std::vector<TSNode>>& functionDefs,
    const std::vector<std::string>& rootFunctions)
{
    std::unordered_set<std::string> visited;
    std::vector<std::string> worklist;
    worklist.reserve(rootFunctions.size() * 2);

    for (auto& r : rootFunctions)
    {
        if (visited.insert(r).second)
            worklist.push_back(r);
    }

    auto parseSingleBuiltinArg = [&](TSNode callExprNode) -> std::string
        {
            TSNode argList = ts_node_child_by_field_name(
                callExprNode, "arguments",
                static_cast<uint32_t>(strlen("arguments")));
            if (ts_node_is_null(argList) || ts_node_named_child_count(argList) != 1)
                throw std::runtime_error("ResourceDescriptorIndex requires exactly one argument");

            TSNode argNode = ts_node_named_child(argList, 0);
            uint32_t start = ts_node_start_byte(argNode);
            uint32_t end = ts_node_end_byte(argNode);

            std::string raw(preprocessedSource + start, end - start);

            // strip quotes if it’s a string literal
            if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
                raw = raw.substr(1, raw.size() - 2);

            return raw;
        };

    auto trimInPlace = [](std::string& s)
        {
            auto l = s.find_first_not_of(" \t\r\n");
            auto r = s.find_last_not_of(" \t\r\n");
            if (l == std::string::npos) { s.clear(); return; }
            s = s.substr(l, r - l + 1);
        };

    // Walk a function body: record builtins, enqueue calls.
    auto processBody = [&](const TSNode& bodyNode)
        {
            std::function<void(TSNode)> walk = [&](TSNode node)
                {
                    if (ts_node_is_null(node)) return;

                    if (strcmp(ts_node_type(node), "call_expression") == 0)
                    {
                        TSNode fnNode = ts_node_child_by_field_name(
                            node, "function",
                            static_cast<uint32_t>(strlen("function")));
                        if (!ts_node_is_null(fnNode))
                        {
                            uint32_t s = ts_node_start_byte(fnNode);
                            uint32_t e = ts_node_end_byte(fnNode);
                            std::string funcName(preprocessedSource + s, e - s);
                            trimInPlace(funcName);

                            if (funcName == "ResourceDescriptorIndex")
                            {
                                outMandatory.insert(parseSingleBuiltinArg(node));
                            }
                            else if (funcName == "OptionalResourceDescriptorIndex")
                            {
                                outOptional.insert(parseSingleBuiltinArg(node));
                            }
                            else
                            {
                                // normal call -> enqueue if we have its definition
                                auto it = functionDefs.find(funcName);
                                if (it != functionDefs.end())
                                {
                                    if (visited.insert(funcName).second)
                                        worklist.push_back(funcName);
                                }
                            }
                        }
                    }

                    uint32_t n = ts_node_child_count(node);
                    for (uint32_t i = 0; i < n; ++i)
                        walk(ts_node_child(node, i));
                };

            walk(bodyNode);
        };

    while (!worklist.empty())
    {
        std::string fn = std::move(worklist.back());
        worklist.pop_back();

        auto it = functionDefs.find(fn);
        if (it == functionDefs.end())
            continue;

        for (const TSNode& body : it->second)
            processBody(body);
    }
}

ShaderLibraryBRSLAnalysis AnalyzePreprocessedShaderLibrary(
    const DxcBuffer* pPreprocessedBuffer)
{
    const char* preprocessedSource = static_cast<const char*>(pPreprocessedBuffer->Ptr);
    size_t sourceSize = pPreprocessedBuffer->Size;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());

    TSTree* tree = ts_parser_parse_string(
        parser, nullptr, preprocessedSource,
        static_cast<uint32_t>(sourceSize));

    TSNode root = ts_tree_root_node(tree);

    // Build function name -> bodies
    std::unordered_map<std::string, std::vector<TSNode>> functionDefs;
    BuildFunctionDefs(functionDefs, preprocessedSource, root);

    ShaderLibraryBRSLAnalysis result;
    result.entryPoints = ExtractShaderLibraryEntryPoints(preprocessedSource, sourceSize, root);

    // Convert entrypoint descs to just root function names
    std::vector<std::string> roots;
    roots.reserve(result.entryPoints.size());
    for (auto& ep : result.entryPoints)
        roots.push_back(ep.functionName);

    // Union of BRSL ids over all entrypoints
    CollectBRSLIdentifiersFromRoots(
        result.mandatoryIdentifiers,
        result.optionalIdentifiers,
        preprocessedSource,
        functionDefs,
        roots);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return result;
}

static inline void TrimInPlace(std::string& s)
{
    auto l = s.find_first_not_of(" \t\n\r");
    auto r = s.find_last_not_of(" \t\n\r");
    if (l == std::string::npos) { s.clear(); return; }
    s = s.substr(l, r - l + 1);
}

static std::string ParseSingleStringArgOrThrow(const char* src, TSNode callExprNode)
{
    TSNode argList = ts_node_child_by_field_name(
        callExprNode, "arguments",
        static_cast<uint32_t>(strlen("arguments")));

    if (ts_node_is_null(argList) || ts_node_named_child_count(argList) != 1)
        throw std::runtime_error("ResourceDescriptorIndex requires exactly one argument");

    TSNode argNode = ts_node_named_child(argList, 0);

    uint32_t start = ts_node_start_byte(argNode);
    uint32_t end = ts_node_end_byte(argNode);

    std::string raw(src + start, end - start);

    // If it's a quoted string literal, strip the quotes
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
        raw = raw.substr(1, raw.size() - 2);

    return raw;
}

std::string rewriteResourceDescriptorCallsMultiRoots(
    const char* preprocessedSource,
    size_t sourceSize,
    const std::vector<std::string>& rootFunctionNames,
    const std::unordered_map<std::string, std::string>& replacementMap)
{
    if (!preprocessedSource || sourceSize == 0)
        return {};
    if (rootFunctionNames.empty())
        return std::string(preprocessedSource, sourceSize);

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());

    TSTree* tree = ts_parser_parse_string(
        parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    std::unordered_map<std::string, std::vector<TSNode>> functionDefs;
    BuildFunctionDefs(functionDefs, preprocessedSource, root);

    std::vector<Replacement> replacements;
    replacements.reserve(128);

    std::unordered_set<std::string> visited;
    visited.reserve(rootFunctionNames.size() * 2);

    std::vector<std::string> worklist;
    worklist.reserve(rootFunctionNames.size() * 2);

    // Seed all roots
    for (auto& r : rootFunctionNames)
    {
        if (visited.insert(r).second)
            worklist.push_back(r);
    }

    auto processBody = [&](const TSNode& bodyNode)
        {
            std::function<void(TSNode)> walk = [&](TSNode node)
                {
                    if (ts_node_is_null(node)) return;

                    if (strcmp(ts_node_type(node), "call_expression") == 0)
                    {
                        TSNode functionNode = ts_node_child_by_field_name(
                            node, "function",
                            static_cast<uint32_t>(strlen("function")));
                        if (!ts_node_is_null(functionNode))
                        {
                            uint32_t s = ts_node_start_byte(functionNode);
                            uint32_t e = ts_node_end_byte(functionNode);
                            std::string funcName(preprocessedSource + s, e - s);
                            TrimInPlace(funcName);

                            if (funcName == "ResourceDescriptorIndex" ||
                                funcName == "OptionalResourceDescriptorIndex")
                            {
                                std::string identifier = ParseSingleStringArgOrThrow(preprocessedSource, node);

                                auto it = replacementMap.find(identifier);
                                if (it == replacementMap.end())
                                {
                                    throw std::runtime_error(
                                        "ResourceDescriptorIndex identifier does not have mapped replacement: " + identifier);
                                }

                                replacements.push_back(Replacement{
                                    ts_node_start_byte(node),
                                    ts_node_end_byte(node),
                                    it->second
                                    });
                            }
                            else
                            {
                                // Normal function call: enqueue if we have a definition.
                                if (functionDefs.count(funcName) && !visited.count(funcName))
                                {
                                    visited.insert(funcName);
                                    worklist.push_back(funcName);
                                }
                            }
                        }
                    }

                    uint32_t n = ts_node_child_count(node);
                    for (uint32_t i = 0; i < n; ++i)
                        walk(ts_node_child(node, i));
                };

            walk(bodyNode);
        };

    while (!worklist.empty())
    {
        std::string fn = std::move(worklist.back());
        worklist.pop_back();

        auto it = functionDefs.find(fn);
        if (it == functionDefs.end())
            continue; // no definition in this file

        for (const TSNode& bodyNode : it->second)
            processBody(bodyNode);
    }

    // Sort replacements by startByte and sanity-check overlaps
    std::sort(replacements.begin(), replacements.end(),
        [](const Replacement& a, const Replacement& b) { return a.startByte < b.startByte; });

    for (size_t i = 1; i < replacements.size(); ++i)
    {
        if (replacements[i - 1].endByte > replacements[i].startByte)
            throw std::runtime_error("Overlapping replacements detected; rewriting would be ambiguous.");
    }

    // Apply splices
    std::string source(preprocessedSource, sourceSize);
    std::string out;
    out.reserve(source.size() + replacements.size() * 16);

    size_t cursor = 0;
    for (const auto& r : replacements)
    {
        out.append(source, cursor, r.startByte - cursor);
        out += r.replacement;
        cursor = r.endByte;
    }
    out.append(source, cursor);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return out;
}

std::string pruneUnusedCodeMultiRoots(
    const char* preprocessedSource,
    size_t sourceSize,
    const std::vector<std::string>& rootFunctionNames)
{
    if (!preprocessedSource || sourceSize == 0)
        return {};
    if (rootFunctionNames.empty())
        return std::string(preprocessedSource, sourceSize);

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_hlsl());

    TSTree* tree = ts_parser_parse_string(
        parser, nullptr, preprocessedSource, static_cast<uint32_t>(sourceSize));
    TSNode root = ts_tree_root_node(tree);

    // function name -> [function_definition nodes], and [body nodes]
    std::unordered_map<std::string, std::vector<TSNode>> defMap;
    std::unordered_map<std::string, std::vector<TSNode>> bodyMap;

    uint32_t topCount = ts_node_child_count(root);
    for (uint32_t i = 0; i < topCount; ++i)
    {
        TSNode node = ts_node_child(root, i);
        if (std::string(ts_node_type(node)) != "function_definition")
            continue;

        // Extract function name (same pattern you used)
        TSNode decl1 = ts_node_child_by_field_name(
            node, "declarator",
            static_cast<uint32_t>(strlen("declarator")));
        TSNode decl2 = !ts_node_is_null(decl1)
            ? ts_node_child_by_field_name(
                decl1, "declarator",
                static_cast<uint32_t>(strlen("declarator")))
            : TSNode{};

        TSNode nameNode = {};
        if (!ts_node_is_null(decl2) && std::string(ts_node_type(decl2)) == "identifier")
        {
            nameNode = decl2;
        }
        else if (!ts_node_is_null(decl2))
        {
            nameNode = ts_node_child_by_field_name(
                decl2, "name",
                static_cast<uint32_t>(strlen("name")));
        }

        if (ts_node_is_null(nameNode))
            continue;

        uint32_t s = ts_node_start_byte(nameNode);
        uint32_t e = ts_node_end_byte(nameNode);
        std::string fnName(preprocessedSource + s, e - s);

        defMap[fnName].push_back(node);

        TSNode body = ts_node_child_by_field_name(
            node, "body",
            static_cast<uint32_t>(strlen("body")));
        bodyMap[fnName].push_back(body);
    }

    // Multi-root BFS/DFS over the call graph
    std::unordered_set<std::string> visited;
    visited.reserve(rootFunctionNames.size() * 2);

    std::vector<std::string> work;
    work.reserve(rootFunctionNames.size() * 2);

    for (auto& r : rootFunctionNames)
    {
        if (visited.insert(r).second)
            work.push_back(r);
    }

    auto enqueueCalls = [&](TSNode body)
        {
            std::function<void(TSNode)> walk = [&](TSNode n)
                {
                    if (ts_node_is_null(n)) return;

                    if (std::string(ts_node_type(n)) == "call_expression")
                    {
                        TSNode fnNode = ts_node_child_by_field_name(
                            n, "function",
                            static_cast<uint32_t>(strlen("function")));

                        if (!ts_node_is_null(fnNode))
                        {
                            uint32_t ss = ts_node_start_byte(fnNode);
                            uint32_t ee = ts_node_end_byte(fnNode);

                            std::string called(preprocessedSource + ss, ee - ss);
                            TrimInPlace(called);

                            if (bodyMap.count(called) && visited.insert(called).second)
                                work.push_back(called);
                        }
                    }

                    uint32_t c = ts_node_child_count(n);
                    for (uint32_t i = 0; i < c; ++i)
                        walk(ts_node_child(n, i));
                };

            walk(body);
        };

    while (!work.empty())
    {
        std::string fn = std::move(work.back());
        work.pop_back();

        auto it = bodyMap.find(fn);
        if (it == bodyMap.end())
            continue;

        for (TSNode body : it->second)
            enqueueCalls(body);
    }

    // Build remove ranges for anything not visited
    std::vector<Range> removeRanges;
    removeRanges.reserve(defMap.size());

    for (auto const& kv : defMap)
    {
        if (visited.count(kv.first))
            continue;

        for (TSNode defNode : kv.second)
        {
            uint32_t origStart = ts_node_start_byte(defNode);
            uint32_t origEnd = ts_node_end_byte(defNode);

			uint32_t adjStart = shrinkToIncludeDecorators(preprocessedSource, origStart); // TODO: Probably not needed with new grammar
            removeRanges.push_back({ adjStart, origEnd });
        }
    }

    if (removeRanges.empty())
    {
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return std::string(preprocessedSource, sourceSize);
    }

    // Merge overlapping remove ranges
    (std::sort)(removeRanges.begin(), removeRanges.end(),
        [](const Range& a, const Range& b) { return a.start < b.start; });

    std::vector<Range> merged;
    merged.reserve(removeRanges.size());

    for (auto& r : removeRanges)
    {
        if (merged.empty() || r.start > merged.back().end)
        {
            merged.push_back(r);
        }
        else
        {
            merged.back().end = (std::max)(merged.back().end, r.end);
        }
    }

    // Complement => keep ranges
    std::vector<Range> keep;
    keep.reserve(merged.size() + 1);

    uint32_t lastEnd = 0;
    for (auto& r : merged)
    {
        if (lastEnd < r.start)
            keep.push_back({ lastEnd, r.start });
        lastEnd = (std::max)(lastEnd, r.end);
    }
    if (lastEnd < sourceSize)
        keep.push_back({ lastEnd, static_cast<uint32_t>(sourceSize) });

    // Splice kept ranges
    std::string out;
    out.reserve(sourceSize);
    for (auto& r : keep)
        out.append(preprocessedSource + r.start, r.end - r.start);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    return out;
}

struct PreprocessedLibraryResult
{
    std::vector<ShaderEntryPointDesc> entryPoints;

    // Stable-ordered (sorted) for determinism
    std::vector<std::string> mandatoryIDs;
    std::vector<std::string> optionalIDs;

    // Maps BRSL identifier string -> replacement token (e.g. "ResourceDescriptorIndex7")
    std::unordered_map<std::string, std::string> replacementMap;

    uint64_t resourceIDsHash = 0;

    // Final transformed source (rewritten + pruned)
    std::string finalSource;
};

static inline bool StartsWith(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

uint64_t hash_list(const std::vector<std::string>& list) {
    const uint64_t FNV_offset = 146527ULL;
    const uint64_t FNV_prime = 1099511628211ULL;
    uint64_t h = FNV_offset;
    for (auto& s : list) {
        uint32_t L = uint32_t(s.size());
        // mix in the length
        for (int i = 0; i < 4; ++i) {
            h ^= (uint8_t)(L >> (i * 8));
            h *= FNV_prime;
        }
        // mix in each character
        for (unsigned char c : s) {
            h ^= c;
            h *= FNV_prime;
        }
    }
    return h;
}

PreprocessedLibraryResult PreprocessShaderLibrary(
    const DxcBuffer& preprocessedBuffer)
{
    const char* src = static_cast<const char*>(preprocessedBuffer.Ptr);
    const size_t srcSize = preprocessedBuffer.Size;

    if (!src || srcSize == 0)
        throw std::runtime_error("PreprocessShaderLibrary: empty preprocessed buffer");

    // Parse library once to find [Shader("...")] entrypoints + union of BRSL ids (mandatory/optional)
    ShaderLibraryBRSLAnalysis analysis = AnalyzePreprocessedShaderLibrary(&preprocessedBuffer);

    if (analysis.entryPoints.empty()) {
        throw std::runtime_error("Shader library contains no functions decorated with [Shader(\"...\")]");
    }

    PreprocessedLibraryResult out = {};
    out.entryPoints = std::move(analysis.entryPoints);

    // Deterministic ordering for indices/hash (unordered_set iteration is nondeterministic)
    out.mandatoryIDs.assign(analysis.mandatoryIdentifiers.begin(), analysis.mandatoryIdentifiers.end());
    out.optionalIDs.assign(analysis.optionalIdentifiers.begin(), analysis.optionalIdentifiers.end());
    std::sort(out.mandatoryIDs.begin(), out.mandatoryIDs.end());
    std::sort(out.optionalIDs.begin(), out.optionalIDs.end());

    // Build replacement map with stable indices: mandatory first, then optional
    out.replacementMap.reserve(out.mandatoryIDs.size() + out.optionalIDs.size());

    uint32_t nextIndex = 0;
    for (const auto& id : out.mandatoryIDs) {
        out.replacementMap[id] = "ResourceDescriptorIndex" + std::to_string(nextIndex++);
    }

    for (const auto& id : out.optionalIDs) {
        out.replacementMap[id] = "ResourceDescriptorIndex" + std::to_string(nextIndex++);
    }

    // Hash IDs deterministically
    {
        std::vector<std::string> combined;
        combined.reserve(out.mandatoryIDs.size() + out.optionalIDs.size());
        combined.insert(combined.end(), out.mandatoryIDs.begin(), out.mandatoryIDs.end());
        combined.insert(combined.end(), out.optionalIDs.begin(), out.optionalIDs.end());
        out.resourceIDsHash = hash_list(combined);
    }

    // Roots = function names of decorated entry points
    std::vector<std::string> roots;
    roots.reserve(out.entryPoints.size());
    for (const auto& ep : out.entryPoints) {
        roots.push_back(ep.functionName);
    }

    // Rewrite calls from all roots (multi-root rewrite)
    std::string rewritten = rewriteResourceDescriptorCallsMultiRoots(src, srcSize, roots, out.replacementMap);

    // Prune unreachable functions from all roots (multi-root prune)
    std::string pruned = pruneUnusedCodeMultiRoots(rewritten.c_str(), rewritten.size(), roots);

    out.finalSource = std::move(pruned);
    return out;
}