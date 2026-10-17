#psp-ge-profile's load test (../README.md, "If nothing appears on screen"): hello.c built as phoboshello.prx, a kernel
#module made the way ../plugin's phobosprof.prx is, and phoboshellou.prx, the same as a user module. Built with
#pspdev's toolchain (psp-config on the PATH). `make hosttest` runs both on the Mac.
all:
	$(MAKE) -f kernel.mak
	$(MAKE) -f user.mak

clean:
	$(MAKE) -f kernel.mak clean
	$(MAKE) -f user.mak clean
	rm -f hosttest-kernel hosttest-user

PSPSDK = $(shell psp-config --pspsdk-path)
HOSTFLAGS = -std=gnu11 -O1 -Wall -Wextra -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -DHOST -I$(PSPSDK)/include

hosttest: hosttest.c hello.c
	cc $(HOSTFLAGS) -o hosttest-kernel hosttest.c
	cc $(HOSTFLAGS) -DUSER_MODULE -o hosttest-user hosttest.c
	./hosttest-kernel
	./hosttest-user

.PHONY: all clean hosttest
