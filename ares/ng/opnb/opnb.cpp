namespace ares::NeoGeo {

OPNB opnb;
#include "serialization.cpp"

auto OPNB::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("YM2610");

  streamFM = node->append<Node::Audio::Stream>("FM");
  streamFM->setChannels(2);
  streamFM->setFrequency(ym2610.sample_rate(8'000'000));
  streamFM->addHighPassFilter(  20.0, 1);
  streamFM->addLowPassFilter (2840.0, 1);
}

auto OPNB::unload() -> void {
  node->remove(streamFM);
  streamFM.reset();
  node.reset();
}

auto OPNB::main() -> void {
  ymfm::ym2610::output_data output;
  ym2610.generate(&output);

  auto ssg = output.data[2] / 32768.0;
  streamFM->frame(output.data[0] / 32768.0 + ssg, output.data[1] / 32768.0 + ssg);

  step(clocksPerSample);
}

auto OPNB::step(u32 clocks) -> void {
  if(busyCyclesRemaining) {
    busyCyclesRemaining -= clocks;
    if(busyCyclesRemaining <= 0) {
      busyCyclesRemaining = 0;
    }
  }

  for(u32 timer : range(2)) {
    if(timerCyclesRemaining[timer]) {
      timerCyclesRemaining[timer] -= clocks;
      if(timerCyclesRemaining[timer] <= 0) {
        timerCyclesRemaining[timer] = 0;
        interface.timerCallback(timer);
      }
    }
  }

  Thread::step(clocks);
  Thread::synchronize();
}

auto OPNB::power(bool reset) -> void {
  ym2610.reset();
  clocksPerSample = 8'000'000.0 / ym2610.sample_rate(8'000'000);
  Thread::create(8'000'000, std::bind_front(&OPNB::main, this));
}

auto OPNB::read(n2 address) -> n8 {
  return ym2610.read(address);
}

auto OPNB::write(n2 address, n8 data) -> void {
  ym2610.write(address, data);
}

auto OPNB::readPCMA(u32 address) -> u8 {
  //CD: ADPCM samples live in the 1MiB PCM DRAM (pcmRam), uploaded via the
  //transfer area. (cartridge.readVA returns 0xff on CD — no cartridge board.)
  return system.readVA(address);
}

auto OPNB::readPCMB(u32 address) -> u8 {
  return system.readVB(address);
}

}
