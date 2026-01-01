// ImmediateBytecode.h
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <span>
#include <cstring>
#include <cassert>
#include <type_traits>

namespace rg::imm
{
    enum class Queue : uint8_t { Graphics, Compute, Copy };

    enum class Op : uint8_t {
        CopyBuffer,
        CopyTextureSubresource,
        CopyTextureRegions, // variable-sized (regions[])
        ClearRTV,
        ClearDSV,
        ClearUAV_U32x4,
        ClearUAV_F32x4,
        ResolveSubresource,
        UAVBarrier,
    };

    static constexpr size_t kAlign = 8;

    inline size_t AlignUp(size_t v, size_t a) { return (v + (a - 1)) & ~(a - 1); }

#pragma pack(push, 1)
    struct CmdHeader {
        Op      op;
        Queue   queue;
        uint16_t flags;        // optional (scopes, debug, etc.)
        uint32_t sizeBytes;    // total size including header + payload + padding
    };
#pragma pack(pop)

    static_assert(sizeof(CmdHeader) == 8);

    class BytecodeWriter {
    public:
        void Reset() { bytes_.clear(); }

        std::span<const std::byte> Bytes() const { return bytes_; }
        std::span<std::byte> Bytes() { return bytes_; }

        void ReserveBytes(size_t n) { bytes_.reserve(n); }

        // Begin/End for variable-sized payloads
        size_t Begin(Op op, Queue q, uint16_t flags = 0) {
            const size_t start = AlignUp(bytes_.size(), kAlign);
            if (start != bytes_.size()) bytes_.resize(start);

            CmdHeader hdr{ op, q, flags, 0 };
            AppendPod(hdr);
            return start;
        }

        void End(size_t start) {
            // pad the command out to alignment, then patch header.sizeBytes
            const size_t endAligned = AlignUp(bytes_.size(), kAlign);
            if (endAligned != bytes_.size()) bytes_.resize(endAligned);

            const uint32_t total = static_cast<uint32_t>(bytes_.size() - start);
            // Patch: header is at [start..start+8)
            std::memcpy(bytes_.data() + start + offsetof(CmdHeader, sizeBytes), &total, sizeof(total));
        }

        template<class T>
        void Write(const T& pod) {
            static_assert(std::is_trivially_copyable_v<T>);
            AppendPod(pod);
        }

        template<class T>
        void WriteSpan(std::span<const T> items) {
            static_assert(std::is_trivially_copyable_v<T>);
            const size_t nbytes = items.size_bytes();
            const size_t old = bytes_.size();
            bytes_.resize(old + nbytes);
            std::memcpy(bytes_.data() + old, items.data(), nbytes);
        }

        // Convenience: fixed-size command
        template<class TPayload>
        void Emit(Op op, Queue q, const TPayload& payload, uint16_t flags = 0) {
            const size_t start = Begin(op, q, flags);
            Write(payload);
            End(start);
        }

    private:
        template<class T>
        void AppendPod(const T& pod) {
            static_assert(std::is_trivially_copyable_v<T>);
            const size_t old = bytes_.size();
            bytes_.resize(old + sizeof(T));
            std::memcpy(bytes_.data() + old, &pod, sizeof(T));
        }

        std::vector<std::byte> bytes_;
    };

    class BytecodeReader {
    public:
        explicit BytecodeReader(std::span<const std::byte> b) : bytes_(b) {}

        struct Cursor {
            const std::byte* p = nullptr;
            const std::byte* end = nullptr;
        };

        Cursor Begin() const {
            return Cursor{ bytes_.data(), bytes_.data() + bytes_.size() };
        }

        bool Next(Cursor& c, CmdHeader& outHdr, std::span<const std::byte>& outPayload) const {
            if (c.p >= c.end) return false;
            if ((size_t)(c.end - c.p) < sizeof(CmdHeader)) return false;

            std::memcpy(&outHdr, c.p, sizeof(CmdHeader));
            if (outHdr.sizeBytes < sizeof(CmdHeader)) return false;
            if ((size_t)(c.end - c.p) < outHdr.sizeBytes) return false;

            const std::byte* payload = c.p + sizeof(CmdHeader);
            const size_t payloadBytes = outHdr.sizeBytes - sizeof(CmdHeader);
            outPayload = { payload, payloadBytes };

            c.p += outHdr.sizeBytes;
            return true;
        }

    private:
        std::span<const std::byte> bytes_;
    };
}
