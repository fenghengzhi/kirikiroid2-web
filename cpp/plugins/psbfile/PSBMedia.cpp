#include "PSBMedia.h"

#include <cstdint>

#include "MsgIntf.h"
#include "PSBPackedInternal.h"
#include "UtilStreams.h"
#include "ncbind.hpp"

namespace PSB {
    void PSBMedia::NormalizeDomainName(ttstr &) {
        // All four vtable bodies are empty.
    }

    void PSBMedia::NormalizePathName(ttstr &) {
        // All four vtable bodies are empty.
    }

    bool PSBMedia::EnsureContainer(const ttstr &name) {
        // All four references preserve the same container-name commit and
        // adaptor-failure behavior below.
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
            // Every target leaves the native holder unclaimed when the class
            // object cannot create an adaptor; nextFile stays void. The
            // container name is nevertheless committed and the call succeeds.
            _file = nextFile;
        }
        _container = container;
        return true;
    }

    bool PSBMedia::Resolve(const ttstr &name, PSBRawNode &value) {
        // All four references preserve the same segmented traversal and defer
        // caller-output replacement until the successful tail.
        iTJSDispatch2 *dispatch = _file.AsObjectNoAddRef();
        PSBFile *file =
            ncbInstanceAdaptor<PSBFile>::GetNativeInstance(dispatch);
        // All four bodies keep the root in a retained local node. The
        // caller's output is not touched until the successful tail, so every
        // missing slash or dictionary key preserves its previous value.
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
                    // The four builds copy the substring owner into segment,
                    // then immediately release the returned temporary. Keep
                    // this scope as the corresponding source-lifetime token.
                    {
                        const ttstr segmentTemporary =
                            rest.SubString(0, slash);
                        segment = segmentTemporary;
                    }
                    rest = rest.SubString(slash + 1, -1);
                }

                // Exactly one narrow holder supplies the same buffer to the
                // contains check and the strict lookup before destruction.
                tTJSNarrowStringHolder key(segment.c_str());
                if(!current.ContainsDictionaryKey(key.Buf)) {
                    return false;
                }
                // Each build copy-assigns the strict getter's retained
                // temporary and then destroys it. Optimized forms still keep
                // the AddRef/Release pair and its incoming-zero deletion edge.
                current = current.GetDictionaryValueStrict(key.Buf);
            }
            if(last) {
                // key and segment die before the sole caller-output update:
                // Release-old -> copy -> AddRef -> write-node.
                value = current;
                return true;
            }
        }
    }

    const std::uint8_t *PSBMedia::GetResourceData(const ttstr &name,
                                                  std::uint32_t &size) {
        // All four references resolve into a retained local node before
        // borrowing its resource pointer.
        PSBRawNode value;
        if(!Resolve(name, value)) {
            return nullptr;
        }
        // Android arm64 inlines the complete raw-resource decoder here. The
        // other three builds preserve calls to their raw GetResource wrappers.
        // Keep the source-level call.
        return value.GetResource(size);
    }

    bool PSBMedia::CheckExistentStorage(const ttstr &name) {
        // All four short-circuit on a failed container check before resolving.
        std::uint32_t size;
        return EnsureContainer(name) && GetResourceData(name, size) != nullptr;
    }

    tTJSBinaryStream *PSBMedia::Open(const ttstr &name, tjs_uint32) {
        // All four return null on a failed container check, but throw after a
        // successful container check when the named resource is absent.
        if(!EnsureContainer(name)) {
            return nullptr;
        }
        std::uint32_t size;
        const std::uint8_t *data = GetResourceData(name, size);
        if(data == nullptr) {
            TVPThrowExceptionMessage(TJS_W("%1: cannot open psbfile"), name);
        }
        // The four memory-stream constructors set Reference=true for every
        // non-null block. Their destructors free only when Reference=false and
        // never retain the PSB owner, so the stream shares the container's raw
        // lifetime.
        return new tTVPMemoryStream(data, size);
    }

    void PSBMedia::GetListAt(const ttstr &name, iTVPStorageLister *lister) {
        // All four list numeric array indices and decoded dictionary names,
        // while ignoring the lister callback's return value.
        if(!EnsureContainer(name)) {
            return;
        }
        PSBRawNode value;
        if(!Resolve(name, value)) {
            return;
        }

        // Android specializes the category classifier into this body; iOS
        // retains calls to the shared classifier.
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
                // Android arm64 scalarizes the keys view and eliminates the
                // unused offsets view. The remaining builds retain both
                // constructions; offsets is deliberately dead afterwards.
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
        // All four release the old string value and leave it empty.
        name.Clear();
    }
} // namespace PSB
