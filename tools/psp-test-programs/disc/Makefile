#disc: built with pspdev's toolchain (psp-config on the PATH). make gives a static executable (disc.elf), which the
#test puts in a disc image itself. See ../build.sh.
TARGET = disc
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
LIBS = -lpspumd

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
