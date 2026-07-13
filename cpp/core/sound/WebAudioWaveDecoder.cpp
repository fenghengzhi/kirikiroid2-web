#include "WebAudioWaveDecoder.h"

#ifdef __EMSCRIPTEN__

#include "WaveIntf.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <spdlog/spdlog.h>

namespace {

class tTVPWebAudioWaveDecoder final : public tTVPWaveDecoder {
    float *PlanarPCM;
    tTVPWaveFormat Format;
    tjs_uint64 CurrentPos;

public:
    tTVPWebAudioWaveDecoder(float *pcm, tjs_uint channels,
                            tjs_uint sampleRate, tjs_uint64 totalSamples) :
        PlanarPCM(pcm), CurrentPos(0) {
        Format.SamplesPerSec = sampleRate;
        Format.Channels = channels;
        Format.BitsPerSample = 32;
        Format.BytesPerSample = sizeof(float);
        Format.TotalSamples = totalSamples;
        Format.TotalTime = totalSamples * 1000 / sampleRate;
        Format.SpeakerConfig = 0;
        Format.IsFloat = true;
        Format.Seekable = true;
    }

    ~tTVPWebAudioWaveDecoder() override { free(PlanarPCM); }

    void GetFormat(tTVPWaveFormat &format) override { format = Format; }

    bool Render(void *buf, tjs_uint bufsamplelen,
                tjs_uint &rendered) override {
        const tjs_uint64 remain = Format.TotalSamples - CurrentPos;
        const tjs_uint writesamples = static_cast<tjs_uint>(
            std::min<tjs_uint64>(bufsamplelen, remain));
        if(writesamples == 0) {
            rendered = 0;
            return false;
        }

        float *dest = static_cast<float *>(buf);
        for(tjs_uint sample = 0; sample < writesamples; ++sample) {
            for(tjs_uint channel = 0; channel < Format.Channels; ++channel) {
                *dest++ = PlanarPCM[channel * Format.TotalSamples +
                                    CurrentPos + sample];
            }
        }

        rendered = writesamples;
        CurrentPos += writesamples;
        return writesamples == bufsamplelen;
    }

    bool SetPosition(tjs_uint64 samplepos) override {
        if(Format.TotalSamples <= samplepos)
            return false;
        CurrentPos = samplepos;
        return true;
    }
};

struct tTVPWebAudioDecodeRequest {
    ttstr StorageName;
    std::shared_ptr<std::vector<tjs_uint8>> Encoded;
    tTVPWebAudioWaveDecoderCallback Completion;
};

using tTVPWebAudioDecodeJSCallback = void (*)(
    void *, int, float *, tjs_uint, tjs_uint, tjs_uint);

// decodeAudioData resamples to its context's sample rate. To preserve Android's
// sample-position and .sli loop semantics, this bridge only tries formats whose
// original sample rate can be recovered from their container/header and creates
// an OfflineAudioContext at that exact rate. Unknown formats fall through to
// the existing WASM decoder creators.
EM_JS(void, TVPWebAudioDecodeStartJS,
      (const tjs_uint8 *data, tjs_uint size, void *opaque,
       tTVPWebAudioDecodeJSCallback callback), {
    const complete = (status, pcm, channels, sampleRate, frames) => {
        getWasmTableEntry(callback)(opaque, status, pcm, channels,
                                    sampleRate, frames);
    };

    const bytes = HEAPU8.slice(data, data + size);
    const u24be = (p) => (bytes[p] << 16) | (bytes[p + 1] << 8) |
        bytes[p + 2];
    const u32le = (p) => (bytes[p] | (bytes[p + 1] << 8) |
        (bytes[p + 2] << 16) | (bytes[p + 3] << 24)) >>> 0;
    const ascii = (p, value) => {
        if(p < 0 || p + value.length > bytes.length) return false;
        for(let i = 0; i < value.length; ++i)
            if(bytes[p + i] !== value.charCodeAt(i)) return false;
        return true;
    };

    const detectSampleRate = () => {
        // RIFF/WAVE: walk chunks instead of assuming the fmt chunk position.
        if(bytes.length >= 12 && ascii(0, 'RIFF') && ascii(8, 'WAVE')) {
            for(let p = 12; p + 8 <= bytes.length;) {
                const chunkSize = u32le(p + 4);
                if(ascii(p, 'fmt ') && chunkSize >= 8 && p + 16 <= bytes.length)
                    return u32le(p + 12);
                const next = p + 8 + chunkSize + (chunkSize & 1);
                if(next <= p || next > bytes.length) break;
                p = next;
            }
        }

        // Ogg identification packets may cross page metadata, but their magic
        // remains contiguous in the original byte stream.
        const scanEnd = Math.min(bytes.length, 256 * 1024);
        for(let p = 0; p + 16 <= scanEnd; ++p) {
            if(bytes[p] === 1 && ascii(p + 1, 'vorbis'))
                return u32le(p + 12);
            if(ascii(p, 'OpusHead'))
                return 48000; // Opus decoder output is always 48 kHz.
        }

        // FLAC STREAMINFO stores a 20-bit sample rate at bytes 10..12.
        if(bytes.length >= 21 && ascii(0, 'fLaC')) {
            let p = 4;
            while(p + 4 <= bytes.length) {
                const type = bytes[p] & 0x7f;
                const length = u24be(p + 1);
                if(type === 0 && length >= 13 && p + 17 <= bytes.length)
                    return (bytes[p + 14] << 12) |
                        (bytes[p + 15] << 4) | (bytes[p + 16] >>> 4);
                p += 4 + length;
            }
        }

        // Skip an ID3v2 tag before looking for MPEG audio or ADTS frames.
        let start = 0;
        if(bytes.length >= 10 && ascii(0, 'ID3')) {
            start = 10 + ((bytes[6] & 0x7f) << 21) +
                ((bytes[7] & 0x7f) << 14) +
                ((bytes[8] & 0x7f) << 7) + (bytes[9] & 0x7f);
        }
        // ISO BMFF needs a demuxer before WebCodecs and does not expose the
        // encoded sample rate at a fixed header offset. Do not mistake media
        // payload bytes for an MP3/ADTS sync word.
        if(bytes.length >= 12 && ascii(4, 'ftyp'))
            return 0;
        const rates = [44100, 48000, 32000];
        const adtsRates = [96000, 88200, 64000, 48000, 44100, 32000,
                           24000, 22050, 16000, 12000, 11025, 8000, 7350];
        for(let p = start; p + 4 <= scanEnd; ++p) {
            if(bytes[p] !== 0xff || (bytes[p + 1] & 0xe0) !== 0xe0)
                continue;

            const version = (bytes[p + 1] >>> 3) & 3;
            const layer = (bytes[p + 1] >>> 1) & 3;
            const rateIndex = (bytes[p + 2] >>> 2) & 3;
            if(version !== 1 && layer !== 0 && rateIndex !== 3) {
                const divisor = version === 3 ? 1 : (version === 2 ? 2 : 4);
                return rates[rateIndex] / divisor;
            }

            // ADTS has layer bits 00 and a four-bit sampling index.
            if(layer === 0) {
                const index = (bytes[p + 2] >>> 2) & 0x0f;
                if(index < adtsRates.length) return adtsRates[index];
            }
        }
        return 0;
    };

    const sampleRate = detectSampleRate();
    const OfflineContext = globalThis.OfflineAudioContext ||
        globalThis.webkitOfflineAudioContext;
    if(!sampleRate || sampleRate < 8000 || sampleRate > 96000 ||
       !OfflineContext) {
        complete(0, 0, 0, 0, 0);
        return;
    }

    let context;
    try {
        context = new OfflineContext(1, 1, sampleRate);
    } catch(error) {
        complete(0, 0, 0, 0, 0);
        return;
    }

    context.decodeAudioData(bytes.buffer).then((audio) => {
        const channels = audio.numberOfChannels;
        const frames = audio.length;
        const decodedBytes = channels * frames * 4;
        const maxDecodedBytes = Module['krkr2WebAudioDecodeMaxBytes'] ||
            256 * 1024 * 1024;
        if(!channels || channels > 2 || !frames ||
           audio.sampleRate !== sampleRate ||
           !Number.isSafeInteger(decodedBytes) ||
           decodedBytes > maxDecodedBytes) {
            complete(0, 0, 0, 0, 0);
            return;
        }

        const pcm = _malloc(decodedBytes);
        if(!pcm) {
            complete(0, 0, 0, 0, 0);
            return;
        }
        try {
            const base = pcm >>> 2;
            for(let channel = 0; channel < channels; ++channel) {
                HEAPF32.set(audio.getChannelData(channel),
                            base + channel * frames);
            }
            complete(1, pcm, channels, sampleRate, frames);
        } catch(error) {
            _free(pcm);
            complete(0, 0, 0, 0, 0);
        }
    }, () => complete(0, 0, 0, 0, 0));
});

void TVPWebAudioDecodeComplete(void *opaque, int status, float *pcm,
                               tjs_uint channels, tjs_uint sampleRate,
                               tjs_uint frames) {
    std::unique_ptr<tTVPWebAudioDecodeRequest> request(
        static_cast<tTVPWebAudioDecodeRequest *>(opaque));
    tTVPWaveDecoder *decoder = nullptr;
    if(status == 1 && pcm && channels && sampleRate && frames) {
        try {
            decoder = new tTVPWebAudioWaveDecoder(pcm, channels, sampleRate,
                                                  frames);
            spdlog::debug("WebAudio decoded '{}' ({} ch, {} Hz, {} frames)",
                          request->StorageName.AsStdString(), channels,
                          sampleRate, frames);
        } catch(...) {
            free(pcm);
            pcm = nullptr;
        }
    } else {
        free(pcm);
        pcm = nullptr;
    }
    if(!decoder) {
        spdlog::debug("WebAudio rejected '{}'; falling back to WASM decoder",
                      request->StorageName.AsStdString());
    }

    auto completion = std::move(request->Completion);
    request->Encoded.reset();
    if(completion)
        completion(decoder);
    else
        delete decoder;
}

void TVPWebAudioDecodeStartOnMain(void *opaque) {
    auto *request = static_cast<tTVPWebAudioDecodeRequest *>(opaque);
    TVPWebAudioDecodeStartJS(
        request->Encoded->data(),
        static_cast<tjs_uint>(request->Encoded->size()), request,
        &TVPWebAudioDecodeComplete);
}

} // namespace

void TVPTryCreateWebAudioWaveDecoderAsync(
    const ttstr &storagename,
    std::shared_ptr<std::vector<tjs_uint8>> encoded,
    tTVPWebAudioWaveDecoderCallback completion) {
    auto *request = new tTVPWebAudioDecodeRequest{
        storagename, std::move(encoded), std::move(completion)};
    // VLFS completes ReadBufferAsync on its native stream executor. DOM/Web
    // Audio APIs must start on the browser main runtime thread; completion is
    // intentionally not proxied back to the sleeping executor.
    emscripten_async_run_in_main_runtime_thread(
        EM_FUNC_SIG_VI, &TVPWebAudioDecodeStartOnMain, request);
}

#endif
