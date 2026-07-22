#include "PSBMedia.h"

#include <cstdint>

#include "MsgIntf.h"
#include "PSBPackedInternal.h"
#include "UtilStreams.h"
#include "ncbind.hpp"

namespace PSB {
    void PSBMedia::NormalizeDomainName(ttstr &) {
        // nullsub_262 @ 0x5998BC.
    }

    void PSBMedia::NormalizePathName(ttstr &) {
        // nullsub_263 @ 0x5998C0.
    }

    bool PSBMedia::EnsureContainer(const ttstr &name) {
        // sub_599E04 @ 0x599E04.
        const tjs_int slash = name.IndexOf(TJS_W('/'));
        if(slash < 0) {
            return false;
        }
        const ttstr container = name.SubString(0, slash);
        if(_file.Type() == tvtObject && _container == container) {
            return true;
        }

        auto *file = new PSBFile();
        if(!file->LoadStorage(container)) {
            delete file;
            return false;
        }

        iTJSDispatch2 *object =
            ncbInstanceAdaptor<PSBFile>::CreateAdaptor(file);
        {
            tTJSVariant nextFile;
            if(object != nullptr) {
                nextFile.SetObject(object, object);
                object->Release();
            }
            // sub_59A330 @ 0x59A330 leaves the native holder unclaimed when
            // the class object cannot create an adaptor; nextFile stays void.
            _file = nextFile;
        }
        _container = container;
        return true;
    }

    bool PSBMedia::Resolve(const ttstr &name, PSBRawNode &value) {
        // sub_59A4B0 @ 0x59A4B0.
        iTJSDispatch2 *dispatch = _file.AsObjectNoAddRef();
        PSBFile *file =
            ncbInstanceAdaptor<PSBFile>::GetNativeInstance(dispatch);
        // sub_59A4B0 @ 0x59A548..0x59A55C keeps the root in a local node.
        // The caller's output is not touched until the successful tail at
        // 0x59A730..0x59A774, so every miss preserves its previous value.
        PSBRawOwner *owner = file->GetOwner();
        PSBRawNode current(owner, owner->GetHeader()->entries);

        const tjs_int firstSlash = name.IndexOf(TJS_W('/'));
        if(firstSlash == -1) {
            return false;
        }
        ttstr rest = name.SubString(firstSlash + 1, -1);
        for(;;) {
            const tjs_int slash = rest.IndexOf(TJS_W('/'));
            const bool last = slash == -1;
            {
                ttstr segment;
                if(last) {
                    segment = rest;
                } else {
                    // 0x59A5C0..0x59A5E8 copies the substring owner into the
                    // segment, then immediately releases the returned
                    // temporary. Keep this AddRef/Release no-op as a
                    // source-lifetime token.
                    {
                        const ttstr segmentTemporary =
                            rest.SubString(0, slash);
                        segment = segmentTemporary;
                    }
                    rest = rest.SubString(slash + 1, -1);
                }

                // sub_59A4B0 @ 0x59A654..0x59A710 constructs exactly one
                // narrow holder and passes the same buffer through contains
                // and strict lookup before destroying it.
                tTJSNarrowStringHolder key(segment.c_str());
                if(!current.ContainsDictionaryKey(key.Buf)) {
                    return false;
                }
                // sub_59A4B0 @ 0x59A694..0x59A704 has the net sequence release
                // old -> install the strict getter's returned owner/node ->
                // keep the incoming zero-ref deletion boundary. Optimized
                // code cannot distinguish a source move from copy plus
                // temporary destruction, so this assignment spelling is not
                // claimed as uniquely proven.
                current = current.GetDictionaryValueStrict(key.Buf);
            }
            if(last) {
                // 0x59A710 destroys key and 0x59A714..0x59A71C releases
                // segment before 0x59A730..0x59A774 alone performs
                // Release-old -> copy -> AddRef -> write-node on caller out.
                value = current;
                return true;
            }
        }
    }

    const std::uint8_t *PSBMedia::GetResourceData(const ttstr &name,
                                                  std::uint32_t &size) {
        // sub_59A0B4 @ 0x59A0B4.
        PSBRawNode value;
        if(!Resolve(name, value)) {
            return nullptr;
        }
        // The optimized Android body @0x59A0F4..0x59A204 expands the
        // resource-index and chunk-table decoders here;
        // PSBRawNode::GetResource @ 0x5996E4 is not part of its call chain.
        const PSBRawHeader *header = value.GetOwner()->GetHeader();
        if(header->chunkData == nullptr) {
            return nullptr;
        }
        const detail::PackedArrayView_guess offsets(header->chunkOffsets);
        const detail::PackedArrayView_guess lengths(header->chunkLengths);
        const std::uint8_t *node = value.GetNode();
        std::uint32_t index;
        switch(node[0]) {
            case 0x19:
                index = node[1];
                break;
            case 0x1a:
                index = detail::ReadUnaligned_guess<std::uint16_t>(node + 1);
                break;
            case 0x1b:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node + 1) &
                    0xffffffu;
                break;
            case 0x1c:
                index = detail::ReadUnaligned_guess<std::uint32_t>(node + 1);
                break;
            default:
                index = 0;
                break;
        }
        size = lengths[index];
        return header->chunkData + offsets[index];
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        // sub_5998C4 @ 0x5998C4.
        std::uint32_t size;
        return EnsureContainer(name) && GetResourceData(name, size) != nullptr;
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32) {
        // sub_59993C @ 0x59993C.
        if(!EnsureContainer(name)) {
            return nullptr;
        }
        std::uint32_t size;
        const std::uint8_t *data = GetResourceData(name, size);
        if(data == nullptr) {
            TVPThrowExceptionMessage(TJS_W("%1: cannot open psbfile"), name);
        }
        // tTVPMemoryStream ctor @0x8F7C74 borrows every non-null block
        // (Reference=true); dtor @0x8F7D04 does not free it or retain the PSB
        // owner. A live stream therefore shares the container's raw lifetime.
        return new tTVPMemoryStream(data, size);
    }

    void PSBMedia::GetListAt(const ttstr &name, iTVPStorageLister *lister) {
        // sub_5999F4 @ 0x5999F4.
        if(!EnsureContainer(name)) {
            return;
        }
        PSBRawNode value;
        if(!Resolve(name, value)) {
            return;
        }

        // sub_5999F4 @ 0x599A4C reads the raw tag here and owns this switch;
        // it does not route through the separate category helper at 0x599554.
        switch(value.GetNode()[0]) {
            case 0x20: {
                const std::uint8_t *packed = value.GetNode() + 1;
                tjs_int count;
                switch(packed[0]) {
                    case 0x0d:
                        count = packed[1];
                        break;
                    case 0x0e:
                        count = detail::ReadUnaligned_guess<std::uint16_t>(
                            packed + 1);
                        break;
                    case 0x0f:
                        count = static_cast<tjs_int>(
                            detail::ReadUnaligned_guess<std::uint32_t>(
                                packed + 1) &
                            0xffffffu);
                        break;
                    case 0x10:
                        count = static_cast<tjs_int>(
                            detail::ReadUnaligned_guess<std::uint32_t>(
                                packed + 1));
                        break;
                    default:
                        return;
                }
                for(tjs_int index = 0; index < count; ++index) {
                    lister->Add(ttstr(index));
                }
                break;
            }
            case 0x21: {
                std::string key;
                const std::uint8_t *packed = value.GetNode() + 1;
                std::uint32_t count;
                switch(packed[0]) {
                    case 0x0d:
                        count = packed[1];
                        break;
                    case 0x0e:
                        count = detail::ReadUnaligned_guess<std::uint16_t>(
                            packed + 1);
                        break;
                    case 0x0f:
                        count = detail::ReadUnaligned_guess<std::uint32_t>(
                                    packed + 1) &
                            0xffffffu;
                        break;
                    case 0x10:
                        count = detail::ReadUnaligned_guess<std::uint32_t>(
                            packed + 1);
                        break;
                    default:
                        return;
                }
                const std::uint8_t valueTag =
                    packed[static_cast<std::ptrdiff_t>(packed[0]) - 0x0b];
                const int width = static_cast<int>(valueTag) - 0x0c;
                const std::uint8_t *values =
                    packed + static_cast<std::ptrdiff_t>(packed[0]) - 0x0a;
                for(std::uint32_t index = 0; index < count; ++index) {
                    const std::uint32_t nameIndex =
                        detail::ReadPackedValue_guess(values + index * width,
                                                      valueTag);
                    detail::DecodeName_guess(key, value.GetOwner(), nameIndex);
                    lister->Add(ttstr(key));
                }
                break;
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
                break;
            default:
                TVPThrowExceptionMessage(TJS_W(
                    "psb: internal error: unknown internal type detected.\n"));
                break;
        }
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        // sub_599DD8 @ 0x599DD8.
        name.Clear();
    }
} // namespace PSB
