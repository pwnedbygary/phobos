//Asynchronous file requests (IoFileMgrForUser's *Async functions): a program asks for a file to be opened, read,
//written, seeked, closed or sent an ioctl, gets on with something else, and later polls the file or waits on it for
//the result. Games use them to load while a loading screen animates, and to stream music and data from the disc.
//
//Each open file takes one request at a time. The request is done here as it's made (the bytes are read into the
//program's memory, the position moves), but its result is held back until the time the file's device would have
//taken has passed: until then a poll finds it under way (1) and a wait blocks the thread. Once the time is up
//(asyncEvents()), the result waits on the file until the program takes it, with sceIoPollAsync, sceIoWaitAsync,
//sceIoWaitAsyncCB or sceIoGetAsyncStat; a thread waiting already takes it then and there (of several, the first to
//begin waiting; the others are told there's none), and a callback set with sceIoSetAsyncCallback is notified. The
//result is 64 bits (SceInt64): what the synchronous function would have returned, an error sign-extended so that
//games testing it for a negative number see one.
//
//What's known and what's chosen. pspsdk's pspiofilemgr.h gives the functions and their arguments, and pspkerror.h
//the errors: ASYNC_BUSY for a file whose request is still under way, NOASYNC for one with no request to wait for.
//pspautotests has no test of these functions, so the rest is chosen as games accept it and isn't measured:
//- The time a request takes: 100 microseconds for the request itself, plus its bytes at the device's rate: the UMD
//  drive's top rate, 11 megabits a second (1,375,000 bytes), for the disc; 4 MB a second for the memory stick (and
//  a host folder standing for the disc). The drive's seeks aren't counted. So a game's loading screen lasts about
//  as long as on a PSP. The priority sceIoChangeAsyncPriority gives the request's thread is checked but not used:
//  a request takes its device's time whatever the priority.
//- A synchronous function on a file whose request is under way is refused (ASYNC_BUSY), as is another request.
//  A request done whose result the program hasn't taken is overwritten by the next one, as nothing waits for it.
//- sceIoOpenAsync gives a descriptor at once, even when the open fails: the descriptor then holds only the error,
//  as its result, and goes once that's taken (as a file closed with sceIoCloseAsync does). The result of an open
//  that worked is the descriptor itself.

namespace {
  constexpr u64 RequestCycles = Kernel::CPUFrequency / 10'000;  //100 microseconds
  constexpr u64 DiscBytesPerSecond = 1'375'000, StickBytesPerSecond = 4'000'000;
}

//Whether a file has an asynchronous request under way: then nothing else may be done with it (ASYNC_BUSY).
auto Kernel::asyncBusy(u32 file) const -> bool {
  auto found = files.find(file);
  return found != files.end() && found->second.async == OpenFile::Async::Pending;
}

//How long a request moving so many bytes takes on the disc or the memory stick, in cycles.
auto Kernel::asyncDuration(bool onDisc, u64 bytes) const -> u64 {
  return RequestCycles + bytes * CPUFrequency / (onDisc ? DiscBytesPerSecond : StickBytesPerSecond);
}

//The file a request goes to: open (not a folder, nor a descriptor kept only for a result), with no request under
//way. Null, with the error for the result, if it can't take one.
auto Kernel::asyncIssue(u32 file) -> OpenFile* {
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || found->second.resultOnly) return result(ErrorBadFile), nullptr;
  if(found->second.async == OpenFile::Async::Pending) return result(ErrorAsyncBusy), nullptr;
  return &found->second;
}

//A request done now with this result, moving so many bytes: the result is held back for the time that takes.
auto Kernel::asyncStart(OpenFile& open, s64 value, u64 bytes) -> void {
  open.async = OpenFile::Async::Pending;
  open.asyncResult = u64(value);
  open.asyncDoneAt = cycles + asyncDuration(open.onDisc, bytes);
}

//The program takes a done request's result: it's written at pointer (a SceInt64, if there's a pointer), and the
//file has no request any more. A descriptor kept only for the result goes.
auto Kernel::asyncTake(u32 file, u32 pointer) -> void {
  auto found = files.find(file);
  if(found == files.end()) return;
  auto& open = found->second;
  if(pointer && memory.reaches(pointer, 8)) {
    memory.write(4, pointer, u32(open.asyncResult));
    memory.write(4, pointer + 4, u32(open.asyncResult >> 32));
  }
  open.async = OpenFile::Async::None;
  if(open.resultOnly) files.erase(found);
}

//The threads waiting on a file's request (still waiting, not running their callbacks), in the order they began.
auto Kernel::asyncWaiters(u32 file) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Async && thread->waitID == file) {
      waiters.push_back(thread.get());
    }
  }
  std::sort(waiters.begin(), waiters.end(), [](Thread* a, Thread* b) { return a->readySince < b->readySince; });
  return waiters;
}

//A thread's wait on a file's request ends as the file now has it: a request done, the thread takes its result (0);
//none left to take (another thread waiting took it, or the thread's own callback did, polling), NOASYNC; the file
//gone (its descriptor went with the result it was kept for, or it was closed), a bad file. Each is what a wait begun
//then would be told: pspautotests' intr/waits recorded NOASYNC for a wait on a request whose result had been taken.
//A request still under way goes on being waited for: false.
auto Kernel::asyncResume(Thread& thread) -> bool {
  u32 file = thread.waitID, pointer = thread.waitPointer;
  auto found = files.find(file);
  if(found == files.end()) return ready(thread, ErrorBadFile), true;
  if(found->second.async == OpenFile::Async::Pending) return false;
  if(found->second.async == OpenFile::Async::None) return ready(thread, ErrorNoAsync), true;
  ready(thread, 0);
  asyncTake(file, pointer);
  return true;
}

//Requests whose time is up are done: each file's callback is notified, and every thread waiting on it runs on, the
//first to have begun waiting taking the result, any others finding none (asyncResume()). A thread waiting where its
//callbacks may run, whose callback that is, runs the callback first and ends its wait after it (resumeWait()).
//Returns whether a thread woke.
auto Kernel::asyncEvents() -> bool {
  bool woke = false;
  std::vector<u32> done;
  for(auto& [file, open] : files) {
    if(open.async == OpenFile::Async::Pending && cycles >= open.asyncDoneAt) done.push_back(file);
  }
  for(u32 file : done) {
    auto& open = files[file];
    open.async = OpenFile::Async::Done;
    if(open.asyncCallback && notifyCallback(open.asyncCallback, open.asyncArgument)) woke = true;
    for(auto thread : asyncWaiters(file)) woke = asyncResume(*thread) || woke;
  }
  return woke;
}

//When the next request is done (never: ~0).
auto Kernel::nextAsyncEvent() const -> u64 {
  u64 next = ~0ull;
  for(auto& [file, open] : files) {
    if(open.async == OpenFile::Async::Pending) next = std::min(next, open.asyncDoneAt);
  }
  return next;
}

//(path, flags, mode): the file is opened as sceIoOpen opens it, and its descriptor returned at once; the request's
//result is the descriptor, or why it couldn't be opened (the descriptor then holding only that). With no
//descriptor left to give, the call itself fails.
auto Kernel::sceIoOpenAsync() -> void {
  std::string path = memory.readString(arg(0), 1024);
  bool disc = onDisc(path);
  u32 file = openFile(path, arg(1));
  if(file == ErrorTooManyFiles) return result(file);
  if(s32(file) < 0) {
    u32 error = file;
    file = newFile();
    if(!file) return result(ErrorTooManyFiles);
    auto& shell = files[file];
    shell.path = path;
    shell.onDisc = disc;
    shell.resultOnly = true;
    asyncStart(shell, s32(error), 0);
    return result(file);
  }
  asyncStart(files[file], file, 0);
  result(file);
}

//(file): closed now; the descriptor stays until the request's result (0) is taken.
auto Kernel::sceIoCloseAsync() -> void {
  auto open = asyncIssue(arg(0));
  if(!open) return;
  open->stream.reset();
  open->flags = 0;
  open->entries.clear();
  open->resultOnly = true;
  asyncStart(*open, 0, 0);
  result(0);
}

//(file, data, size): read as sceIoRead reads; the result is how many bytes (umd0:'s sectors) were read.
auto Kernel::sceIoReadAsync() -> void {
  u32 file = arg(0);
  auto open = asyncIssue(file);
  if(!open) return;
  u32 got = readFile(file, arg(1), arg(2));
  u64 bytes = s32(got) < 0 ? 0 : u64(got) * (open->sectors ? Disc::SectorSize : 1);
  asyncStart(*open, s32(got), bytes);
  result(0);
}

//(file, data, size): written as sceIoWrite writes; the result is how many bytes were written.
auto Kernel::sceIoWriteAsync() -> void {
  u32 file = arg(0);
  auto open = asyncIssue(file);
  if(!open) return;
  u32 wrote = writeFile(file, arg(1), arg(2));
  asyncStart(*open, s32(wrote), s32(wrote) < 0 ? 0 : wrote);
  result(0);
}

//(file, 64-bit offset in a2 and a3, whence in t0, as sceIoLseek takes them): the result is the new position.
auto Kernel::sceIoLseekAsync() -> void {
  u32 file = arg(0);
  auto open = asyncIssue(file);
  if(!open) return;
  u64 position = 0;
  u32 error = seek(file, s64(u64(arg(3)) << 32 | arg(2)), arg(4), position);
  asyncStart(*open, error ? s64(s32(error)) : s64(position), 0);
  result(0);
}

//(file, 32-bit offset, whence): the same.
auto Kernel::sceIoLseek32Async() -> void {
  u32 file = arg(0);
  auto open = asyncIssue(file);
  if(!open) return;
  u64 position = 0;
  u32 error = seek(file, s32(arg(1)), arg(2), position);
  asyncStart(*open, error ? s64(s32(error)) : s64(position), 0);
  result(0);
}

//(file, command, in, in length, out, out length): answered as sceIoIoctl answers; the result is what it returns. A
//request takes the time of moving what it put in its output, not of the output's length, which a game may give as
//anything (0xffffffff had made a 4-byte answer due in 52 minutes).
auto Kernel::sceIoIoctlAsync() -> void {
  u32 file = arg(0);
  auto open = asyncIssue(file);
  if(!open) return;
  u64 moved = 0;
  u32 answer = ioctl(file, arg(1), arg(2), arg(3), arg(4), arg(5), &moved);
  asyncStart(*open, s32(answer), moved);
  result(0);
}

//Polls a file's request (file, where to put its result): 0 and the result if it's done; 1 while it's under way;
//NOASYNC if the file has none (none made, or its result taken already).
auto Kernel::asyncPoll(u32 file, u32 pointer) -> void {
  auto found = files.find(file);
  if(found == files.end() || found->second.folder) return result(ErrorBadFile);
  if(found->second.async == OpenFile::Async::None) return result(ErrorNoAsync);
  if(found->second.async == OpenFile::Async::Pending) return result(1);
  result(0);
  asyncTake(file, pointer);
}

auto Kernel::sceIoPollAsync() -> void {
  asyncPoll(arg(0), arg(1));
}

//Waits for a file's request (file, where to put its result; with callbacks, running the thread's callbacks as
//they're notified): 0 and the result once it's done, at once if it is. A bad file, and one with no request, are
//refused ahead of whether the thread may wait, as intr/waits recorded (a bad file in an interrupt handler, no request
//with dispatching held off).
auto Kernel::asyncWait(u32 file, u32 pointer, bool callbacks) -> void {
  auto found = files.find(file);
  if(found == files.end() || found->second.folder) return result(ErrorBadFile);
  if(found->second.async == OpenFile::Async::None) return result(ErrorNoAsync);
  if(!mayWait()) return;
  result(0);
  if(found->second.async == OpenFile::Async::Done) {
    asyncTake(file, pointer);
    return callbacksOnReturn(callbacks);
  }
  if(!current) return;
  current->waitPointer = pointer;
  current->readySince = ++readySequence;  //its place among those waiting on the file
  block(Wait::Async, file, 0, 0, callbacks);
}

auto Kernel::sceIoWaitAsync() -> void {
  asyncWait(arg(0), arg(1), false);
}

auto Kernel::sceIoWaitAsyncCB() -> void {
  asyncWait(arg(0), arg(1), true);
}

//(file, poll, where to put the result): a poll if poll isn't 0, else a wait (without callbacks).
auto Kernel::sceIoGetAsyncStat() -> void {
  if(arg(1)) return asyncPoll(arg(0), arg(2));
  asyncWait(arg(0), arg(2), false);
}

//(file, priority): the priority of the thread that does a file's requests; file -1 for files opened from now on.
//A user thread's priorities (0x08-0x77) are taken, as games pass them (Burnout Legends 0x65, GTA 0x40, SOCOM 0x13);
//others are refused as sceKernelChangeThreadPriority refuses them. Requests take their device's time whatever the
//priority, so it isn't kept.
auto Kernel::sceIoChangeAsyncPriority() -> void {
  u32 file = arg(0), priority = arg(1);
  if(priority < 0x08 || priority > 0x77) return result(ErrorIllegalPriority);
  if(file != 0xffff'ffff) {
    auto found = files.find(file);
    if(found == files.end() || found->second.folder || found->second.resultOnly) return result(ErrorBadFile);
  }
  result(0);
}

//(file, callback, argument): the callback is notified, with the argument as its word, each time a request on the
//file is done; callback 0 clears it.
auto Kernel::sceIoSetAsyncCallback() -> void {
  u32 file = arg(0), callback = arg(1);
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || found->second.resultOnly) return result(ErrorBadFile);
  if(callback && !callbacks.count(callback)) return result(ErrorUnknownCallback);
  found->second.asyncCallback = callback;
  found->second.asyncArgument = arg(2);
  result(0);
}
