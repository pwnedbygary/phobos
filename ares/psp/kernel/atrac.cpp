//sceAtrac3plus, the library games decode their music and sound streams with, as stubs that report each stream as one
//the library can't read, which games take to mean no music, and carry on: there's no ATRAC3plus decoder yet. Its six
//IDs are handed out and given back as on a PSP (sceAtracGetAtracID, sceAtracReleaseAtracID), but no data can be set
//on one: sceAtracSetData and sceAtracSetDataAndGetID refuse every stream as the PSP refuses data it can't read
//(0x80630006, what pspautotests' audio/atrac/setdata recorded for zeroed data), so a game never has a stream to
//decode; the rest refuse their ID as one with no stream on it (0x80630005, setdata's for an ID not handed out: which
//error a PSP gives for an ID with no data isn't known here). sceAtracGetAtracID with every ID taken fails with
//0x80630007, as setdata's ATRAC3 ID did, there being none for it.

namespace {
  constexpr u32 AtracErrorBadID = 0x8063'0005, AtracErrorBadData = 0x8063'0006, AtracErrorNoID = 0x8063'0007;
  constexpr u32 AtracIDs = 6;
}

//(codec: 0x1000 ATRAC3plus, 0x1001 ATRAC3): the lowest ID free.
auto Kernel::sceAtracGetAtracID() -> void {
  if(arg(0) != 0x1000 && arg(0) != 0x1001) return result(ErrorInvalidValue);
  for(u32 id = 0; id < AtracIDs; id++) {
    if(atracIDs >> id & 1) continue;
    atracIDs |= 1 << id;
    return result(id);
  }
  result(AtracErrorNoID);
}

//(ID): given back.
auto Kernel::sceAtracReleaseAtracID() -> void {
  u32 id = arg(0);
  if(id >= AtracIDs || !(atracIDs >> id & 1)) return result(AtracErrorBadID);
  atracIDs &= ~(1 << id);
  result(0);
}

//(ATRAC3plus IDs, ATRAC3 IDs): how the six are shared between the two codecs. Refused while any is handed out
//(BUSY), or asking for more than there are (0x80000022), as audio/atrac/setdata recorded; IDs here serve either
//codec, so nothing else changes.
auto Kernel::sceAtracReinit() -> void {
  if(atracIDs) return result(ErrorBusy);
  if(s32(arg(0)) > s32(AtracIDs) || s32(arg(1)) > s32(AtracIDs)) return result(ErrorOutOfMemory);
  result(0);
}

//(buffer, size): no stream the library can read, so no ID.
auto Kernel::sceAtracSetDataAndGetID() -> void {
  result(AtracErrorBadData);
}

//(ID, buffer, size): an ID handed out can't take it either.
auto Kernel::sceAtracSetData() -> void {
  u32 id = arg(0);
  if(id >= AtracIDs || !(atracIDs >> id & 1)) return result(AtracErrorBadID);
  result(AtracErrorBadData);
}

//Everything that needs a stream on its ID (decoding, the stream's buffers, its loops and positions, its details).
auto Kernel::sceAtracNoStream() -> void {
  result(AtracErrorBadID);
}
