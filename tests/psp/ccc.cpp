//Character code conversion (ares/psp/kernel/ccc.cpp, sceCcc): the six conversions as pspautotests' ccc/convertstring
//recorded them on a PSP, line for line; and decoding, encoding, lengths, error characters and the JIS tables beyond
//it. The Shift-JIS conversions read tables the program gives; these tests give small ones (ASCII, あ, the
//ideographic space and a half-width katakana), JIS-indexed as the test's own are.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results, Destination = R + 0x100, JisTable = 0x0898'0000, UcsTable = 0x089a'0000;

auto bytes(KernelMachine& m, u32 address, u32 count) -> std::vector<u32> {
  std::vector<u32> read;
  for(u32 n = 0; n < count; n++) read.push_back(m.system.memory.read(1, address + n));
  return read;
}

auto halves(KernelMachine& m, u32 address, u32 count) -> std::vector<u32> {
  std::vector<u32> read;
  for(u32 n = 0; n < count; n++) read.push_back(m.system.memory.read(2, address + n * 2));
  return read;
}

auto utf16(KernelMachine& m, std::initializer_list<u16> units) -> u32 {
  u32 address = m.nextString, n = 0;
  for(u16 unit : units) m.system.memory.write(2, address + n++ * 2, unit);
  m.nextString += (n * 2 + 3) & ~3u;
  return address;
}

//JIS-indexed tables as ccc/convertstring's are: ASCII both ways, あ (0x2422, U+3042), the ideographic space (0x2121,
//U+3000) and the half-width カ (0xb6, U+FF76)
auto tables(KernelMachine& m) -> void {
  m.system.memory.fill(JisTable, 0, 0x20000);
  m.system.memory.fill(UcsTable, 0, 0x20000);
  auto pair = [&](u32 jis, u32 code) {
    m.system.memory.write(2, JisTable + jis * 2, code);
    m.system.memory.write(2, UcsTable + code * 2, jis);
  };
  for(u32 c = 1; c < 0x80; c++) pair(c, c);
  pair(0x2422, 0x3042);
  pair(0x2121, 0x3000);
  pair(0xb6, 0xff76);
  CHECK(m.call("sceCccSetTable", {JisTable, UcsTable}), 0);
}
}

//Every conversion's seven lines from ccc/convertstring.expected: its result and the destination's first four
//characters (it starts as 0xcc bytes), for "Hello, cruel world." into 200 bytes, "AB", no destination, invalid input
//(0xff bytes; 0x80 bytes for Shift-JIS to UTF-16; for UTF-16 high surrogates each before an "A"), and destinations of
//1, 2 and 4 bytes.
static auto cccConvertString() -> void {
  enum : u32 { UTF8, UTF16, SJIS };
  struct Conversion { const char* name; u32 from, to; };
  struct Line { u32 result; u32 first[4]; };
  //the lines, by the destination's kind (halfwords or bytes), in the recording's order
  const Line toHalves[7] = {{19, {0x0048, 0x0065, 0x006c, 0x006c}}, {2, {0x0041, 0x0042, 0x0000, 0xcccc}},
                            {0, {}}, {0, {0x0000, 0xcccc, 0xcccc, 0xcccc}}, {0, {0xcccc, 0xcccc, 0xcccc, 0xcccc}},
                            {0, {0x0000, 0xcccc, 0xcccc, 0xcccc}}, {1, {0x0048, 0x0000, 0xcccc, 0xcccc}}};
  const Line toBytes[7] = {{19, {0x48, 0x65, 0x6c, 0x6c}}, {2, {0x41, 0x42, 0x00, 0xcc}}, {0, {}},
                           {0, {0x00, 0xcc, 0xcc, 0xcc}}, {0, {0x00, 0xcc, 0xcc, 0xcc}},
                           {1, {0x48, 0x00, 0xcc, 0xcc}}, {3, {0x48, 0x65, 0x6c, 0x00}}};
  for(auto [name, from, to] : {Conversion{"sceCccUTF8toUTF16", UTF8, UTF16}, {"sceCccUTF8toSJIS", UTF8, SJIS},
                               {"sceCccUTF16toUTF8", UTF16, UTF8}, {"sceCccUTF16toSJIS", UTF16, SJIS},
                               {"sceCccSJIStoUTF8", SJIS, UTF8}, {"sceCccSJIStoUTF16", SJIS, UTF16}}) {
    KernelMachine m;
    tables(m);
    u32 normal, shortText, invalid;
    if(from == UTF16) {
      normal = utf16(m, {'H', 'e', 'l', 'l', 'o', ',', ' ', 'c', 'r', 'u', 'e', 'l', ' ',
                         'w', 'o', 'r', 'l', 'd', '.', 0});
      shortText = utf16(m, {'A', 'B', 0});
      invalid = utf16(m, {0xd800, 0x41, 0xd800, 0x41, 0xd800, 0x41, 0xd800, 0x41, 0xd800, 0x41, 0});
    } else {
      normal = m.string("Hello, cruel world.");
      shortText = m.string("AB");
      invalid = m.string(std::string(10, char(from == SJIS && to == UTF16 ? 0x80 : 0xff)));
    }
    struct Call { u32 destination, size, source; };
    const Call calls[7] = {{Destination, 200, normal}, {Destination, 200, shortText}, {0, 0, normal},
                           {Destination, 200, invalid}, {Destination, 1, normal}, {Destination, 2, normal},
                           {Destination, 4, normal}};
    for(u32 n = 0; n < 7; n++) {
      m.system.memory.fill(Destination, 0xcc, 8);
      auto& line = (to == UTF16 ? toHalves : toBytes)[n];
      CHECK(m.call(name, {calls[n].destination, calls[n].size, calls[n].source}), line.result);
      if(!calls[n].destination) continue;
      auto first = to == UTF16 ? halves(m, Destination, 4) : bytes(m, Destination, 4);
      for(u32 c = 0; c < 4; c++) CHECK(first[c], line.first[c]);
    }
    CHECK(m.notes.size(), 0);
  }
}

//Beyond the recording: characters past ASCII through each encoding (é, €, あ, an emoji as a UTF-16 surrogate pair,
//Shift-JIS pairs and a half-width katakana) by the tables, and back; a character that doesn't fit whole with the
//terminator isn't written; one the tables don't have ends a Shift-JIS conversion (the error character, 0); lengths;
//the tables looked up with a fallback.
static auto cccCharacters() -> void {
  KernelMachine m;
  tables(m);
  //"Aé€あ😀" in UTF-8: 1 + 2 + 3 + 3 + 4 bytes
  u32 text = m.string("A\xc3\xa9\xe2\x82\xac\xe3\x81\x82\xf0\x9f\x98\x80");
  CHECK(m.call("sceCccStrlenUTF8", {text}), 5);
  CHECK(m.call("sceCccUTF8toUTF16", {Destination, 100, text}), 5);
  auto units = halves(m, Destination, 7);
  std::vector<u32> wanted = {0x41, 0xe9, 0x20ac, 0x3042, 0xd83d, 0xde00, 0};
  for(u32 n = 0; n < 7; n++) CHECK(units[n], wanted[n]);
  CHECK(m.call("sceCccStrlenUTF16", {Destination}), 5);
  CHECK(m.call("sceCccUTF16toUTF8", {Destination + 0x40, 100, Destination}), 5);
  CHECK(m.system.memory.readString(Destination + 0x40, 32) == m.system.memory.readString(text, 32), true);
  //into 9 bytes: "Aé€" (6) and its terminator; あ's 3 bytes would leave no room for it
  u32 four = utf16(m, {0x41, 0xe9, 0x20ac, 0x3042, 0});
  m.system.memory.fill(Destination, 0xcc, 16);
  CHECK(m.call("sceCccUTF16toUTF8", {Destination, 9, four}), 3);
  CHECK(m.system.memory.read(1, Destination + 6), 0);
  CHECK(m.system.memory.read(1, Destination + 7), 0xcc);
  //Shift-JIS: あ (82 a0), the ideographic space (81 40), カ (b6), "A"
  u32 sjis = m.string("\x82\xa0\x81\x40\xb6" "A");
  CHECK(m.call("sceCccStrlenSJIS", {sjis}), 4);
  CHECK(m.call("sceCccSJIStoUTF16", {Destination, 100, sjis}), 4);
  units = halves(m, Destination, 5);
  wanted = {0x3042, 0x3000, 0xff76, 0x41, 0};
  for(u32 n = 0; n < 5; n++) CHECK(units[n], wanted[n]);
  CHECK(m.call("sceCccUTF16toSJIS", {Destination + 0x40, 100, Destination}), 4);
  CHECK(m.system.memory.readString(Destination + 0x40, 32) == m.system.memory.readString(sjis, 32), true);
  CHECK(m.call("sceCccSJIStoUTF8", {Destination, 100, sjis}), 4);
  CHECK(m.system.memory.readString(Destination, 32) == "\xe3\x81\x82\xe3\x80\x80\xef\xbd\xb6" "A", true);
  CHECK(m.call("sceCccUTF8toSJIS", {Destination, 100, m.string("A\xe2\x82\xac" "B")}), 1);
  CHECK(m.call("sceCccUCStoJIS", {0x3042, 0x2222}), 0x2422);
  CHECK(m.call("sceCccUCStoJIS", {0x20ac, 0x2222}), 0x2222);
  CHECK(m.call("sceCccJIStoUCS", {0x2121, 0x3013}), 0x3000);
  CHECK(m.call("sceCccJIStoUCS", {0x3021, 0x3013}), 0x3013);
  CHECK(m.call("sceCccJIStoUCS", {0x12'2121, 0x3013}), 0x3013);
  CHECK(m.notes.size(), 0);
}

//A character at a time: decoded with the pointer moved past it (an invalid byte passed over alone, decoding to the
//error character), encoded with the pointer moved past what was written and returned; the error characters set,
//each returning the one before, standing in conversions for what can't be decoded or encoded; no tables, no
//Shift-JIS.
static auto cccOneAtATime() -> void {
  KernelMachine m;
  constexpr u32 Pointer = R;
  auto decode = [&](const char* name, u32 at) {
    m.system.memory.write(4, Pointer, at);
    return m.call(name, {Pointer});
  };
  u32 text = m.string("\xe2\x82\xac\xff" "B");
  CHECK(decode("sceCccDecodeUTF8", text), 0x20ac);
  CHECK(m.system.memory.read(4, Pointer), text + 3);
  CHECK(m.call("sceCccDecodeUTF8", {Pointer}), 0);  //the 0xff: the error character, at first 0
  CHECK(m.system.memory.read(4, Pointer), text + 4);
  CHECK(m.call("sceCccDecodeUTF8", {Pointer}), 'B');
  CHECK(m.call("sceCccDecodeUTF8", {Pointer}), 0);  //the terminator, passed over too
  CHECK(m.system.memory.read(4, Pointer), text + 6);
  //an overlong form, a surrogate, a cut sequence: each passed over a byte at a time
  for(auto bad : {"\xc0\x80", "\xed\xa0\x80", "\xe2\x82"}) {
    u32 at = m.string(bad);
    CHECK(decode("sceCccDecodeUTF8", at), 0);
    CHECK(m.system.memory.read(4, Pointer), at + 1);
  }
  u32 pair = utf16(m, {0xd83d, 0xde00, 0xdc00, 0x41});
  CHECK(decode("sceCccDecodeUTF16", pair), 0x1f600);
  CHECK(m.system.memory.read(4, Pointer), pair + 4);
  CHECK(m.call("sceCccDecodeUTF16", {Pointer}), 0);  //a low surrogate alone
  CHECK(m.call("sceCccDecodeUTF16", {Pointer}), 0x41);
  //no tables yet: Shift-JIS maps nothing, not even ASCII
  CHECK(decode("sceCccDecodeSJIS", m.string("A")), 0);
  CHECK(m.call("sceCccUCStoJIS", {'A', 7}), 7);
  tables(m);
  CHECK(decode("sceCccDecodeSJIS", m.string("\x82\xa0")), 0x3042);
  //encoding: the pointer moved past what's written, and returned
  m.system.memory.write(4, Pointer, Destination);
  CHECK(m.call("sceCccEncodeUTF8", {Pointer, 0x20ac}), Destination + 3);
  CHECK(m.call("sceCccEncodeUTF16", {Pointer, 0x1f600}), Destination + 7);
  CHECK(m.call("sceCccEncodeSJIS", {Pointer, 0x3042}), Destination + 9);
  CHECK(m.system.memory.read(4, Pointer), Destination + 9);
  auto written = bytes(m, Destination, 9);
  std::vector<u32> wanted = {0xe2, 0x82, 0xac, 0x3d, 0xd8, 0x00, 0xde, 0x82, 0xa0};
  for(u32 n = 0; n < 9; n++) CHECK(written[n], wanted[n]);
  //the error characters (Unicode codes): each set returns the one before
  CHECK(m.call("sceCccSetErrorCharUTF8", {'?'}), 0);
  CHECK(m.call("sceCccSetErrorCharUTF8", {'#'}), '?');
  CHECK(m.call("sceCccSetErrorCharUTF16", {0xfffd}), 0);
  CHECK(m.call("sceCccSetErrorCharSJIS", {0x3000}), 0);
  //"€", 0xff, "B": the invalid byte decodes to '#', and € (not in the tables) encodes to Shift-JIS as the
  //ideographic space
  CHECK(m.call("sceCccUTF8toUTF16", {Destination, 100, text}), 3);
  auto units = halves(m, Destination, 4);
  wanted = {0x20ac, '#', 'B', 0};
  for(u32 n = 0; n < 4; n++) CHECK(units[n], wanted[n]);
  CHECK(m.call("sceCccUTF8toSJIS", {Destination, 100, text}), 3);
  written = bytes(m, Destination, 5);
  wanted = {0x81, 0x40, '#', 'B', 0};
  for(u32 n = 0; n < 5; n++) CHECK(written[n], wanted[n]);
  //a lone surrogate encodes to UTF-16's error character, and its own error character to UTF-8's
  m.system.memory.write(4, Pointer, Destination);
  CHECK(m.call("sceCccEncodeUTF16", {Pointer, 0xd800}), Destination + 2);
  CHECK(m.system.memory.read(2, Destination), 0xfffd);
  CHECK(m.call("sceCccEncodeUTF8", {Pointer, 0x11'0000}), Destination + 3);
  CHECK(m.system.memory.read(1, Destination + 2), '#');
  CHECK(m.notes.size(), 0);
}

auto cccTests() -> Tests {
  return {{"ccc convertstring", cccConvertString}, {"ccc characters", cccCharacters},
          {"ccc one at a time", cccOneAtATime}};
}

}
