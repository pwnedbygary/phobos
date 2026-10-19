//The tags of the CPU's caches, with the PSP's GE timing (Allegrex::Cache: allegrex.hpp).

auto Allegrex::Cache::reset() -> void {
  for(u32 set : range(Sets)) recent[set] = older[set] = Empty;
  fills = writebacks = uncached = 0;
}

//An access compiled code didn't find in its set's most recently used way (or a store to a clean line there): the
//other way, made the most recent; or a miss, the line read in where the older way's was (written back if a store made
//it dirty). Only RAM's lines count (VRAM's and the scratchpad's are held all the same); an uncached mirror's access
//(0x4000'0000 and 0xa000'0000 on: the top three bits not 000 or 100) is never held, and counts as one of its own.
auto Allegrex::dataCacheMiss(u32 address, u32 store) -> u32 {
  auto& c = dataCache;
  if(u32 top = address >> 29; top != 0 && top != 4) {
    if(Cache::ram(address)) c.uncached++;
    return address;
  }
  u32 key = address >> 6, set = key % Cache::Sets, dirty = store ? Cache::Dirty : 0;
  if((c.recent[set] & ~Cache::Dirty) == key) {
    c.recent[set] |= dirty;
    return address;
  }
  u32 line = c.older[set];
  if((line & ~Cache::Dirty) != key) {
    if(line != Cache::Empty && line & Cache::Dirty && Cache::ram(line << 6)) c.writebacks++;
    if(Cache::ram(address)) c.fills++;
    line = key;
  }
  c.older[set] = c.recent[set];
  c.recent[set] = line | dirty;
  return address;
}

//An instruction's line compiled code didn't find in its set's most recently used way: the other way, made the most
//recent; or a miss, the line read in where the older way's was. (Compiled code is a cached mirror's alone. The
//instruction cache's keys are Cache::codeKey()'s.)
auto Allegrex::instructionCacheMiss(u32 address) -> void {
  auto& c = instructionCache;
  u32 key = Cache::codeKey(address), set = address >> 6 & Cache::Sets - 1;
  u32 line = c.older[set];
  if(line != key) {
    if(Cache::ram(address)) c.fills++;
    line = key;
  }
  c.older[set] = c.recent[set];
  c.recent[set] = line;
}
