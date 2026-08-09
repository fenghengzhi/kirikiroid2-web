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

        std::uint8_t *tryDecodeMdf_guess(const std::uint8_t *source,
                                         std::uint32_t &size) {
            // Android PSBFile::Load @0x5982B0..0x59841C and LoadStorage
            // @0x5985E0..0x5986B8 contain complete inline clones of this
            // algorithm. iOS arm64 independently retains one shared helper
            // with exactly the Load/LoadStorage callers. The original identifier
            // is not retained.
            if(size < 0x0bu ||
               detail::ReadUnaligned_guess<std::uint32_t>(source) !=
                   MDF_SIGNATURE) {
                return nullptr;
            }

            const std::uint32_t expected =
                detail::ReadUnaligned_guess<std::uint32_t>(source + 4);
            auto actual = static_cast<unsigned long>(expected);
            auto *decoded = static_cast<std::uint8_t *>(
                TJSAlignedAlloc(expected, 4));
            if(uncompress(decoded, &actual, source + 8,
                          static_cast<unsigned long>(size - 8)) != Z_OK) {
                if(decoded != nullptr) {
                    delete[] decoded;
                }
                return nullptr;
            }
            size = static_cast<std::uint32_t>(actual);
            return decoded;
        }

    } // namespace

    bool detail::FindNameIndex_guess(const std::uint8_t *names,
                                     const char *name,
                                     std::uint32_t &nameIndex) {
        // sub_59641C @ 0x59641C walks the first two consecutive packed arrays
        // as a double-array trie and returns the encoded terminal name index.
        const PsbArray_guess charset(names);
        const PsbArray_guess namesData(names + charset.nBytes);
        const auto *cursor = reinterpret_cast<const std::uint8_t *>(name);
        std::uint32_t parent = 0;
        std::uint32_t state = charset[0] + *cursor;
        if(state >= charset.nElementCount) {
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
            if(state >= charset.nElementCount) {
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
        const PsbArray_guess keys(dictionary);
        std::uint32_t lower = 0;
        std::uint32_t upper = keys.nElementCount;
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
        const PsbArray_guess offsets(dictionary + keys.nBytes);
        valueOffset = keys.nBytes + offsets.nBytes + offsets[middle];
        return true;
    }

    void detail::DecodeName_guess(std::string &name,
                                  const PSBRawOwner *owner,
                                  std::uint32_t nameIndex) {
        // sub_597B1C @ 0x597B1C follows parent links into a byte vector,
        // reverses that vector, then calls std::string::assign(const char *,
        // size_t) @ 0x597E10; the empty path supplies the vector's null begin
        // pointer and a zero length through the same overload.
        const std::uint8_t *names = owner->GetHeader()->names;
        const PsbArray_guess charset(names);
        const std::uint8_t *namesDataCode = names + charset.nBytes;
        const PsbArray_guess namesData(namesDataCode);
        const std::uint8_t *nameIndexesCode =
            namesDataCode + namesData.nBytes;
        const PsbArray_guess nameIndexes(nameIndexesCode);
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
        if(!detail::FindNameIndex_guess(GetOwner()->GetHeader()->names,
                                        key, nameIndex) ||
           !detail::FindDictionaryValueOffset_guess(node_ + 1, nameIndex,
                                                    valueOffset)) {
            return false;
        }
        const std::uint8_t *child = node_ + 1 + valueOffset;
        // sub_598D58 @ 0x598D58 releases the destination before retaining the
        // source owner. Same-lineage iOS @0x1000EDB54..0x1000EDB70 calls the
        // shared PSBFile assignment after capturing child, then writes node.
        // Do not normalize this into a retain-first temporary or add a self
        // guard: the aliased-output boundary is part of the original flow.
        value.file_ = file_;
        value.node_ = child;
        return true;
    }

    bool PSBRawNode::IsValid_guess() const {
        // sub_598E44 @ 0x598E44 short-circuits before reading the node slot
        // when the owner is null.  The binary exposes no source-level name.
        return GetOwner() != nullptr && node_ != nullptr;
    }

    PSBRawNode
    PSBRawNode::GetDictionaryValueStrict(const char *key) const {
        // sub_598C58 @ 0x598C58 returns a newly retained node and throws when
        // either the name trie or the dictionary lookup misses.
        std::uint32_t nameIndex;
        std::uint32_t valueOffset;
        if(!detail::FindNameIndex_guess(GetOwner()->GetHeader()->names,
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
        return PSBRawNode(file_, node_ + 1 + valueOffset);
    }

    bool PSBRawNode::ContainsDictionaryKey(const char *key) const {
        // sub_5995D8 @ 0x5995D8 contains the complete category-specialized
        // classifier residual. Same-lineage iOS arm64 @0x1000EDF14 retains the
        // shared classifier call. The temporary raw node is
        // constructed before that gate and destroyed on every exit.
        PSBRawNode value;
        if(detail::GetTypeCategory_guess(GetType()) != 7) {
            return false;
        }
        return GetDictionaryValue(key, value);
    }

    std::vector<std::string> PSBRawNode::GetDictionaryKeys() const {
        // sub_598E64 @ 0x598E64.
        std::vector<std::string> result;
        // sub_598E64 @ 0x598EA0..0x598F48 contains the complete
        // category-specialized classifier residual. Same-lineage iOS arm64
        // @0x1000EDBBC retains the shared classifier call.
        if(detail::GetTypeCategory_guess(GetType()) != 7) {
            return result;
        }
        // 0x598EF8..0x598F04 constructs this reusable string only after the
        // Dictionary tag gate; non-container and unknown-tag paths never own
        // it, which also changes their exception cleanup layer.
        std::string key;
        const std::uint8_t *packed = node_ + 1;
        // Android arm64 scalarizes the keys view and eliminates the unused
        // offsets view. iOS arm64 independently retains both constructor
        // calls and the same dead second view.
        const detail::PsbArray_guess keys(packed);
        const detail::PsbArray_guess offsets(packed + keys.nBytes);
        (void)offsets;
        result.reserve(keys.nElementCount);
        for(std::size_t index = 0; index < keys.nElementCount; ++index) {
            detail::DecodeName_guess(
                key, GetOwner(), keys[static_cast<std::uint32_t>(index)]);
            result.emplace_back(key);
        }
        return result;
    }

    int PSBRawNode::GetTypeCategory() const {
        // sub_599554 @ 0x599554 is this wrapper plus the complete classifier;
        // all dispatch/raw/media source consumers, including IsInstanceOf at
        // 0x596E24, call the shared helper in PSBPackedInternal.h.
        return detail::GetTypeCategory_guess(GetType());
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
        // iOS arm64 retains calls to the narrow/wide decoders while keeping
        // 0x0b and float/double conversion in this wrapper.
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
                return detail::DecodeInteger32_guess(node_);
            case 0x09:
            case 0x0a:
            case 0x0c:
                return static_cast<tjs_int>(
                    detail::DecodeInteger64_guess(node_));
            case 0x0b:
                return detail::ReadUnaligned_guess<std::int32_t>(node_ + 1);
            case 0x1d:
                return 0;
            case 0x1e:
                return static_cast<tjs_int>(
                    detail::ReadUnaligned_guess<float>(node_ + 1));
            case 0x1f:
                return static_cast<tjs_int>(
                    detail::ReadUnaligned_guess<double>(node_ + 1));
            default:
                TVPThrowExceptionMessage(
                    TJS_W("psb: can't convert value to int."));
        }
        return 0;
    }

    tjs_real PSBRawNode::GetDouble() const {
        // sub_5992E8 @ 0x5992E8 is this wrapper plus the complete inlined raw
        // double decoder. iOS arm64 independently preserves the source-level
        // raw-double helper call and its integer-decoder calls.
        return detail::DecodeNumberAsDouble_guess(node_);
    }

    const char *PSBRawNode::GetString() const {
        // sub_598B58 @ 0x598B58 contains the complete category-4-specialized
        // classifier residual. Same-lineage iOS arm64 @0x1000ED968 retains the
        // shared classifier call before constructing the
        // packed strings-offset view.
        if(detail::GetTypeCategory_guess(GetType()) != 4) {
            return nullptr;
        }
        const PSBRawHeader *header = GetOwner()->GetHeader();
        const detail::PsbArray_guess offsets(header->strings);
        std::uint32_t index = 0;
        switch(node_[0]) {
            case 0x15:
                index = node_[1];
                break;
            case 0x16:
                index = detail::ReadUnaligned_guess<std::uint16_t>(node_ + 1);
                break;
            case 0x17:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node_ + 1) &
                    0xffffffu;
                break;
            case 0x18:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node_ + 1);
                break;
            default:
                break;
        }
        return reinterpret_cast<const char *>(
            header->stringsData + offsets[index]);
    }

    const std::uint8_t *PSBRawNode::GetResource(std::uint32_t &size) const {
        // PSBRawNode_GetResource_guess @ 0x5996E4 leaves size untouched when
        // chunkData is null and only recognizes the resource-index tag family.
        const PSBRawHeader *header = GetOwner()->GetHeader();
        if(header->chunkData == nullptr) {
            return nullptr;
        }
        const detail::PsbArray_guess offsets(header->chunkOffsets);
        const detail::PsbArray_guess lengths(header->chunkLengths);
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

    PSBFile PSBFile::Transfer_guess() {
        // sub_598A64 @ 0x598A64 visibly copies the owner into the hidden
        // return slot, preserves the incoming-zero deletion edge, then clears
        // the source.  This Rule-of-Three expression reconstructs that exact
        // shape. iOS arm64 retains copy/AddRef followed by shared Release and
        // source-null. Exact special-member tokens and
        // the helper's name/member/free identity remain stripped; hence _guess.
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
        std::uint8_t *data = tryDecodeMdf_guess(source, size);
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
        auto *uncompressed = tryDecodeMdf_guess(data, dataSize);
        // sub_598538 @ 0x5986A4..0x5986B4 updates the decoded size first, but
        // only releases/replaces the source when the returned pointer is
        // non-null.
        if(uncompressed != nullptr) {
            TJSAlignedDealloc(data);
            data = uncompressed;
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
        return { *this, owner_->GetHeader()->entries };
    }
} // namespace PSB
