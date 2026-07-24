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
        // The owner retain/release operations are expanded at every Android
        // call site; the PSBFile.dll function range has no standalone entry.
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

    // Two-pointer raw node handle used throughout the Android implementation.
    // The real rvalue assignment in PSBMedia::Resolve @ 0x59A694 follows the
    // retained-copy path plus temporary destruction, so this holder
    // deliberately exposes Rule-of-Three copy lifetime rather than a
    // speculative move path.
    class PSBRawNode final {
    public:
        PSBRawNode() = default;
        PSBRawNode(PSBRawOwner *owner, const std::uint8_t *node) :
            owner_(owner), node_(node) {
            if(owner_ != nullptr) {
                owner_->AddRef();
            }
        }
        PSBRawNode(const PSBRawNode &other) :
            PSBRawNode(other.owner_, other.node_) {}
        ~PSBRawNode() {
            if(owner_ != nullptr) {
                owner_->Release();
            }
        }

        PSBRawNode &operator=(const PSBRawNode &other) {
            if(this == &other) {
                return *this;
            }
            // The hit out-parameter path in sub_598D58 @ 0x598D58 exposes
            // this raw-pair sequence: release destination, copy owner, retain
            // owner, copy node.  It does not prove that the original source
            // factored that one site into a class-wide operator=.
            if(owner_ != nullptr) {
                owner_->Release();
            }
            owner_ = other.owner_;
            if(owner_ != nullptr) {
                owner_->AddRef();
            }
            node_ = other.node_;
            return *this;
        }
        [[nodiscard]] PSBRawOwner *GetOwner() const { return owner_; }
        // sub_597AD4 @ 0x597AD4 receives the address of this first slot at
        // all PSBValueDispatch construction sites.  The binary cannot prove
        // a shared source-level holder type, so this ABI-only accessor keeps
        // the uncertainty explicit instead of inventing one.
        [[nodiscard]] PSBRawOwner *const *
        GetOwnerSlotAddress_guess() const {
            return &owner_;
        }
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
        PSBRawOwner *owner_{};
        const std::uint8_t *node_{};
    };

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
        PSBFile &operator=(const PSBFile &other) noexcept {
            // ResourceManager::load @ 0x6A926C..0x6A92A8.
            if(owner_ != nullptr) {
                owner_->Release();
            }
            owner_ = other.owner_;
            if(owner_ != nullptr) {
                owner_->AddRef();
            }
            return *this;
        }
        [[nodiscard]] PSBFile Transfer_guess() noexcept;
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
        [[nodiscard]] PSBRawOwner *const *
        GetOwnerSlotAddress_guess() const {
            return &owner_;
        }

    private:
        PSBRawOwner *owner_{};
    };
} // namespace PSB
