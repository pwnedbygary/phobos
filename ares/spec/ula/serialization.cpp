auto ULA::serialize(serializer& s) -> void {
  Thread::serialize(s);
  s(io.borderColor);
  s(io.mic);
  s(io.ear);
  s(hcounter);
  s(vcounter);
  s(flashFrameCounter);
  s(flashState);
  s(busValue);
}
