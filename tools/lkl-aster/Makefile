# SPDX-License-Identifier: GPL-2.0

C ?= default
PCI_MSI ?= y
SANDBOX ?= kernel

include ../scripts/Makefile.include

ifeq ($(OUTPUT),)
	KOPT := O=$(OUTPUT)
else
	KOPT :=
endif

CONFIG   := $(OUTPUT)/.config
CONFIG_H := $(OUTPUT)/arch/lkl/include/generated/uapi/asm/config.h

LKLOBJ   := $(OUTPUT)/lkl.o
SOURCES  := main.c setup.S
LINKSCR  := bin.lds
OUTELF   := $(OUTPUT)/lkl.elf
OUTBIN   := $(OUTPUT)/lkl.bin

LKL_CFLAGS  := -nostdlib -fno-pic -no-pie \
	       -mno-mmx -mno-sse -mno-avx -mno-red-zone
ifeq ($(SANDBOX),user)
	BASE_ADDRESS = 0x40001000
	LKL_CFLAGS += -mcmodel=small
	UNSAFE_BYPASS_INDIRECT_MASK=1
	export UNSAFE_BYPASS_INDIRECT_MASK
else
	BASE_ADDRESS = 0xffffffffc0001000
	LKL_CFLAGS += -mcmodel=kernel -fcf-protection
endif
ifeq ($(UNSAFE_BYPASS_INDIRECT_MASK),)
	LKL_CFLAGS += -mcet-switch -mindirect-branch-register
endif
export LKL_CFLAGS

# This is required by the NVIDIA Linux open GPU kernel modules. See
# https://github.com/NVIDIA/open-gpu-kernel-modules/blob/2ccbad25e1af6a6ee6f38cf569f89f8b65d658ab/kernel-open/conftest.sh#L5453-L5458
IGNORE_MISSING_MODULE_SYMVERS := 1
export IGNORE_MISSING_MODULE_SYMVERS

.PHONY: all
all: $(OUTBIN)

$(CONFIG):
	$(Q)$(MAKE) -C ../.. ARCH=lkl $(KOPT) allnoconfig
	$(call QUIET_INSTALL, .config)cat configs/config >> $@
	$(call QUIET_INSTALL, .config)cat configs/config.$(C) >> $@
	$(call QUIET_INSTALL, .config)if [[ "$(PCI_MSI)" == "y" ]]; then \
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
		-fplugin=$(OUTPUT)/scripts/gcc-plugins/indirect_mask_plugin.so \
		-I$(OUTPUT)/include -I$(OUTPUT)/include/lkl \
		-Wl,-T$(LINKSCR),--defsym,BASE_ADDRESS=$(BASE_ADDRESS) \
		-o $@ $(LKLOBJ) $(SOURCES)

$(OUTBIN): $(OUTELF)
	$(Q)objcopy -O binary $< $@
