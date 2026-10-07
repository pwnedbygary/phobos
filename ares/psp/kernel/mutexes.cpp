//Kernel mutexes (ThreadManForUser's sceKernel*Mutex): a lock one thread holds at a time, kept by the kernel, where
//a lightweight mutex keeps its state in the program's own memory (threads.cpp).
//
//The thread that locks a free mutex holds it. One made with the recursive attribute (0x200) may be locked again by
//its holder, each lock counted (a count of more than 1 at once too), and it's free again once unlocked as many
//times; any other mutex counts 1 at most. A thread that finds the mutex held by another waits, first come first
//served, or by priority with attribute 0x100 (the longest waiting among equals); as the holder frees it, it goes
//straight to the first of them, held with the count that thread asked for, so a free mutex never has waiters. A
//thread that ends while it holds mutexes frees them, each going on to its next waiter. Deleting a mutex wakes its
//waiters (WAIT_DELETE); cancelling it wakes them too (WAIT_CANCEL) and sets its count. Nobody's priority changes
//for a mutex: a holder keeps its own whoever waits behind it.
//
//What each call checks, in what order, and with which errors (0x800201c3 to 0x800201c8, which pspsdk's pspkerror.h
//doesn't name: the names here are ours), what a status tells and who gets a mutex next, are as pspautotests'
//threads/mutex programs (create, lock, try, unlock, unlock2, cancel, delete, refer, priority, mutex) and
//threads/scheduling/mutexhandoff recorded them on a PSP, and intr/waits (a lock where no thread may wait is refused
//before its mutex or count is looked at).

//Whether a lock or unlock of count is one the mutex takes: at least 1, and 1 at most unless it's recursive.
static auto mutexCount(const Kernel::Mutex& mutex, s32 count) -> bool {
  return count > 0 && (count == 1 || mutex.attributes & 0x200);
}

//The threads waiting for the mutex, in the order it goes to them.
auto Kernel::mutexWaiters(const Mutex& mutex) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Mutex && thread->waitID == mutex.uid) {
      waiters.push_back(thread.get());
    }
  }
  bool byPriority = mutex.attributes & 0x100;
  std::sort(waiters.begin(), waiters.end(), [&](Thread* a, Thread* b) {
    if(byPriority && a->priority != b->priority) return a->priority < b->priority;
    return a->readySince < b->readySince;
  });
  return waiters;
}

//The mutex has just been freed: it goes to the first thread waiting for it, if any, held with the count that thread
//asked for. (The caller reschedules.)
auto Kernel::mutexHandOver(Mutex& mutex) -> void {
  auto waiters = mutexWaiters(mutex);
  if(waiters.empty()) return;
  auto next = waiters.front();
  mutex.count = next->waitCount;
  mutex.owner = next->uid;
  ready(*next, 0);
}

//A thread has ended (endThread()): the mutexes it held are free, each going on to its next waiter.
auto Kernel::mutexesFreed(u32 thread) -> void {
  for(auto& [uid, mutex] : mutexes) {
    if(!mutex.count || mutex.owner != thread) continue;
    mutex.count = 0;
    mutex.owner = 0;
    mutexHandOver(mutex);
  }
}

//(name, attributes, initial count, options): a mutex, free, or held by the calling thread with its initial count.
//A name is needed (ERROR without one); the attributes may be any of the low twelve bits but 0x400 (ILLEGAL_ATTR
//otherwise); the count can't be negative, nor more than 1 unless the mutex is recursive (ILLEGAL_COUNT); one held
//needs a thread to hold it (fromThread()). The options are read for nothing: any size is taken.
auto Kernel::sceKernelCreateMutex() -> void {
  u32 name = arg(0), attributes = arg(1);
  s32 count = s32(arg(2));
  if(!name) return result(ErrorError);
  if(attributes & ~0xbffu) return result(ErrorIllegalAttribute);
  if(count < 0 || (count > 1 && !(attributes & 0x200))) return result(ErrorIllegalCount);
  if(count && !fromThread()) return;
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  u32 owner = count ? current->uid : 0;
  mutexes[uid] = {uid, memory.readString(name, 31), attributes, count, count, owner};
  result(uid);
}

//(mutex): it's gone; the threads waiting for it are told so (WAIT_DELETE), the best of them running first.
auto Kernel::sceKernelDeleteMutex() -> void {
  auto found = mutexes.find(arg(0));
  if(found == mutexes.end()) return result(ErrorUnknownMutex);
  for(auto thread : mutexWaiters(found->second)) ready(*thread, ErrorWaitDeleted);
  mutexes.erase(found);
  result(0);
  reschedule();
}

//(mutex, count, timeout): takes the mutex, waiting while another thread holds it (for the timeout's microseconds at
//most, if one is given; what's left of it is written back as the wait ends); with callbacks, the thread's callbacks
//run as they're notified meanwhile, and those notified already run even when it needn't wait. Its holder locking
//it again adds to its count if it's recursive (LOCK_OVERFLOW past 2^31 - 1), else is refused
//(RECURSIVE_NOT_ALLOWED). A timeout of 0 on a mutex another holds gives up at once, its time left as it was.
auto Kernel::lockMutex(bool callbacks) -> void {
  if(!mayWait() || !fromThread()) return;
  u32 timeoutPointer = arg(2);
  s32 count = s32(arg(1));
  auto found = mutexes.find(arg(0));
  if(found == mutexes.end()) return result(ErrorUnknownMutex);
  auto& mutex = found->second;
  if(!mutexCount(mutex, count)) return result(ErrorIllegalCount);
  if(!mutex.count || mutex.owner == current->uid) {
    if(mutex.count && !(mutex.attributes & 0x200)) return result(ErrorMutexRecursion);
    if(count > 0x7fff'ffff - mutex.count) return result(ErrorMutexOverflow);
    mutex.count += count;
    mutex.owner = current->uid;
    result(0);
    return callbacksOnReturn(callbacks);
  }
  if(timeoutPointer && !memory.read(4, timeoutPointer)) return result(ErrorWaitTimeout);
  result(0);
  current->waitCount = count;
  current->readySince = ++readySequence;  //its place in the line
  blockTimed(Wait::Mutex, mutex.uid, timeoutPointer, callbacks);
}

auto Kernel::sceKernelLockMutex() -> void {
  lockMutex(false);
}

auto Kernel::sceKernelLockMutexCB() -> void {
  lockMutex(true);
}

//(mutex, count): as locking, but never waits: a mutex another thread holds is refused (MUTEX_LOCKED). Only a thread
//may (fromThread()).
auto Kernel::sceKernelTryLockMutex() -> void {
  if(!fromThread()) return;
  s32 count = s32(arg(1));
  auto found = mutexes.find(arg(0));
  if(found == mutexes.end()) return result(ErrorUnknownMutex);
  auto& mutex = found->second;
  if(!mutexCount(mutex, count)) return result(ErrorIllegalCount);
  if(mutex.count && mutex.owner != current->uid) return result(ErrorMutexLocked);
  if(mutex.count && !(mutex.attributes & 0x200)) return result(ErrorMutexRecursion);
  if(count > 0x7fff'ffff - mutex.count) return result(ErrorMutexOverflow);
  mutex.count += count;
  mutex.owner = current->uid;
  result(0);
}

//(mutex, count): takes count off the holder's locks; at none, the mutex goes to its next waiter, who runs at once if
//it's better than the caller. Only the holder may (MUTEX_UNLOCKED for a free mutex or another's), and no more than
//it holds (UNLOCK_UNDERFLOW). The count is checked before who holds it. Only a thread may (fromThread()).
auto Kernel::sceKernelUnlockMutex() -> void {
  if(!fromThread()) return;
  s32 count = s32(arg(1));
  auto found = mutexes.find(arg(0));
  if(found == mutexes.end()) return result(ErrorUnknownMutex);
  auto& mutex = found->second;
  if(!mutexCount(mutex, count)) return result(ErrorIllegalCount);
  if(!mutex.count || mutex.owner != current->uid) return result(ErrorMutexUnlocked);
  if(count > mutex.count) return result(ErrorUnlockUnderflow);
  result(0);
  mutex.count -= count;
  if(mutex.count) return;
  mutex.owner = 0;
  mutexHandOver(mutex);
  reschedule();
}

//(mutex, new count, where to put how many threads waited): every thread waiting for it is told the wait was
//cancelled (WAIT_CANCEL), and its count becomes the new one: 0 frees it, more makes the caller its holder, so only a
//thread may (fromThread()). A count more than 1 is refused for a mutex that isn't recursive (ILLEGAL_COUNT, nothing
//written). A negative count is taken as the count the mutex was made with. What threads/mutex/cancel recorded of
//one: -1 and -3 accepted on a mutex made free, which is free afterwards; that fits "free" as well as "as it was
//made". The PSP takes a negative count where it refuses one too big (2 for this mutex: ILLEGAL_COUNT), so it isn't
//read as a count, but as something else to set; going back to the count it was made with is the one such meaning,
//and for every mutex the recordings cancel it's free too.
auto Kernel::sceKernelCancelMutex() -> void {
  if(!fromThread()) return;
  s32 count = s32(arg(1));
  auto found = mutexes.find(arg(0));
  if(found == mutexes.end()) return result(ErrorUnknownMutex);
  auto& mutex = found->second;
  if(count > 1 && !(mutex.attributes & 0x200)) return result(ErrorIllegalCount);
  if(count < 0) count = mutex.initial;
  auto waiters = mutexWaiters(mutex);
  if(arg(2)) memory.write(4, arg(2), waiters.size());
  for(auto thread : waiters) ready(*thread, ErrorWaitCancelled);
  mutex.owner = count ? current->uid : 0;
  mutex.count = count;
  result(0);
  reschedule();
}

//(mutex, info): a SceKernelMutexInfo (pspautotests' threads/mutex/shared.h: its size, 56; the name; the attributes;
//the initial and current counts; the holder, -1 for none; how many threads wait), copied as far as the size in its
//first word, whatever that is (a size of 1 copies the size's first byte: 56 still reads back): a size of 0 has
//nothing copied.
auto Kernel::sceKernelReferMutexStatus() -> void {
  auto found = mutexes.find(arg(0));
  if(found == mutexes.end()) return result(ErrorUnknownMutex);
  auto& mutex = found->second;
  u32 info = arg(1), size = memory.read(4, info);
  u8 status[56] = {};
  auto word = [&](u32 offset, u32 value) { for(u32 n : range(4)) status[offset + n] = value >> n * 8; };
  word(0, sizeof(status));
  memcpy(status + 4, mutex.name.data(), std::min<size_t>(mutex.name.size(), 31));
  word(36, mutex.attributes);
  word(40, mutex.initial);
  word(44, mutex.count);
  word(48, mutex.count ? mutex.owner : 0xffff'ffff);
  word(52, mutexWaiters(mutex).size());
  memory.copyIn(info, status, std::min<u32>(size, sizeof(status)));
  result(0);
}
