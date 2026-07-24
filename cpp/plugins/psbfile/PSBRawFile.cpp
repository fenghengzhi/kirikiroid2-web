#include "PSBRawFile.h"
#include "PSBPackedInternal.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include <zlib.h>

#include "MsgIntf.h"
#include "StorageIntf.h"
#include "tjsUtils.h"

namespace PSB {
    namespace {
        constexpr std::uint32_t PSB_SIGNATURE = 0x00425350u;
        constexpr std::uint32_t MDF_SIGNATURE = 0x0066646du;
        constexpr std::size_t MIN_PSB_SIZE = 0x40u;

        void throwUnknownType() {
            TVPThrowExceptionMessage(TJS_W(
                "psb: internal error: unknown internal type detected.\n"));
        }

        // The nested jump tables in GetDouble @ 0x5992E8 and GetInt @
        // 0x599438 expose corresponding decoder-shaped regions except for
        // GetInt's tag-0x0b low-word-only path @ 0x599544. Shared inlined
        // helpers reproduce only the common data flow; their original names,
        // member/free-function identity, and exact factorization are not
        // present in the binary, hence the _guess suffixes.
        tjs_int DecodeInteger32_guess(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x05:
                    return static_cast<std::int8_t>(node[1]);
                case 0x06:
                    return detail::ReadUnaligned_guess<std::int16_t>(node + 1);
                case 0x07:
                    return static_cast<tjs_int>(
                        detail::ReadUnaligned_guess<std::uint16_t>(node + 1) |
                        (static_cast<std::uint32_t>(
                             static_cast<std::int8_t>(node[3]))
                         << 16));
                case 0x08:
                    return detail::ReadUnaligned_guess<std::int32_t>(node + 1);
                default:
                    return 0;
            }
        }

        tjs_int64 DecodeInteger64_guess(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x09:
                    return static_cast<tjs_int64>(
                        detail::ReadUnaligned_guess<std::uint32_t>(node + 1) |
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                             static_cast<std::int8_t>(node[5])))
                         << 32));
                case 0x0a:
                    return static_cast<tjs_int64>(
                        detail::ReadUnaligned_guess<std::uint32_t>(node + 1) |
                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                             detail::ReadUnaligned_guess<std::int16_t>(
                                 node + 5)))
                         << 32));
                case 0x0b:
                    // GetDouble @ 0x599410..0x599424 materializes all seven
                    // bytes without extending bit 55. GetInt has a distinct
                    // low-word-only path @ 0x599544 and does not use this
                    // decoder for tag 0x0b.
                    return static_cast<tjs_int64>(
                        detail::ReadUnaligned_guess<std::uint32_t>(node + 1) |
                        (static_cast<std::uint64_t>(
                             detail::ReadUnaligned_guess<std::uint16_t>(
                                 node + 5))
                         << 32) |
                        (static_cast<std::uint64_t>(node[7]) << 48));
                case 0x0c:
                    return detail::ReadUnaligned_guess<tjs_int64>(node + 1);
                default:
                    return 0;
            }
        }

        float DecodeFloat_guess(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x1d:
                    return 0.0f;
                case 0x1e:
                    return detail::ReadUnaligned_guess<float>(node + 1);
                default:
                    return 0.0f;
            }
        }

        double DecodeDouble_guess(const std::uint8_t *node) {
            switch(node[0]) {
                case 0x1f:
                    return detail::ReadUnaligned_guess<double>(node + 1);
                default:
                    return 0.0;
            }
        }
    } // namespace

    bool detail::FindNameIndex_guess(const std::uint8_t *names,
                                     const char *name,
                                     std::uint32_t &nameIndex) {
        // sub_59641C @ 0x59641C walks the three consecutive packed arrays as
        // a double-array trie and returns the encoded terminal name index.
        const PackedArrayView_guess charset(names);
        const PackedArrayView_guess namesData(charset.end);
        const auto *cursor = reinterpret_cast<const std::uint8_t *>(name);
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

    bool detail::FindDictionaryValueOffset_guess(
        const std::uint8_t *dictionary, std::uint32_t nameIndex,
        std::uint32_t &valueOffset) {
        // sub_59659C @ 0x59659C uses a lower<upper loop.  Even the equal
        // branch joins the post-loop lower>=upper failure gate at
        // 0x596674..0x596678 before decoding the parallel offset table.
        const PackedArrayView_guess keys(dictionary);
        std::uint32_t lower = 0;
        std::uint32_t upper = keys.count;
        std::uint32_t middle;
        while(lower < upper) {
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
        }
        if(lower >= upper) {
            return false;
        }
        const PackedArrayView_guess offsets(keys.end);
        valueOffset =
            static_cast<std::uint32_t>(offsets.end - dictionary) +
            offsets[middle];
        return true;
    }

    void detail::DecodeName_guess(std::string &name,
                                  const PSBRawOwner *owner,
                                  std::uint32_t nameIndex) {
        // sub_597B1C @ 0x597B1C follows parent links into a byte vector,
        // reverses that vector, then calls std::string::assign(const char *,
        // size_t) @ 0x597E10; the empty path supplies the vector's null begin
        // pointer and a zero length through the same overload.
        const PackedArrayView_guess charset(owner->GetHeader()->names);
        const PackedArrayView_guess namesData(charset.end);
        const PackedArrayView_guess nameIndexes(namesData.end);
        std::vector<char> bytes;
        std::uint32_t node = namesData[nameIndexes[nameIndex]];
        while(node != 0) {
            const std::uint32_t parent = namesData[node];
            bytes.push_back(static_cast<char>(node - charset[parent]));
            node = parent;
        }
        std::reverse(bytes.begin(), bytes.end());
        name.assign(bytes.data(), bytes.size());
    }

    PSBRawOwner::PSBRawOwner(std::uint8_t *data, std::size_t size) :
        data_(data), size_(size) {
        // sub_598AAC @ 0x598AAC leaves the reference count at zero and builds
        // the inline header view in the constructor itself.  In particular,
        // it does not call the later refresh helper and leaves the header
        // fields untouched when data is null.
        if(data_ != nullptr) {
            header_ = &headerStorage_;
            headerStorage_.signature =
                detail::ReadUnaligned_guess<std::uint32_t>(data_);
            headerStorage_.version =
                detail::ReadUnaligned_guess<std::uint16_t>(data_ + 4);
            headerStorage_.encrypt =
                detail::ReadUnaligned_guess<std::uint16_t>(data_ + 6);
            headerStorage_.encryptData =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 8);
            headerStorage_.names =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 12);
            headerStorage_.strings =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 16);
            headerStorage_.stringsData =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 20);
            headerStorage_.chunkOffsets =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 24);
            headerStorage_.chunkLengths =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 28);
            headerStorage_.chunkData =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 32);
            headerStorage_.entries =
                data_ + detail::ReadUnaligned_guess<std::uint32_t>(data_ + 36);
        }
    }

    PSBRawOwner::~PSBRawOwner() {
        // sub_598B3C @ 0x598B3C releases the raw PSB allocation through the
        // matching aligned allocator family.
        TJSAlignedDealloc(data_);
    }

    bool PSBRawOwner::Refresh(bool validateOffsets) {
        header_ = &headerStorage_;
        headerStorage_.signature =
            detail::ReadUnaligned_guess<std::uint32_t>(data_);
        headerStorage_.version =
            detail::ReadUnaligned_guess<std::uint16_t>(data_ + 4);
        headerStorage_.encrypt =
            detail::ReadUnaligned_guess<std::uint16_t>(data_ + 6);

        const auto offsetEncrypt =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 8);
        const auto offsetNames =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 12);
        const auto offsetStrings =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 16);
        const auto offsetStringsData =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 20);
        const auto offsetChunkOffsets =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 24);
        const auto offsetChunkLengths =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 28);
        const auto offsetChunkData =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 32);
        const auto offsetEntries =
            detail::ReadUnaligned_guess<std::uint32_t>(data_ + 36);

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

    bool PSBRawNode::GetDictionaryValue(const char *key,
                                        PSBRawNode &value) const {
        // sub_598D58 @ 0x598D58 calls the two real packed helpers directly.
        std::uint32_t nameIndex;
        std::uint32_t valueOffset;
        if(!detail::FindNameIndex_guess(owner_->GetHeader()->names,
                                        key, nameIndex) ||
           !detail::FindDictionaryValueOffset_guess(node_ + 1, nameIndex,
                                                    valueOffset)) {
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
        value.node_ = node_ + 1 + valueOffset;
        return true;
    }

    bool PSBRawNode::IsValid_guess() const {
        // sub_598E44 @ 0x598E44 short-circuits before reading the node slot
        // when the owner is null.  The binary exposes no source-level name.
        return owner_ != nullptr && node_ != nullptr;
    }

    PSBRawNode
    PSBRawNode::GetDictionaryValueStrict(const char *key) const {
        // sub_598C58 @ 0x598C58 returns a newly retained node and throws when
        // either the name trie or the dictionary lookup misses.
        std::uint32_t nameIndex;
        std::uint32_t valueOffset;
        if(!detail::FindNameIndex_guess(owner_->GetHeader()->names,
                                        key, nameIndex) ||
           !detail::FindDictionaryValueOffset_guess(node_ + 1, nameIndex,
                                                    valueOffset)) {
            TVPThrowExceptionMessage(
                TJS_W("psb: undefined object key '%1' is referenced."),
                ttstr(key));
            // sub_598C58 @ 0x598D08 zeroes the returned owner/node pair if
            // the exception helper ever returns instead of unwinding.
            return {};
        }
        return PSBRawNode(owner_, node_ + 1 + valueOffset);
    }

    bool PSBRawNode::ContainsDictionaryKey(const char *key) const {
        // sub_5995D8 @ 0x5995D8 returns false for every known non-dictionary
        // type, delegates dictionaries to sub_598D58 through a temporary raw
        // node, then destroys that temporary.  The AddRef/Release no-op is an
        // original lifecycle step rather than an optimization opportunity.
        PSBRawNode value;
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
                return GetDictionaryValue(key, value);
            }
            default:
                throwUnknownType();
        }
        return false;
    }

    std::vector<std::string> PSBRawNode::GetDictionaryKeys() const {
        // sub_598E64 @ 0x598E64.
        std::vector<std::string> result;
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
                // sub_598E64 @ 0x598F48 returns the already-constructed empty
                // vector if the exception helper unexpectedly returns.
                return result;
        }
        // 0x598EF8..0x598F04 constructs this reusable string only after the
        // Dictionary tag gate; non-container and unknown-tag paths never own
        // it, which also changes their exception cleanup layer.
        std::string key;
        const detail::PackedArrayView_guess keys(node_ + 1);
        result.reserve(keys.count);
        for(std::size_t index = 0; index < keys.count; ++index) {
            detail::DecodeName_guess(
                key, owner_, keys[static_cast<std::uint32_t>(index)]);
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
        // sub_599438 @ 0x599438 is the outer tag dispatcher. Its nested
        // 0x599484/0x5994AC jump tables inline the integer decoders. Tag 0x0b
        // is a distinct low-word-only load @ 0x599544; unlike GetDouble, it
        // must not read the remaining three encoded bytes.
        // LDRSB/LDURSH/FCVTZS write W0 on negative/numeric paths; 18 direct
        // consumers read W0 (four via signed SCVTF D0,W0) and two discard it.
        // This closes the return semantics as signed 32-bit even though some
        // wide-tag paths incidentally leave additional bits in X0.
        switch(GetType()) {
            case 0x02:
                return 1;
            case 0x03:
                return 0;
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
                return DecodeInteger32_guess(node_);
            case 0x09:
            case 0x0a:
            case 0x0c:
                return static_cast<tjs_int>(DecodeInteger64_guess(node_));
            case 0x0b:
                return detail::ReadUnaligned_guess<std::int32_t>(node_ + 1);
            case 0x1d:
            case 0x1e:
                return static_cast<tjs_int>(DecodeFloat_guess(node_));
            case 0x1f:
                return static_cast<tjs_int>(DecodeDouble_guess(node_));
            default:
                TVPThrowExceptionMessage(
                    TJS_W("psb: can't convert value to int."));
        }
        return 0;
    }

    tjs_real PSBRawNode::GetDouble() const {
        // sub_5992E8 @ 0x5992E8 owns the same outer dispatcher and inlines
        // the same four decoder boundaries before converting to tjs_real.
        switch(GetType()) {
            case 0x02:
                return 1.0;
            case 0x03:
                return 0.0;
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
            case 0x08:
                return static_cast<tjs_real>(DecodeInteger32_guess(node_));
            case 0x09:
            case 0x0a:
            case 0x0b:
            case 0x0c:
                return static_cast<tjs_real>(DecodeInteger64_guess(node_));
            case 0x1d:
            case 0x1e:
                return static_cast<tjs_real>(DecodeFloat_guess(node_));
            case 0x1f:
                return DecodeDouble_guess(node_);
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
            case 0x2c: {
                const PSBRawHeader *header = owner_->GetHeader();
                const detail::PackedArrayView_guess offsets(header->strings);
                std::uint32_t index = 0;
                switch(node_[0]) {
                    case 0x15:
                        index = node_[1];
                        break;
                    case 0x16:
                        index = detail::ReadUnaligned_guess<std::uint16_t>(
                            node_ + 1);
                        break;
                    case 0x17:
                        index = detail::ReadUnaligned_guess<std::uint32_t>(
                                    node_ + 1) &
                            0xffffffu;
                        break;
                    case 0x18:
                        index = detail::ReadUnaligned_guess<std::uint32_t>(
                            node_ + 1);
                        break;
                    default:
                        break;
                }
                return reinterpret_cast<const char *>(
                    header->stringsData + offsets[index]);
            }
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
        // PSBRawNode_GetResource_guess @ 0x5996E4 leaves size untouched when
        // chunkData is null and only recognizes the resource-index tag family.
        const PSBRawHeader *header = owner_->GetHeader();
        if(header->chunkData == nullptr) {
            return nullptr;
        }
        const detail::PackedArrayView_guess offsets(header->chunkOffsets);
        const detail::PackedArrayView_guess lengths(header->chunkLengths);
        std::uint32_t index = 0;
        switch(node_[0]) {
            case 0x19:
                index = node_[1];
                break;
            case 0x1a:
                index = detail::ReadUnaligned_guess<std::uint16_t>(node_ + 1);
                break;
            case 0x1b:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node_ + 1) &
                    0xffffffu;
                break;
            case 0x1c:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node_ + 1);
                break;
            default:
                break;
        }
        size = lengths[index];
        return header->chunkData + offsets[index];
    }

    PSBFile PSBFile::Transfer_guess() noexcept {
        // sub_598A64 @ 0x598A64 is the optimized Rule-of-Three sequence:
        // copy into the hidden return slot (AddRef), then copy-assign an empty
        // holder back to the source (Release + clear).  The paired operations
        // naturally retain the incoming-zero deletion edge visible at
        // 0x598A7C..0x598A94; it is not a hand-written move-member check.
        // The binary still cannot recover this helper's original source name
        // or member/free-function identity, hence the _guess suffix.
        PSBFile result(*this);
        *this = PSBFile();
        return result;
    }

    bool PSBFile::Load(tTJSVariant value) {
        // sub_598268 @ 0x598268 is the typed NCB method registered as "load".
        if(value.Type() == tvtString) {
            // 0x598340..0x59834C first obtains the source VariantString's
            // tjs_char pointer, then constructs a fresh ttstr allocation from
            // that pointer.  Do not share/AddRef the source VariantString.
            const ttstr path(value.GetString());
            if(!LoadStorage(path)) {
                TVPThrowExceptionMessage(TJS_W("cannot open psb file : %1"),
                                         path);
            }
            return true;
        }
        if(value.Type() != tvtOctet) {
            TVPThrowExceptionMessage(
                TJS_W("invalid argument for PSBFile.load()"));
            // sub_598268 @ 0x5983A4..0x5983B0 returns true if the exception
            // helper unexpectedly returns; it never falls into octet access.
            return true;
        }

        const auto *octet = value.AsOctetNoAddRef();
        std::uint32_t size = octet->GetLength();
        const std::uint8_t *source = octet->GetData();
        std::uint8_t *data = nullptr;
        if(size >= 0x0bu &&
           detail::ReadUnaligned_guess<std::uint32_t>(source) ==
               MDF_SIGNATURE) {
            const std::uint32_t expected =
                detail::ReadUnaligned_guess<std::uint32_t>(source + 4);
            data = static_cast<std::uint8_t *>(TJSAlignedAlloc(expected, 4));
            auto actual = static_cast<unsigned long>(expected);
            if(uncompress(data, &actual, source + 8,
                          static_cast<unsigned long>(size - 8)) == Z_OK) {
                size = static_cast<std::uint32_t>(actual);
            } else {
                delete[] data;
                data = nullptr;
            }
        }
        if(data == nullptr) {
            data = static_cast<std::uint8_t *>(TJSAlignedAlloc(size, 4));
            std::memcpy(data, source, size);
        }

        if(!Adopt(data, size, {})) {
            delete[] data;
            TVPThrowExceptionMessage(TJS_W("octet: invalid psb file."));
            // sub_598268 @ 0x598338 returns false if the exception helper
            // unexpectedly returns.
            return false;
        }
        return true;
    }

    bool PSBFile::LoadStorage(const ttstr &name, const OwnerFilter &filter) {
        // sub_598538 @ 0x598538 reads into one owned allocation.  A successful
        // mdf decode replaces (and frees) that allocation; a rejected decode
        // keeps the original allocation as the PSB candidate.
        std::unique_ptr<TJS::tTJSBinaryStream> stream(
            TVPCreateStream(TVPGetPlacedPath(name), TJS_BS_READ));
        if(stream == nullptr || stream->GetSize() < 9) {
            return false;
        }

        const auto size = static_cast<std::uint32_t>(stream->GetSize());
        auto *data =
            static_cast<std::uint8_t *>(TJSAlignedAlloc(size, 4));
        // sub_598538 @ 0x5985CC..0x5985DC and its exception landing pad
        // 0x5986D0..0x5986E8 keep this allocation as a raw pointer.  If the
        // read throws, only the stream is destroyed and data is leaked.
        stream->ReadBuffer(data, size);

        std::uint32_t dataSize = size;
        if(size >= 0x0bu &&
           detail::ReadUnaligned_guess<std::uint32_t>(data) == MDF_SIGNATURE) {
            const auto expected =
                detail::ReadUnaligned_guess<std::uint32_t>(data + 4);
            auto *uncompressed = static_cast<std::uint8_t *>(
                TJSAlignedAlloc(expected, 4));
            auto actual = static_cast<unsigned long>(expected);
            if(uncompress(uncompressed, &actual, data + 8,
                          static_cast<unsigned long>(size - 8)) == Z_OK) {
                dataSize = static_cast<std::uint32_t>(actual);
                // sub_598538 @ 0x5986A4..0x5986B4 updates the decoded size
                // first, but only releases/replaces the source when the
                // returned destination pointer is non-null.
                if(uncompressed != nullptr) {
                    TJSAlignedDealloc(data);
                    data = uncompressed;
                }
            } else {
                delete[] uncompressed;
            }
        }

        // The original storage path does not reclaim data when sub_598708
        // rejects it; preserve that boundary leak instead of folding this path
        // into the octet cleanup below.
        return Adopt(data, dataSize, filter);
    }

    bool PSBFile::Adopt(std::uint8_t *data, std::size_t size,
                        const OwnerFilter &filter) {
        // sub_598708 @ 0x598744..0x5987FC owns this validation/allocation
        // sequence; there is no separate owner factory entry.
        if(size < MIN_PSB_SIZE ||
           detail::ReadUnaligned_guess<std::uint32_t>(data) != PSB_SIGNATURE) {
            return false;
        }
        {
            PSBFile replacement;
            replacement.owner_ = new PSBRawOwner(data, size);
            replacement.owner_->AddRef();
            // sub_598708 @ 0x5987FC..0x598844 copy-assigns this temporary
            // holder and then destroys it.  The AddRef/Release pair cancels;
            // its optimized remnant is the old-owner path's incoming-zero
            // deletion gate, while the old-owner-null sibling folds it away.
            *this = replacement;
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
