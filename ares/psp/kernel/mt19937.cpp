//The Mersenne Twister (MT19937, as Matsumoto and Nishimura describe it), its state in the program's memory: the
//kernel's (UtilsForUser's sceKernelUtilsMt19937Init and UInt, a SceKernelUtilsMt19937Context of psputils.h) and
//Sony's library of its own (sceMt19937, libmt19937.prx, which Genso Suikoden carries and the kernel stands in for).
//
//The context, as pspautotests' hash/mt19937ctx recorded the kernel's: a word counting the numbers handed out of the
//current 624 (0 once seeded), then the 624 words, already stirred into the first numbers' when seeded. Each number
//handed out stirs its own word into the next round's straight after (one draw changed the state), which gives
//MT19937's very numbers: a word's next value needs only words already made for the next round or not yet used in
//this one. The library's context is taken to be the same (chosen: no recording shows it; the numbers are MT19937's
//either way). A context of the kernel's older layout, as a state saved before kept one, goes on with MT19937's
//numbers too: that layout stirred all 624 words at once when its count reached 624 (as it was once seeded), so part
//way through a round the words it has handed out aren't stirred yet. The last of them tells: stirred, it agrees with
//the next word and the one 397 on, which an unstirred one does only by a chance in 2^31.

namespace {

auto mtNext(u32 word, u32 following, u32 far) -> u32 {
  u32 y = (word & 0x8000'0000) | (following & 0x7fff'ffff);
  return far ^ y >> 1 ^ (y & 1 ? 0x9908'b0df : 0);
}

}

//(context, seed): the words seeded and stirred, none handed out.
auto Kernel::mt19937Init(u32 context, u32 seed) -> void {
  u32 words[624];
  words[0] = seed;
  for(u32 n = 1; n < 624; n++) words[n] = 1'812'433'253 * (words[n - 1] ^ words[n - 1] >> 30) + n;
  for(u32 n = 0; n < 624; n++) words[n] = mtNext(words[n], words[(n + 1) % 624], words[(n + 397) % 624]);
  for(u32 n = 0; n < 624; n++) memory.write(4, context + 4 + n * 4, words[n]);
  memory.write(4, context, 0);
}

//(context): the next number, its word stirred for the next round.
auto Kernel::mt19937UInt(u32 context) -> u32 {
  auto word = [&](u32 n) { return memory.read(4, context + 4 + n * 4); };
  auto stir = [&](u32 n) {
    memory.write(4, context + 4 + n * 4, mtNext(word(n), word((n + 1) % 624), word((n + 397) % 624)));
  };
  u32 index = memory.read(4, context);
  if(index >= 624) {  //the older layout's round used up, or its words seeded
    for(u32 n = 0; n < 624; n++) stir(n);
    index = 0;
  } else if(index) {  //whether the older layout's, part way through a round
    u32 last = word(index - 1), following = word(index), far = word((index - 1 + 397) % 624);
    if(last != mtNext(0, following, far) && last != mtNext(0x8000'0000, following, far)) {
      for(u32 n = 0; n < index; n++) stir(n);
    }
  }
  u32 y = word(index);
  stir(index);
  memory.write(4, context, (index + 1) % 624);
  y ^= y >> 11;
  y ^= y << 7 & 0x9d2c'5680;
  y ^= y << 15 & 0xefc6'0000;
  y ^= y >> 18;
  return y;
}

auto Kernel::sceKernelUtilsMt19937Init() -> void {
  mt19937Init(arg(0), arg(1));
  result(0);
}

auto Kernel::sceKernelUtilsMt19937UInt() -> void {
  result(mt19937UInt(arg(0)));
}

//(context, seed) and (context): the library's, as the kernel's.
auto Kernel::sceMt19937Init() -> void {
  mt19937Init(arg(0), arg(1));
  result(0);
}

auto Kernel::sceMt19937UInt() -> void {
  result(mt19937UInt(arg(0)));
}
