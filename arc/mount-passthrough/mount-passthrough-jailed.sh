#!/bin/bash
# Copyright 2016 The ChromiumOS Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Runs mount-passthrough with minijail0 as chronos.

main() {
  local FLAGS_TRUE=0
  local FLAGS_FALSE=1
  local FLAGS_source="$1"
  local FLAGS_dest="$2"
  local FLAGS_fuse_umask="$3"
  local FLAGS_fuse_uid="$4"
  local FLAGS_fuse_gid="$5"
  local FLAGS_android_app_access_type="$6"
  local FLAGS_use_default_selinux_context="$7"
  local FLAGS_enter_concierge_namespace="$8"
  local FLAGS_max_number_of_open_fds="$9"

  set -e

  local source="${FLAGS_source}"
  local dest="${FLAGS_dest}"

  # Start constructing minijail0 args...
  set --

  if [ "${FLAGS_enter_concierge_namespace}" = "${FLAGS_TRUE}" ]; then
    # Enter the concierge namespace.
    set -- "$@" -V /run/namespaces/mnt_concierge
  else
    # Use minimalistic-mountns profile.
    set -- "$@" --profile=minimalistic-mountns
    set -- "$@" --no-fs-restrictions
  fi

  # Enter a new cgroup namespace.
  set -- "$@" -N

  # Enter a new UTS namespace.
  set -- "$@" --uts

  # Enter a new VFS namespace and remount /proc read-only.
  set -- "$@" -v -r

  # Enter a new network namespace.
  set -- "$@" -e

  # Enter a new IPC namespace.
  set -- "$@" -l

  # Grant CAP_SYS_ADMIN needed to mount FUSE filesystem.
  set -- "$@" -c 'cap_sys_admin+eip'

  # Set uid and gid of the daemon as chronos.
  set -- "$@" -u chronos -g chronos

  # Inherit supplementary groups.
  set -- "$@" -G

  # Allow sharing mounts between CrOS and Android.
  # WARNING: BE CAREFUL not to unexpectedly expose shared mounts in following
  # bind mounts! Always remount them with MS_REC|MS_PRIVATE unless you want to
  # share those mounts explicitly.
  set -- "$@" -K

  # Specify the maximum number of file descriptors the process can open.
  set -- "$@" -R "RLIMIT_NOFILE,1024,${FLAGS_max_number_of_open_fds}"

  local source_in_minijail="${source}"
  local dest_in_minijail="${dest}"

  if [ "${FLAGS_enter_concierge_namespace}" != "${FLAGS_TRUE}" ]; then
    # Set up the source and destination under /mnt inside the new namespace.
    source_in_minijail=/mnt/source
    dest_in_minijail=/mnt/dest

    # Mount tmpfs on /mnt.
    set -- "$@" -k "tmpfs,/mnt,tmpfs,MS_NOSUID|MS_NODEV|MS_NOEXEC"

    # Bind /dev/fuse to mount FUSE file systems.
    set -- "$@" -b /dev/fuse

    # Mark PRIVATE recursively under (pivot) root, in order not to expose shared
    # mount points accidentally.
    set -- "$@" -k "none,/,none,0x44000"  # private,rec

    # Mount source/dest directories.
    # Note that those directories might be shared mountpoints and we allow them.
    # 0x5000 = bind,rec
    set -- "$@" -k "${source},${source_in_minijail},none,0x5000"
    # 0x84000 = slave,rec
    set -- "$@" -k "${source},${source_in_minijail},none,0x84000"
    # 0x102e = bind,remount,noexec,nodev,nosuid
    set -- "$@" -k "${source},${source_in_minijail},none,0x102e"

    # 0x1000 = bind
    set -- "$@" -k "${dest},${dest_in_minijail},none,0x1000"
    # 0x102e = bind,remount,noexec,nodev,nosuid
    set -- "$@" -k "${dest},${dest_in_minijail},none,0x102e"
  fi

  # Finally, specify command line arguments.
  set -- "$@" -- /usr/bin/mount-passthrough
  set -- "$@" "--source=${source_in_minijail}" "--dest=${dest_in_minijail}" \
      "--fuse_umask=${FLAGS_fuse_umask}" \
      "--fuse_uid=${FLAGS_fuse_uid}" "--fuse_gid=${FLAGS_fuse_gid}" \
      "--android_app_access_type=${FLAGS_android_app_access_type}"

  if [ "${FLAGS_use_default_selinux_context}" = "${FLAGS_TRUE}" ]; then
    set -- "$@" "--use_default_selinux_context"
  fi

  exec minijail0 "$@"
}

main "$@"
