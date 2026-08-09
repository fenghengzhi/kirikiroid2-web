//
// Created by LiDon on 2025/9/16.
//
#include <catch2/catch_test_macros.hpp>

#include "tjsString.h"

TEST_CASE("string to tjsString") {
    using namespace TJS;
    SECTION("Japanese convert") {
        auto v1 = "レイヤー 1";
        auto v1_utf8 = u8"レイヤー 1";
        // auto v1_cp932 = boost::locale::conv::from_utf(v1_utf8, "cp932");
        // auto v1_shift_jis = boost::locale::conv::from_utf(v1_utf8,
        // "SHIFT_JIS");
        auto v1_tjs = ttstr{ v1 };
        REQUIRE(v1_tjs == v1);
        REQUIRE(v1_tjs == v1_utf8);
        REQUIRE(v1_tjs == v1_tjs);
        REQUIRE(v1_tjs.AsStdString() == v1);
    }
}

TEST_CASE("tTJSString character IndexOf") {
    using namespace TJS;

    const ttstr value{ TJS_W("a/b/a") };
    CHECK(value.IndexOf(TJS_W('a')) == 0);
    CHECK(value.IndexOf(TJS_W('/')) == 1);
    CHECK(value.IndexOf(TJS_W('a'), 1) == 4);
    CHECK(value.IndexOf(TJS_W('a'), 5) == -1);
    CHECK(value.IndexOf(TJS_W('z')) == -1);
    CHECK(value.IndexOf(TJS_W('\0')) == -1);

    const ttstr empty;
    CHECK(empty.IndexOf(TJS_W('/')) == -1);
    CHECK(value == TJS_W("a/b/a"));
}
