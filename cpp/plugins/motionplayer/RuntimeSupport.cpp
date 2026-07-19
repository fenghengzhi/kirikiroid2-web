//
// Internal helpers for motionplayer/emoteplayer runtime state.
//

#include "RuntimeSupport.h"
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <mutex>

#include <spdlog/spdlog.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#include "tjsArray.h"
#include "tjsDictionary.h"

#define LOGGER spdlog::get("plugin")

namespace motion::detail {
    namespace {

        struct LogoChainTraceSession {
            std::uint64_t sequence = 0;
            std::string motionPath;
            std::string motionName;
            std::string firstBadStage;
            std::string firstBadExpected;
            std::string firstBadActual;
            std::string upstreamLastGoodStage;
            std::string likelyRootCause;
            bool summaryEmitted = false;
        };

        std::mutex &logoTraceMutex() {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, LogoChainTraceSession>
        &logoTraceSessions() {
            static std::unordered_map<std::string, LogoChainTraceSession> sessions;
            return sessions;
        }

        std::string lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

        std::string basename(const std::string &value) {
            const auto slash = value.find_last_of("/\\");
            return slash == std::string::npos ? value : value.substr(slash + 1);
        }

        bool isTargetLogoMotionPath(const std::string &motionPath) {
            const auto lowered = lowercase(motionPath);
            return lowered.find("yuzulogo.mtn") != std::string::npos ||
                lowered.find("m2logo.mtn") != std::string::npos;
        }

        bool logoTraceQueryEnabled() {
#ifdef EMSCRIPTEN
            return EM_ASM_INT({
                try {
                    if(typeof window !== 'undefined' &&
                       window.__KRKR_TRACE_LOGO_CHAIN__) {
                        return 1;
                    }
                    const params = new URLSearchParams(window.location.search);
                    const traceParam = params.get('trace') || "";
                    if(params.has('traceLogoChain')) {
                        return 1;
                    }
                    return traceParam === 'logo' ||
                        traceParam === 'logo-chain' ||
                        traceParam === '1';
                } catch (e) {
                    return 0;
                }
            }) != 0;
#else
            // libkrkr2.so (Android original) has no logo chain trace feature.
            // Verified via IDA Pro MCP:
            //   - No "tracelogochain" / "snaplogo" / "logoChain*" strings in
            //     either UTF-8 or UTF-16LE encoding (ida-search-string skill
            //     scan across all segments).
            //   - EmoteObject_init at 0x67DBAC (the PSB load entry) contains
            //     zero spdlog/LOGGER calls and zero conditional-trace branches
            //     in its full 1632-byte body.
            //   - libkrkr2.so's only command-line query helper is sub_90DA50
            //     (the equivalent of the named-arg TVPGetCommandLine). The
            //     string pool contains -forcelog / -lowpri / -laxtimer as
            //     query targets, but not -tracelogochain, so no function in
            //     libkrkr2.so ever issues a sub_90DA50(L"-tracelogochain", _)
            //     call. Introducing one here would add a call-site that does
            //     not exist in the original binary.
            //
            // The whole logoChainTrace* subsystem (added in commit 0830b84)
            // is a pure-logging local debug path, preserved on the EMSCRIPTEN
            // side only. For non-EMSCRIPTEN builds the aligned behavior is to
            // never enable it.
            return false;
#endif
        }

        bool logoSnapshotQueryEnabled() {
#ifdef EMSCRIPTEN
            return EM_ASM_INT({
                try {
                    const params = new URLSearchParams(window.location.search);
                    const snapParam = params.get('snap') || "";
                    const traceParam = params.get('trace') || "";
                    return snapParam === '1' ||
                        snapParam === 'logo' ||
                        traceParam === 'snap' ||
                        traceParam === 'logo-snap';
                } catch (e) {
                    return 0;
                }
            }) != 0;
#else
            // Same rationale as logoTraceQueryEnabled above: verified absent
            // from libkrkr2.so, non-EMSCRIPTEN builds stay aligned by never
            // enabling the snapshot feature.
            return false;
#endif
        }

        LogoChainTraceSession &ensureLogoTraceSessionLocked(
            const std::string &motionPath) {
            auto &session = logoTraceSessions()[lowercase(motionPath)];
            if(session.motionPath != motionPath) {
                session = {};
                session.motionPath = motionPath;
                session.motionName = basename(motionPath);
            }
            if(session.motionName.empty()) {
                session.motionName = basename(motionPath);
            }
            return session;
        }

        std::string frameLabel(double frameTime) {
            return std::isfinite(frameTime)
                ? fmt::format("{:.3f}", frameTime)
                : "n/a";
        }

    } // namespace

    // A8: nodes / nodeLabelMap moved to Player. These helpers now take the
    // Player so they can mutate the migrated containers while the still-
    // PlayerRuntime renderItem lifetime map (A9 target) is reached via
    // player._runtime.
    void ensureRootNodeLike_0x6CED30(Player &player) {
        if(!player._nodes.empty()) {
            player._nodes.front().index = 0;
            player._nodes.front().parentIndex = -1;
            return;
        }
        MotionNode root;
        root.index = 0;
        root.parentIndex = -1;
        player._nodes.emplace_back(std::move(root));
    }

    void resetNodeTreeKeepRootLike_0x6B56F8(Player &player) {
        ensureRootNodeLike_0x6CED30(player);
        auto &root = player._nodes.front();
        root.index = 0;
        root.parentIndex = -1;
        if(player._nodes.size() > 1) {
            player._nodes.erase(std::next(player._nodes.begin()),
                                player._nodes.end());
        }
        player._nodeLabelMap.clear();
        player._renderItemNativeFieldLifetimeByNode.clear();
    }

    // Aligned with libkrkr2.so Player_buildNodePathKey @0x6B5C1C.
    //
    // Binary pseudocode (0x6B5C48..0x6B5DCC):
    //   *a3 = nullptr; accumulated = nullptr;
    //   while ( nodeIndex ) {                        // 0 == synthetic root → stop
    //     node = deque_at(nodeIndex);                // 2632-byte stride
    //     segment = ttstr("/") + *(node+0);          // sub_A0CC68(out,"/",node)
    //                                                 //   *(node+0) = label ttstr
    //     accumulated = segment + accumulated;       // sub_A1359C(segment, acc)
    //     nodeIndex = *(node+36);                    // parentIndex
    //   }
    //   return accumulated;
    //
    // Each segment is the node's "label" prefixed with '/'. Newer (more
    // ancestral) segments are prepended, yielding "/top/.../self". The
    // synthetic root at index 0 contributes nothing. Empty labels still emit a
    // bare "/" segment, exactly as the binary's concat path does (it does not
    // skip empty names).
    //
    // This ttstr path is HM3's key (_perNodeLayerStateMap, Player+1184) — the
    // only consumer of this builder (xrefs_to(0x6B5C1C) = 2 callers, both
    // HM3). It is NOT the Player+24 node-index map key; that map uses the raw
    // PSB label (see NodeTree.cpp).
    ttstr buildNodePathKeyLike_0x6B5C1C(
        const std::deque<motion::detail::MotionNode> &nodes, int nodeIndex) {
        ttstr accumulated;
        // 0x6B5C54: `if ( a2 )` — index 0 (root) produces an empty key.
        while(nodeIndex) {
            if(nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) {
                break;
            }
            const motion::detail::MotionNode &node =
                nodes[static_cast<size_t>(nodeIndex)];
            // segment = "/" + label  (sub_A0CC68(out, L"/", node))
            const ttstr segment = ttstr(TJS_W("/")) + node.layerName;
            // accumulated = segment + accumulated  (sub_A1359C(segment, acc))
            accumulated = segment + accumulated;
            // 0x6B5D98: a2 = *(node+36) = parentIndex; loop while ( a2 ).
            nodeIndex = node.parentIndex;
        }
        return accumulated;
    }

    // A10: makePlayerRuntime() deleted along with the now-empty PlayerRuntime
    // struct. Player's constructor invokes ensureRootNodeLike_0x6CED30(*this)
    // directly to seed the root node.

    std::string narrow(const ttstr &value) { return value.AsStdString(); }

    ttstr widen(const std::string &value) { return ttstr{ value }; }

    tTJSVariant makeArray(const std::vector<tTJSVariant> &items) {
        iTJSDispatch2 *array = TJSCreateArrayObject();
        static tjs_uint addHint = 0;
        for(const auto &item : items) {
            tTJSVariant value = item;
            tTJSVariant *args[] = { &value };
            array->FuncCall(0, TJS_W("add"), &addHint, nullptr, 1, args, array);
        }
        tTJSVariant result(array, array);
        array->Release();
        return result;
    }

    tTJSVariant makeDictionary(
        const std::vector<std::pair<std::string, tTJSVariant>> &entries) {
        iTJSDispatch2 *dic = TJSCreateDictionaryObject();
        for(const auto &[key, value] : entries) {
            tTJSVariant tmp = value;
            dic->PropSet(TJS_MEMBERENSURE, widen(key).c_str(), nullptr, &tmp,
                         dic);
        }
        tTJSVariant result(dic, dic);
        dic->Release();
        return result;
    }

    std::vector<tTJSVariant>
    stringsToVariants(const std::vector<std::string> &values) {
        std::vector<tTJSVariant> result;
        result.reserve(values.size());
        for(const auto &value : values) {
            result.emplace_back(widen(value));
        }
        return result;
    }

    bool logoChainTraceEnabled() {
        static const bool enabled = logoTraceQueryEnabled();
        return enabled;
    }

    bool logoSnapshotMarkEnabled() {
        static const bool enabled = logoSnapshotQueryEnabled();
        return enabled;
    }

    bool logoChainTraceEnabledForPath(const std::string &motionPath) {
        return logoChainTraceEnabled() && isTargetLogoMotionPath(motionPath);
    }

    bool logoSnapshotMarkEnabledForPath(const std::string &motionPath) {
        return logoSnapshotMarkEnabled() && isTargetLogoMotionPath(motionPath);
    }

    void resetLogoChainTraceSession(const std::string &motionPath) {
        if(!logoChainTraceEnabledForPath(motionPath)) {
            return;
        }
        std::lock_guard lock(logoTraceMutex());
        auto &session = ensureLogoTraceSessionLocked(motionPath);
        session = {};
        session.motionPath = motionPath;
        session.motionName = basename(motionPath);
    }

    void logoChainTraceLog(const std::string &motionPath,
                           const char *stage,
                           const char *func,
                           const double frameTime,
                           const std::string &message) {
        if(!logoChainTraceEnabledForPath(motionPath) || !LOGGER) {
            return;
        }
        std::lock_guard lock(logoTraceMutex());
        auto &session = ensureLogoTraceSessionLocked(motionPath);
        ++session.sequence;
        LOGGER->warn(
            "CHAIN SEQ={} stage={} func={} motion={} frame={} {}",
            session.sequence, stage, func, session.motionName,
            frameLabel(frameTime), message);
    }

    void logoChainTraceCheck(const std::string &motionPath,
                             const char *stage,
                             const char *func,
                             const double frameTime,
                             const std::string &expected,
                             const std::string &actual,
                             const bool ok,
                             const std::string &likelyRootCause) {
        if(!logoChainTraceEnabledForPath(motionPath) || !LOGGER) {
            return;
        }

        std::lock_guard lock(logoTraceMutex());
        auto &session = ensureLogoTraceSessionLocked(motionPath);
        ++session.sequence;
        LOGGER->warn(
            "CHAIN SEQ={} stage={} func={} motion={} frame={} exp={} act={} ok={}",
            session.sequence, stage, func, session.motionName,
            frameLabel(frameTime), expected, actual, ok ? 1 : 0);

        if(ok) {
            if(session.firstBadStage.empty()) {
                session.upstreamLastGoodStage = stage;
            }
            return;
        }

        if(session.firstBadStage.empty()) {
            session.firstBadStage = stage;
            session.firstBadExpected = expected;
            session.firstBadActual = actual;
            session.likelyRootCause = likelyRootCause;
        }
    }

    void logoChainTraceSummary(const std::string &motionPath,
                               const char *func,
                               const double frameTime,
                               const std::string &note) {
        if(!logoChainTraceEnabledForPath(motionPath) || !LOGGER) {
            return;
        }

        std::lock_guard lock(logoTraceMutex());
        auto &session = ensureLogoTraceSessionLocked(motionPath);
        if(session.summaryEmitted) {
            return;
        }
        session.summaryEmitted = true;

        const auto firstBadStage = session.firstBadStage.empty()
            ? std::string("none")
            : session.firstBadStage;
        const auto expected = session.firstBadExpected.empty()
            ? std::string("all_logged_stages_ok")
            : session.firstBadExpected;
        const auto actual = session.firstBadActual.empty()
            ? std::string("all_logged_stages_ok")
            : session.firstBadActual;
        const auto upstream = session.upstreamLastGoodStage.empty()
            ? std::string("none")
            : session.upstreamLastGoodStage;
        const auto rootCause = session.likelyRootCause.empty()
            ? std::string("not_detected_in_logged_fields")
            : session.likelyRootCause;

        LOGGER->warn(
            "CHAIN SUMMARY func={} motion={} frame={} first_bad_stage={} expected={} actual={} upstream_last_good_stage={} likely_root_cause={}{}{}",
            func, session.motionName, frameLabel(frameTime), firstBadStage,
            expected, actual, upstream, rootCause,
            note.empty() ? "" : " note=", note);
    }

} // namespace motion::detail
