// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/flag_helper.h>

#include "arc/mount-passthrough/mount-passthrough-util.h"

namespace arc {

void ParseCommandLine(int argc,
                      const char* const* argv,
                      CommandLineFlags* flags) {
  DEFINE_string(source, "", "Source path of FUSE mount (required)");
  DEFINE_string(dest, "", "Target path of FUSE mount (required)");
  DEFINE_string(fuse_umask, "",
                "Umask to set filesystem permissions in FUSE (required)");
  DEFINE_int32(fuse_uid, -1, "UID set as file owner in FUSE (required)");
  DEFINE_int32(fuse_gid, -1, "GID set as file group in FUSE (required)");
  DEFINE_string(android_app_access_type, "full", "Access type of Android apps");
  DEFINE_bool(use_default_selinux_context, false,
              "Use default \"fuse\" SELinux context");
  DEFINE_bool(enter_concierge_namespace, false, "Enter concierge namespace");
  // This is larger than the default value 1024 because this process handles
  // many open files. See b/30236190 for more context.
  DEFINE_int32(max_number_of_open_fds, 8192, "Max number of open fds");

  brillo::FlagHelper::Init(argc, argv, "mount-passthrough-jailed");
  flags->source = FLAGS_source;
  flags->dest = FLAGS_dest;
  flags->fuse_umask = FLAGS_fuse_umask;
  flags->fuse_uid = FLAGS_fuse_uid;
  flags->fuse_gid = FLAGS_fuse_gid;
  flags->android_app_access_type = FLAGS_android_app_access_type;
  flags->use_default_selinux_context = FLAGS_use_default_selinux_context;
  flags->enter_concierge_namespace = FLAGS_enter_concierge_namespace;
  flags->max_number_of_open_fds = FLAGS_max_number_of_open_fds;
}

std::vector<std::string> CreateMinijailCommandLineArgs(
    const CommandLineFlags& flags) {
  std::vector<std::string> args;
  args.push_back("/sbin/minijail0");

  // The shell script code is left in as comments for the code review
  // purpose. The code will be deleted in a separate CL.
  //
  // local source="${FLAGS_source}"
  // local dest="${FLAGS_dest}"
  //
  // # Start constructing minijail0 args...
  // set --
  // if [ "${FLAGS_enter_concierge_namespace}" = "${FLAGS_TRUE}" ]; then
  //   # Enter the concierge namespace.
  //   set -- "$@" -V /run/namespaces/mnt_concierge
  // else
  //   # Use minimalistic-mountns profile.
  //   set -- "$@" --profile=minimalistic-mountns
  //   set -- "$@" --no-fs-restrictions
  // fi
  if (flags.enter_concierge_namespace) {
    args.push_back("-V");
    args.push_back("/run/namespaces/mnt_concierge");
  } else {
    args.push_back("--profile=minimalistic-mountns");
    args.push_back("--no-fs-restrictions");
  }

  // # Enter a new cgroup namespace.
  // set -- "$@" -N
  args.push_back("-N");

  // # Enter a new UTS namespace.
  // set -- "$@" --uts
  args.push_back("--uts");

  // # Enter a new VFS namespace and remount /proc read-only.
  // set -- "$@" -v -r
  args.push_back("-v");
  args.push_back("-r");

  // # Enter a new network namespace.
  // set -- "$@" -e
  args.push_back("-e");

  // # Enter a new IPC namespace.
  // set -- "$@" -l
  args.push_back("-l");

  // # Grant CAP_SYS_ADMIN needed to mount FUSE filesystem.
  // set -- "$@" -c 'cap_sys_admin+eip'
  args.push_back("-c");
  args.push_back("cap_sys_admin+eip");

  // # Set uid and gid of the daemon as chronos.
  // set -- "$@" -u chronos -g chronos
  args.push_back("-u");
  args.push_back("chronos");
  args.push_back("-g");
  args.push_back("chronos");

  // # Inherit supplementary groups.
  // set -- "$@" -G
  args.push_back("-G");

  // # Allow sharing mounts between CrOS and Android.  WARNING: BE CAREFUL
  // # not to unexpectedly expose shared mounts in following bind mounts!
  // # Always remount them with MS_REC|MS_PRIVATE unless you want to share
  // # those mounts explicitly.
  // set -- "$@" -K
  args.push_back("-K");

  // # Specify the maximum number of file descriptors the process can
  // # open.
  // set -- "$@" -R "RLIMIT_NOFILE,1024,${FLAGS_max_number_of_open_fds}"
  args.push_back("-R");
  args.push_back("RLIMIT_NOFILE,1024," +
                 base::NumberToString(flags.max_number_of_open_fds));

  // local source_in_minijail="${source}"
  // local dest_in_minijail="${dest}"
  std::string source_in_minijail = flags.source;
  std::string dest_in_minijail = flags.dest;

  // if [ "${FLAGS_enter_concierge_namespace}" != "${FLAGS_TRUE}" ]; then
  if (!flags.enter_concierge_namespace) {
    //   # Set up the source and destination under /mnt inside the new
    //   # namespace.
    //   source_in_minijail=/mnt/source
    //   dest_in_minijail=/mnt/dest
    source_in_minijail = "/mnt/source";
    dest_in_minijail = "/mnt/dest";

    //   # Mount tmpfs on /mnt.
    //   set -- "$@" -k "tmpfs,/mnt,tmpfs,MS_NOSUID|MS_NODEV|MS_NOEXEC"
    args.push_back("-k");
    args.push_back("tmpfs,/mnt,tmpfs,MS_NOSUID|MS_NODEV|MS_NOEXEC");

    //   # Bind /dev/fuse to mount FUSE file systems.
    //   set -- "$@" -b /dev/fuse
    args.push_back("-b");
    args.push_back("/dev/fuse");

    //   # Mark PRIVATE recursively under (pivot) root, in order not to
    //   # expose shared mount points accidentally.
    //   set -- "$@" -k "none,/,none,0x44000"  # private,rec
    args.push_back("-k");
    args.push_back("none,/,none,0x44000");

    //   # Mount source/dest directories.
    //   # Note that those directories might be shared mountpoints and we
    //   # allow them.  0x5000 = bind,rec
    //   set -- "$@" -k "${source},${source_in_minijail},none,0x5000"
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x5000", flags.source.c_str(),
                                      source_in_minijail.c_str()));

    //   # 0x84000 = slave,rec
    //   set -- "$@" -k "${source},${source_in_minijail},none,0x84000"
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x84000",
                                      flags.source.c_str(),
                                      source_in_minijail.c_str()));

    //   # 0x102e = bind,remount,noexec,nodev,nosuid
    //   set -- "$@" -k "${source},${source_in_minijail},none,0x102e"
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x102e", flags.source.c_str(),
                                      source_in_minijail.c_str()));

    //   # 0x1000 = bind
    //   set -- "$@" -k "${dest},${dest_in_minijail},none,0x1000"
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x1000", flags.dest.c_str(),
                                      dest_in_minijail.c_str()));
    //   # 0x102e = bind,remount,noexec,nodev,nosuid
    //   set -- "$@" -k "${dest},${dest_in_minijail},none,0x102e"
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x102e", flags.dest.c_str(),
                                      dest_in_minijail.c_str()));
    // fi
  }

  // # Finally, specify command line arguments.
  // set -- "$@" -- /usr/bin/mount-passthrough
  args.push_back("--");
  args.push_back("/usr/bin/mount-passthrough");

  // set -- "$@" "--source=${source_in_minijail}" "--dest=${dest_in_minijail}" \
  //     "--fuse_umask=${FLAGS_fuse_umask}" \
  //     "--fuse_uid=${FLAGS_fuse_uid}" "--fuse_gid=${FLAGS_fuse_gid}" \
  //     "--android_app_access_type=${FLAGS_android_app_access_type}"
  args.push_back("--source=" + source_in_minijail);
  args.push_back("--dest=" + dest_in_minijail);
  args.push_back("--fuse_umask=" + flags.fuse_umask);
  args.push_back("--fuse_uid=" + base::NumberToString(flags.fuse_uid));
  args.push_back("--fuse_gid=" + base::NumberToString(flags.fuse_gid));
  args.push_back("--android_app_access_type=" + flags.android_app_access_type);

  // if [ "${FLAGS_use_default_selinux_context}" = "${FLAGS_TRUE}" ]; then
  //   set -- "$@" "--use_default_selinux_context"
  // fi
  if (flags.use_default_selinux_context) {
    args.push_back("--use_default_selinux_context");
  }

  // exec minijail0 "$@"
  return args;
}

}  // namespace arc
