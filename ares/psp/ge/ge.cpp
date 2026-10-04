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
#include "transfer.cpp"

//As the GE is when the PSP starts: every command's word zero, no list.
auto GE::power() -> void {
  for(auto& command : commands) command = 0;
  for(auto& byte : clut) byte = 0;
  list = {};
  vertexAddress = indexAddress = 0;
  signalWord = finishWord = endWord = 0;
  for(auto& element : bones) element = 0;
  for(auto& element : world) element = 0;
  for(auto& element : view) element = 0;
  for(auto& element : projection) element = 0;
  for(auto& element : textureMatrix) element = 0;
  boneIndex = worldIndex = viewIndex = projectionIndex = textureIndex = 0;
  pending = Stop::Ended;
  noted.clear();
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

}
