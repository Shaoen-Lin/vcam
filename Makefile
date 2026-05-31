obj-m := my_vcam.o
my_vcam-objs := module.o control.o device.o videobuf.o fb.o drm.o

CFLAGS_utils = -O2 -Wall -Wextra -pedantic -std=c99

.PHONY: all
all: kmod vcam-util

vcam-util: vcam-util.c vcam.h
	$(CC) $(CFLAGS_utils) -o $@ $<

kmod:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

.PHONY: clean
clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	$(RM) vcam-util