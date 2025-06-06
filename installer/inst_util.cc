// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/inst_util.h"

#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

extern "C" {
#include <vboot/vboot_host.h>
}

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>

#include "installer/cgpt_manager.h"
#include "installer/partition_migrator.h"

using std::string;
using std::vector;

// Used by LoggingTimerStart/Finish methods.
static time_t START_TIME = 0;

namespace {
constexpr char kPartitionNumKey[] = "num";
constexpr char kPartitionLabelKey[] = "label";
constexpr char kPartitionSizeKey[] = "size";
constexpr char kPartitionTypeKey[] = "type";
constexpr char kPartitionTypeKernel[] = "kernel";

constexpr char kReclaimedPartitionLabel[] = "super";

// This is an array of device names that are allowed in end in a digit, and
// which use the 'p' notation to denote partitions.
constexpr std::array<std::string_view, 3> kNumberedDevices = {
    "/dev/loop", "/dev/mmcblk", "/dev/nvme"};

std::optional<uint64_t> GetSectorSize(const base::FilePath& device) {
  DCHECK(device.IsAbsolute()) << "device=" << device;

  uint64_t size = 0;
  base::ScopedFD fd(
      HANDLE_EINTR(open(device.value().c_str(), O_RDONLY | O_CLOEXEC)));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "open " << device.value();
    return std::nullopt;
  }
  if (ioctl(fd.get(), BLKSSZGET, &size)) {
    PLOG(ERROR) << "ioctl(BLKSSZGET): " << device.value();
    return std::nullopt;
  }
  return size;
}

std::optional<installer::Partition> ParsePartition(const base::Value& part_info,
                                                   uint64_t sector_size) {
  if (!part_info.is_dict()) {
    return std::nullopt;
  }

  installer::Partition part;
  const auto& part_dict = part_info.GetDict();
  if (auto num = part_dict.FindInt(kPartitionNumKey)) {
    part.number = *num;
  } else {
    return std::nullopt;
  }

  if (auto label = part_dict.FindString(kPartitionLabelKey)) {
    part.label = *label;
  } else {
    LOG(ERROR) << "Missing label for partition num=" << part.number;
    return std::nullopt;
  }

  if (auto size = part_dict.FindString(kPartitionSizeKey);
      !size || !base::StringToUint64(*size, &part.size)) {
    LOG(ERROR) << "Invalid size for partition num=" << part.number;
    return std::nullopt;
  } else {
    part.size /= sector_size;
  }

  if (auto type = part_dict.FindString(kPartitionTypeKey)) {
    if (*type == kPartitionTypeKernel) {
      part.type = GPT_ENT_TYPE_CHROMEOS_KERNEL;
    } else {
      part.type = GPT_ENT_TYPE_BASIC_DATA;
    }
  } else {
    LOG(ERROR) << "Missing type for partition num=" << part.number;
    return std::nullopt;
  }

  return part;
}

}  // namespace

const PartitionNum PartitionNum::STATEFUL = PartitionNum(1);
const PartitionNum PartitionNum::KERN_A = PartitionNum(2);
const PartitionNum PartitionNum::ROOT_A = PartitionNum(3);
const PartitionNum PartitionNum::KERN_B = PartitionNum(4);
const PartitionNum PartitionNum::ROOT_B = PartitionNum(5);
const PartitionNum PartitionNum::KERN_C = PartitionNum(6);
const PartitionNum PartitionNum::ROOT_C = PartitionNum(7);
const PartitionNum PartitionNum::POWERWASH_DATA = PartitionNum(11);
const PartitionNum PartitionNum::EFI_SYSTEM = PartitionNum(12);

bool PartitionNum::IsKernel() const {
  return *this == KERN_A || *this == KERN_B || *this == KERN_C;
}

bool PartitionNum::IsRoot() const {
  return *this == ROOT_A || *this == ROOT_B || *this == ROOT_C;
}

std::string PartitionNum::ToString() const {
  return std::to_string(num_);
}

bool PartitionNum::operator==(const PartitionNum& other) const {
  return num_ == other.num_;
}

std::ostream& operator<<(std::ostream& os, const PartitionNum& partition) {
  os << "PartitionNum(" << partition.Value() << ")";
  return os;
}

ScopedPathRemover::~ScopedPathRemover() {
  if (root_.empty()) {
    return;
  }
  if (!base::DeletePathRecursively(root_)) {
    PLOG(ERROR) << "Cannot remove path " << root_;
  }
}

base::FilePath ScopedPathRemover::Release() {
  base::FilePath r = root_;
  root_.clear();
  return r;
}

// Start a logging timer. There can only be one active at a time.
void LoggingTimerStart() {
  START_TIME = time(NULL);
}

// Log how long since the last call to LoggingTimerStart()
void LoggingTimerFinish() {
  time_t finish_time = time(NULL);
  LOG(INFO) << "Finished after " << difftime(finish_time, START_TIME)
            << " seconds.";
}

// This is a place holder to invoke the backing scripts. Once all scripts have
// been rewritten as library calls this command should be deleted.
// Takes a vector of args and returns error code.
int RunCommand(const vector<string>& cmdline) {
  string command = base::JoinString(cmdline, " ");
  LOG(INFO) << "Running command: " << command;

  fflush(stdout);
  fflush(stderr);

  brillo::ProcessImpl process;
  process.SetSearchPath(true);

  for (const auto& arg : cmdline) {
    process.AddArg(arg);
  }

  LoggingTimerStart();
  int exit_code = process.Run();
  LoggingTimerFinish();

  if (exit_code == -1) {
    LOG(ERROR) << "Failed command - invalid process: " << command;
    return 1;
  } else if (exit_code != 0) {
    LOG(ERROR) << "Failed command: " << command
               << " - exit code: " << exit_code;
  }
  return exit_code;
}

bool WriteFullyToFileDescriptor(const string& content, int fd) {
  const char* buf = content.data();
  size_t nr_written = 0;
  while (nr_written < content.length()) {
    size_t to_write = content.length() - nr_written;
    ssize_t nr_chunk = write(fd, buf + nr_written, to_write);
    if (nr_chunk < 0) {
      warn("Fail to write %d bytes", static_cast<int>(to_write));
      return false;
    }
    nr_written += nr_chunk;
  }
  return true;
}

// Look up a keyed value from a /etc/lsb-release formatted file.
// TODO(dgarrett): If we ever call this more than once, cache
// file contents to avoid reparsing.
bool LsbReleaseValue(const base::FilePath& file,
                     const string& key,
                     string* result) {
  string preamble = key + "=";

  string file_contents;
  if (!base::ReadFileToString(file, &file_contents)) {
    return false;
  }

  vector<string> file_lines = base::SplitString(
      file_contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  vector<string>::iterator line;
  for (line = file_lines.begin(); line < file_lines.end(); line++) {
    if (line->compare(0, preamble.size(), preamble) == 0) {
      *result = line->substr(preamble.size());
      return true;
    }
  }

  return false;
}

base::FilePath GetBlockDevFromPartitionDev(
    const base::FilePath& partition_dev_path) {
  const std::string& partition_dev = partition_dev_path.value();
  size_t i = partition_dev.length();

  while (i > 0 && isdigit(partition_dev[i - 1])) {
    i--;
  }

  for (const std::string_view nd : kNumberedDevices) {
    // kNumberedDevices are of the form "/dev/mmcblk12p34"
    if (base::StartsWith(partition_dev, nd)) {
      if ((i == nd.size()) || (partition_dev[i - 1] != 'p')) {
        // If there was no partition at the end (/dev/mmcblk12) return
        // unmodified.
        return base::FilePath(partition_dev);
      } else {
        // If it ends with a p, strip off the p.
        i--;
      }
    }
  }

  return base::FilePath(partition_dev.substr(0, i));
}

PartitionNum GetPartitionFromPartitionDev(
    const base::FilePath& partition_dev_path) {
  std::string_view partition_dev = partition_dev_path.value();
  if (base::EndsWith(partition_dev, "_0", base::CompareCase::SENSITIVE)) {
    partition_dev = partition_dev.substr(0, partition_dev.size() - 2);
  }
  size_t i = partition_dev.length();

  while (i > 0 && isdigit(partition_dev[i - 1])) {
    i--;
  }

  for (const std::string_view nd : kNumberedDevices) {
    // kNumberedDevices are of the form "/dev/mmcblk12p34"
    // If there is no ending p, there is no partition at the end (/dev/mmcblk12)
    if (base::StartsWith(partition_dev, nd) &&
        ((i == nd.size()) || (partition_dev[i - 1] != 'p'))) {
      return PartitionNum(0);
    }
  }

  std::string_view partition_str = partition_dev.substr(i, i + 1);

  int result = 0;
  if (!base::StringToInt(partition_str, &result)) {
    result = 0;
  }

  if (result == 0) {
    LOG(ERROR) << "Bad partition number from " << partition_dev;
  }

  return PartitionNum(result);
}

base::FilePath MakePartitionDev(const base::FilePath& block_dev_path,
                                PartitionNum partition) {
  const std::string& block_dev = block_dev_path.value();
  for (const std::string_view nd : kNumberedDevices) {
    if (base::StartsWith(block_dev, nd)) {
      return base::FilePath(block_dev + "p" + partition.ToString());
    }
  }

  return base::FilePath(block_dev + partition.ToString());
}

bool Touch(const base::FilePath& filename) {
  int fd = open(filename.value().c_str(), O_WRONLY | O_CREAT,
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  if (fd == -1) {
    return false;
  }

  return (close(fd) == 0);
}

// Replace the first instance of pattern in the file with value.
bool ReplaceInFile(const string& pattern,
                   const string& value,
                   const base::FilePath& path) {
  string contents;
  if (!base::ReadFileToString(path, &contents)) {
    return false;
  }

  // Modify contents
  size_t offset = contents.find(pattern);

  if (offset == string::npos) {
    LOG(ERROR) << "ReplaceInFile failed to find '" << pattern << "' in "
               << path;
    return false;
  }

  contents.replace(offset, pattern.length(), value);

  if (!base::WriteFile(path, contents)) {
    return false;
  }

  return true;
}

void ReplaceAll(string* target, const string& pattern, const string& value) {
  for (size_t offset = 0;;) {
    offset = target->find(pattern, offset);
    if (offset == string::npos) {
      return;
    }
    target->replace(offset, pattern.length(), value);
    offset += value.length();
  }
}

bool MakeFileSystemRw(const base::FilePath& dev_name) {
  const int offset = 0x464 + 3;  // Set 'highest' byte

  base::ScopedFD fd(open(dev_name.value().c_str(), O_RDWR));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to open: " << dev_name;
    return false;
  }

  const off_t magic_offset = 0x438;
  if (lseek(fd.get(), magic_offset, SEEK_SET) != magic_offset) {
    PLOG(ERROR) << "Failed to seek.";
    return false;
  }

  uint16_t fs_id;
  if (read(fd.get(), &fs_id, sizeof(fs_id)) != sizeof(fs_id)) {
    PLOG(ERROR) << "Can't read the filesystem identifier.";
    return false;
  }

  if (fs_id != 0xef53) {
    LOG(ERROR) << "Non-EXT filesystem with magic " << fs_id
               << " can't be made writable.";
    return false;
  }

  // Write out stuff
  if (lseek(fd.get(), offset, SEEK_SET) != offset) {
    PLOG(ERROR) << "Failed to seek.";
    return false;
  }

  unsigned char buff = 0;  // rw enabled.  0xFF for disable_rw_mount

  if (write(fd.get(), &buff, 1) != 1) {
    PLOG(ERROR) << "Failed to write.";
    return false;
  }

  fd.reset();
  return true;
}

extern "C" {

// The external dumpkernelconfig.a library depends on this symbol
// existing, so I redefined it here. I deserve to suffer
// very, very painfully for this, but hey.
__attribute__((__format__(__printf__, 1, 2))) void VbExError(const char* format,
                                                             ...) {
  va_list ap;
  va_start(ap, format);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, format, ap);
  va_end(ap);
}
}

string DumpKernelConfig(const base::FilePath& kernel_dev) {
  string result;

  char* config =
      FindKernelConfig(kernel_dev.value().c_str(), USE_PREAMBLE_LOAD_ADDR);
  if (!config) {
    LOG(ERROR) << "Error retrieving kernel config from " << kernel_dev;
    return result;
  }

  result = string(config, MAX_KERNEL_CONFIG_SIZE);
  free(config);

  return result;
}

bool FindKernelArgValueOffsets(const string& kernel_config,
                               const string& key,
                               size_t* value_offset,
                               size_t* value_length) {
  // We are really looking for key=value
  string preamble = key + "=";

  size_t i;

  // Search for arg...
  for (i = 0; i < kernel_config.size(); i++) {
    // If we hit a " while searching, skip to matching quote
    if (kernel_config[i] == '"') {
      i++;
      while (i < kernel_config.size() && kernel_config[i] != '"') {
        i++;
      }
    }

    // if we found the key
    if (kernel_config.compare(i, preamble.size(), preamble) == 0) {
      break;
    }
  }

  // Didn't find the key
  if (i >= kernel_config.size()) {
    return false;
  }

  // Jump past the key
  i += preamble.size();

  *value_offset = i;

  // If it's a quoted value, look for closing quote
  if (kernel_config[i] == '"') {
    i = kernel_config.find('"', i + 1);

    // If there is no closing quote, it's an error.
    if (i == string::npos) {
      return false;
    }

    i += 1;
  }

  while (i < kernel_config.size() && kernel_config[i] != ' ') {
    i++;
  }

  *value_length = i - *value_offset;
  return true;
}

string ExtractKernelArg(const string& kernel_config, const string& key) {
  size_t value_offset;
  size_t value_length;

  if (!FindKernelArgValueOffsets(kernel_config, key, &value_offset,
                                 &value_length)) {
    return "";
  }

  string result = kernel_config.substr(value_offset, value_length);

  if ((result.length() >= 2) && (result[0] == '"') &&
      (result[result.length() - 1] == '"')) {
    result = result.substr(1, result.length() - 2);
  }

  return result;
}

bool SetKernelArg(const string& key,
                  const string& value,
                  string* kernel_config) {
  size_t value_offset;
  size_t value_length;

  if (!FindKernelArgValueOffsets(*kernel_config, key, &value_offset,
                                 &value_length)) {
    return false;
  }

  string adjusted_value = value;

  if (value.find(" ") != string::npos) {
    adjusted_value = "\"" + value + "\"";
  }

  kernel_config->replace(value_offset, value_length, adjusted_value);
  return true;
}

// For the purposes of ChromeOS, devices that start with
// "/dev/dm" are to be treated as read-only.
bool IsReadonly(const base::FilePath& device) {
  return base::StartsWith(device.value(), "/dev/dm",
                          base::CompareCase::SENSITIVE);
}

bool GetKernelInfo(std::string* result) {
  if (result == nullptr) {
    return false;
  }

  struct utsname buf;
  if (uname(&buf)) {
    PLOG(ERROR) << "uname() failed";
    return false;
  }

  *result = string("") + "sysname(" + buf.sysname + ") nodename(" +
            buf.nodename + ") release(" + buf.release + ") version(" +
            buf.version + ") machine(" + buf.machine + ")";
  return true;
}

namespace installer {

bool MigratePartition(const base::FilePath& device,
                      int reclaimed_partition_num,
                      const std::string_view& partition_layout,
                      bool revert) {
  auto part_info =
      base::JSONReader::ReadAndReturnValueWithError(partition_layout);
  if (!part_info.has_value()) {
    LOG(ERROR) << "Could not parse the partition layout as JSON. Error: "
               << part_info.error().message;
    return false;
  }
  if (!part_info->is_list() || part_info->GetList().empty()) {
    LOG(ERROR) << "Partition layout is not a valid JSON list or empty";
    return false;
  }
  const auto& new_layout = part_info->GetList();

  auto sector_size = GetSectorSize(device);
  if (!sector_size || sector_size.value() == 0) {
    LOG(ERROR) << "Failed to get device size for " << device;
    return false;
  }

  std::vector<Partition> relabeled_partitions = {
      {
          .number = 1,
          .label = "userdata",
          .old_label = "STATE",
      },
  };

  if (auto part = ParsePartition(new_layout[0], *sector_size)) {
    if (part->label != kReclaimedPartitionLabel) {
      LOG(ERROR) << "Unexpected reclaimed partition label: " << part->label;
      return false;
    }
    part->number = reclaimed_partition_num;
    part->old_label = reclaimed_partition_num == 3 ? "ROOT-A" : "ROOT-B",
    relabeled_partitions.push_back(*part);
  } else {
    LOG(ERROR) << "Unable to parse reclaimed partition info";
    return false;
  }
  installer::Partition reclaimed_partition = {
      .number = reclaimed_partition_num,
  };

  std::vector<Partition> new_partitions;
  for (int i = 1; i < new_layout.size(); ++i) {
    if (auto part = ParsePartition(new_layout[i], *sector_size)) {
      new_partitions.push_back(*part);
    } else {
      LOG(ERROR) << "Invalid partition layout.";
      return false;
    }
  }

  std::unique_ptr<CgptManager> cgpt_manager =
      std::make_unique<CgptManager>(base::FilePath(device));

  SectorRange reclaimed_range;
  if (cgpt_manager->GetSectorRange(PartitionNum(reclaimed_partition.number),
                                   reclaimed_range) !=
      CgptErrorCode::kSuccess) {
    return false;
  }

  reclaimed_partition.start = reclaimed_range.start;
  reclaimed_partition.size = reclaimed_range.count;

  installer::PartitionMigrator migrator(
      /*add_at_end=*/true, reclaimed_partition, std::move(new_partitions),
      std::move(relabeled_partitions), std::move(cgpt_manager));

  if (revert) {
    migrator.RevertMigration();
    return true;
  }
  return migrator.RunMigration();
}
}  // namespace installer
