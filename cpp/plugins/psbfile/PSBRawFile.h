#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "tjs.h"

namespace PSB {

    // Semantic source reconstruction of the inline header view built by
    // libkrkr2.so sub_598AAC @ 0x598AAC and refreshed by sub_598960 @ 0x598960.
    // Pointer-sized fields intentionally use the host ABI; ARM64 byte offsets
    // belong to the binary, not to the original portable C++ source.
    struct PSBRawHeader {
        std::uint32_t signature;
        std::uint16_t version;
        std::uint16_t encrypt;
        std::uint8_t *encryptData;
        std::uint8_t *names;
        std::uint8_t *strings;
        std::uint8_t *stringsData;
        std::uint8_t *chunkOffsets;
        std::uint8_t *chunkLengths;
        std::uint8_t *chunkData;
        std::uint8_t *entries;
    };

    // Intrusively referenced owner reconstructed from sub_598708 @ 0x598708.
    // It owns exactly one raw PSB allocation and all node views point into it.
    class PSBRawOwner final {
    public:
        // The authoritative arm64 target expands owner retain/release at its
        // psbfile call sites and has no standalone entry in this range. iOS
        // iOS arm64 lineage retains shared Release: decrement first, then delete
        // this only on zero. The exact identifier/inline token remains stripped.
        void AddRef() { ++refCount_; }
        void Release() {
            if(--refCount_ == 0) {
                delete this;
            }
        }

        [[nodiscard]] bool Refresh(bool validateOffsets);
        [[nodiscard]] PSBRawHeader *GetHeader() { return header_; }
        [[nodiscard]] const PSBRawHeader *GetHeader() const { return header_; }
        [[nodiscard]] std::uint8_t *GetData() { return data_; }
        [[nodiscard]] const std::uint8_t *GetData() const { return data_; }
        [[nodiscard]] std::int64_t GetSize() const { return size_; }

    private:
        friend class PSBFile;
        friend class PSBRawNode;

        PSBRawOwner(std::uint8_t *data, std::size_t size);
        ~PSBRawOwner();

        std::uint32_t refCount_{};
        PSBRawHeader *header_;
        PSBRawHeader headerStorage_;
        std::uint8_t *data_{};
        // Refresh @0x5989E8..0x598A2C and its Adopt-inline copy
        // @0x598894..0x598940 compare this full-width field with signed
        // GT/GE conditions.  Adopt's incoming size remains unsigned for the
        // separate <0x40 gate; only the stored owner field has signed-64
        // semantics.  The stripped binary cannot recover the original
        // typedef spelling, so use the explicit semantic type here.
        std::int64_t size_{};
    };

    class PSBRawNode;

    // Native PSBFile holder reconstructed from sub_5980F4 @ 0x5980F4.  The
    // class deliberately contains only the owner pointer.
    class PSBFile final {
    public:
        using OwnerFilter = std::function<void(PSBRawOwner &)>;

        PSBFile() = default;
        PSBFile(const PSBFile &other) noexcept : owner_(other.owner_) {
            // ResourceManager::load @ 0x6A8E94..0x6A8EB8.
            if(owner_ != nullptr) {
                owner_->AddRef();
            }
        }
        PSBFile &operator=(const PSBFile &other) {
            // ResourceManager::load @ 0x6A926C..0x6A92A8 and the raw-node
            // hit path @0x598DB8..0x598E00 share this exact order.  There is
            // deliberately no self guard.
            if(owner_ != nullptr) {
                owner_->Release();
            }
            owner_ = other.owner_;
            if(owner_ != nullptr) {
                owner_->AddRef();
            }
            return *this;
        }
        [[nodiscard]] PSBFile Transfer_guess();
        ~PSBFile() {
            if(owner_ != nullptr) {
                owner_->Release();
            }
        }

        [[nodiscard]] bool Load(tTJSVariant value);
        [[nodiscard]] iTJSDispatch2 *GetRootDispatch() const;
        [[nodiscard]] bool LoadStorage(const ttstr &name,
                                       const OwnerFilter &filter = {});
        [[nodiscard]] bool Adopt(std::uint8_t *data, std::size_t size,
                                 const OwnerFilter &filter = {});

        [[nodiscard]] PSBRawNode GetRoot() const;
        [[nodiscard]] PSBRawOwner *GetOwner() const { return owner_; }

    private:
        PSBRawOwner *owner_{};
    };

    // Raw node is a PSBFile holder subobject followed by an independent node
    // pointer. Android arm64 scalarizes both fields; iOS arm64 dispatch
    // constructors independently preserve standalone-holder and raw-node-first-
    // subobject callers with a separate node argument. iOS arm64
    // PSBFile::GetRoot @0x1000ED8C8 passes its PSBFile `this` to the shared
    // raw-node constructor @0x1000EEF28. That constructor zero-constructs the
    // first subobject, calls the one-pointer assignment @0x1000ED740, then
    // stores node. Consequently
    // copy assignment/destruction must flow through PSBFile rather than a
    // second, independently implemented owner lifecycle.
    class PSBRawNode final {
    public:
        PSBRawNode() = default;
        explicit PSBRawNode(const PSBFile &file) {
            // Android callers inline this root construction at
            // 0x694AB0..0x694AC8, 0x695FA0..0x695FC0,
            // 0x6A9870..0x6A9890, 0x6A99A4..0x6A99C4,
            // 0x6AA058..0x6AA078, 0x6AA360..0x6AA380, and
            // 0x6AAF08..0x6AAF28. Same-lineage iOS arm64 preserves the shared
            // constructor @0x1001263B8:
            // capture entries first, assign the PSBFile holder, then store
            // the independent node pointer.
            const std::uint8_t *entries =
                file.GetOwner()->GetHeader()->entries;
            file_ = file;
            node_ = entries;
        }
        PSBRawNode(const PSBFile &file, const std::uint8_t *node) {
            file_ = file;
            node_ = node;
        }

        [[nodiscard]] PSBRawOwner *GetOwner() const {
            return file_.GetOwner();
        }
        // The first subobject is observably the same one-pointer holder passed
        // to raw-node and dispatch constructors. Member-vs-base syntax and the
        // original accessor name remain stripped, hence `_guess`.
        [[nodiscard]] const PSBFile &GetFile_guess() const { return file_; }
        [[nodiscard]] const std::uint8_t *GetNode() const { return node_; }
        [[nodiscard]] bool IsValid_guess() const; // 0x598E44

        [[nodiscard]] std::uint8_t GetType() const { return node_[0]; }
        [[nodiscard]] bool GetDictionaryValue(const char *key,
                                              PSBRawNode &value) const;
        [[nodiscard]] PSBRawNode
        GetDictionaryValueStrict(const char *key) const;
        [[nodiscard]] bool ContainsDictionaryKey(const char *key) const;
        [[nodiscard]] std::vector<std::string> GetDictionaryKeys() const;
        [[nodiscard]] int GetTypeCategory() const;
        // sub_599438 @ 0x599438 returns a signed 32-bit TJS integer.  Narrow
        // negative paths write W0 directly and every consuming caller reads
        // W0; full X0 left by some wide-tag paths is not part of the ABI value.
        [[nodiscard]] tjs_int GetInt() const;
        [[nodiscard]] tjs_real GetDouble() const;
        [[nodiscard]] const char *GetString() const;
        [[nodiscard]] const std::uint8_t *
        GetResource(std::uint32_t &size) const;

    private:
        PSBFile file_;
        const std::uint8_t *node_{};
    };
} // namespace PSB
