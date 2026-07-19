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
        std::uint32_t signature{};
        std::uint16_t version{};
        std::uint16_t encrypt{};
        std::uint8_t *encryptData{};
        std::uint8_t *names{};
        std::uint8_t *strings{};
        std::uint8_t *stringsData{};
        std::uint8_t *chunkOffsets{};
        std::uint8_t *chunkLengths{};
        std::uint8_t *chunkData{};
        std::uint8_t *entries{};
    };

    // Intrusively referenced owner reconstructed from sub_598708 @ 0x598708.
    // It owns exactly one raw PSB allocation and all node views point into it.
    class PSBRawOwner final {
    public:
        static PSBRawOwner *Create(std::uint8_t *data, std::size_t size);

        void AddRef();
        void Release();

        [[nodiscard]] bool Refresh(bool validateOffsets);
        [[nodiscard]] PSBRawHeader *GetHeader() { return header_; }
        [[nodiscard]] const PSBRawHeader *GetHeader() const { return header_; }
        [[nodiscard]] std::uint8_t *GetData() { return data_; }
        [[nodiscard]] const std::uint8_t *GetData() const { return data_; }
        [[nodiscard]] std::size_t GetSize() const { return size_; }
        [[nodiscard]] const char *GetString(const std::uint8_t *node) const;
        [[nodiscard]] const std::uint8_t *
        GetResource(const std::uint8_t *node, std::uint32_t &size) const;

    private:
        friend class PSBFile;

        PSBRawOwner(std::uint8_t *data, std::size_t size);
        ~PSBRawOwner();

        std::uint32_t refCount_{};
        PSBRawHeader *header_{};
        PSBRawHeader headerStorage_{};
        std::uint8_t *data_{};
        std::size_t size_{};
    };

    // Two-pointer raw node handle used throughout the Android implementation.
    // Copying it retains the owner; moving it transfers the two fields.
    class PSBRawNode final {
    public:
        PSBRawNode() = default;
        PSBRawNode(PSBRawOwner *owner, const std::uint8_t *node);
        PSBRawNode(const PSBRawNode &other);
        PSBRawNode(PSBRawNode &&other) noexcept;
        ~PSBRawNode();

        PSBRawNode &operator=(const PSBRawNode &other);
        PSBRawNode &operator=(PSBRawNode &&other) noexcept;

        [[nodiscard]] PSBRawOwner *GetOwner() const { return owner_; }
        [[nodiscard]] const std::uint8_t *GetNode() const { return node_; }
        [[nodiscard]] explicit operator bool() const {
            return owner_ != nullptr && node_ != nullptr;
        }

        [[nodiscard]] std::uint8_t GetType() const;
        [[nodiscard]] bool GetArrayCount(std::uint32_t &count) const;
        [[nodiscard]] bool GetArrayElement(std::uint32_t index,
                                           PSBRawNode &value) const;
        [[nodiscard]] const std::uint8_t *
        FindArrayElement(std::uint32_t index) const;
        [[nodiscard]] bool GetDictionaryValue(const std::string &key,
                                              PSBRawNode &value) const;
        [[nodiscard]] PSBRawNode
        GetDictionaryValueStrict(const std::string &key) const;
        [[nodiscard]] bool ContainsDictionaryKey(const std::string &key) const;
        [[nodiscard]] const std::uint8_t *
        FindDictionaryValue(const std::string &key) const;
        [[nodiscard]] bool GetDictionaryCount(std::uint32_t &count) const;
        [[nodiscard]] bool GetDictionaryKey(std::uint32_t index,
                                            std::string &key) const;
        [[nodiscard]] bool GetDictionaryEntry(std::uint32_t index,
                                              std::string &key,
                                              const std::uint8_t *&value) const;
        [[nodiscard]] std::vector<std::string> GetDictionaryKeys() const;
        [[nodiscard]] int GetTypeCategory() const;
        [[nodiscard]] tjs_int GetInt() const;
        [[nodiscard]] tjs_real GetDouble() const;
        [[nodiscard]] const char *GetString() const;
        [[nodiscard]] const std::uint8_t *
        GetResource(std::uint32_t &size) const;
        [[nodiscard]] static tjs_int64 DecodeInteger(const std::uint8_t *node);
        [[nodiscard]] static tjs_real DecodeReal(const std::uint8_t *node);

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
        PSBFile(const PSBFile &) = delete;
        PSBFile &operator=(const PSBFile &) = delete;
        PSBFile(PSBFile &&other) noexcept;
        PSBFile &operator=(PSBFile &&other) noexcept;
        ~PSBFile();

        [[nodiscard]] bool Load(const tTJSVariant &value);
        [[nodiscard]] iTJSDispatch2 *GetRootDispatch() const;
        [[nodiscard]] bool LoadStorage(const ttstr &name,
                                       const OwnerFilter &filter = {});
        [[nodiscard]] bool
        LoadOctet(const std::uint8_t *data, std::uint32_t size,
                  const OwnerFilter &filter = {});
        [[nodiscard]] bool Adopt(std::uint8_t *data, std::size_t size,
                                 const OwnerFilter &filter = {});

        [[nodiscard]] PSBRawNode GetRoot() const;
        [[nodiscard]] PSBRawOwner *GetOwner() const { return owner_; }

    private:
        PSBRawOwner *owner_{};
    };
} // namespace PSB
