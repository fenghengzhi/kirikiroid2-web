// PlayerVariable.cpp — variable/eval-result/parameter bindings
// Split out for maintainability.
//
#include "PlayerInternal.h"
#include "MotionDispatch.h"
#include "ncbind.hpp"

using namespace motion::internal;

namespace {
    tjs_uint32 playerRangeMinHint_guess = 0;
    tjs_uint32 playerRangeMaxHint_guess = 0;

    struct ParameterLabelParts {
        ttstr scope;
        ttstr suffix;
        bool split = false;
    };

    ParameterLabelParts splitParameterLabel_guess(const ttstr &label) {
        ParameterLabelParts parts;
        const int scopePos = label.IndexOf(TJS_W("::"));
        if(scopePos >= 0) {
            parts.scope = label.SubString(0, static_cast<unsigned>(scopePos));
            parts.suffix = label.SubString(
                static_cast<unsigned>(scopePos + 2),
                static_cast<unsigned>(-1));
            parts.split = true;
            return parts;
        }
        const int slashPos = label.IndexOf(TJS_W('/'));
        if(slashPos >= 0) {
            parts.scope = label.SubString(0, static_cast<unsigned>(slashPos));
            parts.suffix = label.SubString(
                static_cast<unsigned>(slashPos + 1),
                static_cast<unsigned>(-1));
            parts.split = true;
        }
        return parts;
    }

    // Shared binder tail: every equal-key parameter is updated, including
    // duplicate ids retained by the native multimap.
    void applyParameterRamps_guess(
        motion::detail::ParameterRampMap &rampMap,
        const ttstr &key,
        int mode,
        double rawValue) {
        const auto range = rampMap.equal_range(key);
        for(auto it = range.first; it != range.second; ++it) {
            motion::detail::MotionParameterEntry *entry = it->second;
            entry->mode = mode;
            normalizeParameterValue_guess(*entry, rawValue);
        }
    }

} // anonymous namespace

namespace motion::internal {

    namespace {

        std::int32_t parameterSignedInt32TowardZeroSaturated_guess(
            double value) noexcept {
            constexpr double lower = -0x1p31;
            constexpr double upper = 0x1p31;
            if(std::isnan(value)) {
                return 0;
            }
            if(value >= upper) {
                return std::numeric_limits<std::int32_t>::max();
            }
            if(value <= lower) {
                return std::numeric_limits<std::int32_t>::min();
            }
            return static_cast<std::int32_t>(value);
        }

    } // anonymous namespace

    void normalizeParameterValue_guess(
        detail::MotionParameterEntry &entry, double rawValue) noexcept {
        // All four references compare the endpoints themselves. Computing the
        // difference first would turn equal infinities into NaN and miss this
        // exact-equality reset path.
        if(entry.rangeBegin == entry.rangeEnd || entry.division <= 0.0) {
            entry.value = 0.0;
            return;
        }

        double value = entry.discretization
            ? static_cast<double>(
                  parameterSignedInt32TowardZeroSaturated_guess(rawValue))
            : rawValue;

        // Operand order matches std::min(begin,end), std::max(begin,end), then
        // std::clamp(value,lo,hi). Ordered comparisons preserve raw NaN and the
        // first endpoint for equal signed-zero endpoints.
        const double lo = entry.rangeEnd < entry.rangeBegin
            ? entry.rangeEnd : entry.rangeBegin;
        const double hi = entry.rangeBegin < entry.rangeEnd
            ? entry.rangeEnd : entry.rangeBegin;
        if(value < lo) {
            value = lo;
        } else if(hi < value) {
            value = hi;
        }

        entry.value = entry.division * (value - entry.rangeBegin) /
            (entry.rangeEnd - entry.rangeBegin);
    }

} // namespace motion::internal

namespace motion {
    void Player::appendParameterEntry_guess(
        const tTJSVariant &parameter) {
        if(parameter.Type() != tvtObject) {
            return;
        }

        ncbPropAccessor parameterObject{tTJSVariant(parameter)};

        // The native routine grows the vector before the first property read.
        // Consequently a property exception leaves this in-place, partially
        // populated record in the vector.
        _parameterEntries.emplace_back();
        detail::MotionParameterEntry &entry = _parameterEntries.back();
        entry.division = 0.0;
        entry.id = parameterObject.GetValue(
            TJS_W("id"), ncbTypedefs::Tag<ttstr>(), 0,
            &detail::commandIdMemberHint_guess);
        entry.discretization = parameterObject.GetValue(
            TJS_W("discretization"), ncbTypedefs::Tag<bool>(), 0,
            &detail::playerParameterDiscretizationHint_guess);
        entry.rangeBegin = parameterObject.GetValue(
            TJS_W("rangeBegin"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::playerParameterRangeBeginHint_guess);
        entry.rangeEnd = parameterObject.GetValue(
            TJS_W("rangeEnd"), ncbTypedefs::Tag<tjs_real>(), 0,
            &detail::playerParameterRangeEndHint_guess);

        const double range = entry.rangeEnd - entry.rangeBegin;
        double division = 0.0;
        tTJSVariant divisionValue;
        if(parameterObject.checkVariant(TJS_W("division"), divisionValue)) {
            division = divisionValue.AsReal();
        } else {
            division = range;
            if(division <= 0.0) {
                division = 1.0;
            }
        }
        entry.division = division;
        normalizeParameterValue_guess(
            entry, readInitialParameterValue_guess(entry.id));
    }

    bool Player::parseParameterList_guess(
        const tTJSVariant &parameters) {
        if(parameters.Type() == tvtVoid) {
            return false;
        }

        ncbPropAccessor parameterList{tTJSVariant(parameters)};
        const tjs_int count = parameterList.GetArrayCount();
        for(tjs_int index = 0; index < count; ++index) {
            const tTJSVariant parameter = parameterList.GetValue(
                index, ncbTypedefs::Tag<tTJSVariant>(), 0);
            appendParameterEntry_guess(parameter);
        }
        finalizeParameterTable_guess();
        return true;
    }

    void Player::finalizeParameterTable_guess() {
        for(Player *destination = this; destination != nullptr;
            destination = destination->_parentPlayer) {
            for(auto &entry : _parameterEntries) {
                // Empty and duplicate ids are both native-visible nodes.
                destination->_parameterRampMap.emplace(entry.id, &entry);
            }
        }
    }

    void Player::purgeParameterRampMap_guess() {
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

    // Rebuild one HM1 entry's non-owning type-3/type-4 child-node cache. All four
    // references keep `chain` across candidate nodes: each ancestor label is
    // inserted at the front and an overlong tail is discarded. Do not clear it
    // inside the outer loop. An empty reference chain consequently matches every
    // eligible node. Only an exact zero weight skips the rebuild; NaN runs it.
    void Player::rebuildEvalCascadeEntry_guess(
        detail::EvalCascadeState &entry) {
        if(entry.weight == 0.0) {
            return;
        }
        entry.weight = 0.0;
        entry.heapResult.clear(); // Preserve backing capacity.

        const std::vector<ttstr> &ref = entry.chainSegments;
        std::vector<ttstr> chain;

        // Index zero is the root. There is no trailing sentinel.
        for(std::size_t scanNodeIndex = 1; scanNodeIndex < _nodes.size();
            ++scanNodeIndex) {
            detail::MotionNode &scanNode = _nodes[scanNodeIndex];
            if(static_cast<unsigned>(scanNode.nodeType - 3) > 1u) {
                continue;
            }
            std::size_t idx = scanNodeIndex;
            bool matched = false;
            for(;;) {
                const std::size_t chainNodeIndex = idx;
                chain.insert(chain.begin(), _nodes[idx].layerName);
                if(chain.size() > ref.size() && !chain.empty()) {
                    chain.pop_back();
                }
                if(chain.size() == ref.size()) {
                    if(ref.empty()) {
                        matched = true;
                        break;
                    }
                    bool allEqual = true;
                    for(std::size_t j = 0; j < ref.size(); ++j) {
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
                const int parentIndex = _nodes[chainNodeIndex].parentIndex;
                if(parentIndex <= 0) {
                    break;
                }
                idx = static_cast<std::size_t>(parentIndex);
            }
            if(matched) {
                entry.heapResult.push_back(&scanNode);
            }
        }
    }

    double Player::readInitialParameterValue_guess(const ttstr &id) const {
        const ttstr cascadeSuffix = TJS_W("::") + id;
        for(const Player *current = this; current != nullptr;
            current = current->_parentPlayer) {
            if(const auto direct = current->_evalResultValues.find(id);
               direct != current->_evalResultValues.end()) {
                return direct->second;
            }

            const Player *parent = current->_parentPlayer;
            if(parent == nullptr) {
                break;
            }
            for(const auto &item : parent->_evalCascadeMap) {
                if(item.first.IndexOf(cascadeSuffix) < 0) {
                    continue;
                }
                const detail::EvalCascadeState &state = item.second;
                for(const detail::MotionNode *node : state.heapResult) {
                    if(node->nodeType == 3) {
                        if(node->getChildPlayer() == this) {
                            return state.writeVal;
                        }
                    } else if(node->nodeType == 4) {
                        const int count = node->getParticleCount();
                        for(int index = 0; index < count; ++index) {
                            if(node->getParticleChild(index) == this) {
                                return state.writeVal;
                            }
                        }
                    }
                }
            }
        }
        return 0.0;
    }

    void Player::bindParameterValue_guess(const ttstr &key, int mode,
        double value) {
        const auto parts = splitParameterLabel_guess(key);
        if(parts.split) {
            const ttstr cascadeKey =
                parts.scope + TJS_W("::") + parts.suffix;
            auto found = _evalCascadeMap.find(cascadeKey);
            if(found == _evalCascadeMap.end()) {
                auto inserted = _evalCascadeMap.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(cascadeKey),
                    std::forward_as_tuple());
                found = inserted.first;
                found->second.keyCopy = cascadeKey;
                found->second.chainSegments =
                    detail::splitTtstr_guess(parts.scope, TJS_W('/'));
                found->second.weight = 1.0;
            }
            found->second.writeVal = value;

            detail::EvalCascadeState &entry = found->second;
            rebuildEvalCascadeEntry_guess(entry);

            const ttstr &suffixKey = parts.suffix;
            for(detail::MotionNode *node : entry.heapResult) {
                if(node->nodeType == 4) {
                    for(int i = 0; i < node->getParticleCount(); ++i) {
                        Player *child = node->getParticleChild(i);
                        applyParameterRamps_guess(
                            child->_parameterRampMap, suffixKey, mode, value);
                    }
                } else if(node->nodeType == 3) {
                    Player *child = node->getChildPlayer();
                    applyParameterRamps_guess(
                        child->_parameterRampMap, suffixKey, mode, value);
                }
            }
        }

        _evalResultValues[key] = value;
        applyParameterRamps_guess(_parameterRampMap, key, mode, value);
    }

    bool Player::hasVariableLabelScope_guess(ttstr key) const {
        for(const auto &item : _variableLabelScopes) {
            if(item.cascadeKey == key) {
                return true;
            }
        }
        return false;
    }

    double Player::getVariable(ttstr label) {
        // The Motion.Player surface is the direct bound-value reader. A
        // splittable label reads HM1; an unsplit label reads HM2.
        const auto parts = splitParameterLabel_guess(label);
        if(parts.split) {
            const ttstr joined =
                parts.scope + TJS_W("::") + parts.suffix;
            if(const auto it = _evalCascadeMap.find(joined);
               it != _evalCascadeMap.end()) {
                return it->second.writeVal;
            }
            return 0.0;
        }
        // HM2 is ttstr-keyed: look up by the original label without narrowing.
        if(const auto it = _evalResultValues.find(label);
           it != _evalResultValues.end()) {
            return it->second;
        }
        return 0.0;
    }

    double Player::readSnapshotOrBoundParameterValue_guess(ttstr label) {
        if(const auto it = _variableSnapshotMap.find(label);
           it != _variableSnapshotMap.end()) {
            return it->second;
        }
        return getVariable(label);
    }

    void Player::foldVariableRangeRecursive_guess(
        const ttstr &label, double &minValue, double &maxValue) {
        for(const auto &entry : _parameterEntries) {
            if(entry.id != label) {
                continue;
            }
            // The endpoint selectors keep rangeBegin on equality/unordered.
            // The outer accumulators deliberately do the opposite: the new
            // candidate replaces the prior value on equality or unordered.
            // That distinction controls both signed-zero identity and whether
            // a later NaN interval invalidates the final range.
            const double entryMin = entry.rangeEnd < entry.rangeBegin
                ? entry.rangeEnd : entry.rangeBegin;
            minValue = minValue < entryMin ? minValue : entryMin;
            const double entryMax = entry.rangeBegin < entry.rangeEnd
                ? entry.rangeEnd : entry.rangeBegin;
            maxValue = entryMax < maxValue ? maxValue : entryMax;
        }

        // The native recursive folder reuses the same visitor as layer lookup
        // and contains().  The visitor can yield nullptr when a child dispatch
        // cannot be resolved; this callback ignores that item and always asks
        // the visitor to continue.
        visitChildPlayerDispatches_guess([&](Player *child) {
            if(child != nullptr) {
                child->foldVariableRangeRecursive_guess(
                    label, minValue, maxValue);
            }
            return true;
        });
    }

    tTJSVariant Player::getVariableRange_guess(ttstr label) {
        double minValue = std::numeric_limits<double>::max();
        double maxValue = -std::numeric_limits<double>::max();
        foldVariableRangeRecursive_guess(label, minValue, maxValue);

        // Native publishes a Dictionary only for ordered min < max.  Writing
        // this as min >= max would accidentally accept unordered extrema.
        if(!(minValue < maxValue)) {
            return {};
        }

        iTJSDispatch2 *dispatch = TJSCreateDictionaryObject();
        tTJSVariant result(dispatch, dispatch);
        dispatch->Release();

        tTJSVariant objectValue(result);
        objectValue.ToObject();
        ncbPropAccessor object(objectValue);
        objectValue.Clear();

        (void)object.SetValue(
            TJS_W("min"), minValue, TJS_MEMBERENSURE,
            &playerRangeMinHint_guess);
        (void)object.SetValue(
            TJS_W("max"), maxValue, TJS_MEMBERENSURE,
            &playerRangeMaxHint_guess);
        return result;
    }


    tjs_error Player::setVariableCompatMethod(tTJSVariant *, tjs_int numparams,
                                              tTJSVariant **param,
                                              iTJSDispatch2 *objthis) {
        auto *self = ncbInstanceAdaptor<Player>::GetNativeInstance(objthis, true);
        if(!self) {
            return TJS_E_INVALIDOBJECT;
        }
        if(numparams < 2) {
            return TJS_E_BADPARAMCOUNT;
        }

        const ttstr key(*param[0]);
        const int mode = numparams == 2
            ? 0
            : static_cast<int>(param[2]->AsInteger());
        self->bindParameterValue_guess(key, mode, param[1]->AsReal());
        return TJS_S_OK;
    }

} // namespace motion
