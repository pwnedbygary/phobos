namespace ares::NeoGeo {

auto Cdd::serialize(serializer& s) -> void {
  s(rx);
  s(tx);
  s(wordCount);
  s(clock);
  s(statusHack);
  s(status);
  s(curStatus);
  s(settleCounter);
  s(min);
  s(sec);
  s(frame);
  s(ext);
  s(control);
  s(statusCdc);
  s(curLba);
  s(curTrack);
  s(reg2);
  s(latch16);
  s(type1Pending);
  s(type2Pending);
  s(type3Pending);
  s(type1Ack);
  s(type2Ack);
  s(type3Ack);
  s(prohibitIrq);
  s(fade);
  s(lastLeft);
  s(lastRight);
}

auto Cdc::serialize(serializer& s) -> void {
  s(reg0);
  s(reg1);
  s(wreg);
  s(rreg);
  s(buffer);
  s(decode);
}

auto Dma::serialize(serializer& s) -> void {
  s(address1);
  s(address2);
  s(value1);
  s(value2);
  s(count);
  s(mode);
}

}
