struct Accuracy {
  //enable all accuracy flags
  static constexpr bool Reference = 0;

  struct CPU {
    static constexpr bool Interpreter = 0 | Reference | !recompiler::generic::supported;
    static constexpr bool Recompiler = !Interpreter;

    //Maximum number of cycles to run the CPU without synchronization.
    //Raised 4x vs upstream (4096*2) for the Android port: the deep AAudio
    //buffer absorbs coarser AI stepping, VI still refreshes every frame
    //(cpu.main exits on vi.refreshed), and the compare/queue timers still
    //bound the budget below this — so the only effect is quartering the
    //synchronize() call rate (each sync steps all peripherals + profiles).
    //Combined with the auto frame-skip, CPU-bound frames get noticeably
    //cheaper on a phone-class big core.
    static constexpr s64 JitInterleaving = 4096 * 8;

    //exceptions when the CPU accesses unaligned memory addresses
    static constexpr bool AddressErrors = 1 | Reference;
  };

  struct RSP {
    static constexpr bool Interpreter = 0 | Reference | !recompiler::generic::supported;
    static constexpr bool Recompiler = !Interpreter;

    //VU instructions
    static constexpr bool SISD = 0 | Reference | !ARCHITECTURE_SUPPORTS_SSE4_1;
    static constexpr bool SIMD = !SISD;
  };

  struct PIF {
    // Emulate a region-locked console
    static constexpr bool RegionLock = false;
    // Emulate the PIF's checksum security check
    static constexpr bool IPL2Checksum = true;
  };
};
