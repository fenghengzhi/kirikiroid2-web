#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "psbfile/PSBDispatch.h"
#include "psbfile/PSBRawFile.h"

namespace tools::rawpsb {

    inline PSB::PSBFile::OwnerFilter decryptFilter(std::uint32_t seed) {
        // EmotePlayer_setEmotePSBDecryptSeed_callback @ 0x685D30 installs
        // sub_6863CC @ 0x6863CC. Keep tools on the same raw-owner boundary.
        return [seed](PSB::PSBRawOwner &owner) {
            auto *header = owner.GetHeader();
            auto *cursor = header->encryptData;
            const auto length = static_cast<std::int32_t>(
                header->chunkOffsets - header->encryptData);
            if(length <= 0) return;

            auto *end = cursor + length;
            std::uint32_t x = 123456789u;
            std::uint32_t y = 362436069u;
            std::uint32_t z = 521288629u;
            std::uint32_t w = seed;
            std::uint32_t bytes = 0;
            do {
                if(bytes == 0) {
                    const std::uint32_t t = x ^ (x << 11u);
                    x = y;
                    y = z;
                    z = w;
                    w = w ^ (w >> 19u) ^ t ^ (t >> 8u);
                    bytes = w;
                }
                *cursor++ ^= static_cast<std::uint8_t>(bytes);
                bytes >>= 8u;
            } while(cursor < end);
        };
    }

    class KeyCollector final : public tTJSDispatch {
    public:
        tjs_error FuncCall(tjs_uint32, const tjs_char *, tjs_uint32 *,
                           tTJSVariant *result, tjs_int numparams,
                           tTJSVariant **param, iTJSDispatch2 *) override {
            if(numparams >= 1) keys.emplace_back(ttstr(*param[0]).AsStdString());
            if(result) *result = true;
            return TJS_S_OK;
        }

        std::vector<std::string> keys;
    };

    class Value final {
    public:
        Value() = default;
        explicit Value(tTJSVariant value) : value_(std::move(value)) {}

        [[nodiscard]] bool valid() const { return value_.Type() != tvtVoid; }
        [[nodiscard]] tTJSVariantType type() const { return value_.Type(); }
        [[nodiscard]] bool isObject() const { return value_.Type() == tvtObject; }
        [[nodiscard]] bool isString() const { return value_.Type() == tvtString; }
        [[nodiscard]] bool isNumber() const {
            return value_.Type() == tvtInteger || value_.Type() == tvtReal;
        }

        [[nodiscard]] bool isInstanceOf(const tjs_char *name) const {
            if(!isObject()) return false;
            auto *dispatch = value_.AsObjectNoAddRef();
            return dispatch &&
                dispatch->IsInstanceOf(0, nullptr, nullptr, name, dispatch) ==
                    TJS_S_TRUE;
        }

        [[nodiscard]] bool isArray() const {
            return isInstanceOf(TJS_W("Array"));
        }

        [[nodiscard]] bool isDictionary() const {
            return isInstanceOf(TJS_W("Dictionary"));
        }

        [[nodiscard]] Value property(const std::string &key) const {
            if(!isObject()) return {};
            auto *dispatch = value_.AsObjectNoAddRef();
            tTJSVariant result;
            const ttstr name(key);
            if(!dispatch || dispatch->PropGet(0, name.c_str(), nullptr, &result,
                                               dispatch) != TJS_S_OK) {
                return {};
            }
            return Value(std::move(result));
        }

        [[nodiscard]] Value at(tjs_int index) const {
            if(!isObject()) return {};
            auto *dispatch = value_.AsObjectNoAddRef();
            tTJSVariant result;
            if(!dispatch || dispatch->PropGetByNum(0, index, &result,
                                                    dispatch) != TJS_S_OK) {
                return {};
            }
            return Value(std::move(result));
        }

        [[nodiscard]] tjs_int count() const {
            if(!isObject()) return 0;
            auto *dispatch = value_.AsObjectNoAddRef();
            tjs_int result = 0;
            if(!dispatch || dispatch->GetCount(&result, nullptr, nullptr,
                                                dispatch) != TJS_S_OK) {
                return 0;
            }
            return result;
        }

        [[nodiscard]] std::vector<std::string> keys() const {
            if(!isObject()) return {};
            auto *dispatch = value_.AsObjectNoAddRef();
            auto *collector = new KeyCollector();
            tTJSVariantClosure closure(collector);
            const auto status = dispatch->EnumMembers(
                TJS_IGNOREPROP | TJS_ENUM_NO_VALUE, &closure, dispatch);
            auto keys = status == TJS_S_OK
                ? std::move(collector->keys) : std::vector<std::string>{};
            collector->Release();
            return keys;
        }

        [[nodiscard]] std::optional<double> number() const {
            if(!isNumber()) return std::nullopt;
            return value_.AsReal();
        }

        [[nodiscard]] double numberOr(double fallback) const {
            return number().value_or(fallback);
        }

        [[nodiscard]] std::optional<std::string> string() const {
            if(!isString()) return std::nullopt;
            return ttstr(value_).AsStdString();
        }

        [[nodiscard]] std::string stringOr(std::string fallback = {}) const {
            if(const auto result = string()) return *result;
            return fallback;
        }

        [[nodiscard]] const tTJSVariant &variant() const { return value_; }

    private:
        tTJSVariant value_;
    };

    struct Document final {
        PSB::PSBFile file;
        Value root;

        [[nodiscard]] bool load(const ttstr &path, std::uint32_t seed = 0) {
            const bool loaded = seed == 0
                ? file.LoadStorage(path)
                : file.LoadStorage(path, decryptFilter(seed));
            if(!loaded) return false;
            iTJSDispatch2 *dispatch = file.GetRootDispatch();
            if(dispatch == nullptr) return false;
            root = Value(tTJSVariant(dispatch, dispatch));
            dispatch->Release();
            return root.valid();
        }
    };

    inline Value navigate(Value value, const std::vector<std::string> &path) {
        for(const auto &segment : path) {
            value = value.property(segment);
            if(!value.valid()) break;
        }
        return value;
    }

} // namespace tools::rawpsb
