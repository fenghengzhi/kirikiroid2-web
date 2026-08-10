// PlayerVariable.cpp — variable/eval-result/parameter bindings
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
#include "MotionDispatch.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace {
    // variableEaseWeightLike_0x671228 (the TJS "ease" -> v22 factor formula)
    //   was removed 2026-06-03 along with the non-faithful Player-side
    //   setVariableResolvedWeightLike_0x671228 shim that was its only caller.
    //   The faithful copy of this formula lives inside EmoteEngine::setVariable
    //   (the real 0x671228 dispatch on `this`=EmoteEngine).

    struct ParameterLabelParts {
        std::string full;
        std::string suffix;
    };

    ParameterLabelParts splitParameterLabelLike_0x6D0BF4(
        const std::string &label) {
        ParameterLabelParts parts;
        parts.full = label;
        const auto scopePos = label.rfind("::");
        if(scopePos != std::string::npos) {
            parts.suffix = label.substr(scopePos + 2);
            return parts;
        }
        const auto slashPos = label.rfind('/');
        if(slashPos != std::string::npos) {
            parts.suffix = label.substr(slashPos + 1);
        }
        return parts;
    }

    // The four-reference split helper is Android arm64 sub_695114, Android
    // armv7 sub_571C50, iOS arm64 sub_1000F52D0, and iOS armv7 sub_F1D20.
    // This narrow-string bridge splits scope into "::" segments and stores the
    // equivalent ttstr values used by chainSegments.
    // Port: returns the segment strings as ttstr (the wcscmp dedup in sub_6B9650
    // reads only the string value). Empty scope -> single empty-string segment
    // (matches the binary: the find fails immediately, the tail = whole string).
    std::vector<ttstr> splitScopeSegments_guess(const std::string &scope) {
        std::vector<ttstr> segments;
        std::string::size_type pos = 0;
        for(;;) {
            const auto sep = scope.find("::", pos);
            if(sep == std::string::npos) {
                segments.push_back(motion::detail::widen(scope.substr(pos)));
                break;
            }
            segments.push_back(
                motion::detail::widen(scope.substr(pos, sep - pos)));
            pos = sep + 2;
        }
        return segments;
    }

    double normalizeParameterValueLike_0x6B1718(
        const motion::detail::MotionParameterEntry &entry,
        double rawValue) {
        const double range = entry.rangeEnd - entry.rangeBegin;
        if(range == 0.0 || entry.rangeScale == 0.0) {
            return 0.0;
        }
        double value = entry.discretization
            ? static_cast<double>(static_cast<int>(rawValue))
            : rawValue;
        const double lo = std::min(entry.rangeBegin, entry.rangeEnd);
        const double hi = std::max(entry.rangeBegin, entry.rangeEnd);
        value = std::clamp(value, lo, hi);
        return (value - entry.rangeBegin) * entry.rangeScale;
    }

    // libkrkr2.so Player_bindParameterValue ramp loop (the structure shared by
    // the HM2 tail @0x6C4C24 keyed by raw label and the per-node type3/4 loops
    // @0x6C4B30/0x6C4A54 keyed by suffix). Walks the +408 multimap's equal_range
    // for `key` (sub_6F2F98 returns [lower,upper); the body iterates every match
    // via _Rb_tree_increment) and, for each matched MotionParameterEntry, writes
    //   entry.mode  = mode                                  (0x6c4c48 *(v83+48))
    //   entry.value = rangeScale * clamp(rawV) / range      (0x6c4ca4 *(v83+40))
    // exactly via normalizeParameterValueLike_0x6B1718 (same clamp/scale math).
    void applyParameterRampsLike_0x6C4C0C(
        motion::detail::ParameterRampMap &rampMap,
        const ttstr &key,
        int mode,
        double rawValue) {
        const auto range = rampMap.equal_range(key);
        for(auto it = range.first; it != range.second; ++it) {
            motion::detail::MotionParameterEntry *entry = it->second;
            if(!entry) {
                continue;
            }
            entry->mode = mode;
            entry->value =
                normalizeParameterValueLike_0x6B1718(*entry, rawValue);
        }
    }

} // anonymous namespace

namespace motion {
    double &Player::ensureEvalResultSlotLike_0x686944(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            return it->second->value;
        }

        _evalResultList.push_back(EvalResultEntry{label, 0.0});
        auto it = _evalResultList.end();
        --it;
        _evalResultListIndex[label] = it;
        return it->value;
    }

    void Player::removeEvalResultSlotLike_Reset(const std::string &label) {
        if(const auto it = _evalResultListIndex.find(label);
           it != _evalResultListIndex.end()) {
            _evalResultList.erase(it->second);
            _evalResultListIndex.erase(it);
        }
    }

    detail::MotionParameterEntry *Player::appendParameterEntryLike_0x6B1718(
        const tTJSVariant &parameter) {
        // sub_6B1718 @0x6B1718 rejects every non-object variant before it
        // creates the ncbPropAccessor/dispatch holder or grows Player+384.
        if(parameter.Type() != tvtObject) {
            return nullptr;
        }

        detail::MotionParameterEntry entry;
        entry.id = detail::motionPropGetString(parameter, TJS_W("id"));
        entry.discretization = detail::motionPropGetBool(
            parameter, TJS_W("discretization"));
        entry.rangeBegin = detail::motionPropGetDouble(
            parameter, TJS_W("rangeBegin"));
        entry.rangeEnd = detail::motionPropGetDouble(
            parameter, TJS_W("rangeEnd"));

        const double range = entry.rangeEnd - entry.rangeBegin;
        double division = 0.0;
        tTJSVariant divisionValue;
        if(detail::motionTryPropGet(parameter, TJS_W("division"),
                                    divisionValue)) {
            division = divisionValue.AsReal();
        } else {
            division = range;
            if(division <= 0.0) {
                division = 1.0;
            }
        }
        entry.rangeScale = (range != 0.0 && division > 0.0)
            ? division / range
            : 0.0;
        entry.mode = 0;
        entry.value = normalizeParameterValueLike_0x6B1718(
            entry, initialParameterRawValueLike_0x6B1ABC(entry.id));

        _parameterEntries.push_back(std::move(entry));
        return &_parameterEntries.back();
    }

    bool Player::parseParameterListLike_0x6B202C(
        const tTJSVariant &parameters) {
        // sub_6B202C @0x6B202C rejects only a void variant. Every non-void
        // value is converted to the ordinary dispatch holder, counted through
        // Motion_propGetCount and indexed with PropGetByNum.
        if(parameters.Type() == tvtVoid) {
            return false;
        }

        const tjs_int count = detail::motionPropGetCount(parameters);
        for(tjs_int index = 0; index < count; ++index) {
            const tTJSVariant parameter =
                detail::motionPropGetByNum(parameters, index);
            appendParameterEntryLike_0x6B1718(parameter);
        }
        finalizeParameterTableLike_0x6B1ECC();
        return true;
    }

    // libkrkr2.so Player_finalizeParameterTable @0x6B1ECC. Iterates the +384
    // parameter-entry vector (v1[48]..v1[49] stepping 56B = one entry) and
    // inserts each into the +408 multimap keyed by entry.id, value = &entry.
    // The binary descent (0x6B1F44) walks the RB-tree comparing entry.id against
    // node keys via wcscmp and inserts UNCONDITIONALLY at the leaf via
    // sub_6F16AC (operator new(0x30); _Rb_tree_insert_and_rebalance; ++count) —
    // duplicate ids are kept (multimap). The binary also AddRef's the id ttstr
    // into the node key (sub_6F16AC 0x6f173c); ttstr's copy ctor does that here.
    //
    // The binary's outer v3=v3[1] loop populates EVERY Player in the parent
    // chain from v1's (the calling Player's) +384 vector. child+8 is the parent
    // Player pointer written at 0x6B43DC; locally it is `_parentPlayer`. This is
    // what makes a grandchild parameter (for example `select`) visible in the
    // immediate child's +408 map: Player_bindParameterValue @0x6C4668 finds the
    // immediate type-3 child by the scope path, then looks up the suffix in that
    // child's +408 map. Fresh Player_playImpl@0x6B2284 and
    // Player_initNonEmoteMotion@0x6B365C decompilation proves neither caller
    // clears +408 before registration: +384 merely rewinds its vector end and
    // repeated +408 nodes therefore persist. Cross-Player nodes are removed by
    // 0x6CDE18 during child destruction. 0x6B1ECC itself also performs no clear.
    void Player::finalizeParameterTableLike_0x6B1ECC() {
        for(Player *destination = this; destination != nullptr;
            destination = destination->_parentPlayer) {
            for(auto &entry : _parameterEntries) {
                if(!entry.id.IsEmpty()) {
                    destination->_parameterRampMap.emplace(entry.id, &entry);
                }
            }
        }
    }

    // libkrkr2.so Player_purgeParameterRampMapByParent_guess @0x6CDE18.
    // Player+408 is a multimap whose mapped values point into the calling
    // Player's +384 parameter vector. Walk this Player and its +8 parent chain;
    // for every parameter entry, erase only equal-key nodes whose mapped pointer
    // is that exact entry. Player_dtor@0x6CFADC calls this before destroying the
    // parameter vector, so no ancestor map can retain a dangling entry pointer.
    void Player::purgeParameterRampMapLike_0x6CDE18() {
        for(Player *destination = this; destination != nullptr;
            destination = destination->_parentPlayer) {
            for(auto &entry : _parameterEntries) {
                const auto range =
                    destination->_parameterRampMap.equal_range(entry.id);
                for(auto it = range.first; it != range.second;) {
                    if(it->second == &entry) {
                        it = destination->_parameterRampMap.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }
    }

    // Aligned with libkrkr2.so sub_6B9650 @0x6B9650. Rebuilds the HM1 entry's
    // heapResult (entry+48 = std::vector<MotionNode*>): the set of type3/4 nodes
    // whose ancestor label-chain (walked bottom-up via node.parentIndex / node+36)
    // matches the entry's chainSegments reference (entry+8..+16).
    //
    // Binary pseudocode (verified against the 0x6b9650 decompile + disasm):
    //   if (entry.weight == 0.0) return;             // 0x6b968c gate
    //   chain = {};                                  // local vector<label>
    //   scanNodeIndex = 1;                           // skip root (deque idx 0)
    //   entry.weight = 0;                            // 0x6b96ac (cleared!)
    //   entry.heapResult.clear();                    // 0x6b96b0 (end = begin)
    //   while (scanNodeIndex < nodeCount - 1) {      // 0x6b97ec
    //     node = nodes[scanNodeIndex];
    //     if ((node.nodeType - 3) > 1) { ++scan; continue; }   // not 3/4
    //     idx = scanNodeIndex;
    //     for (;;) {                                 // ancestor walk 0x6b9834
    //       chainNodeIndex = idx;
    //       chain.insert(end, nodes[idx].label);     // sub_6BA5B4
    //       if (chain.size > ref.size && !chain.empty) chain.pop_back(); // trunc
    //       if (chain.size == ref.size) {
    //         if (ref.empty()) goto MATCH;           // 0x6b98b8
    //         if (chain elementwise == ref) goto MATCH;          // 0x6b98bc loop
    //       }
    //       parentIndex = nodes[chainNodeIndex].parentIndex;     // 0x6b9958
    //       if (parentIndex <= 0) { ++scan; break; } // reached root / no parent
    //       idx = parentIndex;
    //     }
    //     continue;
    //   MATCH:
    //     entry.heapResult.push_back(nodes[scanNodeIndex]);      // 0x6b96f0
    //     ++scanNodeIndex;
    //   }
    //   // chain backing released at function exit (0x6b9968).
    //
    // The repeated re-compare of the frozen ref.size window during the climb is a
    // binary artifact (truncation keeps dropping each newly-added deeper
    // ancestor); it is reproduced faithfully — the climb only terminates at the
    // root (parentIndex<=0). The comparison element type is the node's label
    // (node+0 -> ttstr); the port uses MotionNode::layerName directly.
    void Player::rebuildEvalCascadeHeapResultLike_0x6B9650(
        detail::EvalCascadeState &entry) {
        if(entry.weight == 0.0) {
            return; // 0x6b968c
        }
        entry.weight = 0.0;        // 0x6b96ac — binary clears the gate weight
        entry.heapResult.clear();  // 0x6b96b0 — end = begin (keep capacity)

        const std::vector<ttstr> &ref = entry.chainSegments;
        std::vector<ttstr> chain; // binary local p/v44 (released at 0x6b9968)

        // Loop bound (0x6b97ec): `[deque-size-expr] - 1 > scanNodeIndex`. The
        // deque-size expr (magic 0x18E6527AF1373F07 / 0xE719AD850EC8C0F9 =
        // 329^-1 mod 2^64, 329 = 2632/8) is libstdc++ std::deque::size() inlined
        // for the >512B (1-elem/block) MotionNode deque, which yields realSize+1;
        // the literal `- 1` cancels that +1 bias, so the bound is realSize ==
        // _nodes.size(). The author source is `scanNodeIndex < _nodes.size()`
        // (NOT size()-1, NO trailing sentinel — same inlining cross-checked at
        // PlayerUpdateLayerEval.cpp:700-720 / advanceNodeFrames 0x6B7E44). Root is
        // index 0, scan starts at 1.
        for(std::size_t scanNodeIndex = 1; scanNodeIndex < _nodes.size();
            ++scanNodeIndex) {
            detail::MotionNode &scanNode = _nodes[scanNodeIndex];
            // (nodeType - 3) > 1 (unsigned) -> not in {3,4} (0x6b9824).
            if(static_cast<unsigned>(scanNode.nodeType - 3) > 1u) {
                continue;
            }
            std::size_t idx = scanNodeIndex;
            bool matched = false;
            for(;;) {
                const std::size_t chainNodeIndex = idx;
                // sub_6BA5B4 @0x6BA5B4 receives a2=vector.begin() at
                // 0x6B985C: insert at BEGIN. With pop_back below this retains
                // the newest node/ancestor window while scanning each child.
                chain.insert(chain.begin(), _nodes[idx].layerName);
                // Truncate so chain never exceeds ref length (0x6b9874): drop the
                // oldest tail element when over and chain non-empty.
                if(chain.size() > ref.size() && !chain.empty()) {
                    chain.pop_back();
                }
                if(chain.size() == ref.size()) {
                    if(ref.empty()) {
                        matched = true; // 0x6b98b8 both empty -> push node
                        break;
                    }
                    bool allEqual = true;
                    for(std::size_t j = 0; j < ref.size(); ++j) {
                        // 0x6b98bc: ptr-equal short-circuit, then type tag (+0x3C)
                        // + wcscmp. Port compares the ttstr string value, which is
                        // exactly what ttstr_c_str/sub_9B1ED0(wcscmp) reads.
                        if(chain[j] != ref[j]) {
                            allEqual = false;
                            break;
                        }
                    }
                    if(allEqual) {
                        matched = true;
                        break;
                    }
                }
                // Climb to parent (0x6b9958): parentIndex = nodes[chainNodeIndex]
                // .parentIndex (node+36). <=0 -> root / no parent -> give up.
                const int parentIndex = _nodes[chainNodeIndex].parentIndex;
                if(parentIndex <= 0) {
                    break;
                }
                idx = static_cast<std::size_t>(parentIndex);
            }
            if(matched) {
                // 0x6b96f0 / grow path: push nodes[scanNodeIndex] into heapResult.
                entry.heapResult.push_back(&scanNode);
            }
        }
        // chain (local) released here — matches binary 0x6b9968 cleanup.
    }

    double Player::initialParameterRawValueLike_0x6B1ABC(
        const ttstr &id) const {
        if(id.IsEmpty()) {
            return 0.0;
        }

        const auto parts =
            splitParameterLabelLike_0x6D0BF4(detail::narrow(id));
        const auto findValue =
            [&parts](const detail::LabelValueMap &values,
                     double &out) -> bool {
            // HM2 (Player+320) is ttstr-keyed; widen the std::string label
            // fragments to match the binary's UTF-16 key.
            if(const auto it = values.find(detail::widen(parts.full));
               it != values.end()) {
                out = it->second;
                return true;
            }
            if(!parts.suffix.empty()) {
                if(const auto it = values.find(detail::widen(parts.suffix));
                   it != values.end()) {
                    out = it->second;
                    return true;
                }
            }
            return false;
        };

        for(const Player *player = this; player != nullptr;
            player = player->_parentPlayer) {
            double value = 0.0;
            if(findValue(player->_evalResultValues, value)) {
                return value;
            }
        }

        return 0.0;
    }

    // Aligned with libkrkr2.so Player_bindParameterValue_writesHM1_HM2
    // @0x6C4668. Mirrors the binary's two-region control flow:
    //
    //   [top half — HM1 cascade, gated by sub_6D0BF4(...)&1 at 0x6c46bc]
    //     scopeJoin = scope ? scope : "::"                 (0x6c46c4..0x6c4708)
    //     joinedKey = label ? join(scopeJoin,label) : scopeJoin (0x6c4720..0x6c4790)
    //     node = HM1.find(joinedKey)                       (sub_6F51BC @0x6c4818)
    //     if (!node) { node = HM1.upsert(joinedKey);       (0x6F52AC @0x6c4850)
    //                  node.key = joinedKey;
    //                  node.chainDispatches = split(scope,"\\"); // DEFERRED
    //                  node.weight = 1.0 }                  (0x3FF.. @0x6c4964)
    //     node.writeVal = a4;                              (0x6c4968)
    //     sub_6B9650(node);  // rebuild HM1 heapResult       // PORTED (Stage 2)
    //     ramp child-Player+408 for each heapResult type3/4 node (suffix key)
    //       -> PORTED: heapResult-driven child ramp inside the HM1 block
    //
    //   [LABEL_132 — HM2, unconditional, 0x6c4c0c]
    //     HM2.upsert(rawLabel) = a4;   <-- green-critical, value-equivalent
    //     ramp a1+408 controller multimap for rawLabel  (PORTED: equal_range)
    //
    // The web port mirrors the HM2 store via the existing _evalResultValues /
    // _evalResultList path (raw label -> double, value-identical to a4). The HM2
    // tail ramp loop is now PORTED: the +408 controller multimap
    // (_parameterRampMap, built by finalizeParameterTableLike_0x6B1ECC) is walked
    // by equal_range(rawLabel) and each matched MotionParameterEntry's value/mode
    // is written — replacing the prior non-faithful full-vector scan that matched
    // id==full||id==suffix on the own player (the suffix match belonged to the
    // descendant path, not the own tail). The HM1 cascade is additive structure
    // (separate _evalCascadeMap, no feedback into HM2). NOW PORTED (Stage 2): the
    // chainSegments build (splitScopeSegments_guess), the
    // heapResult rebuild (sub_6B9650 @0x6c4974 ->
    // rebuildEvalCascadeHeapResultLike_0x6B9650), and the heapResult-driven child
    // ramp (0x6c4978: each type3/4 node's child Player +408 map keyed by suffix).
    // The prior _nodes full-recursion approximation was removed.
    void Player::bindParameterValueLike_0x6C4668(const std::string &label,
                                                 int mode,
                                                 double value) {
        if(label.empty()) {
            return;
        }

        const auto parts = splitParameterLabelLike_0x6D0BF4(label);

        // --- HM1 cascade upsert (binary top half, 0x6c46c4..0x6c4968) ---
        // The binary runs this region only when sub_6D0BF4 splits the label
        // (label contains "::" or "/"). Replicate that gate: parts.suffix is
        // non-empty exactly in those two cases (rfind("::") / rfind('/')).
        if(!parts.suffix.empty()) {
            // scope = substring before the separator. scopeJoin = scope ? scope
            // : "::"; here scope is always non-empty when a separator exists
            // (binary 0x6c46c4: v102 is the scope side of the split).
            std::string scope;
            if(const auto sepPos = label.rfind("::");
               sepPos != std::string::npos) {
                scope = label.substr(0, sepPos);
            } else if(const auto slashPos = label.rfind('/');
                      slashPos != std::string::npos) {
                scope = label.substr(0, slashPos);
            }
            // 0x6C46C4..0x6C4708 builds `scope + "::"` when scope exists,
            // otherwise the literal "::".
            const std::string scopeJoin =
                scope.empty() ? "::" : (scope + "::");
            // joinedKey = label ? scopeJoin + label : scopeJoin. The binary's
            // "label" (v101) is parts.suffix; sub_A1359C concatenates without a
            // separator (0x6c4734: join(scopeJoin, label)).
            const std::string joinedKey =
                parts.suffix.empty() ? scopeJoin
                                     : (scopeJoin + parts.suffix);
            const ttstr cascadeKey = detail::widen(joinedKey);

            // HM1 find/upsert into _evalCascadeMap (binary Player+264). The
            // inline hash map is modeled by unordered_map<ttstr,...,ttstr_hash>;
            // upsert == operator[] (insert-if-absent), matching sub_6F51BC +
            // Player_HM1_upsert_evalCascade @0x6F52AC.
            auto found = _evalCascadeMap.find(cascadeKey);
            if(found == _evalCascadeMap.end()) {
                // First insert: seed key, chainSegments,
                // weight=1.0 (0x6c4964).
                auto inserted = _evalCascadeMap.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(cascadeKey),
                    std::forward_as_tuple());
                found = inserted.first;
                found->second.keyCopy = cascadeKey;
                // chainSegments = scope split into "::"-segments (binary
                // split scope by "::". Built once on first insert and frozen —
                // the dedup reference for sub_6B9650.
                found->second.chainSegments =
                    splitScopeSegments_guess(scope);
                found->second.weight = 1.0; // binary node+40 = 1.0
            }
            // node.writeVal = a4 on every bind (binary 0x6c4968, unconditional
            // after the insert branch).
            found->second.writeVal = value;

            // sub_6B9650(a1, node) @0x6c4974: rebuild this entry's heapResult
            // (type3/4 nodes whose ancestor label-chain == chainSegments). Gated
            // internally by entry.weight!=0; it also clears weight to 0.
            detail::EvalCascadeState &entry = found->second;
            rebuildEvalCascadeHeapResultLike_0x6B9650(entry);

            // Consume heapResult @0x6c4978: for each listed node, ramp the child
            // Player(s)' +408 controller map (_parameterRampMap) by the SUFFIX
            // (binary &v101 = parts.suffix), NOT by re-running HM1/HM2. type4
            // (0x6c4b30): per-particle-child native Player; type3 (0x6c4a54): the
            // node's own child Player. Each child's +408 == its _parameterRampMap.
            const ttstr suffixKey = detail::widen(parts.suffix);
            for(detail::MotionNode *node : entry.heapResult) {
                if(!node) {
                    continue;
                }
                if(node->nodeType == 4) {
                    for(int i = 0; i < node->getParticleCount(); ++i) {
                        if(Player *child = node->getParticleChild(i)) {
                            applyParameterRampsLike_0x6C4C0C(
                                child->_parameterRampMap, suffixKey, mode, value);
                        }
                    }
                } else if(node->nodeType == 3) {
                    if(Player *child = node->getChildPlayer()) {
                        applyParameterRampsLike_0x6C4C0C(
                            child->_parameterRampMap, suffixKey, mode, value);
                    }
                }
            }
        }
        // --- end HM1 cascade ---

        // --- HM2 tail (binary LABEL_132 @0x6C4C0C, unconditional) ---
        // Binary: HM2[rawLabel]=a4 (the _evalResultValues mirror, written by
        // writeEvalResultValueLike_0x6C4668's caller), then the OWN player's
        // +408 ramp loop keyed by the RAW label (sub_6F2F98(a1+408, a2),
        // a2 = the original label ttstr — NOT the split suffix). The own ramp
        // therefore matches only entries whose id == the full raw label.
        applyParameterRampsLike_0x6C4C0C(_parameterRampMap, detail::widen(label),
                                         mode, value);
        // The HM1-block heapResult-driven descendant ramp now runs above (inside
        // the HM1 cascade block, binary order: before LABEL_132). The prior
        // _nodes full-recursion approximation (which re-ran the entire
        // HM1/HM2/split on every child by the FULL label) was replaced — the
        // binary ramps each heapResult node's child Player directly by the
        // SUFFIX over its +408 map, without recursing the var-bind.
    }

    void Player::writeEvalResultValueLike_0x6C4668(const std::string &label,
                                                   double value) {
        writeEvalResultValueLike_0x6C4668(label, 0, value);
    }

    void Player::writeEvalResultValueLike_0x6C4668(const std::string &label,
                                                   int mode,
                                                   double value) {
        if(label.empty()) {
            return;
        }
        ensureEvalResultSlotLike_0x686944(label) = value;
        // HM2 (Player+320) is ttstr-keyed; widen the raw std::string label.
        _evalResultValues[detail::widen(label)] = value;
        bindParameterValueLike_0x6C4668(label, mode, value);
    }

    // NOTE: the former Player::setVariableResolvedWeightLike_0x671228 and the
    //   4-arg Player::setVariable(ttstr,double,double,double) were removed
    //   2026-06-03. They were a NON-FAITHFUL local invention: a Player-side
    //   reimplementation of the EmoteEngine HM6->controller-deque dispatch
    //   (cases 0-8) that the binary performs EXCLUSIVELY inside
    //   EmoteEngine_setVariable @0x671228 (`this`=EmoteEngine). No binary
    //   function does that dispatch on a motion::Player. The genuine
    //   Motion.Player.setVariable NCB member (Player_ncb_registerMembers
    //   @0x6D69C8, callback thunk @0x6D0E70) maps to Player_bindParameterValue
    //   @0x6C4668 (`this`=Player), which writes Player HM1 (+264) / HM2 (+320)
    //   directly — ported as writeEvalResultValueLike_0x6C4668 and reached by
    //   Player::setVariableCompatMethod below. The disjoint-map bridge
    //   (EmoteEngine HM7 +1440 -> Player HM1/HM2) lives only in the progress()
    //   bind-loop (G2-C), never in a setVariable double-write.

    bool Player::isLabelInBindScopeListLike_0x6CD16C(const ttstr &key) const {
        // libkrkr2.so Player_isLabelInBindScopeList @0x6CD16C: walks the var-track
        // deque (Player+1312 = _variableLabelScopes), true if any item's
        // cascadeKey (item+0) equals key (pointer-eq or strcmp).
        for(const auto &item : _variableLabelScopes) {
            if(item.cascadeKey == key) {
                return true;
            }
        }
        return false;
    }

    void Player::bindParameterValueLike_0x6C4668(const ttstr &key, double value) {
        // libkrkr2.so Player_bindParameterValue_writesHM1_HM2 @0x6C4668.
        const auto narrowKey = detail::narrow(key);
        // sub_6D0BF4 (0x6D0BF4): split on first "::" (else first "/"); the HM1
        // join key = scope + "::" + label (normalises "/" → "::").
        auto sep = narrowKey.find("::");
        size_t sepLen = 2;
        if(sep == std::string::npos) {
            sep = narrowKey.find('/');
            sepLen = 1;
        }
        if(sep != std::string::npos) {
            // HM1 block (0x6C46BC..0x6C4968): _evalCascadeMap[joined].writeVal.
            // weight seeded 1.0 on first insert (0x6C4964). chainDispatches build
            // split helper + RenderItem/animator passes DEFERRED (no getVariable
            // consumer — the cascade only reads writeVal).
            const ttstr joined = detail::widen(
                narrowKey.substr(0, sep) + "::" + narrowKey.substr(sep + sepLen));
            if(const auto it = _evalCascadeMap.find(joined);
               it == _evalCascadeMap.end()) {
                auto &state = _evalCascadeMap[joined];
                state.weight = 1.0;     // 0x6C4964 first-insert seed
                state.writeVal = value; // 0x6C4968
            } else {
                it->second.writeVal = value;
            }
        }
        // LABEL_132 (0x6C4C0C, green-critical): HM2[rawKey] = value. HM2 is
        // ttstr-keyed (Player+320); use the original ttstr key directly.
        _evalResultValues[key] = value;
    }

    double Player::getVariable(ttstr label) {
        ensureMotionLoaded();
        const auto key = detail::narrow(label);
        if(key.empty()) {
            return 0.0;
        }

        // libkrkr2.so Player_getVariable_wrapper @0x533E1C — 2-branch scope
        // router (M3 / R0-1). inScope → HM1_cascadeJoinAndLookup directly; else
        // evalKey_cascade (HM4-first by raw key) → on miss HM1_cascadeJoinAndLookup.
        const bool inScope = isLabelInBindScopeListLike_0x6CD16C(label);
        if(!inScope) {
            // evalKey_cascade (0x6CD23C): HM4 (@+1240) by raw key → node+16 double.
            if(const auto it = _variableSnapshotMap.find(label);
               it != _variableSnapshotMap.end()) {
                return it->second;
            }
        }
        // HM1_cascadeJoinAndLookup (0x6CD39C): key splittable on "::"/"/" → HM1
        // (_evalCascadeMap[joined].writeVal = node+48); else → HM2
        // (_evalResultValues = node+16). Both populated by bindParameterValue.
        auto sep = key.find("::");
        size_t sepLen = 2;
        if(sep == std::string::npos) {
            sep = key.find('/');
            sepLen = 1;
        }
        if(sep != std::string::npos) {
            const ttstr joined = detail::widen(
                key.substr(0, sep) + "::" + key.substr(sep + sepLen));
            if(const auto it = _evalCascadeMap.find(joined);
               it != _evalCascadeMap.end()) {
                return it->second.writeVal;
            }
            return 0.0;
        }
        // HM2 (Player+320) ttstr-keyed: look up by the original ttstr label.
        if(const auto it = _evalResultValues.find(label);
           it != _evalResultValues.end()) {
            return it->second;
        }
        return 0.0;
        // (The former port-invented PSB frames/ranges fallback is removed — the
        // binary's cascade has no such tail; values now flow var-track → interp →
        // HM4 / bindParameterValue → HM1/HM2. R0-1 RESOLVED for the read path.)
    }

    tTJSVariant Player::getParameterRangeLike_0x6D6590(
        const ttstr &label) {
        // sub_6D6590 seeds the extrema, delegates the recursive walk to
        // sub_6D676C, then returns void unless the interval is non-empty.
        double minValue = std::numeric_limits<double>::max();
        double maxValue = -std::numeric_limits<double>::max();

        const auto foldRangeLike_0x6D676C =
            [&](const auto &self, const Player &current) -> void {
                // sub_6D676C @0x6D67A0 walks Player+384..+392 in 56B steps.
                for(const auto &entry : current._parameterEntries) {
                    if(entry.id != label) {
                        continue;
                    }
                    minValue = std::min(
                        minValue, std::min(entry.rangeBegin, entry.rangeEnd));
                    maxValue = std::max(
                        maxValue, std::max(entry.rangeBegin, entry.rangeEnd));
                }

                // 0x6D6884..0x6D68B0 allocates a callback closure and passes
                // it to Player_visitChildPlayerDispatches @0x6B601C.
                for(const auto &node : current._nodes) {
                    if(node.nodeType == 4) {
                        const int count = node.getParticleCount();
                        for(int i = 0; i < count; ++i) {
                            if(const Player *child = node.getParticleChild(i)) {
                                self(self, *child);
                            }
                        }
                    } else if(node.nodeType == 3) {
                        if(const Player *child = node.getChildPlayer()) {
                            self(self, *child);
                        }
                    }
                }
            };
        foldRangeLike_0x6D676C(foldRangeLike_0x6D676C, *this);

        if(minValue >= maxValue) {
            return {};
        }
        return detail::makeDictionary({
            { "min", minValue },
            { "max", maxValue },
        });
    }


    tjs_error Player::setVariableCompatMethod(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2 || !param[0] || !param[1]) {
            return TJS_E_INVALIDPARAM;
        }

        // Aligned to the raw callback tail merged into libkrkr2.so
        // sub_6D0BF4 (0x6D0E70..0x6D0FB4): args are
        // setVariable(label, value, mode=0), and mode is forwarded as a3 to
        // sub_6C4668. It is not the transition/ease route used by the C++
        // convenience method.
        const auto key = detail::narrow(ttstr(*param[0]));
        const int mode =
            (numparams >= 3 && param[2])
                ? static_cast<int>(param[2]->AsInteger())
                : 0;
        self->writeEvalResultValueLike_0x6C4668(key, mode,
                                                param[1]->AsReal());
        if (self->_engineBack) self->_engineBack->_dirty = true;
        return TJS_S_OK;
    }

} // namespace motion
