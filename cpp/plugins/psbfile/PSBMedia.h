#pragma once

#include "PSBRawFile.h"
#include "StorageIntf.h"

namespace PSB {
    class PSBMedia : public iTVPStorageMedia {
    public:
        // The constructor is inlined into all four pre-register callbacks.
        // They establish the same state: ref=1, void _file, empty _container.
        PSBMedia() { _ref = 1; }

        // All four complete destructors destroy _container before _file.
        ~PSBMedia() override = default;

        // All four vtables use a plain non-atomic post-increment.
        void AddRef() override { _ref++; }

        // All four vtables delete at one; otherwise they post-decrement.
        void Release() override {
            if(_ref == 1)
                delete this;
            else
                _ref--;
        }

        // Raw bytes in all four IDBs confirm UTF-16LE "psb" despite IDA's
        // truncated one-character rendering in the iOS databases.
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
