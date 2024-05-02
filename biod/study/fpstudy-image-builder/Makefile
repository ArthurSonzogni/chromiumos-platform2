# Copyright 2024 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# make CONFIG=hatch-bloonchipper-test all
CONFIG ?= brya-latest-test
OUT_DIR ?= out

# Example: 2023-06-25
DATE_DAY     := $(shell date --iso-8601)
# Example: 2023-06-25_21:13:15+00:00
# DATE_SEC     := $(shell date --rfc-3339=seconds | tr ' ' '_')
# Example: 2023-06-25_14:32:09_PDT
DATE_SEC     := $(shell date "+%Y-%m-%d_%H:%M:%S_%Z")

CONFIG_DIR   := $(CONFIG)
OUT          := $(OUT_DIR)/$(CONFIG)

include $(CONFIG_DIR)/build_config.mk

IMAGE          := fpstudy-image-$(BOARD)-$(BRANCH).bin
BUNDLE         := fpstudy-bundle-$(BOARD)-$(TYPE)-$(BRANCH).tar.gz
BUNDLE_ARCHIVE := fpstudy-bundle-$(BOARD)-$(TYPE)-$(BRANCH)-$(DATE_SEC).tar.gz

BUILD_ROOT     := $(HOME)/chromiumos-fpstudy-$(BOARD)-$(BRANCH)

# Artifacts that we want to see after running "make all"
TARGETS         = $(BUNDLE) README.html README.md checksum.md5

all : $(OUT)/$(BUNDLE)

help:
	@echo "Ex: make CONFIG=$(CONFIG) OUT=~/DriveFS/Shared drives/..."

dirs : $(OUT)

$(OUT) :
	mkdir -p "$@"

$(OUT)/% : TEMPLATE_%.in | dirs
	cp $< $@
	sed -i "s|@BOARD@|$(BOARD)|" $@
	sed -i "s|@DATE_DAY@|$(DATE_DAY)|" $@
	sed -i "s|@DATE_SEC@|$(DATE_SEC)|" $@
	sed -i "s|@BRANCH@|$(BRANCH)|" $@
	sed -i "s|@BRANCH_SHORT@|$(BRANCH_SHORT)|" $@
	sed -i "s|@IMAGE@|$(IMAGE)|" $@
	sed -i "s|@DESCRIPTION@|$(DESCRIPTION)|" $@

%.html : %.md
	pandoc -t html -o $@ $^
%.pdf : %.md
	pandoc -t pdf -o $@ $^

#fpstudy-install.tar.gz: run.sh
#	docker run "-v$(pwd):/out" -it debian /out/run.sh

# If we directly depend on build.sh, it will trigger a massive
# rebuild if the build.sh or build_config.sh files are touched.
#
# If we remove the dependency on these, we will only trigger
# a rebuild if the release version is changed. This doesn't work
# if we are using the "stable" branch name.
$(OUT)/$(IMAGE) : build.sh $(CONFIG_DIR)/build_config.sh | dirs
	bash build.sh "$(CONFIG)" "$(OUT)"

$(OUT)/checksum.md5 : $(addprefix $(OUT)/,README.html README.md $(IMAGE)) | dirs
	$(RM) $@
	bash drivefs-util.sh wait $^
	md5sum $^ | sed "s|$(OUT)/||" >$@

# We remove the archive if tar failed, but we still need to return an error
# to stop the build.
#
# If we are writing to DriveFS, we see that the subsequent tar command fails
# with an error about the file changing while reading. Add this in the hopes
# that it forces DriveFS to stabilize.
$(OUT)/$(BUNDLE) : $(addprefix $(OUT)/,README.html README.md $(IMAGE) checksum.md5) | dirs
	@echo "# wait for drivefs upload ##########################################"
	@# This avoids the "file changed as we read it" tar error we keep seeing.
	@# Note that sync doesn't help here.
	./drivefs-util.sh wait $^
	@echo "# syncing ##########################################################"
	sync $^
	sleep 1 # Wait for any possible file swaps that drive-fs needs to make
	@echo "# bundling #########################################################"
	mkdir -p "$(OUT)/archive"
	tar czvf "$(OUT)/archive/$(BUNDLE_ARCHIVE)" -C "$(OUT)" $(subst $(OUT)/,,$^) || ( rm "$@" && false )
	ln -f -s "archive/$(BUNDLE_ARCHIVE)" "$@"
	@#cp "$(OUT)/archive/$(BUNDLE_ARCHIVE)" "$@"

verify :
	cd "$(OUT)"; md5sum -c checksum.md5

clean :
	$(RM) $(addprefix $(OUT)/,$(TARGETS))
	$(RM) $(OUT)/$(IMAGE)

# Can't access DriveFS mount from root, so we must copt to tmp first.
flash : IMAGE_TMP := $(shell mktemp /tmp/fpstudy-image-XXXX.bin)
flash :
	@[ -b "$(OF)" ] \
		|| ( echo "Error - The variable 'OF=$(OF)' is invalid." \
		&& echo -e "Example: make flash CONFIG=brya-latest-test OF=/dev/sdX." \
		&& exit 1 )
	sudo umount $(OF)* || true
	cp -v "$(OUT)/$(IMAGE)" "$(IMAGE_TMP)"
	sudo dd if="$(IMAGE_TMP)" bs=4M status=progress oflag=sync of="$(OF)"
	sudo sync

.PHONY: all clean dirs verify help flash
