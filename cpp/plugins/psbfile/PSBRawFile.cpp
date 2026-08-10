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
            // Android arm64 inlines this into both callers; the other three
            // references retain an equivalent shared helper. Their allocation
            // helpers return a 16-byte-aligned interior pointer,
            // yet every failure path passes that pointer directly to
            // operator delete[]. Preserve this allocator mismatch as an
            // observed boundary behavior. The original identifier is not
            // retained.
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
        // All four references walk the first two consecutive packed arrays as
        // a double-array trie and return the terminal index.
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
        // All four references use the same lower<upper binary search. Even the
        // equal branch joins the lower>=upper failure gate before decoding the
        // parallel offset table.
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
        // All four references follow parent links into a byte vector, reverse
        // it, then call string::assign(data, size). The empty path supplies a
        // null begin pointer and zero length.
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
        // Android arm64 preserves this constructor boundary; the other three
        // inline the same sequence into Adopt. All four leave refcount at zero,
        // build the inline header view when data is non-null, and otherwise
        // leave its fields untouched.
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
        // Every reference releases only the raw allocation.
        TJSAlignedDealloc(data_);
    }

    bool PSBRawOwner::Refresh(bool validateOffsets) {
        // The qualified four-reference function map is recorded in the audit.
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
        // Every target uses signed pointer-width comparisons here. Keep the
        // offsets unsigned for the zero-extended pointer additions above, but
        // convert them explicitly for validation: on wasm32, comparing
        // intptr_t directly with uint32_t would otherwise become unsigned.
        return size_ > static_cast<std::intptr_t>(offsetEncrypt) &&
            size_ >= static_cast<std::intptr_t>(offsetNames) &&
            size_ >= static_cast<std::intptr_t>(offsetStrings) &&
            size_ >= static_cast<std::intptr_t>(offsetStringsData) &&
            size_ >= static_cast<std::intptr_t>(offsetChunkOffsets) &&
            size_ >= static_cast<std::intptr_t>(offsetChunkLengths) &&
            size_ >= static_cast<std::intptr_t>(offsetChunkData) &&
            size_ > static_cast<std::intptr_t>(offsetEntries);
    }

    bool PSBRawNode::GetDictionaryValue(const char *key,
                                        PSBRawNode &value) const {
        // All four references use the same non-throwing lookup and replacement
        // order.
        std::uint32_t nameIndex;
        std::uint32_t valueOffset;
        if(!detail::FindNameIndex_guess(GetOwner()->GetHeader()->names,
                                        key, nameIndex) ||
           !detail::FindDictionaryValueOffset_guess(node_ + 1, nameIndex,
                                                    valueOffset)) {
            return false;
        }
        const std::uint8_t *child = node_ + 1 + valueOffset;
        // All four capture child, release the destination owner, retain the
        // source owner, then write the child node. Do not normalize this into
        // a retain-first temporary or add a self guard: the aliased-output
        // boundary is part of the original flow.
        value.file_ = file_;
        value.node_ = child;
        return true;
    }

    bool PSBRawNode::IsValid_guess() const {
        // Only the Android links retain this standalone, unreferenced boundary;
        // both short-circuit before reading node when owner is null. The iOS
        // links emit no independent function for it. No source name survives.
        return GetOwner() != nullptr && node_ != nullptr;
    }

    PSBRawNode
    PSBRawNode::GetDictionaryValueStrict(const char *key) const {
        // Every reference returns a newly retained node and throws when either
        // lookup misses.
        std::uint32_t nameIndex;
        std::uint32_t valueOffset;
        if(!detail::FindNameIndex_guess(GetOwner()->GetHeader()->names,
                                        key, nameIndex) ||
           !detail::FindDictionaryValueOffset_guess(node_ + 1, nameIndex,
                                                    valueOffset)) {
            TVPThrowExceptionMessage(
                TJS_W("psb: undefined object key '%1' is referenced."),
                ttstr(key));
            // All four zero the returned owner/node pair if the exception
            // helper ever returns instead of unwinding.
            return {};
        }
        return PSBRawNode(file_, node_ + 1 + valueOffset);
    }

    bool PSBRawNode::ContainsDictionaryKey(const char *key) const {
        // In all four references the temporary raw node is constructed before
        // the category-7 gate and
        // destroyed on every exit. Android inlines the category classifier;
        // iOS retains the shared helper call.
        PSBRawNode value;
        if(detail::GetTypeCategory_guess(GetType()) != 7) {
            return false;
        }
        return GetDictionaryValue(key, value);
    }

    std::vector<std::string> PSBRawNode::GetDictionaryKeys() const {
        // All four references preserve the same vector construction and
        // temporary-string reuse.
        std::vector<std::string> result;
        // Android inlines the complete category classifier; iOS retains its
        // shared helper call. Both forms continue only for category 7.
        if(detail::GetTypeCategory_guess(GetType()) != 7) {
            return result;
        }
        // Every target constructs this reusable string only after the
        // Dictionary gate; rejected paths never own it.
        std::string key;
        const std::uint8_t *packed = node_ + 1;
        // Android arm64 scalarizes the keys view and eliminates the unused
        // offsets view; the other forms preserve enough constructor state to
        // prove the same dead second view in the shared source.
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
        // Android inlines the classifier; iOS keeps a one-call wrapper. The
        // resulting category map is identical in all four references.
        return detail::GetTypeCategory_guess(GetType());
    }

    tjs_int PSBRawNode::GetInt() const {
        // Android arm64 inlines both integer decoders; the others retain
        // helper calls. Tag 0x0b returns only the low signed-32 ABI value in
        // this wrapper, unlike GetDouble's full integer64 conversion.
        // LDRSB/LDURSH/FCVTZS write W0 on negative/numeric paths; 18 direct
        // consumers read W0 (four via signed SCVTF D0,W0) and two discard it.
        // This closes the return semantics as signed 32-bit even though some
        // wide-tag paths incidentally leave additional bits in X0.
        // All four keep float/double-to-int conversion in this wrapper.
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
        // Android arm64 inlines the raw-number decoder; the other three retain
        // a thin wrapper and shared decoder with identical tag semantics.
        return detail::DecodeNumberAsDouble_guess(node_);
    }

    const char *PSBRawNode::GetString() const {
        // Android inlines the category-4 classifier; iOS retains its helper.
        // The packed string-index path is otherwise identical in all four.
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
        // All four leave size untouched when chunkData is null. They decode
        // only tags 0x19..0x1c; another tag keeps index zero.
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
        // Each reference copies into the hidden return slot, performs the
        // AddRef/Release pair,
        // then clears the source. Android arm64 folds the pair to its
        // incoming-zero deletion edge. Exact special-member tokens remain
        // stripped; hence _guess.
        PSBFile result(*this);
        *this = PSBFile();
        return result;
    }

    bool PSBFile::Load(tTJSVariant value) {
        // All four references implement the same string/octet-only dispatcher.
        if(value.Type() == tvtString) {
            // Each reference obtains the source VariantString's tjs_char
            // pointer, constructs a fresh ttstr allocation, then destroys the
            // temporary. Do not share/AddRef the source VariantString.
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
            // All four return true if the exception helper unexpectedly
            // returns; none falls through into octet access.
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
            // All four return false if the exception helper unexpectedly
            // returns.
            return false;
        }
        return true;
    }

    bool PSBFile::LoadStorage(const ttstr &name, const OwnerFilter &filter) {
        // In every reference a successful MDF decode replaces and frees the
        // source allocation; a rejected decode keeps the source as the PSB
        // candidate.
        std::unique_ptr<TJS::tTJSBinaryStream> stream(
            TVPCreateStream(TVPGetPlacedPath(name), TJS_BS_READ));
        if(stream == nullptr || stream->GetSize() < 9) {
            return false;
        }

        const auto size = static_cast<std::uint32_t>(stream->GetSize());
        auto *data =
            static_cast<std::uint8_t *>(TJSAlignedAlloc(size, 4));
        // The four DWARF/SJLJ cleanup paths destroy the stream (and any
        // temporary string) but do not reclaim this raw pointer if reading
        // throws.
        stream->ReadBuffer(data, size);

        std::uint32_t dataSize = size;
        auto *uncompressed = tryDecodeMdf_guess(data, dataSize);
        // All four update the decoded size first, but release/replace the
        // source only when the returned pointer is non-null.
        if(uncompressed != nullptr) {
            TJSAlignedDealloc(data);
            data = uncompressed;
        }

        // All four storage paths also leave data allocated when Adopt rejects
        // it; preserve that boundary leak rather than folding it into the
        // octet cleanup path.
        return Adopt(data, dataSize, filter);
    }

    bool PSBFile::Adopt(std::uint8_t *data, std::size_t size,
                        const OwnerFilter &filter) {
        // All four references use the same validation, temporary-holder
        // replacement and optional-filter order.
        if(size < MIN_PSB_SIZE ||
           detail::ReadUnaligned_guess<std::uint32_t>(data) != PSB_SIGNATURE) {
            return false;
        }
        {
            PSBFile replacement;
            replacement.owner_ = new PSBRawOwner(data, size);
            replacement.owner_->AddRef();
            // All four copy-assign this temporary holder and then destroy it.
            // The AddRef/Release pair cancels;
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
        // Every reference reads entries without a null guard and returns a
        // retained raw node.
        // Only the script-visible root getter checks an empty file.
        return { *this, owner_->GetHeader()->entries };
    }
} // namespace PSB
