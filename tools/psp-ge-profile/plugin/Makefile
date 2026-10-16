#psp-ge-profile's plugin: a kernel PRX for ARK's plugin loader (see ../README.md). Built with pspdev's toolchain
#(psp-config on the PATH): make gives phobosprof.prx. -G0: hooks run in the game's syscalls and interrupts, where gp
#is the game's or the firmware's, never this module's. The kernel's libc isn't linked (text.c has what's needed), and
#GCC mustn't turn text.c's loops back into calls to memset and memcpy.
TARGET = phobosprof
OBJS = main.o hooks.o text.o systemctrl.o
PRX_EXPORTS = exports.exp
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
CFLAGS = -O2 -G0 -Wall -Wextra -fno-pic -fno-builtin -fno-tree-loop-distribute-patterns -std=gnu11
ASFLAGS = $(CFLAGS)

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

#after build.mak, so that its first rule stays the one make builds; build.mak doesn't follow headers itself
main.o hooks.o text.o: plugin.h profile.h

#The record path on the Mac (hosttest.c: hooks.c and text.c with the firmware faked), then the decoder's tests, which
#read the capture it writes back.
hosttest: hosttest.c hooks.c text.c plugin.h profile.h
	cc -std=gnu11 -O1 -Wall -Wextra -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-unused-function -DHOST \
	  -I. -I$(PSPSDK)/include -o hosttest hosttest.c hooks.c text.c
	./hosttest hosttest.bin
	cd ../decoder && python3 -m unittest test_geprof
.PHONY: hosttest
