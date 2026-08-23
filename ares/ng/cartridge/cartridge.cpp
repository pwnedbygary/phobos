namespace ares::NeoGeo {

Cartridge& cartridge = cartridgeSlot.cartridge;
#include "board/board.cpp"
#include "slot.cpp"
#include "serialization.cpp"

auto Cartridge::allocate(Node::Port parent) -> Node::Peripheral {
  return node = parent->append<Node::Peripheral>("Neo Geo Cartridge");
}

auto Cartridge::connect() -> void {
  if(!node->setPak(pak = platform->pak(node))) return;

  information = {};
  information.title = pak->attribute("title");
  information.board = pak->attribute("board");

  if(information.board == "rom_mslugx") board = std::make_unique<Board::MSlugX>(*this);
  if(information.board == "cmc50_jockeygp") board = std::make_unique<Board::JockeyGP>(*this);
  if(information.board == "pvc_kf2k3" || information.board == "pvc_kf2k3h") board = std::make_unique<Board::PVC>(*this);
  if(information.board == "rom_kof98") board = std::make_unique<Board::ProgSF1>(*this);
  if(information.board.beginsWith("sma_")) board = std::make_unique<Board::SMA>(*this);
  if(!board) board = std::make_unique<Board::Rom>(*this);
  board->pak = pak;

  //fix layer banking schemes used by later cartridges
  if(information.board == "cmc50_kof2000n" || information.board == "pvc_kf2k3" || information.board == "pvc_kf2k3h" || information.board == "pvc_svc" || information.board == "k2k2_matrim" || information.board == "sma_kof2k") {
    board->fixBankType = 2;  //KOF2000-style tile banking
  } else if(information.board.beginsWith("cmc42_") || information.board == "cmc50_kof2001" || information.board == "pcm2_mslug4" || information.board == "pcm2_rotd" || information.board == "pcm2_pnyaa" || information.board == "pvc_mslug5" || information.board == "k2k2_samsh5" || information.board == "k2k2_sams5s" || information.board == "sma_kof99" || information.board == "sma_garou" || information.board == "sma_garouh" || information.board == "sma_mslug3" || information.board == "sma_mslug3a") {
    board->fixBankType = 1;  //Garou-style line banking
  }

  board->load();

  power();
}

auto Cartridge::disconnect() -> void {
  if(!node || !board) return;
  board->unload();
  board->pak.reset();
  board.reset();
  node.reset();
}

auto Cartridge::save() -> void {
  if(!node) return;
  if(board) board->save();
}

auto Cartridge::power() -> void {
  if(board) board->power();
}

auto Cartridge::readP(n1 upper, n1 lower, n24 address, n16 data) -> n16 {
  if(board) return board->readP(upper, lower, address, data);
  return data;
}

auto Cartridge::writeP(n1 upper, n1 lower, n24 address, n16 data) -> void {
  if(board) return board->writeP(upper, lower, address, data);
}

auto Cartridge::readM(n32 address) -> n8 {
 if(board) return board->readM(address);
 return 0xff;
}

auto Cartridge::mromSize() const -> u32 {
  if(board) return board->mromSize();
  return 0;
}

auto Cartridge::readC(n32 address) -> n8 {
  if(board) return board->readC(address);
  return 0xff;
}

auto Cartridge::readS(n32 address) -> n8 {
  if(board) return board->readS(address);
  return 0xff;
}

auto Cartridge::readVA(n32 address) -> n8 {
  if(board) return board->readVA(address);
  return 0xff;
}

auto Cartridge::readVB(n32 address) -> n8 {
  if(board) return board->readVB(address);
  return 0xff;
}

auto Cartridge::fixBankType() const -> n2 {
  if(board) return board->fixBankType;
  return 0;
}

auto Cartridge::cromMask() const -> u32 {
  if(board) return board->cromMask();
  return 0;
}

}
