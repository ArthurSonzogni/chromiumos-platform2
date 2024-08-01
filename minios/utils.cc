// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/utils.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/kernel_config_utils.h>
#include <brillo/secure_blob.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_enumerate.h>
#include <brillo/udev/utils.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <minios/proto_bindings/minios.pb.h>
#include <vpd/types.h>
#include <vpd/vpd.h>

#include "minios/process_manager.h"

namespace {
constexpr char kLogConsole[] = "/run/frecon/vt1";
const char kMountStatefulCommand[] = "/usr/bin/stateful_partition_for_recovery";
const char kMountFlag[] = "--mount";

const std::vector<std::string> kUnmountCommand{"/bin/busybox", "umount"};

const char kTarCommand[] = "/bin/tar";
// Compress and archive. Also resolve symlinks.
// Using `gzip` as it's the only installed compress utility on MiniOS.
const char kTarCompressFlags[] = "-czhf";
const char kTarExtractFlags[] = "-xzf";
const char kTarChangeDirFlag[] = "-C";

const char kVpdLogStoreSecretKey[] = "minios_log_store_key";

const std::vector<std::string> kFilesToCompress{"/var/log/update_engine.log",
                                                "/var/log/upstart.log",
                                                "/var/log/minios.log"};
// NOLINTNEXTLINE
const std::string kFutilityShowCmd[]{"/usr/bin/futility", "show", "-P"};
const char kKeyblockSizePrefix[] = "kernel::keyblock::size::";
const char kKernelPreambleSizePrefix[] = "kernel::preamble::size::";
const char kKernelBodySizePrefix[] = "kernel::body::size::";

const char kMiniOsVersionKey[] = "cros_minios_version";

const char kBlockSubsystem[] = "block";
const char kFileSystemProperty[] = "ID_FS_USAGE";
const char kFilesystem[] = "filesystem";

constexpr char kMiniOsFlag[] = "cros_minios";
}  // namespace

namespace minios {

const char kCategoryInit[] = "init";
const char kCategoryReboot[] = "reboot";
const char kCategoryUpdate[] = "update";

const char kLogFilePath[] = "/var/log/minios.log";

const base::FilePath kDefaultArchivePath{"/tmp/logs.tar"};
const int kLogStoreKeySizeBytes = 32;
const brillo::SecureBlob kNullKey{std::string(kLogStoreKeySizeBytes, '\0')};

const base::FilePath kStatefulPath{"/stateful"};
const base::FilePath kUnencryptedMiniosPath{"unencrypted/minios/"};
const char kLogArchiveFile[] = "logs.tar";

std::tuple<bool, std::string> ReadFileContentWithinRange(
    const base::FilePath& file_path,
    int64_t start_offset,
    int64_t end_offset,
    int max_columns) {
  base::File f(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!f.IsValid()) {
    PLOG(ERROR) << "Failed to open file " << file_path.value();
    return {false, {}};
  }

  if (f.Seek(base::File::Whence::FROM_BEGIN, start_offset) != start_offset) {
    PLOG(ERROR) << "Failed to seek file " << file_path.value() << " at offset "
                << start_offset;
    return {false, {}};
  }

  int64_t bytes_to_read = end_offset - start_offset;
  std::string content;
  content.reserve(bytes_to_read);

  int current_col = 0;
  while (bytes_to_read-- > 0) {
    char c;
    switch (f.ReadAtCurrentPos(&c, 1)) {
      case -1:
        PLOG(ERROR) << "Failed to read file " << file_path.value();
        return {false, {}};
      case 0:
        // Equivalent of EOF.
        return {true, content};
      default:
        break;
    }
    if (c == '\n') {
      if (content.empty() || content.back() != '\n')
        content.push_back(c);
      current_col = 0;
      continue;
    }
    if (current_col < max_columns) {
      content.push_back(c);
      if (++current_col >= max_columns) {
        content.push_back('\n');
        current_col = 0;
      }
    }
  }
  return {true, content};
}

std::tuple<bool, std::string, int64_t> ReadFileContent(
    const base::FilePath& file_path,
    int64_t offset,
    int num_lines,
    int num_cols) {
  base::File f(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!f.IsValid())
    return {false, {}, 0};

  if (f.Seek(base::File::Whence::FROM_BEGIN, offset) == -1)
    return {false, {}, 0};

  char c;
  std::string content;
  content.reserve(num_lines * num_cols);
  int64_t bytes_read = 0;
  int current_col = 0, read_buffer_lines = 0;
  while (f.ReadAtCurrentPos(&c, 1) > 0 && read_buffer_lines < num_lines) {
    ++bytes_read;
    if (c == '\n') {
      // Skip double newlining.
      if (content.back() != '\n') {
        content.push_back(c);
        ++read_buffer_lines;
      }
      current_col = 0;
      continue;
    }
    if (current_col < num_cols) {
      content.push_back(c);
      if (++current_col >= num_cols) {
        content.push_back('\n');
        current_col = 0;
        ++read_buffer_lines;
      }
    }
  }
  return {true, content, bytes_read};
}

bool GetCrosRegionData(std::shared_ptr<ProcessManagerInterface> process_manager,
                       std::string key,
                       std::string* value) {
  int exit_code = 0;
  std::string error, xkb_keyboard;
  // Get the first item in the keyboard list for a given region.
  if (!process_manager->RunCommandWithOutput(
          {"/usr/bin/cros_region_data", "-s", key}, &exit_code, value,
          &error) ||
      exit_code) {
    LOG(ERROR) << "Could not get " << key << " region data. Exit code "
               << exit_code << " with error " << error;
    *value = "";
    return false;
  }
  return true;
}

bool TriggerShutdown() {
  ProcessManager process_manager;
  base::FilePath console = GetLogConsole();
  if (process_manager.RunCommand({"/sbin/poweroff", "-f"},
                                 ProcessManager::IORedirection{
                                     .input = console,
                                     .output = console,
                                 })) {
    LOG(ERROR) << "Could not trigger shutdown";
    return false;
  }
  LOG(INFO) << "Shutdown requested.";
  return true;
}

std::string GetKeyboardLayout(
    std::shared_ptr<ProcessManagerInterface> process_manager) {
  std::string keyboard_layout;
  if (!GetCrosRegionData(process_manager, "keyboards", &keyboard_layout)) {
    LOG(WARNING) << "Could not get region data. Defaulting to 'us'.";
    return "us";
  }
  // Get the country code from the full keyboard string (i.e xkb:us::eng).
  const auto& keyboard_parts = base::SplitString(
      keyboard_layout, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (keyboard_parts.size() < 2 || keyboard_parts[1].size() < 2) {
    LOG(WARNING) << "Could not get country code from " << keyboard_layout
                 << " Defaulting to 'us'.";
    return "us";
  }
  return keyboard_parts[1];
}

base::FilePath GetLogConsole() {
  static base::FilePath target;

  if (target.empty()) {
    base::FilePath log_console(kLogConsole);
    if (!base::ReadSymbolicLink(log_console, &target)) {
      target = log_console;
    }
  }

  return target;
}

bool MountStatefulPartition(
    std::shared_ptr<ProcessManagerInterface> process_manager) {
  if (!process_manager) {
    PLOG(WARNING) << "Invalid process manager";
    return false;
  }
  base::FilePath console = GetLogConsole();
  if (process_manager->RunCommand({kMountStatefulCommand, kMountFlag},
                                  ProcessManager::IORedirection{
                                      .input = console,
                                      .output = console,
                                  }) != 0) {
    PLOG(WARNING) << "Failed to mount stateful partition";
    return false;
  }
  return true;
}

bool UnmountPath(std::shared_ptr<ProcessManagerInterface> process_manager,
                 const base::FilePath& path) {
  if (!process_manager) {
    LOG(WARNING) << "Invalid process manager";
    return false;
  }

  auto unmount_cmd = kUnmountCommand;
  unmount_cmd.push_back(path.value());
  if (process_manager->RunCommand(unmount_cmd, {}) != 0) {
    PLOG(WARNING) << "Failed to umount path=" << path;
    return false;
  }
  return true;
}

bool UnmountStatefulPartition(
    std::shared_ptr<ProcessManagerInterface> process_manager) {
  return UnmountPath(process_manager, kStatefulPath);
}

int CompressLogs(std::shared_ptr<ProcessManagerInterface> process_manager,
                 const base::FilePath& archive_path) {
  // Note: These are the explicit set of logs that are approved by privacy team.
  // Adding files to this list would require clearance from Privacy team.
  std::vector<std::string> compress_command = {kTarCommand, kTarCompressFlags,
                                               archive_path.value()};
  compress_command.insert(compress_command.end(), kFilesToCompress.begin(),
                          kFilesToCompress.end());
  base::FilePath console = GetLogConsole();
  return process_manager->RunCommand(compress_command,
                                     ProcessManager::IORedirection{
                                         .input = console,
                                         .output = console,
                                     });
}

// Helper function to step through futility output and return integer values.
std::optional<uint64_t> ParseFutilityOutputInt(
    const std::string& futility_output, const std::string& key) {
  std::string line;
  std::istringstream ft_output(futility_output);
  // Tokenize the key.
  auto key_tok = base::SplitStringUsingSubstr(key, "::", base::TRIM_WHITESPACE,
                                              base::SPLIT_WANT_NONEMPTY);
  // Step through the provided output one line at a time.
  while (std::getline(ft_output, line)) {
    // Tokenize the line.
    auto line_tok = base::SplitStringUsingSubstr(
        line, "::", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    // The line will only have 1 more element than the key (the value).
    if (line_tok.size() == key_tok.size() + 1 &&
        std::equal(line_tok.begin(), line_tok.end() - 1, key_tok.begin())) {
      uint64_t val;
      if (!base::StringToUint64(line_tok.back(), &val)) {
        LOG(ERROR) << "Parsed value was not a number " << line_tok.back();
        return std::nullopt;
      }
      return val;
    }
  }

  LOG(ERROR) << "No Match found for key " << key;
  return std::nullopt;
}

// Kernel sizes are calculated by parsing out and adding together keyblock
// size, kernel preamble size and kernel body size. Failure to find any of
// them, or if any are set to 0 returns a nullopt, otherwise returns the sum
// of those numbers.
std::optional<uint64_t> KernelSize(
    std::shared_ptr<ProcessManagerInterface> process_manager,
    const base::FilePath& device) {
  std::vector<std::string> futility_show_command{begin(kFutilityShowCmd),
                                                 end(kFutilityShowCmd)};
  futility_show_command.push_back(device.value().c_str());

  int return_code = 0;
  std::string std_out, std_err;
  // Run futility command for a given path to begin parsing output.
  if (!process_manager->RunCommandWithOutput(
          futility_show_command, &return_code, &std_out, &std_err) ||
      return_code != 0) {
    LOG(ERROR) << "Failed to run futility command, code: " << return_code;
    return std::nullopt;
  }

  const auto keyblock_size =
      ParseFutilityOutputInt(std_out, kKeyblockSizePrefix);
  if (!keyblock_size.has_value() || keyblock_size.value() == 0) {
    LOG(ERROR) << "Keyblock size not found, or invalid";
    return std::nullopt;
  }

  auto kernel_preamble_size =
      ParseFutilityOutputInt(std_out, kKernelPreambleSizePrefix);
  if (!kernel_preamble_size.has_value() || kernel_preamble_size.value() == 0) {
    LOG(ERROR) << "Kernel preamble size not found, or invalid";
    return std::nullopt;
  }

  auto kernel_body_size =
      ParseFutilityOutputInt(std_out, kKernelBodySizePrefix);
  if (!kernel_body_size.has_value() || kernel_body_size.value() == 0) {
    LOG(ERROR) << "Kernel body size not found, or invalid";
    return std::nullopt;
  }

  return keyblock_size.value() + kernel_preamble_size.value() +
         kernel_body_size.value();
}

std::optional<std::string> GetMiniOSVersion() {
  auto kernel_config = brillo::GetCurrentKernelConfig();
  if (!kernel_config) {
    LOG(ERROR) << "Failed to read kernel config.";
    return std::nullopt;
  }
  auto version = brillo::ExtractKernelArgValue(kernel_config.value(),
                                               std::string{kMiniOsVersionKey});
  if (!version) {
    LOG(ERROR) << "Failed to extract version value with key: "
               << kMiniOsVersionKey;
  }
  return version;
}

bool GetRemovableDevices(std::vector<base::FilePath>& devices,
                         std::unique_ptr<brillo::Udev> udev) {
  devices.clear();
  auto udev_enumerate = udev->CreateEnumerate();
  // Look for all block devices with a filesystem.
  if (!udev_enumerate->AddMatchSubsystem(kBlockSubsystem)) {
    LOG(ERROR) << "Failed to add udev match subsystem";
    return false;
  }

  if (!udev_enumerate->AddMatchProperty(kFileSystemProperty, kFilesystem)) {
    LOG(ERROR) << "Failed to add udev match property";
    return false;
  }

  if (!udev_enumerate->ScanDevices()) {
    LOG(ERROR) << "Failed to scan for block devices";
    return false;
  }

  // Step through removable devices and look for removable property, only
  // store devices that are removable.
  for (auto entry = udev_enumerate->GetListEntry(); entry;
       entry = entry->GetNext()) {
    auto dev = udev->CreateDeviceFromSysPath(entry->GetName());
    if (!dev) {
      LOG(WARNING) << "No device found at path: " << entry->GetName();
    } else if (brillo::IsRemovable(*dev)) {
      devices.emplace_back(dev->GetDeviceNode());
    }
  }
  return true;
}

bool IsLogStoreKeyValid(const brillo::SecureBlob& key) {
  if (key.size() != kLogStoreKeySizeBytes) {
    LOG(ERROR) << "Key not of expected size, key_size=" << key.size()
               << " expected=" << kLogStoreKeySizeBytes;
    return false;
  }
  return true;
}

std::optional<brillo::SecureBlob> GetLogStoreKey(
    std::shared_ptr<vpd::Vpd> vpd) {
  const auto value = vpd->GetValue(vpd::VpdRw, kVpdLogStoreSecretKey);

  if (!value)
    return std::nullopt;

  brillo::SecureBlob key;
  brillo::SecureBlob::HexStringToSecureBlob(value.value(), &key);
  if (!IsLogStoreKeyValid(key)) {
    return std::nullopt;
  }

  return key;
}

bool SaveLogStoreKey(std::shared_ptr<vpd::Vpd> vpd,
                     const brillo::SecureBlob& key) {
  if (!IsLogStoreKeyValid(key)) {
    return false;
  }

  const auto& hex_key = brillo::SecureBlobToSecureHex(key);

  return vpd && vpd->WriteValue(vpd::VpdRw, kVpdLogStoreSecretKey,
                                hex_key.to_string());
}

bool ClearLogStoreKey(std::shared_ptr<vpd::Vpd> vpd) {
  return SaveLogStoreKey(vpd, kNullKey);
}

std::optional<brillo::SecureBlob> ReadFileToSecureBlob(
    const base::FilePath& file_path) {
  base::File file{file_path, base::File::FLAG_OPEN | base::File::FLAG_READ};
  if (!file.IsValid()) {
    LOG(ERROR) << "Failed to open file=" << file_path;
    return std::nullopt;
  }

  brillo::SecureBlob file_contents;
  file_contents.resize(file.GetLength());

  if (!file.ReadAtCurrentPosAndCheck(file_contents)) {
    PLOG(ERROR) << "Failed to read file=" << file_path;
    return std::nullopt;
  }
  return file_contents;
}

bool WriteSecureBlobToFile(const base::FilePath& file_path,
                           const brillo::SecureBlob& data) {
  if (!base::WriteFile(file_path, data)) {
    PLOG(ERROR) << "Failed to write plain data to archive=" << file_path;
    return false;
  }
  return true;
}

std::optional<EncryptedLogFile> EncryptLogArchive(
    const brillo::SecureBlob& plain_data, const brillo::SecureBlob& key) {
  brillo::Blob iv, tag, ciphertext;
  if (!hwsec_foundation::AesGcmEncrypt(plain_data, std::nullopt, key, &iv, &tag,
                                       &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt file contents";
    return std::nullopt;
  }
  EncryptedLogFile encrypted_archive;
  encrypted_archive.set_iv(brillo::BlobToString(iv));
  encrypted_archive.set_tag(brillo::BlobToString(tag));
  encrypted_archive.set_ciphertext(brillo::BlobToString(ciphertext));
  return encrypted_archive;
}

std::optional<brillo::SecureBlob> DecryptLogArchive(
    const EncryptedLogFile& encrypted_archive, const brillo::SecureBlob& key) {
  if (encrypted_archive.ByteSizeLong() == 0)
    return std::nullopt;
  brillo::SecureBlob plain_data;
  if (!hwsec_foundation::AesGcmDecrypt(
          brillo::Blob(encrypted_archive.ciphertext().begin(),
                       encrypted_archive.ciphertext().end()),
          std::nullopt,
          brillo::Blob(encrypted_archive.tag().begin(),
                       encrypted_archive.tag().end()),
          key,
          brillo::Blob(encrypted_archive.iv().begin(),
                       encrypted_archive.iv().end()),
          &plain_data)) {
    LOG(ERROR) << "Failed to decrypt data";
    return std::nullopt;
  }
  return plain_data;
}

std::optional<uint64_t> GetMiniOsPriorityPartition(
    std::shared_ptr<crossystem::Crossystem> cros_system) {
  const auto minios_priority = cros_system->GetMiniOsPriority();
  if (!minios_priority.has_value()) {
    LOG(ERROR) << "Couldn't find MiniOS priority.";
    return std::nullopt;
  }
  if (minios_priority == "A")
    return 9;
  else if (minios_priority == "B")
    return 10;
  LOG(ERROR) << "Invalid MiniOS priority.";
  return std::nullopt;
}

std::optional<bool> IsRunningFromMiniOs() {
  const auto cmd_line = brillo::GetCurrentKernelConfig();
  if (!cmd_line) {
    LOG(ERROR) << "Couldn't extract kernel config.";
    return std::nullopt;
  }
  return brillo::FlagExists(cmd_line.value(), kMiniOsFlag);
}

bool ExtractArchive(std::shared_ptr<ProcessManagerInterface> process_manager,
                    const base::FilePath& archive_path,
                    const base::FilePath& dest_path,
                    const std::vector<std::string>& args) {
  if (!base::PathExists(archive_path)) {
    LOG(ERROR) << "Archive path doesn't exist=" << archive_path;
    return false;
  }

  if (!base::CreateDirectory(dest_path)) {
    LOG(ERROR) << "Invalid destination=" << dest_path;
    return false;
  }
  std::vector<std::string> extract_command = {
      kTarCommand, kTarExtractFlags, archive_path.value(), kTarChangeDirFlag,
      dest_path.value()};
  extract_command.insert(extract_command.end(), args.begin(), args.end());

  ProcessManager::IORedirection io_redirect;
  if (const auto is_minios_opt = IsRunningFromMiniOs();
      is_minios_opt && is_minios_opt.value()) {
    base::FilePath console = GetLogConsole();
    io_redirect.input = console;
    io_redirect.output = console;
  }

  return process_manager->RunCommand(extract_command, io_redirect) == 0;
}

}  // namespace minios
