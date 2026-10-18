#phoboshello.prx: hello.c as a kernel module, with ../plugin/Makefile's flags and libraries (see Makefile). The
#system's game information comes from pspsdk's 6.60 SysMemForKernel stubs, the rest from its usual kernel ones.
TARGET = phoboshello
OBJS = hello-kernel.o exports.o
PRX_EXPORTS = exports.exp
BUILD_PRX = 1
USE_KERNEL_LIBS = 1
LIBS = -lpspsysmem_kernel_660
CFLAGS = -O2 -G0 -Wall -Wextra -fno-pic -fno-builtin -fno-tree-loop-distribute-patterns -std=gnu11
ASFLAGS = $(CFLAGS)

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

hello-kernel.o: hello.c
	$(CC) $(CFLAGS) -c -o $@ $<
