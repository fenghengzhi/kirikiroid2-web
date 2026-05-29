// Unit tests for motion::detail::ttstr_hash — verifies the KiriKiri UTF-16
// hash algorithm (1025/9/32769) reverse-engineered from libkrkr2.so. Iteration
// order of motion::Player's embedded unordered_maps depends on this hash
// matching the Android build byte-for-byte; a divergence here will silently
// reorder script-visible enumerations.

#include <catch2/catch_test_macros.hpp>

#include <unordered_map>

#include "tjs.h"
#include "tjsString.h"

#include "motionplayer/internal/player_containers.h"
#include "motionplayer/internal/ttstr_hash.h"

using motion::detail::DispatchAliasMap;
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

TEST_CASE("ttstr_hash_utf16: empty string collapses to -1 sentinel",
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
    REQUIRE(hasher(key) == ttstr_hash_utf16(key.c_str()));
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

TEST_CASE("DispatchAliasMap: stores and retrieves non-owning pointers",
          "[motionplayer][ttstr_hash][container]") {
    DispatchAliasMap m;
    int placeholder_a = 0;
    int placeholder_b = 0;
    auto *fake_a = reinterpret_cast<iTJSDispatch2 *>(&placeholder_a);
    auto *fake_b = reinterpret_cast<iTJSDispatch2 *>(&placeholder_b);

    m[ttstr(u"slot.a")] = fake_a;
    m[ttstr(u"slot.b")] = fake_b;

    REQUIRE(m.size() == 2);
    REQUIRE(m.find(ttstr(u"slot.a"))->second == fake_a);
    REQUIRE(m.find(ttstr(u"slot.b"))->second == fake_b);

    m.erase(ttstr(u"slot.a"));
    REQUIRE(m.find(ttstr(u"slot.a")) == m.end());
    REQUIRE(m.size() == 1);
}

TEST_CASE("EvalCascadeState: default-constructed destructor is safe",
          "[motionplayer][value_struct]") {
    using motion::detail::EvalCascadeState;
    EvalCascadeState s;
    REQUIRE(s.mainDispatch == nullptr);
    REQUIRE(s.chainDispatches.empty());
    REQUIRE(s.heapResult == nullptr);
    // Destructor runs on scope exit; nullptr fast-paths must not crash.
}

TEST_CASE("EvalCascadeState: move construction transfers ownership",
          "[motionplayer][value_struct]") {
    using motion::detail::EvalCascadeState;
    EvalCascadeState src;
    src.heapResult = ::operator new(16);
    src.keyCopy = ttstr(u"cascade.key");
    void *owned = src.heapResult;

    EvalCascadeState dst(std::move(src));
    REQUIRE(dst.heapResult == owned);
    REQUIRE(src.heapResult == nullptr);
    REQUIRE(dst.keyCopy == ttstr(u"cascade.key"));
    // dst destructor will free owned; src destructor is now a no-op for heap.
}

TEST_CASE("EvalCascadeState: move assignment transfers ownership",
          "[motionplayer][value_struct]") {
    using motion::detail::EvalCascadeState;
    EvalCascadeState src;
    src.heapResult = ::operator new(8);
    src.keyCopy = ttstr(u"alpha");
    void *owned = src.heapResult;

    EvalCascadeState dst;
    dst = std::move(src);

    REQUIRE(dst.heapResult == owned);
    REQUIRE(src.heapResult == nullptr);
    REQUIRE(dst.keyCopy == ttstr(u"alpha"));
}

TEST_CASE("EvalCascadeMap: stores EvalCascadeState by ttstr key",
          "[motionplayer][value_struct][container]") {
    using motion::detail::EvalCascadeMap;
    using motion::detail::EvalCascadeState;
    EvalCascadeMap m;
    {
        EvalCascadeState s;
        s.heapResult = ::operator new(8);
        m.emplace(ttstr(u"first"), std::move(s));
    }
    REQUIRE(m.size() == 1);
    REQUIRE(m.find(ttstr(u"first")) != m.end());
    REQUIRE(m.find(ttstr(u"first"))->second.heapResult != nullptr);
    REQUIRE(m.find(ttstr(u"missing")) == m.end());
    // Map destructor releases the EvalCascadeState heap on scope exit.
}

TEST_CASE("VariableLabelScope: default flags are all true",
          "[motionplayer][value_struct]") {
    using motion::detail::VariableLabelScope;
    VariableLabelScope s;
    REQUIRE(s.flagActive);
    REQUIRE(s.flagValidated);
    REQUIRE(s.flagField124);
    REQUIRE(s.cascadeKey == ttstr());
    REQUIRE(s.labelName == ttstr());
    REQUIRE(s.scope == ttstr());
}

TEST_CASE("VariableLabelScopeDeque: append-and-iterate preserves order",
          "[motionplayer][value_struct][container]") {
    using motion::detail::VariableLabelScope;
    using motion::detail::VariableLabelScopeDeque;
    VariableLabelScopeDeque d;
    for (int i = 0; i < 5; ++i) {
        VariableLabelScope s;
        s.cascadeKey = ttstr(u"k") + ttstr(i);
        s.labelName = ttstr(u"label_") + ttstr(i);
        d.push_back(std::move(s));
    }
    REQUIRE(d.size() == 5);
    REQUIRE(d.front().cascadeKey == ttstr(u"k0"));
    REQUIRE(d.back().cascadeKey == ttstr(u"k4"));
    REQUIRE(d[2].labelName == ttstr(u"label_2"));
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
    REQUIRE(s.field_28 == 0);
    REQUIRE(s.field_52 == 0);
    REQUIRE(s.sourceRect_x == 0);
    REQUIRE(s.sourceRect_y == 0);
    REQUIRE(s.sourceRect_w == 0);
    REQUIRE(s.sourceRect_h == 0);
    REQUIRE(s.field_96 == 0);
    REQUIRE(s.qword_120 == 0);
    REQUIRE(s.skipFlag_128 == 0);
    REQUIRE(s.flag_129 == 0);
    REQUIRE(s.qword_168 == 0);
    REQUIRE_FALSE(static_cast<bool>(s.dispatch_8));
    REQUIRE_FALSE(static_cast<bool>(s.dispatch_44));
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
    src.sourceRect_w = 1920;
    src.ttstr_544 = ttstr(u"node.path.snap");

    void *h320 = src.heap_320.get();
    void *h584 = src.heap_584.get();

    PerNodeLayerState dst(std::move(src));
    REQUIRE(dst.heap_320.get() == h320);
    REQUIRE(dst.heap_584.get() == h584);
    REQUIRE(src.heap_320.get() == nullptr);
    REQUIRE(src.heap_584.get() == nullptr);
    REQUIRE(dst.nodeType == 3);
    REQUIRE(dst.sourceRect_w == 1920);
    REQUIRE(dst.ttstr_544 == ttstr(u"node.path.snap"));
}

TEST_CASE("PerNodeLayerStateMap: emplace and lookup by node-path key",
          "[motionplayer][value_struct][container]") {
    using motion::detail::PerNodeLayerState;
    using motion::detail::PerNodeLayerStateMap;
    PerNodeLayerStateMap m;
    {
        PerNodeLayerState s;
        s.nodeType = 4;
        s.skipFlag_128 = 1;
        s.ttstr_672 = ttstr(u"variable.snap.head");
        s.heap_320.reset(::operator new(48));
        m.emplace(ttstr(u"root/group/leaf"), std::move(s));
    }
    REQUIRE(m.size() == 1);
    auto it = m.find(ttstr(u"root/group/leaf"));
    REQUIRE(it != m.end());
    REQUIRE(it->second.nodeType == 4);
    REQUIRE(it->second.skipFlag_128 == 1);
    REQUIRE(it->second.ttstr_672 == ttstr(u"variable.snap.head"));
    REQUIRE(static_cast<bool>(it->second.heap_320));
    REQUIRE(m.find(ttstr(u"root/missing")) == m.end());
    // Map dtor releases all PerNodeLayerState entries on scope exit.
}
