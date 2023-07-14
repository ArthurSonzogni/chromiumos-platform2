# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

CFG_TEE_TA_LOG_LEVEL ?= 4

# The UUID for the Trusted Application
BINARY=ebb0fd23-257e-4cd4-82de-8833c3a12603

COMPILER=gcc

# Place outputs in $(CURDIR)/out
O := $(CURDIR)/out

# Add platform2 include path support.
CFLAGS+=-I../

# Remove the unused function
CFLAGS += -fdata-sections -ffunction-sections
LDADD += --gc-sections

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@echo 'Note: $$(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk not found, cannot clean TA'
	@echo 'Note: TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR)'
endif

.DEFAULT_GOAL := build_and_copy
.PHONY: build_and_copy
build_and_copy: all
	mkdir -p $(TA_OUTPUT_DIR)
	-cp $(O)/*.{elf,ta,dmp,map} $(TA_OUTPUT_DIR)/
