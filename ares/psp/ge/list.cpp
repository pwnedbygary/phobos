//Reading the display list: the commands that move about it, stop it, or fill in the GE's registers.

//A matrix's next element, where its NUMBER command left off. Past the matrix's end, data goes nowhere.
static auto matrixData(u32* matrix, u32 size, u32& index, u32 argument) -> void {
  if(index < size) matrix[index++] = argument;
}

auto GE::run(u64 budget) -> Stop {
  u64 ran = 0;
  return run(budget, ran);
}

//Runs commands from list.address until something stops it (the stall address, an END, a fault) or budget commands
//have run (Busy: call again to go on); ran says how many did, the one that stopped it among them. Every command's
//word is kept, whatever the command. Meanwhile primitives wait to be drawn together (threads.cpp), and as it returns
//they're drawn, or go on being drawn by the GE's workers while the CPU runs on.
auto GE::run(u64 budget, u64& ran) -> Stop {
  settle();  //what the last list left being drawn
  drawing.deferring = drawing.threads > 1;
  struct Drawn {
    GE& ge;
    ~Drawn() { ge.drawing.deferring = false; ge.launch(true); }
  } drawn{*this};
  for(ran = 0; ran < budget;) {
    if(list.stall && list.address == list.stall) return Stop::Stalled;
    u32 at = list.address;
    drawnFirst(at, 4);  //(a list in VRAM, where what waits may draw)
    u32 word = memory.read(4, at);
    ran++;
    list.address = (at + 4) & 0x0fff'ffff;
    u32 command = word >> 24, argument = word & 0xff'ffff;
    commands[command] = word;
    switch(command) {
    case VertexAddress: vertexAddress = relative(argument); break;
    case IndexAddress:  indexAddress = relative(argument); break;
    case Primitive:     primitive(argument >> 16 & 7, argument & 0xffff); break;
    case Bezier:        patch(false, argument); break;
    case Spline:        patch(true, argument); break;
    case BoundingBox:   boxOutside = boundingBox(argument & 0xffff); break;
    case ConditionalJump:  //where JUMP would go, if the last BOUNDING_BOX was out of sight (pspsdk's sceGuEndObject)
      if(boxOutside) list.address = relative(argument & ~3u);
      break;
    case Jump:          list.address = relative(argument & ~3u); break;
    case Call:
      if(list.depth == 2) {
        note("a display list CALLed three deep, and the GE has room for two");
        return Stop::Faulted;
      }
      list.returnAddress[list.depth] = list.address;
      list.returnOffset[list.depth] = list.offset;
      list.depth++;
      list.address = relative(argument & ~3u);
      break;
    case Return:
      if(list.depth == 0) {
        note("a display list RETurned with no CALL to return from");
        return Stop::Faulted;
      }
      list.depth--;
      list.address = list.returnAddress[list.depth];
      list.offset = list.returnOffset[list.depth];
      break;
    case Signal: signalWord = word; pending = Stop::Signaled; break;
    case Finish: finishWord = word; pending = Stop::Finished; break;
    case End: {
      endWord = word;
      auto stop = pending;
      pending = Stop::Ended;
      return stop;
    }
    case OffsetAddress: list.offset = argument << 8; break;
    case Origin:        list.offset = at; break;  //the ORIGIN command's own address
    case BoneMatrixNumber:       boneIndex = argument & 0x7f; break;
    case BoneMatrixData:         matrixData(bones, 96, boneIndex, argument); break;
    case WorldMatrixNumber:      worldIndex = argument & 0xf; break;
    case WorldMatrixData:        matrixData(world, 12, worldIndex, argument); break;
    case ViewMatrixNumber:       viewIndex = argument & 0xf; break;
    case ViewMatrixData:         matrixData(view, 12, viewIndex, argument); break;
    case ProjectionMatrixNumber: projectionIndex = argument & 0xf; break;
    case ProjectionMatrixData:   matrixData(projection, 16, projectionIndex, argument); break;
    case TextureMatrixNumber:    textureIndex = argument & 0xf; break;
    case TextureMatrixData:      matrixData(textureMatrix, 12, textureIndex, argument); break;
    case TransferStart:          flush(), transfer(); break;
    case ClutLoad:               loadClut(); break;
    }
  }
  return Stop::Busy;
}

//An address from a command's argument: BASE's bits 16-19 become its bits 24-27, and the offset is added. (That the
//offset is added here, and wraps at 28 bits, is as PPSSPP has it; pspsdk's library only ever uses BASE.)
auto GE::relative(u32 argument) const -> u32 {
  u32 address = (commands[Base] << 8 & 0x0f00'0000) | (argument & 0xff'ffff);
  return (address + list.offset) & 0x0fff'ffff;
}
