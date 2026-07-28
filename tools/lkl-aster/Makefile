# SPDX-License-Identifier: GPL-2.0

C ?= default
PCI_MSI ?= y

ifneq ($(OUTPUT),)
override OUTPUT := $(abspath $(OUTPUT))
$(shell mkdir -p $(OUTPUT))
endif

include ../scripts/Makefile.include

ifeq ($(OUTPUT),)
OUTPUT := $(CURDIR)/build
OUTBIN := $(CURDIR)/lkl.bin
else
OUTBIN := $(OUTPUT)/lkl.bin
endif

KOPT := O=$(OUTPUT)

CONFIG   := $(OUTPUT)/.config
CONFIG_H := $(OUTPUT)/arch/lkl/include/generated/uapi/asm/config.h

LKLOBJ   := $(OUTPUT)/lkl.o
SOURCES  := main.c setup.S
LINKSCR  := bin.lds
OUTELF   := $(OUTPUT)/lkl.elf

LKL_CFLAGS  := -nostdlib -fno-pic -no-pie -mcmodel=kernel \
	       -mno-sse -mno-avx -mno-red-zone -fcf-protection
export LKL_CFLAGS

# This is required by the NVIDIA Linux open GPU kernel modules. See
# https://github.com/NVIDIA/open-gpu-kernel-modules/blob/2ccbad25e1af6a6ee6f38cf569f89f8b65d658ab/kernel-open/conftest.sh#L5453-L5458
IGNORE_MISSING_MODULE_SYMVERS := 1
export IGNORE_MISSING_MODULE_SYMVERS

.PHONY: all
all: $(OUTBIN)

$(OUTPUT):
	$(Q)mkdir -p $@

$(CONFIG): | $(OUTPUT)
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) allnoconfig
	$(call QUIET_INSTALL, .config)cat configs/config >> $@
	$(call QUIET_INSTALL, .config)cat configs/config.$(C) >> $@
	$(call QUIET_INSTALL, .config)if [ "$(PCI_MSI)" = "y" ]; then \
		echo "CONFIG_PCI_MSI=y" >> $@; \
	fi
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) oldnoconfig

$(CONFIG_H):
	$(Q)mkdir -p $$(dirname $@)
	$(call QUIET_INSTALL, config.h)cp configs/config.h $@

.PHONY: $(LKLOBJ) # Force kernel complication in case we change the sources.
$(LKLOBJ): $(CONFIG) $(CONFIG_H)
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT)
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) install INSTALL_PATH=$(OUTPUT)

$(OUTELF): $(LKLOBJ) $(SOURCES) $(LINKSCR)
	$(Q)gcc $(LKL_CFLAGS) -Wall -Wextra -O2 \
		-I$(OUTPUT)/include -I$(OUTPUT)/include/lkl \
		-Wl,-T$(LINKSCR) -o $@ $(LKLOBJ) $(SOURCES)

$(OUTBIN): $(OUTELF)
	$(Q)objcopy -O binary $< $@
