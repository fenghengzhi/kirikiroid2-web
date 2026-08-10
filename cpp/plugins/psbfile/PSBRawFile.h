#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "tjs.h"

namespace PSB {

    // Semantic source reconstruction of the header view shared by all four
    // Refresh implementations. Pointer-sized fields intentionally use the
    // host ABI; binary byte offsets are not source ABI. The qualified function
    // map belongs in the four-binary audit.
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

    // Intrusively referenced owner confirmed independently by the four Adopt
    // paths. It owns exactly one raw PSB allocation and all node views point
    // into it.
    class PSBRawOwner final {
    public:
        // Android arm64 expands this at its call sites; the other three retain
        // a shared decrement-then-delete-on-zero helper. The exact original
        // identifier/inline spelling is stripped.
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
        [[nodiscard]] std::intptr_t GetSize() const { return size_; }

    private:
        friend class PSBFile;
        friend class PSBRawNode;

        PSBRawOwner(std::uint8_t *data, std::size_t size);
        ~PSBRawOwner();

        std::uint32_t refCount_{};
        PSBRawHeader *header_;
        PSBRawHeader headerStorage_;
        std::uint8_t *data_{};
        // All four Refresh implementations compare this field with signed
        // GT/GE conditions. Its width is 8 bytes in both 64-bit binaries and
        // 4 bytes in both 32-bit binaries, so intptr_t captures the recovered
        // ABI even though the original typedef spelling is stripped.
        std::intptr_t size_{};
    };

    class PSBRawNode;

    // Native one-pointer holder confirmed independently by all four factory
    // functions.
    class PSBFile final {
    public:
        using OwnerFilter = std::function<void(PSBRawOwner &)>;

        PSBFile() = default;
        PSBFile(const PSBFile &other) noexcept : owner_(other.owner_) {
            // The four raw GetRoot/dispatch constructors copy this one slot
            // and increment the same owner refcount.
            if(owner_ != nullptr) {
                owner_->AddRef();
            }
        }
        PSBFile &operator=(const PSBFile &other) {
            // The four non-throwing dictionary hit paths and retained iOS
            // holder helpers share this release-old/copy/AddRef order. There
            // is deliberately no self guard.
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
    // pointer. Android scalarizes both fields; iOS GetRoot helpers preserve a
    // standalone holder argument and separate node argument. Both iOS helper
    // paths zero-construct the first subobject, assign the one-pointer holder,
    // then store node. Consequently
    // copy assignment/destruction must flow through PSBFile rather than a
    // second, independently implemented owner lifecycle.
    class PSBRawNode final {
    public:
        PSBRawNode() = default;
        explicit PSBRawNode(const PSBFile &file) {
            // The four retained/inlined root constructions capture entries
            // first, assign the PSBFile holder, then store the independent
            // node pointer.
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
        [[nodiscard]] bool IsValid_guess() const;

        [[nodiscard]] std::uint8_t GetType() const { return node_[0]; }
        [[nodiscard]] bool GetDictionaryValue(const char *key,
                                              PSBRawNode &value) const;
        [[nodiscard]] PSBRawNode
        GetDictionaryValueStrict(const char *key) const;
        [[nodiscard]] bool ContainsDictionaryKey(const char *key) const;
        [[nodiscard]] std::vector<std::string> GetDictionaryKeys() const;
        [[nodiscard]] int GetTypeCategory() const;
        // The four GetInt wrappers return a signed 32-bit TJS integer. Narrow
        // negative paths write W0 directly; full X0 left by some 64-bit paths
        // is not part of the source ABI value.
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
