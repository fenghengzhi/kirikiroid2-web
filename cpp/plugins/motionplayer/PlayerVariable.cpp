// PlayerVariable.cpp — variable/eval-result/parameter bindings
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "EmotePlayer.h" // for EmoteEngine (back-pointer deref)
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

    bool parameterIdMatchesLabelLike_0x6D0BF4(
        const motion::detail::MotionParameterEntry &entry,
        const ParameterLabelParts &parts) {
        return !entry.id.empty() &&
            (entry.id == parts.full ||
             (!parts.suffix.empty() && entry.id == parts.suffix));
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

    void bindParameterEntriesLike_0x6C4668(
        std::vector<motion::detail::MotionParameterEntry> &entries,
        const ParameterLabelParts &parts,
        int mode,
        double rawValue) {
        for(auto &entry : entries) {
            if(!parameterIdMatchesLabelLike_0x6D0BF4(entry, parts)) {
                continue;
            }
            entry.value = normalizeParameterValueLike_0x6B1718(entry, rawValue);
            entry.mode = mode;
        }
    }

    std::optional<double> parameterPsbNumberLike_0x6B1718(
        const std::shared_ptr<PSB::IPSBValue> &value) {
        if(auto number = std::dynamic_pointer_cast<PSB::PSBNumber>(value)) {
            switch(number->numberType) {
                case PSB::PSBNumberType::Float:
                    return number->getValue<float>();
                case PSB::PSBNumberType::Double:
                    return number->getValue<double>();
                case PSB::PSBNumberType::Int:
                    return static_cast<double>(number->getValue<int>());
                case PSB::PSBNumberType::Long:
                default:
                    return static_cast<double>(number->getValue<tjs_int64>());
            }
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>(value)) {
            return boolean->value ? 1.0 : 0.0;
        }
        return std::nullopt;
    }

    std::optional<double> parameterDictionaryNumberLike_0x6B1718(
        const std::shared_ptr<const PSB::PSBDictionary> &dic,
        const char *key) {
        if(!dic) {
            return std::nullopt;
        }
        return parameterPsbNumberLike_0x6B1718((*dic)[key]);
    }

    std::string parameterDictionaryStringLike_0x6B1718(
        const std::shared_ptr<const PSB::PSBDictionary> &dic,
        const char *key) {
        if(!dic) {
            return {};
        }
        if(auto str = std::dynamic_pointer_cast<PSB::PSBString>((*dic)[key])) {
            return str->value;
        }
        return {};
    }

    bool parameterDictionaryBoolLike_0x6B1718(
        const std::shared_ptr<const PSB::PSBDictionary> &dic,
        const char *key) {
        if(!dic) {
            return false;
        }
        if(auto boolean = std::dynamic_pointer_cast<PSB::PSBBool>((*dic)[key])) {
            return boolean->value;
        }
        if(auto number = parameterPsbNumberLike_0x6B1718((*dic)[key])) {
            return *number != 0.0;
        }
        return false;
    }

} // anonymous namespace

namespace motion {
    bool Player::shouldMirrorEvalLabelLike_0x67C6B0(const std::string &label) {
        if(!_mirrorEvalEnabled || label.empty() || !_activeMotion) {
            return false;
        }
        const auto &matchList = _activeMotion->mirrorVariableMatchList;
        return std::find(matchList.begin(), matchList.end(), label) !=
               matchList.end();
    }

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
        const std::shared_ptr<const PSB::PSBDictionary> &dic) {
        if(!dic) {
            return nullptr;
        }

        detail::MotionParameterEntry entry;
        entry.id = parameterDictionaryStringLike_0x6B1718(dic, "id");
        entry.discretization =
            parameterDictionaryBoolLike_0x6B1718(dic, "discretization");
        entry.rangeBegin =
            parameterDictionaryNumberLike_0x6B1718(dic, "rangeBegin")
                .value_or(0.0);
        entry.rangeEnd =
            parameterDictionaryNumberLike_0x6B1718(dic, "rangeEnd")
                .value_or(0.0);

        const double range = entry.rangeEnd - entry.rangeBegin;
        double division = 0.0;
        if(const auto explicitDivision =
               parameterDictionaryNumberLike_0x6B1718(dic, "division")) {
            division = *explicitDivision;
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
        const std::shared_ptr<PSB::IPSBValue> &value) {
        if(!value) {
            return false;
        }

        const auto list = std::dynamic_pointer_cast<PSB::PSBList>(value);
        if(!list) {
            return false;
        }

        _parameterEntries.reserve(
            _parameterEntries.size() + list->size());
        for(const auto &item : *list) {
            auto dic = std::dynamic_pointer_cast<PSB::PSBDictionary>(item);
            appendParameterEntryLike_0x6B1718(dic);
        }
        finalizeParameterTableLike_0x6B1ECC();
        return true;
    }

    void Player::finalizeParameterTableLike_0x6B1ECC() {
        if(false) {
            return;
        }

        _parameterEntryById.clear();
        for(size_t i = 0; i < _parameterEntries.size(); ++i) {
            const auto &entry = _parameterEntries[i];
            if(!entry.id.empty()) {
                _parameterEntryById[entry.id] = i;
            }
        }
    }

    double Player::initialParameterRawValueLike_0x6B1ABC(
        const std::string &id) const {
        if(id.empty()) {
            return 0.0;
        }

        const auto parts = splitParameterLabelLike_0x6D0BF4(id);
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
    //                  node.chainDispatches = sub_697D34(scope,"\\"); // DEFERRED
    //                  node.weight = 1.0 }                  (0x3FF.. @0x6c4964)
    //     node.writeVal = a4;                              (0x6c4968)
    //     sub_6B9650(node);  // aux type-3/4 node list      // DEFERRED
    //     ramp node+408 controller lists (type3/type4)      // DEFERRED
    //
    //   [LABEL_132 — HM2, unconditional, 0x6c4c0c]
    //     HM2.upsert(rawLabel) = a4;   <-- green-critical, value-equivalent
    //     ramp a1+408 controller list for rawLabel          // DEFERRED
    //
    // The web port mirrors the HM2 store via the existing _evalResultValues /
    // _evalResultList path (raw label -> double, value-identical to a4) and
    // the parameter-entry binding the differential trace depends on. The HM1
    // cascade is added as additive structure (separate _evalCascadeMap, no
    // feedback into HM2). The chainDispatches build, the aux node list and the
    // controller ramps are DEFERRED with documented binary addresses because
    // their inputs (sub_697D34 TJS-dispatch scope resolution, sub_6B9650 aux
    // walk, and the unpopulated node+408 controller RB-trees) cannot be
    // implemented without guessing and have no consumer in the port
    // (getVariable reads HM2, not HM1).
    void Player::bindParameterValueLike_0x6C4668(const std::string &label,
                                                 int mode,
                                                 double value) {
        if(label.empty()) {
            return;
        }

        const auto parts = splitParameterLabelLike_0x6D0BF4(label);
        bindParameterEntriesLike_0x6C4668(_parameterEntries, parts,
                                          mode, value);

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
            const std::string scopeJoin = scope.empty() ? "::" : scope;
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
                // First insert (0x6c4850): seed key, weight=1.0 (0x6c4964).
                // chainDispatches/aux/ramps DEFERRED (see header comment).
                auto inserted = _evalCascadeMap.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(cascadeKey),
                    std::forward_as_tuple());
                found = inserted.first;
                found->second.keyCopy = cascadeKey;
                found->second.weight = 1.0; // binary node+40 = 1.0
            }
            // node.writeVal = a4 on every bind (binary 0x6c4968, unconditional
            // after the insert branch).
            found->second.writeVal = value;
        }
        // --- end HM1 cascade ---

        for(auto &node : _nodes) {
            if(node.nodeType == 3) {
                if(auto *child = node.getChildPlayer()) {
                    child->bindParameterValueLike_0x6C4668(label, mode, value);
                }
            } else if(node.nodeType == 4) {
                for(int i = 0; i < node.getParticleCount(); ++i) {
                    if(auto *child = node.getParticleChild(i)) {
                        child->bindParameterValueLike_0x6C4668(label, mode,
                                                               value);
                    }
                }
            }
        }
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
            // (sub_697D34) + RenderItem/animator passes DEFERRED (no getVariable
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

    tjs_int Player::countVariables() {
        ensureMotionLoaded();
        return _activeMotion
            ? static_cast<tjs_int>(_activeMotion->variableLabels.size())
            : 0;
    }

    ttstr Player::getVariableLabelAt(tjs_int idx) {
        ensureMotionLoaded();
        if(!_activeMotion || idx < 0 ||
           static_cast<size_t>(idx) >= _activeMotion->variableLabels.size()) {
            return {};
        }
        return detail::widen(_activeMotion->variableLabels[idx]);
    }

    tjs_int Player::countVariableFrameAt(tjs_int idx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0;
        }
        const auto frames = getVariableFrameList(label);
        return getObjectCount(frames);
    }

    ttstr Player::getVariableFrameLabelAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(!_activeMotion) {
            return {};
        }
        const auto it = _activeMotion->variableFrames.find(key);
        if(it == _activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return {};
        }
        return detail::widen(it->second[frameIdx].label);
    }

    double Player::getVariableFrameValueAt(tjs_int idx, tjs_int frameIdx) {
        const auto label = getVariableLabelAt(idx);
        if(label.IsEmpty()) {
            return 0.0;
        }

        const auto key = detail::narrow(label);
        if(!_activeMotion) {
            return 0.0;
        }
        const auto it = _activeMotion->variableFrames.find(key);
        if(it == _activeMotion->variableFrames.end() || frameIdx < 0 ||
           static_cast<size_t>(frameIdx) >= it->second.size()) {
            return 0.0;
        }
        return it->second[frameIdx].value;
    }

    tTJSVariant Player::getVariableRange(ttstr label) {
        ensureMotionLoaded();
        if(!_activeMotion) {
            return {};
        }

        const auto key = detail::narrow(label);
        if(const auto it = _activeMotion->variableRanges.find(key);
           it != _activeMotion->variableRanges.end()) {
            return detail::makeArray(
                { tTJSVariant(it->second.first), tTJSVariant(it->second.second) });
        }
        return {};
    }

    tTJSVariant Player::getVariableFrameList(ttstr label) {
        ensureMotionLoaded();
        if(!_activeMotion) {
            return detail::makeArray({});
        }

        const auto key = detail::narrow(label);
        if(const auto it = _activeMotion->variableFrames.find(key);
           it == _activeMotion->variableFrames.end()) {
            return detail::makeArray({});
        } else {
            std::vector<tTJSVariant> frames;
            for(const auto &frame : it->second) {
                frames.push_back(detail::makeDictionary({
                    { "label", detail::widen(frame.label) },
                    { "frame", frame.value },
                    { "value", frame.value },
                }));
            }
            return detail::makeArray(frames);
        }
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
