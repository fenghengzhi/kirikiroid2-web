// Unit tests for motion::detail::ttstr_hash — verifies the KiriKiri UTF-16
// hash algorithm (1025/9/32769) recovered independently from all four current
// references. Matching hash and Hint-cache behavior is required for native
// bucket selection; final unordered iteration remains STL/ABI-specific.

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>
#include <vector>

#include "tjs.h"
#include "tjsString.h"

#include "motionplayer/internal/player_containers.h"
#include "motionplayer/internal/ttstr_hash.h"

using motion::detail::LabelValueMap;
using motion::detail::ttstr_equal;
using motion::detail::ttstr_hash;
using motion::detail::ttstr_hash_utf16;

namespace {
    constexpr std::uint32_t kEmptySentinel =
        static_cast<std::uint32_t>(-1);

    // Hand-traced hash("a") = U+0061:
    //   acc=0, c=0x61, p++ → mixed=0x61, c=0, acc = 0x18461 ^ (0x18461>>6)
    //     0x18461 >> 6 = 0x611, acc = 0x18461 ^ 0x611 = 0x18270
    //   exit loop, acc *= 9 = 0xD95F0
    //   acc >> 11 = 0x1B2, acc ^ (acc>>11) = 0xD9442
    //   h = (32769 * 0xD9442) & 0xFFFFFFFF = 0x6CA2E9442 & 0xFFFFFFFF
    //     = 0xCA2E9442
    constexpr std::uint32_t kHashOfA = 0xCA2E9442u;
} // namespace

TEST_CASE("ttstr_hash_utf16: raw empty payload uses the nonzero sentinel",
          "[motionplayer][ttstr_hash]") {
    REQUIRE(ttstr_hash_utf16(nullptr) == kEmptySentinel);

    const tjs_char empty[] = {0};
    REQUIRE(ttstr_hash_utf16(empty) == kEmptySentinel);
}

TEST_CASE("ttstr_hash_utf16: matches hand-traced value for 'a'",
          "[motionplayer][ttstr_hash]") {
    const tjs_char a[] = {u'a', 0};
    REQUIRE(ttstr_hash_utf16(a) == kHashOfA);
}

TEST_CASE("ttstr_hash_utf16: deterministic for repeated calls",
          "[motionplayer][ttstr_hash]") {
    const tjs_char foo[] = {u'f', u'o', u'o', 0};
    const std::size_t first = ttstr_hash_utf16(foo);
    const std::size_t second = ttstr_hash_utf16(foo);
    REQUIRE(first == second);
}

TEST_CASE("ttstr_hash_utf16: differs for distinct inputs",
          "[motionplayer][ttstr_hash]") {
    const tjs_char a[] = {u'a', 0};
    const tjs_char b[] = {u'b', 0};
    const tjs_char foo[] = {u'f', u'o', u'o', 0};
    const tjs_char bar[] = {u'b', u'a', u'r', 0};

    REQUIRE(ttstr_hash_utf16(a) != ttstr_hash_utf16(b));
    REQUIRE(ttstr_hash_utf16(foo) != ttstr_hash_utf16(bar));
    REQUIRE(ttstr_hash_utf16(a) != ttstr_hash_utf16(foo));
}

TEST_CASE("ttstr_hash functor: agrees with raw helper on ttstr",
          "[motionplayer][ttstr_hash]") {
    ttstr key(u"hello");
    const ttstr_hash hasher{};
    tjs_uint32 *hint = key.GetHint();
    REQUIRE(hint != nullptr);
    *hint = 0;
    const auto expected = ttstr_hash_utf16(key.c_str());
    REQUIRE(hasher(key) == expected);
    REQUIRE(*hint == expected);

    constexpr tjs_uint32 cached = 0x12345678u;
    *hint = cached;
    REQUIRE(hasher(key) == cached);
}

TEST_CASE("ttstr_hash functor: copied ttstr aliases share the Hint cache",
          "[motionplayer][ttstr_hash]") {
    ttstr original(u"alias");
    ttstr alias = original;
    const ttstr_hash hasher{};

    tjs_uint32 *originalHint = original.GetHint();
    tjs_uint32 *aliasHint = alias.GetHint();
    REQUIRE(originalHint != nullptr);
    REQUIRE(aliasHint == originalHint);

    *originalHint = 0;
    const auto expected = static_cast<tjs_uint32>(
        ttstr_hash_utf16(original.c_str()));
    REQUIRE(hasher(alias) == expected);
    REQUIRE(*originalHint == expected);

    constexpr tjs_uint32 externallySeeded = 0x89ABCDEFu;
    *aliasHint = externallySeeded;
    REQUIRE(hasher(original) == externallySeeded);
}

TEST_CASE("ttstr_hash functor: null ttstr hashes to zero without a Hint",
          "[motionplayer][ttstr_hash]") {
    ttstr key;
    const ttstr_hash hasher{};
    REQUIRE(key.GetHint() == nullptr);
    REQUIRE(hasher(key) == 0);
}

TEST_CASE("ttstr_hash functor: allocated empty payload caches the sentinel",
          "[motionplayer][ttstr_hash]") {
    ttstr key(TJS::tTJSStringBufferLength(0));
    const ttstr_hash hasher{};
    tjs_uint32 *hint = key.GetHint();
    REQUIRE(hint != nullptr);
    REQUIRE(*hint == 0);
    REQUIRE(hasher(key) == kEmptySentinel);
    REQUIRE(*hint == kEmptySentinel);
}

TEST_CASE("ttstr_hash functor: agrees with raw helper on c-string overload",
          "[motionplayer][ttstr_hash]") {
    const tjs_char raw[] = {u'h', u'e', u'l', u'l', u'o', 0};
    const ttstr_hash hasher{};
    REQUIRE(hasher(raw) == ttstr_hash_utf16(raw));
    REQUIRE(hasher(static_cast<const tjs_char *>(nullptr)) == kEmptySentinel);
}

TEST_CASE("ttstr_equal functor: delegates to ttstr operator==",
          "[motionplayer][ttstr_hash]") {
    ttstr a(u"same");
    ttstr b(u"same");
    ttstr c(u"different");
    ttstr sameLengthDifferent(u"samp");
    const ttstr_equal eq{};
    REQUIRE(eq(a, b));
    REQUIRE_FALSE(eq(a, c));
    REQUIRE_FALSE(eq(a, sameLengthDifferent));
}

TEST_CASE("ttstr_equal functor: null backing and allocated empty are distinct",
          "[motionplayer][ttstr_hash]") {
    ttstr nullA;
    ttstr nullB;
    ttstr emptyA(TJS::tTJSStringBufferLength(0));
    ttstr emptyB(TJS::tTJSStringBufferLength(0));
    ttstr emptyAlias = emptyA;
    const ttstr_equal eq{};

    REQUIRE(nullA.GetHint() == nullptr);
    REQUIRE(nullB.GetHint() == nullptr);
    REQUIRE(emptyA.GetHint() != nullptr);
    REQUIRE(emptyB.GetHint() != nullptr);
    REQUIRE(emptyA.GetHint() != emptyB.GetHint());
    REQUIRE(emptyAlias.GetHint() == emptyA.GetHint());

    REQUIRE(eq(nullA, nullB));
    REQUIRE(eq(emptyA, emptyAlias));
    REQUIRE(eq(emptyA, emptyB));
    REQUIRE_FALSE(eq(nullA, emptyA));
    REQUIRE_FALSE(eq(emptyA, nullA));
}

TEST_CASE("LabelValueMap: stores and retrieves double values",
          "[motionplayer][ttstr_hash][container]") {
    LabelValueMap m;
    m[ttstr(u"alpha")] = 1.5;
    m[ttstr(u"beta")] = -2.0;
    m[ttstr(u"gamma")] = 0.0;

    REQUIRE(m.size() == 3);
    REQUIRE(m.find(ttstr(u"alpha")) != m.end());
    REQUIRE(m.find(ttstr(u"alpha"))->second == 1.5);
    REQUIRE(m.find(ttstr(u"beta"))->second == -2.0);
    REQUIRE(m.find(ttstr(u"gamma"))->second == 0.0);
    REQUIRE(m.find(ttstr(u"missing")) == m.end());
}

TEST_CASE("LabelValueMap: null and allocated-empty ttstr are distinct keys",
          "[motionplayer][ttstr_hash][container]") {
    LabelValueMap m;
    ttstr nullKey;
    ttstr emptyA(TJS::tTJSStringBufferLength(0));
    ttstr emptyB(TJS::tTJSStringBufferLength(0));

    m[nullKey] = 1.0;
    m[emptyA] = 2.0;
    REQUIRE(m.size() == 2);
    REQUIRE(m.find(nullKey) != m.end());
    REQUIRE(m.find(nullKey)->second == 1.0);
    REQUIRE(m.find(emptyA) != m.end());
    REQUIRE(m.find(emptyA)->second == 2.0);

    // Independently allocated empty backings compare equal and hash to the
    // same UINT32_MAX sentinel, so this updates the existing empty-key node.
    m[emptyB] = 3.0;
    REQUIRE(m.size() == 2);
    REQUIRE(m.find(emptyA)->second == 3.0);
    REQUIRE(m.find(emptyB)->second == 3.0);
}

TEST_CASE("EvalCascadeState: default-constructed destructor is safe",
          "[motionplayer][value_struct]") {
    using motion::detail::EvalCascadeState;
    EvalCascadeState s;
    REQUIRE(s.keyCopy == ttstr());
    REQUIRE(s.chainSegments.empty());
    REQUIRE(s.writeVal == 0.0);
    REQUIRE(s.weight == 0.0);
    REQUIRE(s.heapResult.empty());
}

TEST_CASE("EvalCascadeState: move construction transfers vectors",
          "[motionplayer][value_struct]") {
    using motion::detail::EvalCascadeState;
    int placeholder = 0;
    auto *fakeNode = reinterpret_cast<motion::detail::MotionNode *>(&placeholder);
    EvalCascadeState src;
    src.keyCopy = ttstr(u"cascade.key");
    src.chainSegments.push_back(ttstr(u"scope"));
    src.heapResult.push_back(fakeNode);

    EvalCascadeState dst(std::move(src));
    REQUIRE(dst.keyCopy == ttstr(u"cascade.key"));
    REQUIRE(dst.chainSegments == std::vector<ttstr>{ttstr(u"scope")});
    REQUIRE(dst.heapResult ==
            std::vector<motion::detail::MotionNode *>{fakeNode});
}

TEST_CASE("EvalCascadeState: move assignment transfers vectors",
          "[motionplayer][value_struct]") {
    using motion::detail::EvalCascadeState;
    int placeholder = 0;
    auto *fakeNode = reinterpret_cast<motion::detail::MotionNode *>(&placeholder);
    EvalCascadeState src;
    src.keyCopy = ttstr(u"alpha");
    src.chainSegments.push_back(ttstr(u"alpha"));
    src.heapResult.push_back(fakeNode);

    EvalCascadeState dst;
    dst = std::move(src);

    REQUIRE(dst.keyCopy == ttstr(u"alpha"));
    REQUIRE(dst.chainSegments == std::vector<ttstr>{ttstr(u"alpha")});
    REQUIRE(dst.heapResult ==
            std::vector<motion::detail::MotionNode *>{fakeNode});
}

TEST_CASE("EvalCascadeMap: stores EvalCascadeState by ttstr key",
          "[motionplayer][value_struct][container]") {
    using motion::detail::EvalCascadeMap;
    using motion::detail::EvalCascadeState;
    EvalCascadeMap m;
    {
        EvalCascadeState s;
        int placeholder = 0;
        s.heapResult.push_back(
            reinterpret_cast<motion::detail::MotionNode *>(&placeholder));
        m.emplace(ttstr(u"first"), std::move(s));
    }
    REQUIRE(m.size() == 1);
    REQUIRE(m.find(ttstr(u"first")) != m.end());
    REQUIRE(m.find(ttstr(u"first"))->second.heapResult.size() == 1);
    REQUIRE(m.find(ttstr(u"missing")) == m.end());
    // MotionNode pointers are non-owning; destruction only frees vector backing.
}

TEST_CASE("VariableLabelScope: default track state matches binary seeds",
          "[motionplayer][value_struct]") {
    using motion::detail::VariableLabelScope;
    VariableLabelScope s;
    REQUIRE(s.cascadeKey == ttstr());
    REQUIRE(s.activeSlotCursor == 0);
    REQUIRE(s.value == 0.0);
    REQUIRE(s.frameSource.Type() == tvtVoid);
    REQUIRE(s.slot[0].typeZeroFlag);
    REQUIRE(s.slot[1].typeZeroFlag);
    REQUIRE_FALSE(s.slot[0].merged);
    REQUIRE_FALSE(s.slot[1].merged);
}

TEST_CASE("VariableLabelScopeDeque: append-and-iterate preserves order",
          "[motionplayer][value_struct][container]") {
    using motion::detail::VariableLabelScope;
    using motion::detail::VariableLabelScopeDeque;
    VariableLabelScopeDeque d;
    for (int i = 0; i < 5; ++i) {
        VariableLabelScope s;
        s.cascadeKey = ttstr(u"k") + ttstr(i);
        s.value = static_cast<double>(i);
        d.push_back(std::move(s));
    }
    REQUIRE(d.size() == 5);
    REQUIRE(d.front().cascadeKey == ttstr(u"k0"));
    REQUIRE(d.back().cascadeKey == ttstr(u"k4"));
    REQUIRE(d[2].value == 2.0);
}

TEST_CASE("PerNodeLayerState: map insertion starts with a raw-zero ClipSlot",
          "[motionplayer][value_struct]") {
    using motion::detail::PerNodeLayerState;
    PerNodeLayerState s;
    const auto &slot = s.clipSlot;
    REQUIRE(s.nodeType == 0);
    REQUIRE(slot.frameIndex == 0);
    REQUIRE(slot.clipStartTime == 0.0);
    REQUIRE(slot.ti == 0);
    REQUIRE(slot.contentMask == 0);
    REQUIRE_FALSE(slot.done);
    REQUIRE_FALSE(slot.crossfading);
    REQUIRE_FALSE(slot.merged);
    REQUIRE(slot.iconValue.IsEmpty());
    REQUIRE(slot.srcValue.IsEmpty());
    REQUIRE(slot.blendMode == 0);
    REQUIRE(slot.ox == 0.0);
    REQUIRE(slot.oy == 0.0);
    for(const auto color : slot.packedColors) {
        REQUIRE(color == 0);
    }
    REQUIRE(slot.opacity == 0);
    REQUIRE(slot.x == 0.0);
    REQUIRE(slot.y == 0.0);
    REQUIRE(slot.z == 0.0);
    REQUIRE_FALSE(slot.flipX);
    REQUIRE_FALSE(slot.flipY);
    REQUIRE(slot.angle == 0.0);
    REQUIRE(slot.scaleX == 0.0);
    REQUIRE(slot.scaleY == 0.0);
    REQUIRE(slot.slantX == 0.0);
    REQUIRE(slot.slantY == 0.0);
    REQUIRE(slot.cccVariant.Type() == tvtVoid);
    REQUIRE(slot.occVariant.Type() == tvtVoid);
    REQUIRE(slot.accVariant.Type() == tvtVoid);
    REQUIRE(slot.zccVariant.Type() == tvtVoid);
    REQUIRE(slot.sccVariant.Type() == tvtVoid);
    REQUIRE(slot.cpVariant.Type() == tvtVoid);
    REQUIRE(slot.actionValue.IsEmpty());
    REQUIRE(slot.meshCurveVariant.Type() == tvtVoid);
    REQUIRE(slot.meshControlPoints.empty());
    REQUIRE(slot.motionDtgtValue.IsEmpty());
    REQUIRE(slot.modelDtgt.IsEmpty());
    REQUIRE(slot.prtFmin == 0.0);
    REQUIRE(slot.prtF == 0.0);
    REQUIRE(slot.prtZmin == 0.0);
    REQUIRE(slot.prtZ == 0.0);
    REQUIRE(slot.cameraTarget.IsEmpty());
    REQUIRE(slot.anchorTarget.IsEmpty());
    REQUIRE(s.childPlayerSnapshot.Type() == tvtVoid);
    REQUIRE(s.meshControlPoints.empty());
    for(const auto value : s.particleInterp) {
        REQUIRE(value == 0.0);
    }
    REQUIRE(s.particleArraySnapshot.Type() == tvtVoid);
}

TEST_CASE("PerNodeLayerState: embedded ClipSlot owns the recovered payloads",
          "[motionplayer][value_struct]") {
    using motion::detail::PerNodeLayerState;
    {
        PerNodeLayerState s;
        s.clipSlot.iconValue = ttstr(u"icon");
        s.clipSlot.srcValue = ttstr(u"src");
        s.clipSlot.cccVariant = tTJSVariant(ttstr(u"curve"));
        s.clipSlot.actionValue = ttstr(u"action");
        s.clipSlot.meshControlPoints = {{1.0f, 2.0f}};
        s.clipSlot.motionDtgtValue = ttstr(u"motion-target");
        s.clipSlot.modelDtgt = ttstr(u"model-target");
        s.clipSlot.cameraTarget = ttstr(u"camera-target");
        s.clipSlot.anchorTarget = ttstr(u"anchor-target");
        s.childPlayerSnapshot = tTJSVariant(ttstr(u"child"));
        s.meshControlPoints = {{3.0f, 4.0f}};
        s.particleArraySnapshot = tTJSVariant(ttstr(u"particles"));
        REQUIRE(s.clipSlot.meshControlPoints.size() == 1);
        REQUIRE(s.meshControlPoints.size() == 1);
    }
}

TEST_CASE("PerNodeLayerState: move construction transfers both mesh vectors",
          "[motionplayer][value_struct]") {
    using motion::detail::PerNodeLayerState;
    PerNodeLayerState src;
    src.nodeType = 3;
    src.clipSlot.x = 1920.0;
    src.clipSlot.srcValue = ttstr(u"node.path.snap");
    src.clipSlot.meshControlPoints = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    src.meshControlPoints = {{5.0f, 6.0f}, {7.0f, 8.0f}};
    const auto *slotMesh = src.clipSlot.meshControlPoints.data();
    const auto *outerMesh = src.meshControlPoints.data();

    PerNodeLayerState dst(std::move(src));
    REQUIRE(dst.nodeType == 3);
    REQUIRE(dst.clipSlot.x == 1920.0);
    REQUIRE(dst.clipSlot.srcValue == ttstr(u"node.path.snap"));
    REQUIRE(dst.clipSlot.meshControlPoints.data() == slotMesh);
    REQUIRE(dst.meshControlPoints.data() == outerMesh);
}

TEST_CASE("PerNodeLayerStateMap: emplace and lookup by node-path key",
          "[motionplayer][value_struct][container]") {
    using motion::detail::PerNodeLayerState;
    using motion::detail::PerNodeLayerStateMap;
    PerNodeLayerStateMap m;
    {
        PerNodeLayerState s;
        s.nodeType = 4;
        s.clipSlot.flipX = true;
        s.clipSlot.srcValue = ttstr(u"variable.snap.head");
        s.meshControlPoints = {{1.0f, 2.0f}};
        m.emplace(ttstr(u"root/group/leaf"), std::move(s));
    }
    REQUIRE(m.size() == 1);
    auto it = m.find(ttstr(u"root/group/leaf"));
    REQUIRE(it != m.end());
    REQUIRE(it->second.nodeType == 4);
    REQUIRE(it->second.clipSlot.flipX);
    REQUIRE(it->second.clipSlot.srcValue == ttstr(u"variable.snap.head"));
    REQUIRE(it->second.meshControlPoints.size() == 1);
    REQUIRE(m.find(ttstr(u"root/missing")) == m.end());
    // Map dtor releases all PerNodeLayerState entries on scope exit.
}
