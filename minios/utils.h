// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_UTILS_H_
#define MINIOS_UTILS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/udev/udev.h>

#include "minios/process_manager.h"

namespace minios {

// Alert Log error categories.
extern const char kCategoryInit[];
extern const char kCategoryReboot[];
extern const char kCategoryUpdate[];

extern const char kLogFilePath[];

extern const base::FilePath kDefaultArchivePath;

// Reads the content of `file_path` from `start_offset` to `end_offset` with
// maximum characters per line being `max_columns` at max. If the file ends
// before reading all bytes between `start_offset` and `end_offset` it will
// return true.
// - bool: Success or failure.
// - std::string: The content read.
std::tuple<bool, std::string> ReadFileContentWithinRange(
    const base::FilePath& file_path,
    int64_t start_offset,
    int64_t end_offset,
    int num_cols);

// Reads the content of `file_path` from `offset`.
// The `num_lines` and `num_cols` is the maximum amount of lines and characters
// per line that will be read.
// The return will include:
// - bool: Success or failure.
// - std::string: The content read.
// - int64_t: The number of bytes read.
// Note: The number of bytes read can differ than the length of the content
// output in the second tuple element because the content read is formatted to
// number of lines and columns format to fit onto the requested area of
// `num_lines` * `num_cols`.
std::tuple<bool, std::string, int64_t> ReadFileContent(
    const base::FilePath& file_path,
    int64_t offset,
    int num_lines,
    int num_cols);

// Gets VPD region data given a key. Returns false on failure.
bool GetCrosRegionData(std::shared_ptr<ProcessManagerInterface> process_manager,
                       std::string key,
                       std::string* value);

// Gets XKB keyboard data and extracts country code from it. Defaults to "us" on
// failure.
std::string GetKeyboardLayout(
    std::shared_ptr<ProcessManagerInterface> process_manager);

// Read frecon created symbolic link and return the virtual terminal path.
base::FilePath GetLogConsole();

bool TriggerShutdown();

// Create a tag that can be added to an Error log message to allow easier
// filtering from listnr logs. Expected to be used as the first field of a log
// message. e.g.: `LOG(ERROR) << AlertLogTag(kCategoryName) << err_msg << ....;`
inline std::string AlertLogTag(const std::string& category) {
  return base::StringPrintf("[CoreServicesAlert<%s>] ", category.c_str());
}

// Mount the stateful partition at `/stateful/` if its not currently mounted.
// Returns true if successfully mounted, false otherwise.
bool MountStatefulPartition(
    std::shared_ptr<ProcessManagerInterface> process_manager);

// Compress a pre-determined list of NBR logs and save it to the provided path.
// Returns the result of running a `tar` command.
int CompressLogs(std::shared_ptr<ProcessManagerInterface> process_manager,
                 const base::FilePath& archive_path = kDefaultArchivePath);

// Calculate kernel size.
std::optional<uint64_t> KernelSize(
    std::shared_ptr<ProcessManagerInterface> process_manager,
    const base::FilePath& device);

// Read the kernel cmdline and get the current version.
std::optional<std::string> GetMiniOSVersion();

// Enumerate udev devices and query for removable storage devices. Returns true
// on success and devices will be added to the passed in vector. Vector will be
// cleared before any devices are possibly added to it.
bool GetRemovableDevices(
    std::vector<base::FilePath>& devices,
    std::unique_ptr<brillo::Udev> udev = brillo::Udev::Create());

// Check if the given log store key is valid.
bool IsLogStoreKeyValid(const std::string& key);

// Trim the provided key for any trailing whitespace beyond
// `kLogStoreKeySizeBytes`.
void TrimLogStoreKey(std::string& key);

// Get log encryption key from VPD. Returns `nullopt` if not found.
std::optional<std::string> GetLogStoreKey(
    std::shared_ptr<ProcessManagerInterface> process_manager);

// Save a given log encryption key to VPD. Returns true on success, false
// otherwise.
bool SaveLogStoreKey(std::shared_ptr<ProcessManagerInterface> process_manager,
                     const std::string& key);

}  // namespace minios
#endif  // MINIOS_UTILS_H__
