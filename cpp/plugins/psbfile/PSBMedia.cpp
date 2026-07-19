#include "PSBMedia.h"

#include <cstdint>

#include "MsgIntf.h"
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
        if(object != nullptr) {
            _file = tTJSVariant(object, object);
            object->Release();
        } else {
            // sub_59A330 @ 0x59A330 leaves the native holder unclaimed when
            // the class object cannot create an adaptor.
            _file.Clear();
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
        PSBRawNode current = file->GetRoot();

        const tjs_int firstSlash = name.IndexOf(TJS_W('/'));
        if(firstSlash < 0) {
            return false;
        }
        ttstr rest = name.SubString(firstSlash + 1, -1);
        for(;;) {
            const tjs_int slash = rest.IndexOf(TJS_W('/'));
            const bool last = slash < 0;
            const ttstr segment = last ? rest : rest.SubString(0, slash);
            if(!last) {
                rest = rest.SubString(slash + 1, -1);
            }

            const std::string key = segment.AsStdString();
            if(!current.ContainsDictionaryKey(key)) {
                return false;
            }
            // sub_59A4B0 @ 0x59A694..0x59A704 moves the strict getter's
            // returned owner/node directly into the current value: release
            // old, install both fields, preserve the zero-ref deletion branch.
            // There is no intermediate AddRef/Release copy no-op here.
            current = current.GetDictionaryValueStrict(key);
            if(last) {
                // 0x59A730..0x59A774 alone performs Release-old -> copy ->
                // AddRef -> write-node on the caller-provided output.
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

        // sub_5999F4 @ 0x599A4C reads the raw tag here and owns this switch;
        // it does not route through the separate category helper at 0x599554.
        switch(value.GetNode()[0]) {
            case 0x20: {
                std::uint32_t rawCount{};
                (void)value.GetArrayCount(rawCount);
                const tjs_int count = static_cast<tjs_int>(rawCount);
                for(tjs_int index = 0; index < count; ++index) {
                    lister->Add(ttstr(index));
                }
                break;
            }
            case 0x21: {
                std::uint32_t count{};
                (void)value.GetDictionaryCount(count);
                std::string key;
                for(std::uint32_t index = 0; index < count; ++index) {
                    (void)value.GetDictionaryKey(index, key);
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
        name = ttstr();
    }
} // namespace PSB
