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
        PSBRawNode current(*file, owner->GetHeader()->entries);

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
                // sub_59A4B0 @ 0x59A694..0x59A704 copy-assigns the strict
                // getter's retained temporary and then destroys that
                // temporary.  0x59A6D0/0x59A6D8 reads and writes back the same
                // refcount: the optimizer's remnant of AddRef followed by
                // Release, including its incoming-zero deletion boundary.
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
        // sub_59A0B4 @ 0x59A0EC..0x59A214 is the complete inlined body of
        // PSBRawNode::GetResource @ 0x5996E8..0x5997E8 after binding `this`
        // to this local node.  Preserve that source-level member call; the
        // Android arm64 -O3 build removes the BL but keeps every callee branch;
        // iOS arm64 independently preserves the call after Resolve.
        return value.GetResource(size);
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

        // sub_5999F4 @ 0x599A4C contains the complete category-specialized
        // classifier residual. Same-lineage iOS arm64 @0x1000EE50C retains
        // the shared classifier call.
        switch(detail::GetTypeCategory_guess(value.GetNode()[0])) {
            case 6: {
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
            case 7: {
                std::string key;
                const std::uint8_t *packed = value.GetNode() + 1;
                // Android arm64 @0x599B00..0x599C6C scalarizes the first view
                // and eliminates the unused second one. iOS arm64 retains
                // both constructor calls; the second record is deliberately
                // dead after construction.
                const detail::PsbArray_guess keys(packed);
                const detail::PsbArray_guess offsets(packed + keys.nBytes);
                (void)offsets;
                for(std::uint32_t index = 0; index < keys.nElementCount;
                    ++index) {
                    detail::DecodeName_guess(key, value.GetOwner(), keys[index]);
                    lister->Add(ttstr(key));
                }
                break;
            }
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            default:
                break;
        }
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        // sub_599DD8 @ 0x599DD8.
        name.Clear();
    }
} // namespace PSB
