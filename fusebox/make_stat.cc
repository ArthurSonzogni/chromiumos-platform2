// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fusebox/make_stat.h"

#include <base/check.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/time/time.h>

namespace fusebox {

bool IsAllowedStatMode(mode_t mode, mode_t allowed) {
  return mode & allowed;
}

mode_t MakeStatModeBits(mode_t mode, bool read_only) {
  CHECK(IsAllowedStatMode(mode));

  // Set read-only user bits.
  if (read_only)
    mode &= ~S_IWUSR;

  // Setup user execute bits.
  mode &= ~S_IXUSR;
  if (S_ISDIR(mode))
    mode |= S_IXUSR;

  // Dup user bits in group bits.
  mode &= ~S_IRWXG;
  if (mode & S_IRUSR)
    mode |= S_IRGRP;
  if (mode & S_IWUSR)
    mode |= S_IWGRP;
  if (mode & S_IXUSR)
    mode |= S_IXGRP;

  // Clear other permission bits.
  mode &= ~S_IRWXO;

  return mode;
}

struct stat MakeTimeStat(mode_t mode, time_t time) {
  CHECK(IsAllowedStatMode(mode));

  struct stat stat = {0};
  stat.st_mode = mode;
  stat.st_atime = time;
  stat.st_mtime = time;
  stat.st_ctime = time;

  return stat;
}

struct stat MakeStat(ino_t ino, const struct stat& s, bool read_only) {
  CHECK(IsAllowedStatMode(s.st_mode));

  struct stat stat = s;
  stat.st_ino = ino;
  stat.st_mode = MakeStatModeBits(s.st_mode, read_only);
  stat.st_nlink = 1;
  stat.st_uid = kChronosUID;
  stat.st_gid = kChronosAccessGID;

  return stat;
}

struct stat MakeStatFromProto(ino_t ino, const DirEntryProto& proto) {
  struct stat stat = {0};
  stat.st_ino = ino;
  stat.st_mode = MakeStatModeBits(
      proto.has_mode_bits() ? proto.mode_bits() : (S_IFREG | 0600), false);
  stat.st_size = proto.has_size() ? proto.size() : 0;
  stat.st_nlink = 1;
  stat.st_uid = kChronosUID;
  stat.st_gid = kChronosAccessGID;

  if (proto.has_mtime()) {
    struct timeval tv = base::Time::FromDeltaSinceWindowsEpoch(
                            base::Microseconds(proto.mtime()))
                            .ToTimeVal();
    stat.st_mtime = base::saturated_cast<decltype(stat.st_mtime)>(tv.tv_sec);
  }
  if (proto.has_atime()) {
    struct timeval tv = base::Time::FromDeltaSinceWindowsEpoch(
                            base::Microseconds(proto.atime()))
                            .ToTimeVal();
    stat.st_atime = base::saturated_cast<decltype(stat.st_atime)>(tv.tv_sec);
  }
  if (proto.has_ctime()) {
    struct timeval tv = base::Time::FromDeltaSinceWindowsEpoch(
                            base::Microseconds(proto.ctime()))
                            .ToTimeVal();
    stat.st_ctime = base::saturated_cast<decltype(stat.st_ctime)>(tv.tv_sec);
  }

  DCHECK(IsAllowedStatMode(stat.st_mode));
  return stat;
}

std::string StatModeToString(mode_t mode) {
  std::string mode_string("?");

  if (S_ISSOCK(mode))
    mode_string.at(0) = 's';
  else if (S_ISLNK(mode))
    mode_string.at(0) = 'l';
  else if (S_ISFIFO(mode))
    mode_string.at(0) = 'p';
  else if (S_ISBLK(mode))
    mode_string.at(0) = 'b';
  else if (S_ISCHR(mode))
    mode_string.at(0) = 'c';
  else if (S_ISDIR(mode))
    mode_string.at(0) = 'd';
  else if (S_ISREG(mode))
    mode_string.at(0) = '-';

  mode_string.append((mode & S_IRUSR) ? "r" : "-");
  mode_string.append((mode & S_IWUSR) ? "w" : "-");
  mode_string.append((mode & S_IXUSR) ? "x" : "-");
  mode_string.append((mode & S_IRGRP) ? "r" : "-");
  mode_string.append((mode & S_IWGRP) ? "w" : "-");
  mode_string.append((mode & S_IXGRP) ? "x" : "-");
  mode_string.append((mode & S_IROTH) ? "r" : "-");
  mode_string.append((mode & S_IWOTH) ? "w" : "-");
  mode_string.append((mode & S_IXOTH) ? "x" : "-");

  return mode_string;
}

void ShowStat(const struct stat& stat, const std::string& name) {
  const char* type = S_ISDIR(stat.st_mode) ? "DIR" : "REG";

  LOG(INFO) << " ENTRY: " << name;
  LOG(INFO) << "    ls: " << StatModeToString(stat.st_mode);
  LOG(INFO) << "  mode: " << type << " 0x" << std::hex << stat.st_mode;
  LOG(INFO) << "   dev: " << stat.st_dev;
  LOG(INFO) << "   ino: " << stat.st_ino;
  LOG(INFO) << "  size: " << stat.st_size;
  LOG(INFO) << " nlink: " << stat.st_nlink;
  LOG(INFO) << "  rdev: " << stat.st_rdev;
  LOG(INFO) << "   uid: " << stat.st_uid;
  LOG(INFO) << "   gid: " << stat.st_gid;
  LOG(INFO) << " atime: " << stat.st_atime;
  LOG(INFO) << " mtime: " << stat.st_mtime;
  LOG(INFO) << " ctime: " << stat.st_ctime;
}

}  // namespace fusebox
