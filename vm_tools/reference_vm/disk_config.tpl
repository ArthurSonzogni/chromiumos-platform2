# Copyright 2023 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

disk_config disk1 disklabel:gpt bootable:1 fstabkey:uuid align-at:1MiB
primary /boot/efi 2GB vfat iversion,umask=0077
primary /boot 2GB ext2 iversion
primary - 0- - -
disk_config lvm fstabkey:uuid
vg {{ vg_name }} disk1.3
{{ vg_name }}-root / 10GB ext4 errors=remount-ro,iversion
{{ vg_name }}-swap swap 2GB swap sw
