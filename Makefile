obj-m += guru_drv.o

GIT_VERSION := "$(shell git -C $(PWD) describe --tags --dirty --always)"
BUILD_TYPE ?= Release
ifeq ($(BUILD_TYPE), Debug)
ccflags-y += -O0 -ggdb3
endif
ccflags-y += -Wall -Wextra -Wno-type-limits -Wno-unused-variable -Wno-unused-parameter -DGURU_GIT_VERSION=\"$(GIT_VERSION)\"

KERNEL_VERSION := $(shell uname -r)
LINUX_SRC_PATH ?= /lib/modules/$(KERNEL_VERSION)/build

guru_drv-y := guru-drv.o guru-device.o canguru-lite-device.o canguru-net.o guru-msg-std.o canguru-msg-net.o

MK_MOD_CMD := $(MAKE) -C $(LINUX_SRC_PATH) M=$(PWD) modules C=1

all: clean drv
	
drv:
	$(MK_MOD_CMD)
	
clean:
	make -C $(LINUX_SRC_PATH) M=$(PWD) clean

.PHONY: drv
