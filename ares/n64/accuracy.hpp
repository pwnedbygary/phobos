struct Accuracy {
  //enable all accuracy flags
  static constexpr bool Reference = 0;

  struct CPU {
    static constexpr bool Interpreter = 0 | Reference | !recompiler::generic::supported;
    static constexpr bool Recompiler = !Interpreter;

    //Maximum number of cycles to run the CPU without synchronization.
    //FINAL VALUE (2026-08-12): 2048*2. Verified on-device:
    //  - Conker's BFD pub menu: STALL-FREE at 2048*2 (clean 60fps, no N64 STALL)
    //  - Mario Tennis: clearly faster than 1024*2 (gameplay 50-55 → 55-60fps)
    //  - 4096*2 (upstream): NO ADDITIONAL MT benefit (A/B 2026-08-12 — identical
    //    FPS/frame-time; the dips are SI-DMA game-loading waits, not CPU sync
    //    overhead) AND reintroduces the Conker pub freeze → keep 2048*2.
    static constexpr s64 JitInterleaving = 2048 * 2;

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
