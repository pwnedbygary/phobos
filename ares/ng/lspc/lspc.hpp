//line sprite controller

struct LSPC : Thread {
  Node::Object node;
  Node::Video::Screen screen;
  Memory::Writable<n16> vram;
  Memory::Writable<n16> pram;

  struct Debugger {
    //debugger.cpp
    auto load(Node::Object) -> void;
    auto unload(Node::Object) -> void;

    struct Memory {
      Node::Debugger::Memory vram;
      Node::Debugger::Memory pram;
    } memory;
  } debugger;

  //lspc.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto loadZoomy(const u8* data, u32 size) -> void;

  auto step(u32 clocks) -> void;
  auto main() -> void;
  auto frame() -> void;
  auto power(bool reset) -> void;

  //render.cpp
  auto render(n9 y) -> void;

  //color.cpp
  auto color(n32) -> n64;

  //serialization.cpp
  auto serialize(serializer&) -> void;

  struct Animation {
    n1 disable;
    n8 speed;
    n8 counter;
    n3 frame;
  } animation;

  struct Timer {
    n1  interruptEnable;
    n1  reloadOnChange;
    n1  reloadOnVblank;
    n1  reloadOnZero;
    n32 reload;
    n32 counter;
    n1  stopPAL;  //todo
  } timer;

  struct IRQ {
    n1 powerAcknowledge;
    n1 timerAcknowledge;
    n1 vblankAcknowledge;
  } irq;

  struct IO {
    n9  hcounter;
    n9  vcounter;
    n1  shadow;
    n16 vramAddress;
    n16 vramIncrement;
    n1  pramBank;
    u32 cddCounter;  //Neo Geo CD: 75Hz CDD tick accumulator (6MHz / 75)
  } io;

  n8 vscale[256][256];
  n1 hscale[16][16];
};

//Diagnostic: when set, the sprite renderer logs sprites that draw a long run
//of near-black pixels (candidates for the NGCD "black rectangle" HUD bug),
//including the tile number, palette and whether all four plane bytes are the
//untouched 0xFF fill (=> tile data was never uploaded).
extern bool ngGfxDiag;

extern LSPC lspc;
