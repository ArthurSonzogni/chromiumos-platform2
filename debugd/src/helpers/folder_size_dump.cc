// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The folder_size_dump helper dumps the size of various system folders.

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <brillo/flag_helper.h>
#include <base/logging.h>
#include <base/files/dir_reader_posix.h>
#include <base/process/launch.h>
#include <re2/re2.h>

namespace {

typedef bool (*FilterFunction)(const std::string& path);

class DirAdder {
 public:
  DirAdder(const char* path, FilterFunction filter, bool include_self)
      : path_(path), filter_(filter), include_self_(include_self) {}

  bool AppendDirEntries(std::vector<std::string>* output) const {
    base::DirReaderPosix dir_reader(path_);

    if (!dir_reader.IsValid()) {
      return false;
    }

    while (dir_reader.Next()) {
      std::string name(dir_reader.name());

      if (name == "." || name == "..") {
        continue;
      }

      auto entry = path_ + name;
      if (filter_(entry))
        output->push_back(entry);
    }

    if (!dir_reader.IsValid()) {
      return false;
    }

    return true;
  }

  void AppendSelf(std::vector<std::string>* output) const {
    if (include_self_) {
      output->push_back(path_);
    }
  }

  const char* GetPath() const { return path_; }

 private:
  const char* path_;
  FilterFunction filter_;
  bool include_self_;
};

constexpr char kUserRegex[] = "[a-z0-9]{40}";
bool FilterUserDirs(const std::string& entry) {
  return !RE2::PartialMatch(entry, kUserRegex);
}

bool FilterStateful(const std::string& entry) {
  base::FilePath path(entry);

  if (path.BaseName().value() == "dev_image")
    return false;

  if (path.BaseName().value() == "encrypted")
    return false;

  if (path.BaseName().value() == "home")
    return false;

  return true;
}

bool FilterEncrypted(const std::string& entry) {
  base::FilePath path(entry);

  if (path.BaseName().value() == "chronos")
    return false;

  return true;
}

bool FilterNone(const std::string&) {
  return true;
}

bool DumpDirectory(const DirAdder& entry) {
  std::vector<std::string> duArgv{"du", "--human-readable", "--total",
                                  "--summarize", "--one-file-system"};

  auto argCount = duArgv.size();

  if (!entry.AppendDirEntries(&duArgv)) {
    LOG(ERROR) << "Failed to generate directory list for: " << entry.GetPath();
    return false;
  }

  // Sort directory entries.
  std::sort(duArgv.begin() + argCount, duArgv.end());

  entry.AppendSelf(&duArgv);

  // Get the output of du.
  std::string output;
  if (!base::GetAppOutputAndError(base::CommandLine(duArgv), &output)) {
    LOG(ERROR) << "Failed to generate directory dump for: " << entry.GetPath();
    return false;
  }

  // Filter out 0 sized entries to reduce size.
  // Matches "0 <dir>" lines in the output and remove them.
  RE2::GlobalReplace(&output, R"((?m:^0\s+.*$))", "");
  // Remove extra newlines.
  RE2::GlobalReplace(&output, R"(^\n+)", "");
  RE2::GlobalReplace(&output, R"(\n{2,})", "\n");

  std::cout << "--- " << entry.GetPath() << " ---" << std::endl;
  std::cout << output;

  return true;
}

const DirAdder kSystemDirs[]{
    {"/home/chronos/", FilterUserDirs, false},
    {"/home/.shadow/", FilterUserDirs, false},
    {"/mnt/stateful_partition/", FilterStateful, false},
    {"/mnt/stateful_partition/encrypted/", FilterEncrypted, false},
    {"/run/", FilterNone, true},
    {"/tmp/", FilterNone, true},
    {"/var/", FilterNone, true},
};

bool DumpSystemDirectories() {
  bool result = true;
  for (const auto& entry : kSystemDirs) {
    if (!DumpDirectory(entry)) {
      result = false;
    }
  }

  return result;
}

bool DumpDaemonStore() {
  const std::string kShadowPath = "/home/.shadow/";
  const std::string kDaemonSubPath = "/mount/root/";

  base::DirReaderPosix dir_reader(kShadowPath.c_str());

  if (!dir_reader.IsValid()) {
    return false;
  }

  std::vector<std::string> deamonPaths;
  while (dir_reader.Next()) {
    std::string name(dir_reader.name());

    if (name == "." || name == "..") {
      continue;
    }

    // Skip non user directories.
    if (!RE2::FullMatch(name, kUserRegex)) {
      continue;
    }

    auto entry = kShadowPath + name + kDaemonSubPath;
    deamonPaths.push_back(entry);
  }

  bool result = true;
  if (!dir_reader.IsValid()) {
    return false;
  }

  for (const auto& entry : deamonPaths) {
    // Ignore errors for unmounted users.
    DumpDirectory(DirAdder(entry.c_str(), FilterNone, true));
  }

  return result;
}

const DirAdder kUserDir("/home/chronos/user/", FilterNone, true);

bool DumpUserDirectories() {
  bool result = true;

  std::cout << "--- Daemon store ---" << std::endl;
  if (!DumpDaemonStore()) {
    result = false;
  }

  std::cout << "--- User directory ---" << std::endl;
  if (!DumpDirectory(kUserDir)) {
    result = false;
  }

  return result;
}

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(user, 0, "Dump user directories' sizes");
  DEFINE_bool(system, 0, "Dump system directories' sizes");
  brillo::FlagHelper::Init(argc, argv,
                           "Dump user and system directories' sizes");

  if (FLAGS_system) {
    if (!DumpSystemDirectories()) {
      LOG(ERROR) << "Failed system directory dump";
    }
  }

  if (FLAGS_user) {
    if (!DumpUserDirectories()) {
      LOG(ERROR) << "Failed user directory dump";
    }
  }

  return 0;
}
