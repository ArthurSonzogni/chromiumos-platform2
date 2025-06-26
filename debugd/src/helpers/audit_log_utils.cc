// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/helpers/audit_log_utils.h"

#include <algorithm>
#include <span>
#include <string>
#include <string_view>

#include <base/strings/string_split.h>
#include <re2/re2.h>

namespace debugd {
namespace {

// e.g. type=AVC msg=audit(12/10/21 22:31:04.221:217) : avc:  denied  { map }
// for  scontext=u:r:dexoptanalyzer:s0 tcontext=u:object_r:app_data_file:s0 ...
constexpr char kAvcRegex[] =
    R"((type=AVC msg=audit\(.+\) ?: avc:  (denied|granted)  {.+} for ) (.+))";

// e.g. type=SYSCALL msg=audit(12/10/21 22:31:04.221:218) : arch=x86_64
// syscall=openat success=yes exit=4 a0=0xffffff9c a1=0x5c7adae22fc0 ...
constexpr char kSyscallRegex[] = R"((type=SYSCALL msg=audit\(.+\) ?:) (.+))";

// e.g. type=SECCOMP msg=audit(1750916964.825:3800): auid=4294967295 uid=1000
// gid=1001 ses=4294967295 subj=u:r:cros_disks:s0 pid=66406
// comm="AsyncLocalStore" exe="/opt/google/drive-file-stream/drivefs" sig=31
// arch=c00000b7 syscall=227 compat=0 ip=0x7f682d988c
// code=0x80000000AUID="unset" UID="chronos" GID="chronos-access" ARCH=aarch64
// SYSCALL=msync
constexpr char kSeccompRegex[] = R"((type=SECCOMP msg=audit\(.+\) ?:) (.+))";

// The arrays of allowed tags are sorted.
// This allows them to be looked up by binary search.
constexpr std::string_view kAllowedAvcTags[] = {
    "comm", "dev",      "ino",    "path",    "permissive",
    "pid",  "scontext", "tclass", "tcontext"};

constexpr std::string_view kAllowedSyscallTags[] = {
    "a0",    "a1",      "a2",   "a3",      "a4",   "a5",   "arch",
    "auid",  "comm",    "egid", "euid",    "exe",  "exit", "fsgid",
    "fsuid", "gid",     "per",  "pid",     "ppid", "ses",  "sgid",
    "subj",  "success", "suid", "syscall", "uid"};

constexpr std::string_view kAllowedSeccompTags[] = {
    "arch", "comm", "exe", "gid", "pid", "sig", "syscall", "uid"};

}  // namespace

std::string FilterAuditLine(std::string_view line) {
  while (line.ends_with('\n')) {
    line.remove_suffix(1);
  }

  std::string result;
  std::string_view pairs;
  std::span<const std::string_view> allowed_tags;
  if (RE2::FullMatch(line, kAvcRegex, &result, nullptr, &pairs)) {
    allowed_tags = std::span(kAllowedAvcTags);
  } else if (RE2::FullMatch(line, kSyscallRegex, &result, &pairs)) {
    allowed_tags = std::span(kAllowedSyscallTags);
  } else if (RE2::FullMatch(line, kSeccompRegex, &result, &pairs)) {
    allowed_tags = std::span(kAllowedSeccompTags);
  } else {
    // Unsupported type or invalid format.
    result.clear();
    return result;
  }

  // Only keep the key=value pairs for which the key is in the allowlist.
  for (const std::string_view pair : base::SplitStringPiece(
           pairs, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    if (const auto key_value = base::SplitStringOnce(pair, '=')) {
      if (std::ranges::binary_search(allowed_tags, key_value->first)) {
        result += ' ';
        result += pair;
      }
    }
  }

  return result;
}

}  // namespace debugd
