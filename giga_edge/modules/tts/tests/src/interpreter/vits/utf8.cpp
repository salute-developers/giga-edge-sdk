#include "giga_edge/modules/tts/interpreter/backends/vits/utf8.h"

#include "giga_edge/modules/tts/interpreter/backends/vits/encoding.h"

#include <catch.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

using namespace giga_edge::vits;
using Catch::Matchers::Contains;

TEST_CASE("vits utf8: code points, boundaries and malformed text", "[tts][vits]") {
  const std::string text = "aё€😀";
  CHECK(DecodeUtf8(text, 0).value == U'a');
  CHECK(DecodeUtf8(text, 0).size == 1);
  CHECK(DecodeUtf8(text, 1).value == U'ё');
  CHECK(DecodeUtf8(text, 1).size == 2);
  CHECK(DecodeUtf8(text, 3).value == U'€');
  CHECK(DecodeUtf8(text, 3).size == 3);
  CHECK(DecodeUtf8(text, 6).value == U'😀');
  CHECK(DecodeUtf8(text, 6).size == 4);
  CHECK(IsUtf8Boundary(text, 1));
  CHECK_FALSE(IsUtf8Boundary(text, 2));
  CHECK(IsUtf8Boundary(text, text.size()));
  CHECK_FALSE(IsUtf8Boundary(text, text.size() + 1));

  // Nothing to decode: an empty text, the end, past the end.
  CHECK_THROWS_WITH(DecodeUtf8("", 0), Contains("no UTF-8 code point at byte 0"));
  CHECK_THROWS_WITH(DecodeUtf8(text, text.size()), Contains("no UTF-8 code point at byte 10"));
  CHECK_THROWS_WITH(DecodeUtf8(text, text.size() + 5), Contains("no UTF-8 code point"));
  // A continuation byte, a cut sequence, overlong, a surrogate, past U+10FFFF, a bad lead byte.
  for (const auto* bad :
       {"\x80", "\xD1", "\xE2\x82", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xFF"}) {
    INFO(bad);
    CHECK_THROWS_WITH(DecodeUtf8(bad, 0), Contains("invalid UTF-8 at byte 0"));
  }
  CHECK(IsLetter(U'ё'));
  CHECK(IsLetter(U'Z'));
  CHECK_FALSE(IsLetter(U'^'));
  CHECK_FALSE(IsLetter(U'1'));
}

TEST_CASE("vits encoding: Adler-32, little-endian numbers and UTF-8 of a code point", "[tts][vits]") {
  CHECK(Adler32(std::string_view("Wikipedia")) == 0x11E60398U);
  // Over many chunks of the sums.
  CHECK(Adler32(std::string(100'000, '\xFF')) == 0x149A302CU);
  CHECK(Adler32(std::string_view()) == 1U);
  const uint8_t bytes[] = {0x01, 0x02, 0x03, 0x04, 0xFF};
  CHECK(ReadU32(bytes) == 0x04030201U);
  CHECK(ReadU32(bytes + 1) == 0xFF040302U);
  CHECK(Adler32(bytes, 4) == Adler32(std::string_view("\x01\x02\x03\x04", 4)));
  for (const char32_t symbol :
       {U'a', U'ё', U'€', U'\U0001F600', U'\u007F', U'\u0080', U'\u07FF', U'\u0800', U'\uFFFF'}) {
    std::string text = "x";
    AppendUtf8(text, symbol);
    const auto decoded = DecodeUtf8(text, 1);
    CHECK(decoded.value == symbol);
    CHECK(1 + decoded.size == text.size());
  }
}
