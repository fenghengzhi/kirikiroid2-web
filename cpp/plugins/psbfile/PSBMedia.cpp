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
        value = file->GetRoot();

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
            if(!value.ContainsDictionaryKey(key)) {
                return false;
            }
            value = value.GetDictionaryValueStrict(key);
            if(last) {
                return true;
            }
        }
    }

    bool PSBMedia::GetResourceData(const ttstr &name, const std::uint8_t *&data,
                                   std::uint32_t &size) {
        // sub_59A0B4 @ 0x59A0B4.
        PSBRawNode value;
        if(!Resolve(name, value)) {
            return false;
        }
        data = value.GetResource(size);
        return data != nullptr;
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        // sub_5998C4 @ 0x5998C4.
        const std::uint8_t *data{};
        std::uint32_t size{};
        return EnsureContainer(name) && GetResourceData(name, data, size);
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32) {
        // sub_59993C @ 0x59993C.
        if(!EnsureContainer(name)) {
            return nullptr;
        }
        const std::uint8_t *data{};
        std::uint32_t size{};
        if(!GetResourceData(name, data, size)) {
            TVPThrowExceptionMessage(TJS_W("%1: cannot open psbfile"), name);
        }
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

        const int category = value.GetTypeCategory();
        if(category == 6) {
            std::uint32_t count{};
            (void)value.GetArrayCount(count);
            for(std::uint32_t index = 0; index < count; ++index) {
                lister->Add(ttstr(static_cast<tjs_int>(index)));
            }
        } else if(category == 7) {
            std::uint32_t count{};
            (void)value.GetDictionaryCount(count);
            std::string key;
            for(std::uint32_t index = 0; index < count; ++index) {
                (void)value.GetDictionaryKey(index, key);
                lister->Add(ttstr(key));
            }
        }
    }

    void PSBMedia::GetLocallyAccessibleName(ttstr &name) {
        // sub_599DD8 @ 0x599DD8.
        name = ttstr();
    }
} // namespace PSB
