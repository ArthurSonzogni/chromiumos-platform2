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

const DirAdder kDirs[]{
    {"/home/chronos/", FilterUserDirs, false},
    {"/home/.shadow/", FilterUserDirs, false},
    {"/mnt/stateful_partition/", FilterStateful, false},
    {"/mnt/stateful_partition/encrypted/", FilterEncrypted, false},
    {"/run/", FilterNone, true},
    {"/tmp/", FilterNone, true},
    {"/var/", FilterNone, true},
};

}  // namespace

int main() {
  int exit_code = 0;
  for (const auto& entry : kDirs) {
    std::vector<std::string> argv{"du", "--human-readable", "--total",
                                  "--summarize", "--one-file-system"};

    auto arg_count = argv.size();

    if (!entry.AppendDirEntries(&argv)) {
      LOG(ERROR) << "Failed to generate directory list for " << entry.GetPath();
      exit_code = 1;
      continue;
    }

    // Sort directory entries.
    std::sort(argv.begin() + arg_count, argv.end());

    entry.AppendSelf(&argv);

    // Get the output of du.
    int exit_code = 0;
    std::string output;
    if (!base::GetAppOutputAndError(base::CommandLine(argv), &output)) {
      LOG(ERROR) << "Failed to generate directory dump";
      exit_code = 1;
    }

    // Filter out 0 sized entries to reduce size.
    // Matches "0 <dir>" lines in the output and remove them.
    RE2::GlobalReplace(&output, R"((?m:^0\s+.*$))", "");
    // Remove extra newlines.
    RE2::GlobalReplace(&output, R"(^\n+)", "");
    RE2::GlobalReplace(&output, R"(\n{2,})", "\n");

    std::cout << "--- " << entry.GetPath() << " ---" << std::endl;
    std::cout << output;
  }

  return exit_code;
}
