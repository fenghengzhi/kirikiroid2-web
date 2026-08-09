// Unit tests for motion::detail::ttstr_hash — verifies the KiriKiri UTF-16
// hash algorithm (1025/9/32769) reverse-engineered from libkrkr2.so. Iteration
// order of motion::Player's embedded unordered_maps depends on this hash
// matching the Android build byte-for-byte; a divergence here will silently
// reorder script-visible enumerations.

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
}

TEST_CASE("ttstr_equal functor: delegates to ttstr operator==",
          "[motionplayer][ttstr_hash]") {
    ttstr a(u"same");
    ttstr b(u"same");
    ttstr c(u"different");
    const ttstr_equal eq{};
    REQUIRE(eq(a, b));
    REQUIRE_FALSE(eq(a, c));
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

TEST_CASE("HeapRef: default empty, frees on destruction, transfers on move",
          "[motionplayer][raii]") {
    using motion::detail::HeapRef;
    {
        HeapRef empty;
        REQUIRE_FALSE(static_cast<bool>(empty));
        REQUIRE(empty.get() == nullptr);
    } // empty.~HeapRef on nullptr must not crash

    HeapRef src(::operator new(32));
    REQUIRE(static_cast<bool>(src));
    void *owned = src.get();

    HeapRef dst(std::move(src));
    REQUIRE(dst.get() == owned);
    REQUIRE(src.get() == nullptr);
    REQUIRE_FALSE(static_cast<bool>(src));

    HeapRef other(::operator new(16));
    void *other_owned = other.get();
    other = std::move(dst);
    REQUIRE(other.get() == owned);
    REQUIRE(dst.get() == nullptr);
    (void)other_owned; // freed by move-assign before taking new pointer
}

TEST_CASE("HeapRef: reset releases current and adopts new",
          "[motionplayer][raii]") {
    using motion::detail::HeapRef;
    HeapRef r(::operator new(8));
    REQUIRE(static_cast<bool>(r));
    r.reset();
    REQUIRE(r.get() == nullptr);
    void *fresh = ::operator new(8);
    r.reset(fresh);
    REQUIRE(r.get() == fresh);
}

TEST_CASE("DispatchRef: default empty, no crash on null destruction",
          "[motionplayer][raii]") {
    using motion::detail::DispatchRef;
    {
        DispatchRef empty;
        REQUIRE_FALSE(static_cast<bool>(empty));
        REQUIRE(empty.get() == nullptr);
    }
}

TEST_CASE("DispatchRef: move transfers and nullifies source",
          "[motionplayer][raii]") {
    using motion::detail::DispatchRef;
    // Without a live iTJSDispatch2 we test only the pointer plumbing using a
    // null transition; the Release call path is exercised by integration
    // tests where a real refcounted dispatch is available.
    DispatchRef src;
    DispatchRef dst(std::move(src));
    REQUIRE(dst.get() == nullptr);
    REQUIRE(src.get() == nullptr);
}

TEST_CASE("PerNodeLayerState: default-construct fields are zero / empty",
          "[motionplayer][value_struct]") {
    using motion::detail::PerNodeLayerState;
    PerNodeLayerState s;
    REQUIRE(s.nodeType == 0);
    REQUIRE(s.contentMask == 0);
    REQUIRE(s.doneFlag == 0);
    REQUIRE(s.blendMode == 16);
    REQUIRE(s.ox == 0.0);
    REQUIRE(s.oy == 0.0);
    REQUIRE(s.opacity == 255);
    REQUIRE(s.coordX == 0.0);
    REQUIRE(s.coordY == 0.0);
    REQUIRE(s.coordZ == 0.0);
    REQUIRE(s.flipX == 0);
    REQUIRE(s.flipY == 0);
    REQUIRE(s.angle == 0.0);
    REQUIRE(s.scaleX == 1.0);
    REQUIRE(s.scaleY == 1.0);
    REQUIRE_FALSE(static_cast<bool>(s.dispatch_8));
    REQUIRE(s.srcValue_44 == ttstr());
    REQUIRE_FALSE(static_cast<bool>(s.dispatch_288));
    REQUIRE_FALSE(static_cast<bool>(s.dispatch_392));
    REQUIRE_FALSE(static_cast<bool>(s.dispatch_504));
    REQUIRE_FALSE(static_cast<bool>(s.heap_320));
    REQUIRE_FALSE(static_cast<bool>(s.heap_584));
    REQUIRE(s.ttstr_188 == ttstr());
    REQUIRE(s.ttstr_688 == ttstr());
}

TEST_CASE("PerNodeLayerState: destructor frees owned heap blocks",
          "[motionplayer][value_struct]") {
    using motion::detail::PerNodeLayerState;
    {
        PerNodeLayerState s;
        s.heap_320.reset(::operator new(64));
        s.heap_584.reset(::operator new(128));
        s.ttstr_188 = ttstr(u"frame.skip.head");
        s.ttstr_516 = ttstr(u"transform.snap");
        s.ttstr_688 = ttstr(u"variable.snap.tail");
        // Verify population took effect before scope exit.
        REQUIRE(static_cast<bool>(s.heap_320));
        REQUIRE(static_cast<bool>(s.heap_584));
        REQUIRE(s.ttstr_516 == ttstr(u"transform.snap"));
    } // ~PerNodeLayerState: HeapRef frees both heap blocks, ttstr Releases.
}

TEST_CASE("PerNodeLayerState: move construction transfers heap ownership",
          "[motionplayer][value_struct]") {
    using motion::detail::PerNodeLayerState;
    PerNodeLayerState src;
    src.heap_320.reset(::operator new(64));
    src.heap_584.reset(::operator new(128));
    src.nodeType = 3;
    src.coordX = 1920.0;
    src.ttstr_560 = ttstr(u"node.path.snap");

    void *h320 = src.heap_320.get();
    void *h584 = src.heap_584.get();

    PerNodeLayerState dst(std::move(src));
    REQUIRE(dst.heap_320.get() == h320);
    REQUIRE(dst.heap_584.get() == h584);
    REQUIRE(src.heap_320.get() == nullptr);
    REQUIRE(src.heap_584.get() == nullptr);
    REQUIRE(dst.nodeType == 3);
    REQUIRE(dst.coordX == 1920.0);
    REQUIRE(dst.ttstr_560 == ttstr(u"node.path.snap"));
}

TEST_CASE("PerNodeLayerStateMap: emplace and lookup by node-path key",
          "[motionplayer][value_struct][container]") {
    using motion::detail::PerNodeLayerState;
    using motion::detail::PerNodeLayerStateMap;
    PerNodeLayerStateMap m;
    {
        PerNodeLayerState s;
        s.nodeType = 4;
        s.flipX = 1;
        s.ttstr_560 = ttstr(u"variable.snap.head");
        s.heap_320.reset(::operator new(48));
        m.emplace(ttstr(u"root/group/leaf"), std::move(s));
    }
    REQUIRE(m.size() == 1);
    auto it = m.find(ttstr(u"root/group/leaf"));
    REQUIRE(it != m.end());
    REQUIRE(it->second.nodeType == 4);
    REQUIRE(it->second.flipX == 1);
    REQUIRE(it->second.ttstr_560 == ttstr(u"variable.snap.head"));
    REQUIRE(static_cast<bool>(it->second.heap_320));
    REQUIRE(m.find(ttstr(u"root/missing")) == m.end());
    // Map dtor releases all PerNodeLayerState entries on scope exit.
}
