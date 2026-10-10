//Character code conversion (sceCcc, Sony's libccc.prx, which games carry and the kernel stands in for): strings
//converted between UTF-8, UTF-16 and Shift-JIS, a character at a time decoded and encoded, and lengths counted.
//
//The library has no JIS tables of its own: the program hands it two (sceCccSetTable), each 65536 halfwords, from JIS
//to Unicode and from Unicode to JIS, and its Shift-JIS conversions read them in the program's memory (pspautotests'
//ccc/convertstring "crashes without table set"; its own tables, jis2ucs.bin and ucs2jis.bin, are indexed by the JIS
//code: 0x2422 for あ, 0x2121 for the ideographic space, single bytes for ASCII and the half-width katakana). A
//Shift-JIS pair is turned into its JIS code the standard way before it's looked up, and back.
//
//As ccc/convertstring recorded on a PSP for all six conversions: a conversion returns how many characters it wrote,
//its terminator not counted; the destination's size is in bytes, and a character is written only while it fits with
//room for the terminator after it, which is written if it fits (one byte for UTF-8 and Shift-JIS, two for UTF-16: a
//UTF-16 destination of 1 byte gets nothing, of 2 just its terminator, of 4 one character); no destination (size 0)
//converts nothing; and input that isn't valid for its encoding (0xff bytes, 0x80 bytes in Shift-JIS, a UTF-16 high
//surrogate without its low one) converts to nothing. That last is taken as each encoding's error character (what
//sceCccSetErrorChar* sets, the decoder's answer for what it can't decode, and the encoder's for what it can't encode)
//being 0 until the program sets one: a 0 ends the string. Chosen, as no recording tells the three error characters'
//first values or how far an invalid sequence goes (one byte, or one UTF-16 unit, here). The King of Fighters - Orochi
//Saga decodes its text with sceCccDecodeUTF8 from its first frame, and Genso Suikoden converts Shift-JIS to UTF-8.

namespace {
  enum : u32 { CccUTF8, CccUTF16, CccSJIS };
}

//The JIS code of a Shift-JIS pair (lead 0x81-0x9f or 0xe0-0xfc, trail 0x40-0x7e or 0x80-0xfc), and the pair of a
//JIS code (rows 0x21 on): JIS X 0208's 94 rows two to a Shift-JIS lead byte.
static auto sjisToJis(u32 lead, u32 trail) -> u32 {
  u32 row = (lead - (lead <= 0x9f ? 0x70 : 0xb0)) << 1;
  if(trail < 0x9f) return (row - 1) << 8 | (trail - (trail < 0x7f ? 0x1f : 0x20));
  return row << 8 | (trail - 0x7e);
}

static auto jisToSjis(u32 jis) -> u32 {
  u32 row = jis >> 8, cell = jis & 0xff;
  u32 lead = ((row + 1) >> 1) + (row <= 0x5e ? 0x70 : 0xb0);
  u32 trail = cell + (row & 1 ? (cell < 0x60 ? 0x1f : 0x20) : 0x7e);
  return lead << 8 | trail;
}

//A JIS code to Unicode through the program's table, and back (0: the table has none, or there's no table).
auto Kernel::cccToUnicode(u32 jis) -> u32 {
  if(!ccc.jisToUnicode || jis > 0xffff) return 0;
  return memory.read(2, ccc.jisToUnicode + jis * 2);
}

auto Kernel::cccToJis(u32 code) -> u32 {
  if(!ccc.unicodeToJis || code > 0xffff) return 0;
  return memory.read(2, ccc.unicodeToJis + code * 2);
}

//The next character of a string in the program's memory at `at`, which moves past it: its Unicode code, 0 at the
//terminator (one unit, which it moves past too), or the encoding's error character for what isn't valid there (one
//byte or unit passed over).
auto Kernel::cccDecode(u32 kind, u32& at) -> u32 {
  if(kind == CccUTF16) {
    u32 unit = memory.read(2, at);
    at += 2;
    if(unit >= 0xdc00 && unit <= 0xdfff) return ccc.errorUTF16;
    if(unit < 0xd800 || unit > 0xdbff) return unit;
    u32 low = memory.read(2, at);
    if(low < 0xdc00 || low > 0xdfff) return ccc.errorUTF16;
    at += 2;
    return 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
  }
  u32 lead = memory.read(1, at);
  if(kind == CccSJIS) {
    u32 jis = 0, length = 1;
    if(lead < 0x80 || (lead >= 0xa1 && lead <= 0xdf)) {
      jis = lead;
    } else if((lead >= 0x81 && lead <= 0x9f) || (lead >= 0xe0 && lead <= 0xfc)) {
      u32 trail = memory.read(1, at + 1);
      if((trail >= 0x40 && trail <= 0x7e) || (trail >= 0x80 && trail <= 0xfc)) jis = sjisToJis(lead, trail), length = 2;
    }
    if(!jis && lead) return at++, ccc.errorSJIS;
    at += length;
    if(!jis) return 0;
    u32 code = cccToUnicode(jis);
    return code ? code : ccc.errorSJIS;
  }
  //UTF-8 as RFC 3629 has it: no overlong forms, no surrogates, nothing past U+10FFFF
  u32 length = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3
             : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
  if(!length) return at++, ccc.errorUTF8;
  u32 code = length == 1 ? lead : lead & (0x7f >> length);
  for(u32 n = 1; n < length; n++) {
    u32 next = memory.read(1, at + n);
    if((next & 0xc0) != 0x80) return at++, ccc.errorUTF8;
    code = code << 6 | (next & 0x3f);
  }
  static constexpr u32 least[5] = {0, 0, 0x80, 0x800, 0x10000};
  if(code < least[length] || code > 0x10'ffff || (code >= 0xd800 && code <= 0xdfff)) return at++, ccc.errorUTF8;
  at += length;
  return code;
}

//A character's bytes in an encoding, as many as it returns: for one the encoding can't hold (a surrogate or past
//U+10FFFF; for Shift-JIS, one the program's table doesn't have), the encoding's error character's (none: nothing to
//write). The error characters are Unicode codes, as the decoders give them.
auto Kernel::cccEncoding(u32 kind, u32 code, u8 bytes[4]) -> u32 {
  if(kind == CccSJIS) {
    u32 jis = code ? cccToJis(code) : 0;
    if(code && !jis) jis = ccc.errorSJIS ? cccToJis(ccc.errorSJIS) : 0;
    if(code && !jis) return 0;
    if(jis < 0x100) return bytes[0] = jis, 1;
    u32 sjis = jisToSjis(jis);
    return bytes[0] = sjis >> 8, bytes[1] = sjis, 2;
  }
  if(code > 0x10'ffff || (code >= 0xd800 && code <= 0xdfff)) {
    code = kind == CccUTF8 ? ccc.errorUTF8 : ccc.errorUTF16;
    if(!code || code > 0x10'ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
  }
  if(kind == CccUTF16) {
    if(code < 0x10000) return bytes[0] = code, bytes[1] = code >> 8, 2;
    u32 high = 0xd800 + ((code - 0x10000) >> 10), low = 0xdc00 + (code & 0x3ff);
    return bytes[0] = high, bytes[1] = high >> 8, bytes[2] = low, bytes[3] = low >> 8, 4;
  }
  if(code < 0x80) return bytes[0] = code, 1;
  if(code < 0x800) return bytes[0] = 0xc0 | code >> 6, bytes[1] = 0x80 | (code & 0x3f), 2;
  if(code < 0x10000) {
    return bytes[0] = 0xe0 | code >> 12, bytes[1] = 0x80 | (code >> 6 & 0x3f), bytes[2] = 0x80 | (code & 0x3f), 3;
  }
  bytes[0] = 0xf0 | code >> 18, bytes[1] = 0x80 | (code >> 12 & 0x3f);
  bytes[2] = 0x80 | (code >> 6 & 0x3f), bytes[3] = 0x80 | (code & 0x3f);
  return 4;
}

//(destination, its size in bytes, source): the source string converted, as many characters as fit with the
//terminator after them; how many were written.
auto Kernel::cccConvert(u32 from, u32 to) -> void {
  u32 at = arg(0), source = arg(2), written = 0;
  u64 end = u64(arg(0)) + arg(1);
  u32 terminator = to == CccUTF16 ? 2 : 1;
  while(true) {
    u32 code = cccDecode(from, source);
    if(!code) break;
    u8 bytes[4];
    u32 length = cccEncoding(to, code, bytes);
    if(!length || u64(at) + length + terminator > end) break;
    for(u32 n = 0; n < length; n++) memory.write(1, at + n, bytes[n]);
    at += length;
    written++;
  }
  if(u64(at) + terminator <= end) memory.write(terminator, at, 0);
  result(written);
}

//(string): how many characters it has before its terminator.
auto Kernel::cccLength(u32 kind) -> void {
  u32 at = arg(0), count = 0;
  while(cccDecode(kind, at)) count++;
  result(count);
}

//(where the destination pointer is, character): the character written there in the encoding, the pointer moved past
//it; the pointer as it is now. (What the PSP returns isn't recorded: the pointer after it, chosen.)
auto Kernel::cccEncode(u32 kind) -> void {
  u32 pointer = arg(0), at = memory.read(4, pointer);
  u8 bytes[4];
  u32 length = cccEncoding(kind, arg(1), bytes);
  for(u32 n = 0; n < length; n++) memory.write(1, at + n, bytes[n]);
  memory.write(4, pointer, at + length);
  result(at + length);
}

//(where the source pointer is): the next character's code, the pointer moved past it.
auto Kernel::cccDecodeNext(u32 kind) -> void {
  u32 pointer = arg(0), at = memory.read(4, pointer);
  u32 code = cccDecode(kind, at);
  memory.write(4, pointer, at);
  result(code);
}

//(character): the encoding's error character set, the one before returned.
auto Kernel::cccErrorCharacter(u16& character) -> void {
  u16 before = character;
  character = arg(0);
  result(before);
}

//(JIS to Unicode, Unicode to JIS): the program's tables, read where they are whenever a Shift-JIS string is converted.
auto Kernel::sceCccSetTable() -> void {
  ccc.jisToUnicode = arg(0);
  ccc.unicodeToJis = arg(1);
  result(0);
}

//(Unicode or JIS code, fallback): its JIS or Unicode code by the program's tables, or the fallback where they have
//none.
auto Kernel::sceCccUCStoJIS() -> void {
  u32 jis = cccToJis(arg(0));
  result(jis ? jis : arg(1));
}

auto Kernel::sceCccJIStoUCS() -> void {
  u32 code = cccToUnicode(arg(0));
  result(code ? code : arg(1));
}

auto Kernel::sceCccUTF8toUTF16() -> void { cccConvert(CccUTF8, CccUTF16); }
auto Kernel::sceCccUTF8toSJIS() -> void { cccConvert(CccUTF8, CccSJIS); }
auto Kernel::sceCccUTF16toUTF8() -> void { cccConvert(CccUTF16, CccUTF8); }
auto Kernel::sceCccUTF16toSJIS() -> void { cccConvert(CccUTF16, CccSJIS); }
auto Kernel::sceCccSJIStoUTF8() -> void { cccConvert(CccSJIS, CccUTF8); }
auto Kernel::sceCccSJIStoUTF16() -> void { cccConvert(CccSJIS, CccUTF16); }
auto Kernel::sceCccStrlenUTF8() -> void { cccLength(CccUTF8); }
auto Kernel::sceCccStrlenUTF16() -> void { cccLength(CccUTF16); }
auto Kernel::sceCccStrlenSJIS() -> void { cccLength(CccSJIS); }
auto Kernel::sceCccEncodeUTF8() -> void { cccEncode(CccUTF8); }
auto Kernel::sceCccEncodeUTF16() -> void { cccEncode(CccUTF16); }
auto Kernel::sceCccEncodeSJIS() -> void { cccEncode(CccSJIS); }
auto Kernel::sceCccDecodeUTF8() -> void { cccDecodeNext(CccUTF8); }
auto Kernel::sceCccDecodeUTF16() -> void { cccDecodeNext(CccUTF16); }
auto Kernel::sceCccDecodeSJIS() -> void { cccDecodeNext(CccSJIS); }
auto Kernel::sceCccSetErrorCharUTF8() -> void { cccErrorCharacter(ccc.errorUTF8); }
auto Kernel::sceCccSetErrorCharUTF16() -> void { cccErrorCharacter(ccc.errorUTF16); }
auto Kernel::sceCccSetErrorCharSJIS() -> void { cccErrorCharacter(ccc.errorSJIS); }
