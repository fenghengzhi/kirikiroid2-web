#include <cstdint>
#include <zlib.h>
#include <optional>

#include "TextStream.h"

#ifndef KRKR2_NO_OPENCV
#include <opencv2/core/hal/interface.h>
#endif
#include "MsgIntf.h"
#include "UtilStreams.h"
#include "tjsError.h"
#include "CharacterSet.h"
#include "BinaryStream.h"

extern "C" int gbk_mbtowc(unsigned short *wc, const unsigned char *s);
extern "C" int sjis_mbtowc(unsigned short *wc, const unsigned char *s);

using tTVPMbToWc = int (*)(unsigned short *, const unsigned char *);

static ttstr G_DefaultReadEncoding(TJS_W("UTF-8"));
static tTVPMbToWc G_DefaultReadConverter = nullptr;

// libkrkr2.so sub_8F516C @ 0x8F516C.
static int TVPUtf8MbToWc(unsigned short *wc, const unsigned char *s) {
    const unsigned int first = s[0];
    if(first < 0x80) {
        *wc = static_cast<unsigned short>(first);
        return 1;
    }
    if(first < 0xC2)
        return -1;
    if(first <= 0xDF) {
        const unsigned int second = s[1] ^ 0x80;
        if(second > 0x3F)
            return -1;
        *wc = static_cast<unsigned short>(((first & 0x1F) << 6) | second);
        return 2;
    }
    if(first > 0xEF || static_cast<signed char>(s[1]) > -65 ||
       static_cast<signed char>(s[2]) > -65)
        return -1;

    const unsigned int second = s[1];
    if(first == 0xE0 && second < 0xA0)
        return -1;

    *wc = static_cast<unsigned short>(((second ^ 0x80) << 6) |
                                     (first << 12) | (s[2] ^ 0x80));
    return 3;
}

static bool TVPDecodeNarrowText(const std::vector<std::uint8_t> &raw,
                                tTVPMbToWc converter,
                                std::u16string &decoded) {
    std::vector<std::uint8_t> terminated(raw);
    terminated.push_back(0);

    decoded.clear();
    decoded.reserve(raw.size());
    const unsigned char *current = terminated.data();
    const unsigned char *end = current + raw.size();
    while(current < end && *current) {
        unsigned short wc;
        const int consumed = converter(&wc, current);
        if(consumed < 1 || current + consumed > end) {
            decoded.clear();
            return false;
        }
        decoded.push_back(static_cast<char16_t>(wc));
        current += consumed;
    }
    return true;
}

// libkrkr2.so sub_8F5244 @ 0x8F5244. An explicitly selected converter is
// authoritative. Without one, the original fallback order is SJIS, UTF-8, GBK.
static void TVPDecodeNarrowText(const std::vector<std::uint8_t> &raw,
                                std::u16string &decoded) {
    if(G_DefaultReadConverter) {
        if(TVPDecodeNarrowText(raw, G_DefaultReadConverter, decoded))
            return;
        TVPThrowExceptionMessage(TJSNarrowToWideConversionError);
    }

    if(TVPDecodeNarrowText(raw, sjis_mbtowc, decoded))
        return;
    if(TVPDecodeNarrowText(raw, TVPUtf8MbToWc, decoded)) {
        G_DefaultReadConverter = TVPUtf8MbToWc;
        return;
    }
    if(TVPDecodeNarrowText(raw, gbk_mbtowc, decoded)) {
        G_DefaultReadConverter = gbk_mbtowc;
        return;
    }
    TVPThrowExceptionMessage(TJSNarrowToWideConversionError);
}

std::string checkTextEncoding(const void *buf, size_t size,
                              std::uint8_t &bomSize) {
    auto raw = static_cast<const unsigned char *>(buf);
    std::string encoding;
    // --- 检查 BOM ---
    if(size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        // UTF-16LE BOM
        bomSize = 2;
        encoding = "UTF-16LE";
    } else if(size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        // UTF-16BE BOM
        bomSize = 2;
        encoding = "UTF-16BE";
    } else if(size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        // UTF-8 BOM
        bomSize = 3;
        encoding = "UTF-8";
    } else if(size >= 4 && raw[0] == 0xFF && raw[1] == 0xFE && raw[2] == 0x00 &&
              raw[3] == 0x00) {
        // UTF-32LE BOM
        bomSize = 4;
        encoding = "UTF-32LE";
    } else if(size >= 4 && raw[0] == 0x00 && raw[1] == 0x00 && raw[2] == 0xFE &&
              raw[3] == 0xFF) {
        // UTF-32BE BOM
        bomSize = 4;
        encoding = "UTF-32BE";
    }

    return encoding;
}

/*
 *  note: encryption of mode 0 or 1 ( simple crypt ) does never
 *  intend data pretection security.
 */
class tTVPTextReadStream : public iTJSTextReadStream {
    std::unique_ptr<tTJSBinaryStream> _stream{};
    std::u16string _buffer; // 全部文本，UTF-16
    size_t _pos = 0; // 当前读取位置

public:
    tTVPTextReadStream(const ttstr &name, const ttstr &mode) {
        _stream.reset(TVPCreateStream(name, TJS_BS_READ));
        size_t ofs = parseModeNumber(mode.c_str(), TJS_W('o'), 255, 0).value();
        _stream->SetPosition(ofs);

        auto totalSize = _stream->GetSize();
        auto size = static_cast<size_t>(totalSize - ofs);
        std::vector<std::uint8_t> raw(size);
        _stream->ReadBuffer(raw.data(), size);

        // ---------- 检查是否加密/压缩 ----------
        if(size >= 3 && raw[0] == 0xFE && raw[1] == 0xFE) {
            std::uint8_t m = raw[2];
            if(m == 0 || m == 1) {
                // 解密 UTF-16 数据
                // Header: 3 bytes crypt sig [0xFE,0xFE,mode] + 2 bytes BOM [0xFF,0xFE]
                const auto *src =
                    reinterpret_cast<const char16_t *>(raw.data() + 5);
                size_t len = (size - 5) / 2;
                _buffer.resize(len);
                for(size_t i = 0; i < len; i++) {
                    char16_t ch = src[i];
                    if(m == 0) {
                        if(ch >= 0x20)
                            ch ^= (((ch & 0xfe) << 8) ^ 1);
                    } else if(m == 1) {
                        ch =
                            ((ch & 0xaaaaaaaa) >> 1) | ((ch & 0x55555555) << 1);
                    }
                    _buffer[i] = ch;
                }
                return;
            }
            if(m == 2) {
                // 压缩流
                if(size < 3 + 2 + 16)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                // 读压缩大小和解压大小
                std::uint8_t *ptr = raw.data() + 5;
                std::uint64_t compressed =
                    *reinterpret_cast<std::uint64_t *>(ptr);
                ptr += 8;
                std::uint64_t uncompressed =
                    *reinterpret_cast<std::uint64_t *>(ptr);
                ptr += 8;

                std::vector<std::uint8_t> compBuf(compressed);
                memcpy(compBuf.data(), ptr, compressed);

                std::vector<std::uint8_t> uncompBuf(uncompressed);
                auto destLen = static_cast<unsigned long>(uncompressed);
                int ret = uncompress(uncompBuf.data(), &destLen, compBuf.data(),
                                     static_cast<unsigned long>(compressed));
                if(ret != Z_OK || destLen != uncompressed)
                    TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);

                // 解压得到 UTF-16 数据
                _buffer.assign(reinterpret_cast<char16_t *>(uncompBuf.data()),
                               reinterpret_cast<char16_t *>(uncompBuf.data() +
                                                            uncompressed));
                return;
            }
            TVPThrowExceptionMessage(TVPUnsupportedCipherMode, name);
        }
        std::uint8_t bomSize = 0;
        std::string encoding = checkTextEncoding(raw.data(), size, bomSize);
        raw.erase(raw.begin(), raw.begin() + bomSize);

        // libkrkr2.so sub_8F60B0 @ 0x8F60B0: after encrypted/BOM formats,
        // ordinary text is decoded only through sub_8F5244's converter chain.
        if(encoding.empty()) {
            TVPDecodeNarrowText(raw, _buffer);
            return;
        }

        if(encoding == "ASCII") {
            _buffer.assign(raw.data(), raw.data() + raw.size());
            return;
        }

        if(encoding == "UTF-8") {
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(
                reinterpret_cast<const char *>(raw.data()),
                reinterpret_cast<const char *>(raw.data() + raw.size()));
            return;
        }

        if(encoding == "UTF-16" || encoding == "UTF-16LE" ||
           encoding == "UTF-16BE") {
            _buffer.assign(
                reinterpret_cast<const char16_t *>(raw.data()),
                reinterpret_cast<const char16_t *>(raw.data() + raw.size()));

            if(encoding == "UTF-16BE") {
                size_t len = raw.size() / 2;
                _buffer.resize(len);
                auto src = reinterpret_cast<const char16_t *>(raw.data());
                for(size_t i = 0; i < len; i++) {
                    char16_t ch = src[i];
                    _buffer[i] = (ch >> 8) | (ch << 8);
                }
            }

            return;
        }

        if(encoding == "UTF-32" || encoding == "UTF-32LE" ||
           encoding == "UTF-32BE") {
            _buffer = boost::locale::conv::utf_to_utf<char16_t>(
                reinterpret_cast<const char32_t *>(raw.data()),
                reinterpret_cast<const char32_t *>(raw.data() + raw.size()));
            return;
        }
    }

    ~tTVPTextReadStream() override = default;

    tjs_uint Read(tTJSString &targ, tjs_uint size) override {
        static_assert(sizeof(tjs_char) == sizeof(char16_t),
                      "Char size mismatch");
        if(_pos >= _buffer.size()) {
            targ.Clear();
            return 0;
        }
        size_t remain = _buffer.size() - _pos;
        size_t n = size ? size : remain;
        tjs_char *buf = targ.AllocBuffer(n);
        std::copy_n(_buffer.data() + _pos, n, buf);
        buf[n] = 0;
        _pos += n;

        for(size_t j = 0; j < n; j++) {
            if(buf[j] == 0) {
                buf[j] = 0x0020;
            }
        }
        buf[n] = 0;

        targ.FixLen();
        return n;
    }

    void Destruct() override { delete this; }
};


class tTVPTextWriteStream : public iTJSTextWriteStream {
    // TODO: 32bit wchar_t support

    static constexpr size_t COMPRESSION_BUFFER_SIZE = 1024 * 1024;

    std::unique_ptr<tTJSBinaryStream> _stream{};
    tjs_int _cryptMode{};
    // -1 for no-crypt
    // 0: (unused)	(old buggy crypt mode)
    // 1: simple crypt
    // 2: complessed
    int _compressionLevel{}; // compression level of zlib

    std::unique_ptr<z_stream_s> _zStream{};
    tjs_uint _compressionSizePosition{ 0 };
    std::vector<Bytef> _compressionBuffer =
        std::vector<Bytef>(COMPRESSION_BUFFER_SIZE);
    bool _compressionFailed{ false };

public:
    tTVPTextWriteStream(const ttstr &name, const ttstr &mode) {
        // mode supports following modes:
        // dN: deflate(compress) at mode N ( currently not implemented
        // ) cN: write in cipher at mode N ( currently n is ignored )
        // zN: write with compress at mode N ( N is compression level
        // ) oN: write from binary offset N (in bytes)

        // check c/z mode
        _cryptMode =
            parseModeNumber(mode.c_str(), TJS_W('c'), 1, -1).value_or(1);

        if(auto z = parseModeNumber(mode.c_str(), TJS_W('z'), 1,
                                    Z_DEFAULT_COMPRESSION)) {
            _compressionLevel = z.value();
        } else {
            _cryptMode = 2;
        }

        if(_cryptMode != -1 && _cryptMode != 1 && _cryptMode != 2)
            TVPThrowExceptionMessage(TVPUnsupportedModeString,
                                     TJS_W("unsupported cipher mode"));

        // check o mode
        int ofs = parseModeNumber(mode.c_str(), TJS_W('o'), 255, 0).value();
        if(ofs != 0) {
            _stream.reset(TVPCreateStream(name, TJS_BS_UPDATE));
            _stream->SetPosition(ofs);
        } else {
            _stream.reset(TVPCreateStream(name, TJS_BS_WRITE));
        }

        if(_cryptMode == 1 || _cryptMode == 2) {
            // simple crypt or compressed
            tjs_uint8 crypt_mode_sig[4];
            crypt_mode_sig[0] = crypt_mode_sig[1] = 0xfe;
            crypt_mode_sig[2] = static_cast<tjs_uint8>(_cryptMode);
            crypt_mode_sig[3] = 0;
            _stream->WriteBuffer(crypt_mode_sig, 3);
        }

        // now output text stream will write unicode texts
        static tjs_uint8 bommark[2] = { 0xff, 0xfe };
        _stream->WriteBuffer(bommark, 2);

        if(_cryptMode == 2) {
            // allocate and initialize zlib straem
            _zStream.reset(new z_stream_s());
            _zStream->zalloc = Z_NULL;
            _zStream->zfree = Z_NULL;
            _zStream->opaque = Z_NULL;
            if(deflateInit(_zStream.get(), _compressionLevel) != Z_OK) {
                _compressionFailed = true;
                TVPThrowExceptionMessage(TVPCompressionFailed);
            }

            _zStream->next_in = nullptr;
            _zStream->avail_in = 0;
            _zStream->next_out = _compressionBuffer.data();
            _zStream->avail_out = COMPRESSION_BUFFER_SIZE;

            // Compression Size (write dummy)
            _compressionSizePosition =
                static_cast<tjs_uint>(_stream->GetPosition());
            WriteI64LE(0);
            WriteI64LE(0);
        }
    }

    ~tTVPTextWriteStream() override {
        if(_cryptMode == 2) {

            if(!_compressionFailed) {
                try {
                    // close zlib stream
                    int result = 0;
                    do {
                        result = deflate(_zStream.get(), Z_FINISH);
                        if(result != Z_OK && result != Z_STREAM_END) {
                            TVPThrowExceptionMessage(TVPCompressionFailed);
                        }
                        _stream->WriteBuffer(_compressionBuffer.data(),
                                             COMPRESSION_BUFFER_SIZE -
                                                 _zStream->avail_out);
                        _zStream->next_out = _compressionBuffer.data();
                        _zStream->avail_out = COMPRESSION_BUFFER_SIZE;
                    } while(result != Z_STREAM_END);

                    // rollback and write compression size.
                    _stream->SetPosition(_compressionSizePosition);
                    WriteI64LE(_zStream->total_out);
                    WriteI64LE(_zStream->total_in);
                } catch(...) {
                    // delete zlib compress stream
                    if(_zStream) {
                        deflateEnd(_zStream.get());
                    }
                    throw;
                }
            }
            // delete zlib compress stream
            if(_zStream) {
                deflateEnd(_zStream.get());
            }
        }
    }

    void WriteI64LE(tjs_uint64 v) {
        // write 64bit little endian value to the file.
        tjs_uint8 buf[8];
        for(int i = 0; i < 8; i++) {
            buf[i] = static_cast<tjs_uint8>(v >> (i * 8));
        }
        _stream->WriteBuffer(buf, 8);
    }

    void Write(const ttstr &targ) override {
        tjs_int len = targ.GetLen();
        if(len <= 0) return;
        const tjs_char *src = targ.c_str();

        if(src[0] == 0) {
            return;
        }

        auto buf = std::make_unique<tjs_uint16[]>(len + 1);
        tjs_int i;
        for(i = 0; i < len; i++) {
            buf[i] = src[i];
            if(buf[i] == 0) {
                buf[i] = 0x0020;
            }
        }
        buf[i] = 0;

        if(_cryptMode == 1) {
            // simple crypt
            if(tjs_uint16 *p = buf.get()) {
                while(*p) {
                    tjs_char ch = *p;
                    ch = (ch & 0xaaaaaaaa) >> 1 | (ch & 0x55555555) << 1;
                    *p = ch;
                    p++;
                }
            }

            WriteRawData(buf.get(), len * sizeof(tjs_uint16));
        } else {
            WriteRawData(buf.get(), len * sizeof(tjs_uint16));
        }
    }

    void WriteRawData(void *ptr, size_t size) {
        if(_cryptMode == 2) {
            // compressed with zlib stream.
            _zStream->next_in = static_cast<Bytef *>(ptr);
            _zStream->avail_in = size;

            while(_zStream->avail_in > 0) {
                int result = deflate(_zStream.get(), Z_NO_FLUSH);
                if(result != Z_OK) {
                    _compressionFailed = true;
                    TVPThrowExceptionMessage(TVPCompressionFailed);
                }
                if(_zStream->avail_out == 0) {
                    _stream->WriteBuffer(_compressionBuffer.data(),
                                         COMPRESSION_BUFFER_SIZE);
                    _zStream->next_out = _compressionBuffer.data();
                    _zStream->avail_out = COMPRESSION_BUFFER_SIZE;
                }
            }
        } else {
            _stream->WriteBuffer(ptr, size); // write directly
        }
    }

    void Destruct() override { delete this; }
};

iTJSTextReadStream *TVPCreateTextStreamForRead(const ttstr &name,
                                               const ttstr &mode) {
    return new tTVPTextReadStream(name, mode);
}

iTJSTextWriteStream *TVPCreateTextStreamForWrite(const ttstr &name,
                                                 const ttstr &mode) {
    return new tTVPTextWriteStream(name, mode);
}

//---------------------------------------------------------------------------
void TVPSetDefaultReadEncoding(const ttstr &encoding) {
    // libkrkr2.so sub_8F6AFC @ 0x8F6AFC.
    G_DefaultReadEncoding = encoding;
    ttstr codestr = encoding;
    codestr.ToLowerCase();
    if(codestr == TJS_W("gbk")) {
        G_DefaultReadConverter = gbk_mbtowc;
    } else if(codestr == TJS_W("utf8") || codestr == TJS_W("utf-8")) {
        G_DefaultReadConverter = TVPUtf8MbToWc;
    } else if(codestr == TJS_W("sjis") || codestr == TJS_W("shiftjis") ||
       codestr == TJS_W("shift_jis") || codestr == TJS_W("shift-jis")) {
        G_DefaultReadConverter = sjis_mbtowc;
    } else {
        TVPThrowExceptionMessage(TVPUnsupportedEncoding, encoding);
    }
}

//---------------------------------------------------------------------------
const tjs_char *TVPGetDefaultReadEncoding() {
    return G_DefaultReadEncoding.c_str();
}
