#include "PSBRawFile.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <zlib.h>

#include "MsgIntf.h"
#include "StorageIntf.h"

namespace PSB {
    namespace {
        constexpr std::uint32_t PSB_SIGNATURE = 0x00425350u;
        constexpr std::uint32_t MDF_SIGNATURE = 0x0066646du;
        constexpr std::size_t MIN_PSB_SIZE = 0x40u;

        template <typename T>
        T readUnaligned(const std::uint8_t *data) {
            T result{};
            std::memcpy(&result, data, sizeof(result));
            return result;
        }

        std::uint32_t readPackedCount(const std::uint8_t *data) {
            switch(data[0]) {
                case 0x0d:
                    return data[1];
                case 0x0e:
                    return readUnaligned<std::uint16_t>(data + 1);
                case 0x0f:
                    return readUnaligned<std::uint32_t>(data + 1) & 0xffffffu;
                case 0x10:
                    return readUnaligned<std::uint32_t>(data + 1);
                default:
                    return 0;
            }
        }

        std::uint32_t readPackedValue(const std::uint8_t *data,
                                      std::uint8_t tag) {
            // sub_59641C @ 0x596498 and sub_59659C @ 0x5966E4 use the
            // unsigned range (tag - 0x0d) <= 4.  The fifth accepted tag
            // therefore deliberately follows AArch64's register-shift modulo
            // rule instead of being normalized to an ordinary 1..4 width.
            if(static_cast<std::uint32_t>(tag) - 0x0du > 4u) {
                return 0;
            }
            const auto shift =
                (8u * (0x10u - static_cast<std::uint32_t>(tag))) & 31u;
            return readUnaligned<std::uint32_t>(data) & (0xffffffffu >> shift);
        }

        tjs_int64 readSigned(const std::uint8_t *data, std::uint8_t width) {
            if(width == 0 || width > 8) {
                return 0;
            }
            std::uint64_t value{};
            std::memcpy(&value, data, width);
            // sub_59673C @ 0x59673C and sub_5992E8 @ 0x5992E8 leave
            // the seven-byte tag 0x0b zero-extended; widths 1..6 retain
            // their ordinary sign extension and width 8 is read verbatim.
            if(width < 7 && (data[width - 1] & 0x80u) != 0) {
                value |= ~std::uint64_t{} << (width * 8);
            }
            return static_cast<tjs_int64>(value);
        }

        // Packed integer array used by all PSB collection sections.  These
        // expressions are the source-level form of the repeated tag/width
        // expansions in sub_59641C, sub_59659C and sub_597B1C.
        struct PackedArray {
            const std::uint8_t *begin{};
            std::uint32_t count{};
            int width{};
            std::uint8_t valueTag{};
            const std::uint8_t *values{};
            const std::uint8_t *end{};

            explicit PackedArray(const std::uint8_t *data) : begin(data) {
                const auto countTag = data[0];
                count = readPackedCount(data);
                const auto *entryTag =
                    data + static_cast<std::ptrdiff_t>(countTag) - 0x0b;
                valueTag = entryTag[0];
                width = static_cast<int>(valueTag) - 0x0c;
                values = entryTag + 1;
                end = values + count * width;
            }

            [[nodiscard]] std::uint32_t operator[](std::uint32_t index) const {
                return readPackedValue(
                    values + index * width, valueTag);
            }
        };

        struct NameTables {
            PackedArray charset;
            PackedArray namesData;
            PackedArray nameIndexes;

            explicit NameTables(const std::uint8_t *names) :
                charset(names), namesData(charset.end),
                nameIndexes(namesData.end) {}

            // sub_59641C @ 0x59641C: walk the double-array trie and return the
            // encoded terminal name index.
            [[nodiscard]] bool Find(const char *name,
                                    std::uint32_t &nameIndex) const {
                const auto *cursor =
                    reinterpret_cast<const std::uint8_t *>(name);
                std::uint32_t parent = 0;
                std::uint32_t state = charset[0] + *cursor;
                if(state >= charset.count) {
                    return false;
                }

                for(;;) {
                    if(namesData[state] != parent) {
                        return false;
                    }
                    if(*cursor == 0) {
                        nameIndex = charset[state];
                        return true;
                    }
                    ++cursor;
                    parent = state;
                    state = charset[state] + *cursor;
                    if(state >= charset.count) {
                        return false;
                    }
                }
            }

            // sub_597B1C @ 0x597B1C: follow parent links, emit each byte, then
            // reverse the temporary byte vector.
            void Decode(std::uint32_t nameIndex, std::string &name) const {
                std::vector<char> bytes;
                std::uint32_t node = namesData[nameIndexes[nameIndex]];
                while(node != 0) {
                    const std::uint32_t parent = namesData[node];
                    bytes.push_back(static_cast<char>(node - charset[parent]));
                    node = parent;
                }
                std::reverse(bytes.begin(), bytes.end());
                name.assign(bytes.begin(), bytes.end());
            }
        };

        // sub_59659C @ 0x59659C is a separate packed-dictionary search
        // helper.  Its output is a 32-bit byte offset relative to dictionary,
        // not a ready-made pointer.
        bool findDictionaryValueOffset(const std::uint8_t *dictionary,
                                       std::uint32_t nameIndex,
                                       std::uint32_t &valueOffset) {
            const PackedArray keys(dictionary);
            std::uint32_t lower = 0;
            std::uint32_t upper = keys.count;
            if(upper == 0) {
                return false;
            }
            std::uint32_t middle{};
            for(;;) {
                middle = (upper + lower) / 2;
                const std::uint32_t candidate = keys[middle];
                if(candidate == nameIndex) {
                    break;
                }
                if(candidate >= nameIndex) {
                    upper = middle;
                } else {
                    lower = middle + 1;
                }
                if(lower >= upper) {
                    return false;
                }
            }

            const PackedArray offsets(keys.end);
            valueOffset =
                static_cast<std::uint32_t>(offsets.end - dictionary) +
                offsets[middle];
            return true;
        }

        std::uint32_t readNodeIndex(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x15:
                case 0x19:
                    return node[1];
                case 0x16:
                case 0x1a:
                    return readUnaligned<std::uint16_t>(node + 1);
                case 0x17:
                case 0x1b:
                    return readUnaligned<std::uint32_t>(node + 1) & 0xffffffu;
                case 0x18:
                case 0x1c:
                    return readUnaligned<std::uint32_t>(node + 1);
                case 0x2c:
                case 0x2d:
                    return 0;
                default:
                    return 0;
            }
        }

        std::uint8_t *copyBuffer(const std::uint8_t *data, std::size_t size) {
            auto result = std::make_unique<std::uint8_t[]>(size);
            std::memcpy(result.get(), data, size);
            return result.release();
        }

        // sub_598268 @ 0x598268 and sub_598538 @ 0x598538 attempt the
        // lower-case mdf wrapper and fall back to an unchanged copy when zlib
        // rejects it.
        std::pair<std::uint8_t *, std::uint32_t>
        copyOrUncompress(const std::uint8_t *data, std::uint32_t size) {
            if(size >= 0x0bu &&
               readUnaligned<std::uint32_t>(data) == MDF_SIGNATURE) {
                const auto expected = readUnaligned<std::uint32_t>(data + 4);
                auto uncompressed = std::make_unique<std::uint8_t[]>(expected);
                auto actual = static_cast<unsigned long>(expected);
                if(uncompress(uncompressed.get(), &actual, data + 8,
                              static_cast<unsigned long>(size - 8)) == Z_OK) {
                    return { uncompressed.release(),
                             static_cast<std::uint32_t>(actual) };
                }
            }
            return { copyBuffer(data, size), size };
        }

        void throwUnknownType() {
            TVPThrowExceptionMessage(TJS_W(
                "psb: internal error: unknown internal type detected.\n"));
        }
    } // namespace

    PSBRawOwner::PSBRawOwner(std::uint8_t *data, std::size_t size) :
        data_(data), size_(size) {
        // sub_598AAC @ 0x598AAC leaves the reference count at zero and builds
        // the inline header view in the constructor itself.  In particular,
        // it does not call the later refresh helper and leaves the header
        // fields untouched when data is null.
        if(data_ != nullptr) {
            header_ = &headerStorage_;
            headerStorage_.signature =
                readUnaligned<std::uint32_t>(data_);
            headerStorage_.version =
                readUnaligned<std::uint16_t>(data_ + 4);
            headerStorage_.encrypt =
                readUnaligned<std::uint16_t>(data_ + 6);
            headerStorage_.encryptData =
                data_ + readUnaligned<std::uint32_t>(data_ + 8);
            headerStorage_.names =
                data_ + readUnaligned<std::uint32_t>(data_ + 12);
            headerStorage_.strings =
                data_ + readUnaligned<std::uint32_t>(data_ + 16);
            headerStorage_.stringsData =
                data_ + readUnaligned<std::uint32_t>(data_ + 20);
            headerStorage_.chunkOffsets =
                data_ + readUnaligned<std::uint32_t>(data_ + 24);
            headerStorage_.chunkLengths =
                data_ + readUnaligned<std::uint32_t>(data_ + 28);
            headerStorage_.chunkData =
                data_ + readUnaligned<std::uint32_t>(data_ + 32);
            headerStorage_.entries =
                data_ + readUnaligned<std::uint32_t>(data_ + 36);
        }
    }

    PSBRawOwner::~PSBRawOwner() { delete[] data_; }

    PSBRawOwner *PSBRawOwner::Create(std::uint8_t *data, std::size_t size) {
        if(size < MIN_PSB_SIZE ||
           readUnaligned<std::uint32_t>(data) != PSB_SIGNATURE) {
            return nullptr;
        }

        auto *owner = new PSBRawOwner(data, size);
        owner->refCount_ = 1;
        return owner;
    }

    void PSBRawOwner::AddRef() { ++refCount_; }

    void PSBRawOwner::Release() {
        if(--refCount_ == 0) {
            delete this;
        }
    }

    const char *PSBRawOwner::GetString(const std::uint8_t *node) const {
        const PackedArray offsets(header_->strings);
        return reinterpret_cast<const char *>(header_->stringsData +
                                              offsets[readNodeIndex(node)]);
    }

    const std::uint8_t *
    PSBRawOwner::GetResource(const std::uint8_t *node,
                             std::uint32_t &size) const {
        if(header_->chunkData == nullptr) {
            return nullptr;
        }
        const PackedArray offsets(header_->chunkOffsets);
        const PackedArray lengths(header_->chunkLengths);
        const std::uint32_t index = readNodeIndex(node);
        size = lengths[index];
        return header_->chunkData + offsets[index];
    }

    bool PSBRawOwner::Refresh(bool validateOffsets) {
        header_ = &headerStorage_;
        headerStorage_.signature = readUnaligned<std::uint32_t>(data_);
        headerStorage_.version = readUnaligned<std::uint16_t>(data_ + 4);
        headerStorage_.encrypt = readUnaligned<std::uint16_t>(data_ + 6);

        const auto offsetEncrypt = readUnaligned<std::uint32_t>(data_ + 8);
        const auto offsetNames = readUnaligned<std::uint32_t>(data_ + 12);
        const auto offsetStrings = readUnaligned<std::uint32_t>(data_ + 16);
        const auto offsetStringsData = readUnaligned<std::uint32_t>(data_ + 20);
        const auto offsetChunkOffsets =
            readUnaligned<std::uint32_t>(data_ + 24);
        const auto offsetChunkLengths =
            readUnaligned<std::uint32_t>(data_ + 28);
        const auto offsetChunkData = readUnaligned<std::uint32_t>(data_ + 32);
        const auto offsetEntries = readUnaligned<std::uint32_t>(data_ + 36);

        headerStorage_.encryptData = data_ + offsetEncrypt;
        headerStorage_.names = data_ + offsetNames;
        headerStorage_.strings = data_ + offsetStrings;
        headerStorage_.stringsData = data_ + offsetStringsData;
        headerStorage_.chunkOffsets = data_ + offsetChunkOffsets;
        headerStorage_.chunkLengths = data_ + offsetChunkLengths;
        headerStorage_.chunkData = data_ + offsetChunkData;
        headerStorage_.entries = data_ + offsetEntries;

        if(!validateOffsets) {
            return true;
        }
        return size_ > offsetEncrypt && size_ >= offsetNames &&
            size_ >= offsetStrings && size_ >= offsetStringsData &&
            size_ >= offsetChunkOffsets && size_ >= offsetChunkLengths &&
            size_ >= offsetChunkData && size_ > offsetEntries;
    }

    PSBRawNode::PSBRawNode(PSBRawOwner *owner, const std::uint8_t *node) :
        owner_(owner), node_(node) {
        if(owner_ != nullptr) {
            owner_->AddRef();
        }
    }

    PSBRawNode::PSBRawNode(const PSBRawNode &other) :
        PSBRawNode(other.owner_, other.node_) {}

    PSBRawNode::PSBRawNode(PSBRawNode &&other) noexcept :
        owner_(other.owner_), node_(other.node_) {
        other.owner_ = nullptr;
        other.node_ = nullptr;
    }

    PSBRawNode::~PSBRawNode() {
        if(owner_ != nullptr) {
            owner_->Release();
        }
    }

    PSBRawNode &PSBRawNode::operator=(const PSBRawNode &other) {
        if(this == &other) {
            return *this;
        }
        // sub_598D58 @ 0x598D58 exposes the raw-node copy order used by the
        // Android holder: release the destination first, copy the owner,
        // retain that owner, then copy the node pointer.  A retain-first
        // copy-and-move implementation changes the aliasing boundary.
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

    PSBRawNode &PSBRawNode::operator=(PSBRawNode &&other) noexcept {
        if(this == &other) {
            return *this;
        }
        if(owner_ != nullptr) {
            owner_->Release();
        }
        owner_ = other.owner_;
        node_ = other.node_;
        // PSBMedia::Resolve @ 0x59A698..0x59A6EC releases the destination,
        // installs the strict getter's returned owner/node without AddRef,
        // then preserves the moved holder's zero-reference deletion branch.
        // The destination deliberately remains dangling if that branch fires.
        if(owner_ != nullptr && owner_->refCount_ == 0) {
            delete owner_;
        }
        other.owner_ = nullptr;
        other.node_ = nullptr;
        return *this;
    }

    std::uint8_t PSBRawNode::GetType() const {
        // sub_599554 @ 0x599554 and sub_5995D8 @ 0x5995D8 dereference the
        // node directly.  sub_598E44 is the separate explicit validity check;
        // type access itself does not normalize a null node to tag zero.
        return node_[0];
    }

    bool PSBRawNode::GetArrayCount(std::uint32_t &count) const {
        if(GetType() != 0x20u) {
            return false;
        }
        count = PackedArray(node_ + 1).count;
        return true;
    }

    bool PSBRawNode::GetArrayElement(std::uint32_t index,
                                     PSBRawNode &value) const {
        const auto *node = FindArrayElement(index);
        if(node == nullptr) {
            return false;
        }
        value = PSBRawNode(owner_, node);
        return true;
    }

    const std::uint8_t *
    PSBRawNode::FindArrayElement(std::uint32_t index) const {
        if(GetType() != 0x20u) {
            return nullptr;
        }
        const PackedArray offsets(node_ + 1);
        return index < offsets.count ? offsets.end + offsets[index] : nullptr;
    }

    bool PSBRawNode::GetDictionaryValue(const std::string &key,
                                        PSBRawNode &value) const {
        const auto *node = FindDictionaryValue(key);
        if(node == nullptr) {
            return false;
        }
        // sub_598D58 @ 0x598D58 releases the destination before retaining the
        // source owner.  Do not normalize this into a retain-first temporary:
        // the ordering (including an aliasing destination boundary) is part
        // of the original intrusive lifecycle.
        if(value.owner_ != nullptr) {
            value.owner_->Release();
        }
        value.owner_ = owner_;
        if(value.owner_ != nullptr) {
            value.owner_->AddRef();
        }
        value.node_ = node;
        return true;
    }

    PSBRawNode
    PSBRawNode::GetDictionaryValueStrict(const std::string &key) const {
        // sub_598C58 @ 0x598C58 returns a newly retained node and throws when
        // either the name trie or the dictionary lookup misses.
        const auto *node = FindDictionaryValue(key);
        if(node == nullptr) {
            TVPThrowExceptionMessage(
                TJS_W("psb: undefined object key '%1' is referenced."),
                ttstr(key));
            // sub_598C58 @ 0x598D08 zeroes the returned owner/node pair if
            // the exception helper ever returns instead of unwinding.
            return {};
        }
        return PSBRawNode(owner_, node);
    }

    bool PSBRawNode::ContainsDictionaryKey(const std::string &key) const {
        // sub_5995D8 @ 0x5995D8 returns false for every known non-dictionary
        // type, delegates dictionaries to sub_598D58 through a temporary raw
        // node, then destroys that temporary.  The AddRef/Release no-op is an
        // original lifecycle step rather than an optimization opportunity.
        switch(GetType()) {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x20:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f:
            case 0x30:
            case 0x31:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3f:
            case 0x41:
                return false;
            case 0x21: {
                PSBRawNode value;
                return GetDictionaryValue(key, value);
            }
            default:
                throwUnknownType();
        }
        return false;
    }

    const std::uint8_t *
    PSBRawNode::FindDictionaryValue(const std::string &key) const {
        std::uint32_t nameIndex{};
        const NameTables names(owner_->GetHeader()->names);
        if(!names.Find(key.c_str(), nameIndex)) {
            return nullptr;
        }

        // sub_59659C @ 0x59659C stops at the first midpoint that equals the
        // requested name index.  It does not continue toward the first equal
        // entry as std::lower_bound would do.
        std::uint32_t valueOffset{};
        if(!findDictionaryValueOffset(node_ + 1, nameIndex, valueOffset)) {
            return nullptr;
        }
        return node_ + 1 + valueOffset;
    }

    bool PSBRawNode::GetDictionaryCount(std::uint32_t &count) const {
        if(GetType() != 0x21u) {
            return false;
        }
        const PackedArray keys(node_ + 1);
        count = keys.count;
        return true;
    }

    bool PSBRawNode::GetDictionaryKey(std::uint32_t index,
                                      std::string &key) const {
        if(GetType() != 0x21u) {
            return false;
        }
        const PackedArray keys(node_ + 1);
        if(index >= keys.count) {
            return false;
        }
        const NameTables names(owner_->GetHeader()->names);
        names.Decode(keys[index], key);
        return true;
    }

    bool PSBRawNode::GetDictionaryEntry(std::uint32_t index, std::string &key,
                                        const std::uint8_t *&value) const {
        if(GetType() != 0x21u) {
            return false;
        }
        const PackedArray keys(node_ + 1);
        if(index >= keys.count) {
            return false;
        }
        const NameTables names(owner_->GetHeader()->names);
        const PackedArray offsets(keys.end);
        names.Decode(keys[index], key);
        value = offsets.end + offsets[index];
        return true;
    }

    std::vector<std::string> PSBRawNode::GetDictionaryKeys() const {
        // sub_598E64 @ 0x598E64.
        std::vector<std::string> result;
        std::string key;
        switch(GetType()) {
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x20:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2c:
            case 0x2d:
            case 0x2e:
            case 0x2f:
            case 0x30:
            case 0x31:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3f:
            case 0x41:
                return result;
            case 0x21:
                break;
            default:
                throwUnknownType();
        }
        const PackedArray keys(node_ + 1);
        const NameTables names(owner_->GetHeader()->names);
        result.reserve(keys.count);
        for(std::size_t index = 0; index < keys.count; ++index) {
            names.Decode(keys[static_cast<std::uint32_t>(index)], key);
            result.emplace_back(key);
        }
        return result;
    }

    int PSBRawNode::GetTypeCategory() const {
        // sub_599554 @ 0x599554.
        switch(GetType()) {
            case 0x01:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x3f:
                return 0;
            case 0x02:
            case 0x03:
            case 0x27:
            case 0x2f:
            case 0x33:
            case 0x37:
            case 0x3b:
                return 1;
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x28:
            case 0x29:
            case 0x30:
            case 0x31:
            case 0x34:
            case 0x35:
            case 0x38:
            case 0x39:
            case 0x3c:
            case 0x3d:
                return 2;
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x2e:
            case 0x41:
                return 3;
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x2c:
                return 4;
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x2d:
                return 5;
            case 0x20:
                return 6;
            case 0x21:
                return 7;
            default:
                throwUnknownType();
        }
        return -1;
    }

    tjs_int PSBRawNode::GetInt() const {
        // sub_599438 @ 0x599438.
        switch(GetType()) {
            case 0x02:
                return 1;
            case 0x03:
            case 0x04:
                return 0;
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
                return static_cast<tjs_int>(DecodeInteger(node_));
            case 0x1d:
                return 0;
            case 0x1e:
            case 0x1f:
                return static_cast<tjs_int>(DecodeReal(node_));
            default:
                TVPThrowExceptionMessage(
                    TJS_W("psb: can't convert value to int."));
        }
        return 0;
    }

    tjs_real PSBRawNode::GetDouble() const {
        // sub_5992E8 @ 0x5992E8.
        switch(GetType()) {
            case 0x02:
                return 1.0;
            case 0x03:
            case 0x04:
                return 0.0;
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
                return static_cast<tjs_real>(DecodeInteger(node_));
            case 0x1d:
                return 0.0;
            case 0x1e:
            case 0x1f:
                return DecodeReal(node_);
            default:
                TVPThrowExceptionMessage(
                    TJS_W("psb: can't convert value to double."));
        }
        return 0.0;
    }

    const char *PSBRawNode::GetString() const {
        // sub_598B58 @ 0x598B58 owns this tag switch; it does not route through
        // the separate category helper sub_599554.  The returned pointer is a
        // borrowed view into the owner's string-data section.
        switch(GetType()) {
            case 0x15:
            case 0x16:
            case 0x17:
            case 0x18:
            case 0x2c:
                return owner_->GetString(node_);
            case 0x01:
            case 0x02:
            case 0x03:
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
            case 0x19:
            case 0x1a:
            case 0x1b:
            case 0x1c:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x20:
            case 0x21:
            case 0x23:
            case 0x24:
            case 0x25:
            case 0x26:
            case 0x27:
            case 0x28:
            case 0x29:
            case 0x2d:
            case 0x2e:
            case 0x2f:
            case 0x30:
            case 0x31:
            case 0x33:
            case 0x34:
            case 0x35:
            case 0x37:
            case 0x38:
            case 0x39:
            case 0x3b:
            case 0x3c:
            case 0x3d:
            case 0x3f:
            case 0x41:
                return nullptr;
            default:
                throwUnknownType();
        }
        return nullptr;
    }

    const std::uint8_t *PSBRawNode::GetResource(std::uint32_t &size) const {
        // PSB_getResourceData @ 0x5996E4 returns the borrowed pointer and uses
        // size as its only output parameter.
        return owner_->GetResource(node_, size);
    }

    tjs_int64 PSBRawNode::DecodeInteger(const std::uint8_t *node) {
        const auto type = node[0];
        return type >= 0x04u && type <= 0x0cu
            ? readSigned(node + 1, static_cast<std::uint8_t>(type - 0x04u))
            : 0;
    }

    tjs_real PSBRawNode::DecodeReal(const std::uint8_t *node) {
        switch(node[0]) {
            case 0x1e:
                return readUnaligned<float>(node + 1);
            case 0x1f:
                return readUnaligned<double>(node + 1);
            default:
                return 0.0;
        }
    }

    PSBFile::PSBFile(const PSBFile &other) noexcept : owner_(other.owner_) {
        // ResourceManager_loadResource @ 0x6A8E94..0x6A8EB8 copies the
        // cached one-pointer holder and increments the intrusive owner count.
        if(owner_ != nullptr) {
            owner_->AddRef();
        }
    }

    PSBFile &PSBFile::operator=(const PSBFile &other) noexcept {
        // ResourceManager_loadResource @ 0x6A926C..0x6A92A8 releases the
        // mapped holder, copies the incoming pointer, then increments it.
        if(owner_ != nullptr) {
            owner_->Release();
        }
        owner_ = other.owner_;
        if(owner_ != nullptr) {
            owner_->AddRef();
        }
        return *this;
    }

    PSBFile::PSBFile(PSBFile &&other) noexcept : owner_(other.owner_) {
        // sub_598A64 @ 0x598A64 copies the pointer to the destination, destroys
        // an owner whose intrusive count is already zero, and only then clears
        // the source.  The zero-count state is unreachable through Create(),
        // but the branch is an original lifecycle boundary rather than a
        // safety condition to normalize away.
        if(owner_ != nullptr && owner_->refCount_ == 0) {
            delete owner_;
        }
        other.owner_ = nullptr;
    }

    PSBFile &PSBFile::operator=(PSBFile &&other) noexcept {
        if(this == &other) {
            return *this;
        }
        if(owner_ != nullptr) {
            owner_->Release();
        }
        owner_ = other.owner_;
        other.owner_ = nullptr;
        return *this;
    }

    PSBFile::~PSBFile() {
        if(owner_ != nullptr) {
            owner_->Release();
        }
    }

    bool PSBFile::Load(tTJSVariant value) {
        // sub_598268 @ 0x598268 is the typed NCB method registered as "load".
        if(value.Type() == tvtString) {
            const ttstr path(value);
            if(!LoadStorage(path)) {
                TVPThrowExceptionMessage(TJS_W("cannot open psb file : %1"),
                                         path);
            }
            return true;
        }
        if(value.Type() == tvtOctet) {
            const auto *octet = value.AsOctetNoAddRef();
            if(!LoadOctet(octet->GetData(), octet->GetLength())) {
                TVPThrowExceptionMessage(TJS_W("octet: invalid psb file."));
            }
            return true;
        }
        TVPThrowExceptionMessage(TJS_W("invalid argument for PSBFile.load()"));
        return true;
    }

    bool PSBFile::LoadStorage(const ttstr &name, const OwnerFilter &filter) {
        // sub_598538 @ 0x598538 reads into one owned allocation.  A successful
        // mdf decode replaces (and frees) that allocation; a rejected decode
        // keeps the original allocation as the PSB candidate.
        const ttstr placed = TVPGetPlacedPath(name);
        std::unique_ptr<TJS::tTJSBinaryStream> stream(
            TVPCreateStream(placed, TJS_BS_READ));
        if(stream == nullptr || stream->GetSize() < 9) {
            return false;
        }

        const auto size = static_cast<std::uint32_t>(stream->GetSize());
        auto *data = new std::uint8_t[size];
        // sub_598538 @ 0x5985CC..0x5985DC and its exception landing pad
        // 0x5986D0..0x5986E8 keep this allocation as a raw pointer.  If the
        // read throws, only the stream is destroyed and data is leaked.
        stream->ReadBuffer(data, size);

        std::uint32_t dataSize = size;
        if(size >= 0x0bu &&
           readUnaligned<std::uint32_t>(data) == MDF_SIGNATURE) {
            const auto expected = readUnaligned<std::uint32_t>(data + 4);
            auto *uncompressed = new std::uint8_t[expected];
            auto actual = static_cast<unsigned long>(expected);
            if(uncompress(uncompressed, &actual, data + 8,
                          static_cast<unsigned long>(size - 8)) == Z_OK) {
                delete[] data;
                data = uncompressed;
                dataSize = static_cast<std::uint32_t>(actual);
            } else {
                delete[] uncompressed;
            }
        }

        // The original storage path does not reclaim data when sub_598708
        // rejects it; preserve that boundary leak instead of folding this path
        // into the octet cleanup below.
        return Adopt(data, dataSize, filter);
    }

    bool PSBFile::LoadOctet(const std::uint8_t *data, std::uint32_t size,
                            const OwnerFilter &filter) {
        auto [copy, copySize] = copyOrUncompress(data, size);
        if(Adopt(copy, copySize, filter)) {
            return true;
        }
        delete[] copy;
        return false;
    }

    bool PSBFile::Adopt(std::uint8_t *data, std::size_t size,
                        const OwnerFilter &filter) {
        PSBRawOwner *next = PSBRawOwner::Create(data, size);
        if(next == nullptr) {
            return false;
        }

        if(owner_ != nullptr) {
            owner_->Release();
        }
        owner_ = next;
        // sub_598708 @ 0x598828..0x598840 preserves the moved holder's
        // zero-reference deletion branch immediately after replacing the
        // destination.  Create() normally sets this count to one, but the
        // branch is still part of the Android object-lifetime boundary.
        if(next->refCount_ == 0) {
            delete next;
        }

        if(filter) {
            filter(*owner_);
            return owner_->Refresh(true);
        }
        return true;
    }

    PSBRawNode PSBFile::GetRoot() const {
        // sub_598A3C @ 0x598A3C dereferences owner/header without a null
        // guard.  Only the typed root getter sub_5981F8 guards an empty file.
        return { owner_, owner_->GetHeader()->entries };
    }
} // namespace PSB
