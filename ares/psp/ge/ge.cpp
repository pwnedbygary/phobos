#include "ge.hpp"
#include "../memory/memory.hpp"

namespace ares::PlayStationPortable {

#include "list.cpp"
#include "vertex.cpp"
#include "texture.cpp"
#include "pixel.cpp"
#include "lighting.cpp"
#include "transform.cpp"
#include "draw.cpp"
#include "curves.cpp"
#include "four.cpp"
#include "raster.cpp"
#include "threads.cpp"
#include "transfer.cpp"

//The GE watches the pages of the textures it keeps decoded (texture.cpp), and a hardware renderer those of its frame
//buffers (ge.hpp's Renderer), and hears of their changes here.
thread_local bool GE::readingDeferred = false;

GE::GE(Memory& memory) : memory(memory) {
  memory.watchedWritten = [this](u32 page) {
    textureWritten(page);
    if(renderer) renderer->written(*this, page);
  };
  memory.finishDrawing = [this] { settleAll(); };  //(threads.cpp)
  //readingDeferred: ensureDecoded reading a prior batch's pixels while this batch is already "drawn"
  memory.vramDrawnOver = [this](u32 first, u32 last) {
    return !readingDeferred && drawnOver(first, last);
  };
  memory.vramChangedBusy = [this](u32 first, u32 last) {
    if(renderer) renderer->besideChanged(*this, first, last);
  };
}

//The hardware renderer that draws from now on, or none for the software renderer: the one before settled (what it
//drew put back in memory's VRAM), and the new one told that its copies of VRAM, if it has any, aren't this
//machine's.
auto GE::setRenderer(Renderer* next) -> void {
  settleAll();
  renderer = next;
  if(renderer) renderer->forget(*this);
}

//As the GE is when the PSP starts: every command's word zero, no list.
auto GE::power() -> void {
  for(auto& command : commands) command = 0;
  for(auto& byte : clut) byte = 0;
  list = {};
  vertexAddress = indexAddress = 0;
  boxOutside = false;
  signalWord = finishWord = endWord = 0;
  for(auto& element : bones) element = 0;
  for(auto& element : world) element = 0;
  for(auto& element : view) element = 0;
  for(auto& element : projection) element = 0;
  for(auto& element : textureMatrix) element = 0;
  boneIndex = worldIndex = viewIndex = projectionIndex = textureIndex = 0;
  pending = Stop::Ended;
  noted.clear();
  flush();
  dropTextures();
  paletteChanged();
  if(renderer) renderer->forget(*this);
}

auto GE::note(const std::string& text) -> void {
  if(!noted.insert(text).second) return;
  if(log) log(text);
}

//A command's floating-point argument: the top 24 bits of a 32-bit float (the GU library drops the fraction's lowest
//eight bits to fit it in). A whole command word does as well: the shift pushes its command byte out.
auto GE::float24(u32 argument) -> float {
  u32 bits = argument << 8;
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

//Saving and loading the GE, for save states: its commands' last words (from which every draw works its state out
//afresh), the palette, the list it's running, where its vertices and indices are, the last bounding box's result
//(a list may stop at its stall address between BOUNDING_BOX and BJUMP), the matrices, and what its next END means.
//What it noted stays noted; the textures it kept decoded go on loading (memory changed under them).
//Loading returns false for what no GE could hold: CALLs more than two deep, or an END that means anything but the
//end, a FINISH or a SIGNAL.
auto GE::serialize(serializer& s) -> bool {
  flush();  //(nothing waits outside run(), but a state is always of finished drawing)
  s(commands);
  s(clut);
  if(s.reading()) {
    dropTextures(), paletteChanged();
    if(renderer) renderer->forget(*this);
  }
  s(list);
  s(vertexAddress);
  s(indexAddress);
  s(boxOutside);
  s(signalWord);
  s(finishWord);
  s(endWord);
  s(bones);
  s(world);
  s(view);
  s(projection);
  s(textureMatrix);
  s(boneIndex);
  s(worldIndex);
  s(viewIndex);
  s(projectionIndex);
  s(textureIndex);
  s(pending);
  return list.depth <= 2 && (pending == Stop::Ended || pending == Stop::Finished || pending == Stop::Signaled);
}

}
