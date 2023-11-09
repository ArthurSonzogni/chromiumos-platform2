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
#include <base/strings/stringprintf.h>
#include <brillo/secure_blob.h>
#include <brillo/udev/udev.h>
#include <libcrossystem/crossystem.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/cgpt_util_interface.h"
#include "minios/process_manager_interface.h"

namespace minios {

// Alert Log error categories.
extern const char kCategoryInit[];
extern const char kCategoryReboot[];
extern const char kCategoryUpdate[];

extern const char kLogFilePath[];

extern const base::FilePath kDefaultArchivePath;
extern const char kStatefulPath[];
extern const int kLogStoreKeySizeBytes;
extern const brillo::SecureBlob kZeroKey;

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

// Mount the stateful partition at `/stateful/`. Returns true if successfully
// mounted, false otherwise.
bool MountStatefulPartition(
    std::shared_ptr<ProcessManagerInterface> process_manager);

// Unmount path. Returns true if successfully unmounted, false otherwise.
bool UnmountPath(std::shared_ptr<ProcessManagerInterface> process_manager,
                 const base::FilePath& path);

// Unmount `kStatefulPath`. Returns true if successful, false otherwise.
bool UnmountStatefulPartition(
    std::shared_ptr<ProcessManagerInterface> process_manager);

// Compress a pre-determined list of NBR logs and save it to the provided
// path. Returns the result of running a `tar` command.
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
bool IsLogStoreKeyValid(const brillo::SecureBlob& key);

// Trim the provided key for any trailing whitespace beyond
// `kLogStoreHexKeySizeBytes`.
void TrimLogStoreKey(std::string& key);

// Get log encryption key from VPD. Returns `nullopt` if not found.
std::optional<brillo::SecureBlob> GetLogStoreKey(
    std::shared_ptr<ProcessManagerInterface> process_manager);

// Save a given log encryption key to VPD. Returns true on success, false
// otherwise.
bool SaveLogStoreKey(std::shared_ptr<ProcessManagerInterface> process_manager,
                     const brillo::SecureBlob& key);

// Overwrite log store key in VPD with zeros. Returns true on success, false
// otherwise.
bool ClearLogStoreKey(std::shared_ptr<ProcessManagerInterface> process_manager);

// Read contents of a given file into a secureblob. Returns file contents on
// success and nullopt otherwise.
std::optional<brillo::SecureBlob> ReadFileToSecureBlob(
    const base::FilePath& log_archive_path);

// Read contents of a secureblob into a given file. Returns true on success,
// false otherwise.
bool WriteSecureBlobToFile(const base::FilePath& log_archive_path,
                           const brillo::SecureBlob& data);

// Encrypt data with the given key. Returns encrypted contents, iv and
// tag on success, nullopt otherwise.
std::optional<EncryptedLogFile> EncryptLogArchiveData(
    const brillo::SecureBlob& plain_data, const brillo::SecureBlob& key);

// Decrypt encrypted contents (along with iv and tag) with given key. Returns
// plain text data on success, nullopt otherwise.
std::optional<brillo::SecureBlob> DecryptLogArchiveData(
    const EncryptedLogFile& encrypted_contents, const brillo::SecureBlob& key);

// Get the size of a partition in bytes. Returns size on success, or nullopt on
// failure.
std::optional<uint64_t> GetPartitionSize(
    uint64_t partition_number, std::shared_ptr<CgptUtilInterface> cgpt_util);

std::optional<uint64_t> GetMiniOsPriorityPartition(
    std::shared_ptr<crossystem::Crossystem> cros_system);

}  // namespace minios

#endif  // MINIOS_UTILS_H__
