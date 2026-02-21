#pragma once

#include <slang.h>
#include <slang-com-ptr.h>
#include <slang-com-helper.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

struct SlangReflectedField
{
    std::string path;      // e.g. "Camera.viewProjection" or "Camera.clippingPlanes[3].plane"
    size_t      offset = 0; // byte offset from start of root struct
    size_t      size = 0; // byte size for this field's type (may include padding)
    size_t      stride = 0; // byte stride (size rounded up to alignment); useful for arrays/struct stepping
    size_t      arrayCount = 0; // 0 if not array, ~size_t(0) if unbounded
    std::string typeName;  // as reported by Slang for the field's type layout
};

enum class NumericKind
{
    None,       // struct/array/container/resource/etc.
    SInt,       // signed integer (int, int64, min16int, ...)
    UInt,       // unsigned integer (uint, uint64, ...)
    Float,      // float/half/double/min16float/...
    Bool,       // bool (still 32-bit in HLSL data, but classify separately)
};

struct NumericInfo
{
    NumericKind kind = NumericKind::None;
    uint32_t    bits = 0;      // 16/32/64 etc (0 if non-numeric)
    uint32_t    lanes = 1;     // 1 for scalar, N for vector, rows*cols for matrix
    uint32_t    rows = 1;      // for matrix (optional)
    uint32_t    cols = 1;      // for matrix (optional)
};

enum class LayoutNodeKind
{
    Scalar, Vector, Matrix,
    Struct,
    Array,
    Container,   // ConstantBuffer/ParameterBlock/SSBO, etc.
    Resource,    // Texture/Sampler/etc.
    Unknown,
};

struct LayoutNode
{
    std::string name;      // e.g. "viewProjection" or "[3]"
    std::string path;      // e.g. "Camera.clippingPlanes[3].plane"
    std::string typeName;  // from Slang type layout (best-effort)

    LayoutNodeKind kind = LayoutNodeKind::Unknown;
    NumericInfo numeric;

    size_t offsetBytes = 0;   // absolute offset from root (bytes)
    size_t sizeBytes = 0;   // type size (bytes)
    size_t strideBytes = 0;   // type stride (bytes; size rounded to alignment)
    size_t alignBytes = 0;

    // Arrays:
    size_t arrayCount = 0;          // ~size_t(0) => unbounded
    size_t elementStrideBytes = 0;  // distance between consecutive elements

    std::vector<LayoutNode> children;
};


// helpers

static inline void appendDiagnostics(std::string& dst, slang::IBlob* blob)
{
    if (!blob) return;
    const char* text = (const char*)blob->getBufferPointer();
    if (!text) return;
    dst.append(text, text + blob->getBufferSize());
    if (!dst.empty() && dst.back() != '\n') dst.push_back('\n');
}

static inline std::string toHex(size_t v)
{
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(sizeof(size_t) * 2);
    for (int i = int(sizeof(size_t) * 2) - 1; i >= 0; --i)
    {
        unsigned nibble = unsigned((v >> (i * 4)) & 0xF);
        s.push_back(kHex[nibble]);
    }
    return s;
}

static NumericInfo getNumericInfo(slang::TypeLayoutReflection* typeLayout)
{
    NumericInfo info{};
    if (!typeLayout) return info;

    slang::TypeReflection* t = typeLayout->getType();
    if (!t) return info;

    using Kind = slang::TypeReflection::Kind;

    Kind k = t->getKind();

    // Figure out lane counts
    if (k == Kind::Vector)
    {
        info.lanes = (uint32_t)t->getElementCount();
        t = t->getElementType();
        if (!t) return info;
        k = t->getKind();
    }
    else if (k == Kind::Matrix)
    {
        info.rows = (uint32_t)t->getRowCount();
        info.cols = (uint32_t)t->getColumnCount();
        info.lanes = info.rows * info.cols;
        t = t->getElementType(); // scalar element
        if (!t) return info;
        k = t->getKind();
    }

    if (k != Kind::Scalar)
        return info;

    using ST = slang::TypeReflection::ScalarType;
    ST st = t->getScalarType();

    switch (st)
    {
        // floats
    case ST::Float16:
    case ST::Float32:
    case ST::Float64:
        info.kind = NumericKind::Float;
        break;

        // signed ints
    case ST::Int8:
    case ST::Int16:
    case ST::Int32:
    case ST::Int64:
        info.kind = NumericKind::SInt;
        break;

        // unsigned ints
    case ST::UInt8:
    case ST::UInt16:
    case ST::UInt32:
    case ST::UInt64:
        info.kind = NumericKind::UInt;
        break;

    case ST::Bool:
        info.kind = NumericKind::Bool;
        break;

    default:
        info.kind = NumericKind::None;
        break;
    }

    // Bit width
    switch (st)
    {
    case ST::Bool:    info.bits = 1;  break;
    case ST::Int8:    info.bits = 8;  break;
    case ST::UInt8:   info.bits = 8;  break;
    case ST::Int16:   info.bits = 16; break;
    case ST::UInt16:  info.bits = 16; break;
    case ST::Float16: info.bits = 16; break;
    case ST::Int32:   info.bits = 32; break;
    case ST::UInt32:  info.bits = 32; break;
    case ST::Float32: info.bits = 32; break;
    case ST::Int64:   info.bits = 64; break;
    case ST::UInt64:  info.bits = 64; break;
    case ST::Float64: info.bits = 64; break;
    default:          info.bits = 0;  break;
    }

    return info;
}

// Pick the last `struct Name {` in the snippet.
static inline std::optional<std::string> extractLastStructName(std::string_view src)
{
    std::regex re(R"(struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    std::cmatch m;

    std::optional<std::string> last;
    const char* begin = src.data();
    const char* end = src.data() + src.size();

    while (std::regex_search(begin, end, m, re))
    {
        if (m.size() >= 2)
            last = std::string(m[1].first, m[1].second);
        begin = m.suffix().first;
    }
    return last;
}

static inline std::string makeMinimalShader(
    std::string_view userStructText,
    std::string_view rootStructName)
{
    // Tiny dummy shader: a global ConstantBuffer<Root> and a trivial compute entry point.
    // The assignment "Root tmp = __dbgValue;" forces the parameter to be referenced.
    std::string s;
    s.reserve(userStructText.size() + 512);

    s.append(userStructText);

    s.append("\n\n");
    s.append("StructuredBuffer<");
    s.append(rootStructName);
    s.append("> __dbgValue;\n\n");

    s.append("[shader(\"compute\")]\n");
    s.append("[numthreads(1,1,1)]\n");
    s.append("void computeMain(uint3 tid : SV_DispatchThreadID)\n");
    s.append("{\n");
    s.append("    ");
    s.append(rootStructName);
    s.append(" tmp = __dbgValue[0];\n");
    s.append("    (void)tmp;\n");
    s.append("}\n");

    return s;
}

static LayoutNodeKind classifyKind(slang::TypeLayoutReflection* t)
{
    if (!t) return LayoutNodeKind::Unknown;

    using K = slang::TypeReflection::Kind;
    switch (t->getKind())
    {
    case K::Scalar:  return LayoutNodeKind::Scalar;
    case K::Vector:  return LayoutNodeKind::Vector;
    case K::Matrix:  return LayoutNodeKind::Matrix;
    case K::Struct:  return LayoutNodeKind::Struct;
    case K::Array:   return LayoutNodeKind::Array;

    case K::ConstantBuffer:
    case K::ParameterBlock:
    case K::TextureBuffer:
    case K::ShaderStorageBuffer:
        return LayoutNodeKind::Container;

    case K::Resource:
    case K::SamplerState:
        return LayoutNodeKind::Resource;

    default:
        return LayoutNodeKind::Unknown;
    }
}

static void buildChildrenForType(
    LayoutNode& node,
    slang::TypeLayoutReflection* typeLayout,
    size_t baseOffsetBytes,
    bool expandArrays,
    size_t maxArrayExpand = 64) // safety cap for UI
{
    if (!typeLayout) return;

    using K = slang::TypeReflection::Kind;
    switch (typeLayout->getKind())
    {
    case K::Struct:
    {
        int fieldCount = typeLayout->getFieldCount();
        for (int i = 0; i < fieldCount; ++i)
        {
            auto* fieldVar = typeLayout->getFieldByIndex(i);
            if (!fieldVar) continue;

            auto* fieldType = fieldVar->getTypeLayout();
            if (!fieldType) continue;

            const char* fieldNameC = fieldVar->getName();
            std::string fieldName = fieldNameC ? fieldNameC : "<unnamed>";

            size_t rel = fieldVar->getOffset(slang::ParameterCategory::Uniform);
            size_t abs = baseOffsetBytes + rel;

            LayoutNode child;
            child.name = fieldName;
            child.path = node.path.empty() ? fieldName : (node.path + "." + fieldName);
            child.typeName = fieldType->getName() ? fieldType->getName() : "";
            child.numeric = getNumericInfo(fieldType);
            child.kind = classifyKind(fieldType);
            child.offsetBytes = abs;
            child.sizeBytes = fieldType->getSize();
            child.alignBytes = fieldType->getAlignment();
            child.strideBytes = fieldType->getStride();

            buildChildrenForType(child, fieldType, abs, expandArrays, maxArrayExpand);
            node.children.push_back(std::move(child));
        }
    }
    break;

    case K::Array:
    {
        node.arrayCount = typeLayout->getElementCount(); // ~size_t(0) if unbounded
        auto* elemType = typeLayout->getElementTypeLayout();
        if (!elemType) break;

        node.elementStrideBytes = elemType->getStride();
        node.numeric = getNumericInfo(typeLayout->getElementTypeLayout());

        if (!expandArrays) break;
        if (node.arrayCount == ~size_t(0)) break;

        size_t count = node.arrayCount;
        if (count > maxArrayExpand) count = maxArrayExpand;

        for (size_t idx = 0; idx < count; ++idx)
        {
            LayoutNode elem;
            elem.name = "[" + std::to_string(idx) + "]";
            elem.path = node.path + elem.name;
            elem.typeName = elemType->getName() ? elemType->getName() : "";
            elem.kind = classifyKind(elemType);
            elem.offsetBytes = baseOffsetBytes + idx * node.elementStrideBytes;
            elem.sizeBytes = elemType->getSize();
            elem.alignBytes = elemType->getAlignment();
            elem.strideBytes = elemType->getStride();

            buildChildrenForType(elem, elemType, elem.offsetBytes, expandArrays, maxArrayExpand);
            node.children.push_back(std::move(elem));
        }
    }
    break;

    case K::ConstantBuffer:
    case K::ParameterBlock:
    case K::TextureBuffer:
    case K::ShaderStorageBuffer:
    {
        //use getElementVarLayout() (not getElementTypeLayout()).
        auto* elemVar = typeLayout->getElementVarLayout();
        if (!elemVar) break;

        auto* elemType = elemVar->getTypeLayout();
        if (!elemType) break;

        size_t elemRel = elemVar->getOffset(slang::ParameterCategory::Uniform);
        size_t elemAbs = baseOffsetBytes + elemRel;

        LayoutNode elem;
        elem.name = "<element>";
        elem.path = node.path.empty() ? "<element>" : (node.path + "." + elem.name);
        elem.typeName = elemType->getName() ? elemType->getName() : "";
        elem.kind = classifyKind(elemType);
        elem.offsetBytes = elemAbs;
        elem.sizeBytes = elemType->getSize();
        elem.alignBytes = elemType->getAlignment();
        elem.strideBytes = elemType->getStride();

        buildChildrenForType(elem, elemType, elemAbs, expandArrays, maxArrayExpand);
        node.children.push_back(std::move(elem));
    }
    break;

    default:
        // Leaf types: scalar/vector/matrix/resource/etc.
        break;
    }
}

static LayoutNode buildRootTree(
    slang::TypeLayoutReflection* rootTypeLayout,
    std::string rootName,
    bool expandArrays)
{
    LayoutNode root;
    root.name = std::move(rootName);
    root.path = root.name;
    root.typeName = rootTypeLayout && rootTypeLayout->getName() ? rootTypeLayout->getName() : "";
    root.kind = classifyKind(rootTypeLayout);
    root.offsetBytes = 0;
    root.sizeBytes = rootTypeLayout ? rootTypeLayout->getSize() : 0;
    root.alignBytes = rootTypeLayout ? rootTypeLayout->getAlignment() : 0;
    root.strideBytes = rootTypeLayout ? rootTypeLayout->getStride() : 0;

    buildChildrenForType(root, rootTypeLayout, /*baseOffset*/0, expandArrays);
    return root;
}

static slang::VariableLayoutReflection* findVariableInScopeByName(
    slang::VariableLayoutReflection* scopeVarLayout,
    const char* name)
{
    if (!scopeVarLayout || !name) return nullptr;

    slang::TypeLayoutReflection* scopeTypeLayout = scopeVarLayout->getTypeLayout();
    if (!scopeTypeLayout) return nullptr;

    using Kind = slang::TypeReflection::Kind;
    Kind kind = scopeTypeLayout->getKind();

    if (kind == Kind::Struct)
    {
        int fieldCount = scopeTypeLayout->getFieldCount();
        for (int i = 0; i < fieldCount; ++i)
        {
            slang::VariableLayoutReflection* field = scopeTypeLayout->getFieldByIndex(i);
            if (!field) continue;

            const char* fieldName = field->getName();
            if (fieldName && std::strcmp(fieldName, name) == 0)
                return field;
        }
        return nullptr;
    }

    // If the scope is wrapped, unwrap via element-var-layout.
    if (kind == Kind::ConstantBuffer || kind == Kind::ParameterBlock || kind == Kind::ShaderStorageBuffer)
    {
        auto* elemVar = scopeTypeLayout->getElementVarLayout();
        return findVariableInScopeByName(elemVar, name);
    }

    return nullptr;
}

static inline std::string trimCopy(std::string_view sv)
{
    size_t b = 0;
    while (b < sv.size() && isspace((unsigned char)sv[b])) ++b;
    size_t e = sv.size();
    while (e > b && isspace((unsigned char)sv[e - 1])) --e;
    return std::string(sv.substr(b, e - b));
}

struct PreparedRootSnippet
{
    std::string rootTypeName; // e.g. "Camera" or "__DbgRoot"
    std::string typeDeclText; // text containing struct declarations (and wrapper if needed)
};

// If snippet contains a struct definition, we keep it and use the last struct name.
// Otherwise, we wrap the snippet into `struct __DbgRoot { ... };`
//
// Accepts:
//  - "uint value;"                  -> wrapper with that member
//  - "uint;" or "uint"              -> wrapper with `uint value;` (fallback)
//  - "float4 pos; row_major float4x4 view;" -> wrapper with those members
static inline PreparedRootSnippet prepareRootSnippet(std::string_view userText)
{
    if (auto root = extractLastStructName(userText))
    {
        return PreparedRootSnippet{ *root, std::string(userText) };
    }

    std::string members = trimCopy(userText);

    std::string decl;
    decl.reserve(members.size() + 64);
    decl += "struct __DbgRoot {\n";
    decl += members;
    decl += "\n};\n";

    return PreparedRootSnippet{ "__DbgRoot", std::move(decl) };
}


inline SlangResult ReflectStructLayoutWithSlang(
    LayoutNode& out,
    const std::string& userStructText,
    std::string* outDiagnostics = nullptr,
    const char* targetProfile = "sm_6_6",
    SlangCompileTarget   targetFormat = SLANG_DXIL,
    bool                expandArrays = true)
{

    std::string diagnostics;

    PreparedRootSnippet prepared = prepareRootSnippet(userStructText);
    std::string shaderSrc = makeMinimalShader(prepared.typeDeclText, prepared.rootTypeName);

    // Give each compile a unique module name/path to avoid session cache returning stale results.
    size_t h = std::hash<std::string>{}(userStructText);
    std::string moduleName = "dbg_struct_" + toHex(h);
    std::string modulePath = moduleName + ".slang";

    // Create global session
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    {
        SlangResult r = createGlobalSession(globalSession.writeRef());
        if (SLANG_FAILED(r))
        {
            if (outDiagnostics) *outDiagnostics = "createGlobalSession failed\n";
            return r;
        }
    }

    // Create session
    Slang::ComPtr<slang::ISession> session;
    {
        slang::SessionDesc sessionDesc = {};
        slang::TargetDesc targetDesc = {};
        targetDesc.format = targetFormat;
        targetDesc.profile = globalSession->findProfile(targetProfile);

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        SlangResult r = globalSession->createSession(sessionDesc, session.writeRef());
        if (SLANG_FAILED(r))
        {
            if (outDiagnostics) *outDiagnostics = "createSession failed\n";
            return r;
        }
    }

    // Load module from source string
    Slang::ComPtr<slang::IModule> module;
    {
        Slang::ComPtr<slang::IBlob> diagBlob;
        module = session->loadModuleFromSourceString(
            moduleName.c_str(),
            modulePath.c_str(),
            shaderSrc.c_str(),
            diagBlob.writeRef());

        appendDiagnostics(diagnostics, diagBlob);

        if (!module)
        {
            if (outDiagnostics) *outDiagnostics = diagnostics;
            return SLANG_FAIL;
        }
    }

    // Find entry point
    Slang::ComPtr<slang::IEntryPoint> entryPoint;
    {
        module->findEntryPointByName("computeMain", entryPoint.writeRef());
        if (!entryPoint)
        {
            diagnostics += "findEntryPointByName(computeMain) failed\n";
            if (outDiagnostics) *outDiagnostics = diagnostics;
            return SLANG_FAIL;
        }
    }

    // Compose + Link
    Slang::ComPtr<slang::IComponentType> linkedProgram;
    {
        Slang::ComPtr<slang::IComponentType> composed;
        std::array<slang::IComponentType*, 2> parts = { module.get(), entryPoint.get() };

        {
            Slang::ComPtr<slang::IBlob> diagBlob;
            SlangResult r = session->createCompositeComponentType(
                parts.data(), parts.size(),
                composed.writeRef(),
                diagBlob.writeRef());
            appendDiagnostics(diagnostics, diagBlob);
            if (SLANG_FAILED(r))
            {
                if (outDiagnostics) *outDiagnostics = diagnostics;
                return r;
            }
        }

        {
            Slang::ComPtr<slang::IBlob> diagBlob;
            SlangResult r = composed->link(linkedProgram.writeRef(), diagBlob.writeRef());
            appendDiagnostics(diagnostics, diagBlob);
            if (SLANG_FAILED(r))
            {
                if (outDiagnostics) *outDiagnostics = diagnostics;
                return r;
            }
        }
    }

    // Reflection: ProgramLayout + global scope
    slang::ProgramLayout* programLayout = linkedProgram->getLayout(/*targetIndex*/ 0);
    if (!programLayout)
    {
        diagnostics += "getLayout(targetIndex=0) returned null\n";
        if (outDiagnostics) *outDiagnostics = diagnostics;
        return SLANG_FAIL;
    }

    slang::VariableLayoutReflection* globalScope = programLayout->getGlobalParamsVarLayout();
    if (!globalScope)
    {
        diagnostics += "getGlobalParamsVarLayout() returned null\n";
        if (outDiagnostics) *outDiagnostics = diagnostics;
        return SLANG_FAIL;
    }

    // Find the global StructuredBuffer<Root> we injected.
    slang::VariableLayoutReflection* dbgVar = findVariableInScopeByName(globalScope, "__dbgValue");
    if (!dbgVar)
    {
        diagnostics += "Could not find global '__dbgValue' in reflected global scope\n";
        if (outDiagnostics) *outDiagnostics = diagnostics;
        return SLANG_FAIL;
    }

    // Unwrap StructuredBuffer<T> -> T using element var layout
    slang::TypeLayoutReflection* dbgTypeLayout = dbgVar->getTypeLayout();
    if (!dbgTypeLayout)
    {
        diagnostics += "__dbgValue has null type layout\n";
        if (outDiagnostics) *outDiagnostics = diagnostics;
        return SLANG_FAIL;
    }

    slang::TypeLayoutReflection* rootTypeLayout = dbgTypeLayout;
    // Prefer element var layout if present
    if (auto* elemVar = dbgTypeLayout->getElementVarLayout())
    {
        if (auto* elemType = elemVar->getTypeLayout())
            rootTypeLayout = elemType;
    }
    else if (auto* elemType = dbgTypeLayout->getElementTypeLayout())
    {
        rootTypeLayout = elemType;
    }

    if (!rootTypeLayout)
    {
        diagnostics += "Failed to unwrap __dbgValue to root type layout\n";
        if (outDiagnostics) *outDiagnostics = diagnostics;
        return SLANG_FAIL;
    }

    out = buildRootTree(rootTypeLayout, prepared.rootTypeName, expandArrays);

    if (outDiagnostics) *outDiagnostics = diagnostics;
    return SLANG_OK;
}
