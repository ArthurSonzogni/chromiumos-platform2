// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/format_manager.h"

#include <glib.h>

#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <brillo/process.h>

#include "cros-disks/format_manager_observer_interface.h"

using base::FilePath;
using std::string;

namespace {

// Expected locations of an external format program
const char* const kFormatProgramPaths[] = {
    "/usr/sbin/mkfs.", "/bin/mkfs.", "/sbin/mkfs.", "/usr/bin/mkfs.",
};

// Supported file systems
const char* const kSupportedFilesystems[] = {
    "vfat",
};

const char kDefaultLabel[] = "UNTITLED";

void OnFormatProcessExit(pid_t pid, gint status, gpointer data) {
  cros_disks::FormatManager* format_manager =
      reinterpret_cast<cros_disks::FormatManager*>(data);
  format_manager->FormattingFinished(pid, status);
}

}  // namespace

namespace cros_disks {

FormatManager::FormatManager() {
}

FormatManager::~FormatManager() {
}

FormatErrorType FormatManager::StartFormatting(const string& device_path,
                                               const string& device_file,
                                               const string& filesystem) {
  // Check if the file system is supported for formatting
  if (!IsFilesystemSupported(filesystem)) {
    LOG(WARNING) << filesystem
                 << " filesystem is not supported for formatting";
    return FORMAT_ERROR_UNSUPPORTED_FILESYSTEM;
  }

  // Localize mkfs on disk
  string format_program = GetFormatProgramPath(filesystem);
  if (format_program.empty()) {
    LOG(WARNING) << "Could not find a format program for filesystem '"
                 << filesystem << "'";
    return FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND;
  }

  if (ContainsKey(format_process_, device_path)) {
    LOG(WARNING) << "Device '" << device_path
                 << "' is already being formatted";
    return FORMAT_ERROR_DEVICE_BEING_FORMATTED;
  }

  brillo::ProcessImpl* process = &format_process_[device_path];
  process->AddArg(format_program);

  // Allow to create filesystem across the entire device.
  if (filesystem == "vfat") {
    process->AddArg("-I");
    // FAT type should be predefined, because mkfs autodetection is faulty.
    process->AddStringOption("-F", "32");
    process->AddStringOption("-n", kDefaultLabel);
  }
  process->AddArg(device_file);
  if (!process->Start()) {
    LOG(WARNING) << "Cannot start a process for formatting '"
                 << device_path << "' as filesystem '" << filesystem << "'";
    format_process_.erase(device_path);
    return FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
  }
  pid_to_device_path_[process->pid()] = device_path;
  g_child_watch_add(process->pid(), &OnFormatProcessExit, this);
  return FORMAT_ERROR_NONE;
}

void FormatManager::FormattingFinished(pid_t pid, int status) {
  string device_path = pid_to_device_path_[pid];
  format_process_.erase(device_path);
  pid_to_device_path_.erase(pid);
  FormatErrorType error_type = FORMAT_ERROR_UNKNOWN;
  if (WIFEXITED(status)) {
    int exit_status = WEXITSTATUS(status);
    if (exit_status == 0) {
      error_type = FORMAT_ERROR_NONE;
      LOG(INFO) << "Process " << pid << " for formatting '" << device_path
                << "' completed successfully";
    } else {
      error_type = FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
      LOG(ERROR) << "Process " << pid << " for formatting '" << device_path
                 << "' exited with a status " << exit_status;
    }
  } else if (WIFSIGNALED(status)) {
    error_type = FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
    LOG(ERROR) << "Process " << pid << " for formatting '" << device_path
               << "' killed by a signal " << WTERMSIG(status);
  }
  if (observer_)
    observer_->OnFormatCompleted(device_path, error_type);
}

string FormatManager::GetFormatProgramPath(const string& filesystem) const {
  for (const char* program_path : kFormatProgramPaths) {
    string path = program_path + filesystem;
    if (base::PathExists(FilePath(path)))
      return path;
  }
  return string();
}

bool FormatManager::IsFilesystemSupported(const string& filesystem) const {
  for (const char* supported_filesystem : kSupportedFilesystems) {
    if (filesystem == supported_filesystem)
      return true;
  }
  return false;
}

}  // namespace cros_disks
