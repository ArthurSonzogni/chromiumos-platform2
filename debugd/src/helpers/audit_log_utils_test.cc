// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/helpers/audit_log_utils.h"

#include <string>

#include <gtest/gtest.h>

namespace debugd {
namespace {

TEST(AuditLogUtilsTest, FilterAuditLine_TypeAvc) {
  // Taken from /var/log/audit/audit.log
  std::string line =
      "type=AVC msg=audit(1642142055.386:35): avc:  denied  { getattr } for  "
      "pid=1012 comm=\"pvdisplay\" path=\"/dev/tpm0\" dev=\"devtmpfs\" "
      "ino=1079 scontext=u:r:cros_spaced:s0 tcontext=u:object_r:tpm_device:s0 "
      "tclass=chr_file permissive=0";
  std::string input = line + " unknown_tag=value\n";
  EXPECT_EQ(line, FilterAuditLine(input));

  // Taken from `ausearch -i`
  line =
      "type=AVC msg=audit(01/14/22 15:34:15.379:6) : avc:  denied  { search } "
      "for  pid=989 comm=spaced dev=\"sysfs\" ino=15194 "
      "scontext=u:r:cros_spaced:s0 tcontext=u:object_r:sysfs_loop:s0 "
      "tclass=dir permissive=0";
  input = line + " unknown_tag=value\n";
  EXPECT_EQ(line, FilterAuditLine(input));

  // Taken from `ausearch -i`
  line =
      "type=AVC msg=audit(01/14/22 15:34:20.570:56) : avc:  granted  { execute "
      "} for  pid=2363 comm=crash_reporter path=/sbin/crash_reporter "
      "dev=\"dm-0\" ino=151005 scontext=u:r:cros_browser:s0 "
      "tcontext=u:object_r:cros_crash_reporter_exec:s0 tclass=file";
  input = line + " unknown_tag=value\n";
  EXPECT_EQ(line, FilterAuditLine(input));
}

TEST(AuditLogUtilsTest, FilterAuditLine_TypeSyscall) {
  // Taken from /var/log/audit/audit.log
  std::string line =
      "type=SYSCALL msg=audit(1642142055.379:10): arch=c000003e syscall=257 "
      "success=no exit=-13 a0=ffffff9c a1=56080c7abbb0 a2=800 a3=0 ppid=1 "
      "pid=989 auid=4294967295 uid=20181 gid=20181 euid=20181 suid=20181 "
      "fsuid=20181 egid=20181 sgid=20181 fsgid=20181 ses=4294967295 "
      "comm=\"spaced\" exe=\"/usr/sbin/spaced\" subj=u:r:cros_spaced:s0";
  std::string input = line + " unknown_tag=value\n";
  EXPECT_EQ(line, FilterAuditLine(input));

  // Taken from `ausearch -i`
  line =
      "type=SYSCALL msg=audit(01/14/22 15:39:20.823:64) : arch=x86_64 "
      "syscall=execve success=yes exit=0 a0=0x58b90baa8750 a1=0x58b90baa86c0 "
      "a2=0x58b90baa8700 a3=0x30 ppid=1 pid=2392 auid=unset uid=root gid=root "
      "euid=root suid=root fsuid=root egid=root sgid=root fsgid=root ses=unset "
      "comm=periodic_schedu exe=/usr/bin/periodic_scheduler "
      "subj=u:r:cros_periodic_scheduler:s0";
  input = line + " unknown_tag=value\n";
  EXPECT_EQ(line, FilterAuditLine(input));
}

TEST(AuditLogUtilsTest, FilterAuditLine_TypeSeccomp) {
  // Taken from `grep SECCOMP /var/log/audit/audit.log`
  std::string line =
      "type=SECCOMP msg=audit(1750922692.830:4911): auid=4294967295 uid=1000 "
      "gid=1001 ses=4294967295 subj=u:r:cros_disks:s0 pid=83658 "
      "comm=\"AsyncLocalStore\" exe=\"/opt/google/drive-file-stream/drivefs\" "
      "sig=31 arch=c00000b7 syscall=227 compat=0 ip=0x7d0767988c "
      "code=0x80000000AUID=\"unset\" UID=\"chronos\" GID=\"chronos-access\" "
      "ARCH=aarch64 SYSCALL=msync unknown_tag=value\n";
  std::string want =
      "type=SECCOMP msg=audit(1750922692.830:4911): uid=1000 gid=1001 "
      "pid=83658 comm=\"AsyncLocalStore\" "
      "exe=\"/opt/google/drive-file-stream/drivefs\" sig=31 arch=c00000b7 "
      "syscall=227";
  EXPECT_EQ(want, FilterAuditLine(line));

  // Taken from `ausearch --interpret --message SECCOMP`
  line =
      "type=SECCOMP msg=audit(06/26/25 17:24:52.830:4911) : auid=unset "
      "uid=chronos gid=chronos-access ses=unset subj=u:r:cros_disks:s0 "
      "pid=83658 comm=AsyncLocalStore "
      "exe=/opt/google/drive-file-stream/drivefs sig=SIGSYS arch=aarch64 "
      "syscall=msync compat=0 ip=0x7d0767988c code=kill unknown_tag=value\n";
  want =
      "type=SECCOMP msg=audit(06/26/25 17:24:52.830:4911) : uid=chronos "
      "gid=chronos-access pid=83658 comm=AsyncLocalStore "
      "exe=/opt/google/drive-file-stream/drivefs sig=SIGSYS arch=aarch64 "
      "syscall=msync";
  EXPECT_EQ(want, FilterAuditLine(line));
}

TEST(AuditLogUtilsTest, FilterAuditLine_UnsupportedType) {
  // Taken from /var/log/audit/audit.log
  std::string line =
      "type=DAEMON_START msg=audit(1642142055.120:5354): op=start ver=2.8.4 "
      "auid=4294967295 pid=681 uid=0 ses=4294967295 subj=u:r:cros_auditd:s0 "
      "res=success\n";
  EXPECT_EQ("", FilterAuditLine(line));

  // Taken from `ausearch -i`
  line =
      "type=DAEMON_END msg=audit(01/14/22 16:21:57.503:5355) : op=terminate "
      "auid=root pid=1 subj=u:r:cros_init:s0 res=success\n";
  EXPECT_EQ("", FilterAuditLine(line));
}

}  // namespace
}  // namespace debugd
