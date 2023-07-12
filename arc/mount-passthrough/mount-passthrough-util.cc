// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/logging.h>
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
  DEFINE_int32(
      media_provider_uid, -1,
      "UID of Android's MediaProvider "
      "(required in Android R+ for setting non-default SELinux context)");
  DEFINE_bool(enable_casefold_lookup, false, "Enable casefold lookup");
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
  flags->media_provider_uid = FLAGS_media_provider_uid;
  flags->enable_casefold_lookup = FLAGS_enable_casefold_lookup;
  flags->enter_concierge_namespace = FLAGS_enter_concierge_namespace;
  flags->max_number_of_open_fds = FLAGS_max_number_of_open_fds;
}

std::vector<std::string> CreateMinijailCommandLineArgs(
    const CommandLineFlags& flags) {
  std::vector<std::string> args;
  args.push_back("/sbin/minijail0");

  if (flags.enter_concierge_namespace) {
    // Enter the concierge namespace.
    args.push_back("-V");
    args.push_back("/run/namespaces/mnt_concierge");
  } else {
    // Use minimalistic-mountns profile.
    args.push_back("--profile=minimalistic-mountns");
    args.push_back("--no-fs-restrictions");
  }

  // Enter a new cgroup namespace.
  args.push_back("-N");

  // Enter a new UTS namespace.
  args.push_back("--uts");

  // Enter a new VFS namespace and remount /proc read-only.
  args.push_back("-v");
  args.push_back("-r");

  // Enter a new network namespace.
  args.push_back("-e");

  // Enter a new IPC namespace.
  args.push_back("-l");

  // Grant CAP_SYS_ADMIN needed to mount FUSE filesystem.
  args.push_back("-c");
  args.push_back("cap_sys_admin+eip");

  // Set uid and gid of the daemon as chronos.
  args.push_back("-u");
  args.push_back("chronos");
  args.push_back("-g");
  args.push_back("chronos");

  // Inherit supplementary groups.
  args.push_back("-G");

  // Allow sharing mounts between CrOS and Android.  WARNING: BE CAREFUL
  // not to unexpectedly expose shared mounts in following bind mounts!
  // Always remount them with MS_REC|MS_PRIVATE unless you want to share
  // those mounts explicitly.
  args.push_back("-K");

  // Specify the maximum number of file descriptors the process can open.
  args.push_back("-R");
  args.push_back(base::StringPrintf("RLIMIT_NOFILE,%d,%d",
                                    flags.max_number_of_open_fds,
                                    flags.max_number_of_open_fds));

  std::string source_in_minijail = flags.source;
  std::string dest_in_minijail = flags.dest;

  if (!flags.enter_concierge_namespace) {
    // Set up the source and destination under /mnt inside the new
    // namespace.
    source_in_minijail = "/mnt/source";
    dest_in_minijail = "/mnt/dest";

    // Mount tmpfs on /mnt.
    args.push_back("-k");
    args.push_back("tmpfs,/mnt,tmpfs,MS_NOSUID|MS_NODEV|MS_NOEXEC");

    // Bind /dev/fuse to mount FUSE file systems.
    args.push_back("-b");
    args.push_back("/dev/fuse");

    // Mark PRIVATE recursively under (pivot) root, in order not to
    // expose shared mount points accidentally.
    // 0x44000 = private,rec
    args.push_back("-k");
    args.push_back("none,/,none,0x44000");

    // Mount source/dest directories. Note that those directories might
    // be shared mountpoints and we allow them.
    // 0x5000 = bind,rec
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x5000", flags.source.c_str(),
                                      source_in_minijail.c_str()));

    // 0x84000 = slave,rec
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x84000",
                                      flags.source.c_str(),
                                      source_in_minijail.c_str()));

    // 0x102e = bind,remount,noexec,nodev,nosuid
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x102e", flags.source.c_str(),
                                      source_in_minijail.c_str()));

    // 0x1000 = bind
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x1000", flags.dest.c_str(),
                                      dest_in_minijail.c_str()));
    // 0x102e = bind,remount,noexec,nodev,nosuid
    args.push_back("-k");
    args.push_back(base::StringPrintf("%s,%s,none,0x102e", flags.dest.c_str(),
                                      dest_in_minijail.c_str()));
  }

  // Finally, specify command line arguments.
  args.push_back("--");
  args.push_back("/usr/bin/mount-passthrough");

  args.push_back("--source=" + source_in_minijail);
  args.push_back("--dest=" + dest_in_minijail);
  args.push_back("--fuse_umask=" + flags.fuse_umask);
  args.push_back("--fuse_uid=" + base::NumberToString(flags.fuse_uid));
  args.push_back("--fuse_gid=" + base::NumberToString(flags.fuse_gid));
  args.push_back("--android_app_access_type=" + flags.android_app_access_type);

  if (flags.use_default_selinux_context) {
    args.push_back("--use_default_selinux_context");
  }

  if (flags.media_provider_uid >= 0) {
    args.push_back(base::StringPrintf("--media_provider_uid=%d",
                                      flags.media_provider_uid));
  }

  if (flags.enable_casefold_lookup) {
    args.push_back("--enable_casefold_lookup");
  }

  return args;
}

base::FilePath CasefoldLookup(const base::FilePath& root,
                              const base::FilePath& path) {
  // For simplicity, do not deal with paths referencing their parents.
  if (path.ReferencesParent()) {
    return path;
  }

  // Just return the original path as-is if it is not a descendant of the root.
  // Note that `IsParent()` returns true if and only if the path is an ancestor
  // (not necessarily the direct parent) of the specified child.
  if (!root.IsParent(path)) {
    return path;
  }

  // Look for the nearest existing ancestor under the root.
  base::FilePath lookup_path = path;
  std::vector<std::string> components;
  while (lookup_path != root) {
    if (access(lookup_path.value().c_str(), F_OK) == 0) {
      break;
    }

    base::FilePath parent = lookup_path.DirName();
    // A cheap check to ensure that the loop is terminated. This should not be
    // needed as long as everything is functioning.
    if (parent.value().length() >= lookup_path.value().length()) {
      LOG(ERROR) << "Unexpectedly long path length " << parent.value().length()
                 << " for the parent of a path of length "
                 << lookup_path.value().length();
      break;
    }

    components.push_back(lookup_path.BaseName().value());
    lookup_path = std::move(parent);
  }

  // Repeat the following:
  // Open `lookup_path` assuming that it is a directory, look for an entry that
  // matches the original path component in the case insensitive way, and append
  // it to `lookup_path` if there is such an entry.
  for (auto it = components.rbegin(); it != components.rend(); ++it) {
    DIR* dirp = opendir(lookup_path.value().c_str());
    if (dirp == nullptr) {
      // `lookup_path` cannot be opened. Append the remaining path components
      // and return, since there is no point in continuing the lookup.
      do {
        lookup_path = lookup_path.Append(*it);
        ++it;
      } while (it != components.rend());
      return lookup_path;
    }

    // Iterate through the entries. Note that `readdir(3)` does not guarantee
    // the order of ieteration.
    while (true) {
      struct dirent* entry = readdir(dirp);
      if (entry == nullptr) {
        // There is no matching entry. Append the remaining path components and
        // return, since there is no point in continuing the lookup.
        closedir(dirp);
        do {
          lookup_path = lookup_path.Append(*it);
          ++it;
        } while (it != components.rend());
        return lookup_path;
      }

      const std::string entry_name = entry->d_name;
      if (base::FilePath::CompareEqualIgnoreCase(entry_name, *it)) {
        // A matching entry is found. Append it to `lookup_path` and continue
        // the lookup.
        lookup_path = lookup_path.Append(entry_name);
        closedir(dirp);
        break;
      }
    }
  }

  return lookup_path;
}

}  // namespace arc
