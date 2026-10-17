#phoboshellou.prx: hello.c as a user module (see Makefile), with the same flags; only pspsdk's user stubs and its
#module-information bounds are linked (-nostdlib, as USE_KERNEL_LIBS gives the kernel build: no C library, no
#start-up file of pspsdk's).
TARGET = phoboshellou
OBJS = hello-user.o exports.o
PRX_EXPORTS = exports.exp
BUILD_PRX = 1
LIBS = -nostdlib -lpspuser -lpspmodinfo
CFLAGS = -O2 -G0 -Wall -Wextra -fno-pic -fno-builtin -fno-tree-loop-distribute-patterns -std=gnu11 -DUSER_MODULE
ASFLAGS = $(CFLAGS)

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak

hello-user.o: hello.c
	$(CC) $(CFLAGS) -c -o $@ $<
