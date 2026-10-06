#hello: built with pspdev's toolchain (psp-config on the PATH). make gives a static executable (hello.elf) and an
#EBOOT.PBP holding it; make BUILD_PRX=1, a relocatable module (hello.prx) instead. See ../build.sh.
TARGET = hello
OBJS = main.o
CFLAGS = -O2 -G0 -Wall
LIBS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = hello

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
