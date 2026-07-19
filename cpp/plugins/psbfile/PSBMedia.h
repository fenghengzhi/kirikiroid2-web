//
// Created by LiDon on 2025/9/11.
//
#pragma once

#include "PSBRawFile.h"
#include "StorageIntf.h"

namespace PSB {
    class PSBMedia : public iTVPStorageMedia {
    public:
        PSBMedia() { _ref = 1; }

        ~PSBMedia() override = default;

        void AddRef() override { _ref++; }

        void Release() override {
            if(_ref == 1)
                delete this;
            else
                _ref--;
        }

        void GetName(ttstr &name) override { name = TJS_W("psb"); }

        void NormalizeDomainName(ttstr &name) override;

        void NormalizePathName(ttstr &name) override;

        bool CheckExistentStorage(const ttstr &name) override;

        tTJSBinaryStream *Open(const ttstr &name, tjs_uint32 flags) override;

        void GetListAt(const ttstr &name, iTVPStorageLister *lister) override;

        void GetLocallyAccessibleName(ttstr &name) override;

    private:
        [[nodiscard]] bool EnsureContainer(const ttstr &name);
        [[nodiscard]] bool Resolve(const ttstr &name, PSBRawNode &value);
        [[nodiscard]] const std::uint8_t *
        GetResourceData(const ttstr &name, std::uint32_t &size);

        int _ref = 0;
        tTJSVariant _file;
        ttstr _container;
    };
} // namespace PSB
