// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/blkdev_utils/lvm_device.h"

#include <fcntl.h>
#include <linux/dm-ioctl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

// lvm2 has multiple options for managing LVM objects:
// - liblvm2app: deprecated.
// - liblvm2cmd: simple interface to directly parse cli commands into functions.
// - lvmdbusd: persistent daemon that can be reached via D-Bus.
//
// Since the logical/physical volume and volume group creation can occur during
// early boot when dbus is not available, the preferred solution is to use
// lvm2cmd.
#include <lvm2cmd.h>

#include <base/files/scoped_file.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <brillo/process/process.h>
#include <brillo/scoped_umask.h>

namespace brillo {
namespace {

const int64_t kLbaSize = 512;
const int64_t kCentiFactor = 10000;
const ssize_t kLbaCountValIdx = 1;
const ssize_t kDataAllocStatIdx = 5;
const ssize_t kDataUsedBlocksStatIdx = 0;
const ssize_t kDataTotalBlocksStatIdx = 1;
const char kCheckThinpoolMappingsConfig[] =
    R"('global/thin_check_options = [ "-q", "--clear-needs-check-flag"]')";

void LogLvmError(int rc, const std::string& cmd) {
  switch (rc) {
    case LVM2_COMMAND_SUCCEEDED:
      break;
    case LVM2_NO_SUCH_COMMAND:
      LOG(ERROR) << "Failed to run lvm2 command: no such command " << cmd;
      break;
    case LVM2_INVALID_PARAMETERS:
      LOG(ERROR) << "Failed to run lvm2 command: invalid parameters " << cmd;
      break;
    case LVM2_PROCESSING_FAILED:
      LOG(ERROR) << "Failed to run lvm2 command: processing failed " << cmd;
      break;
    default:
      LOG(ERROR) << "Failed to run lvm2 command: invalid return code " << cmd;
      break;
  }
}

std::string GetPoolStatusString(const std::string& pool,
                                std::shared_ptr<LvmCommandRunner> lvm) {
  std::vector<char> buf(1024, 0);
  struct dm_ioctl* param = reinterpret_cast<struct dm_ioctl*>(buf.data());
  param->version[0] = DM_VERSION_MAJOR;
  param->version[1] = DM_VERSION_MINOR;
  param->version[2] = DM_VERSION_PATCHLEVEL;
  param->data_size = buf.size();
  param->data_start = sizeof(struct dm_ioctl);
  strncpy(param->name, pool.c_str(), DM_NAME_LEN);
  param->flags = DM_NOFLUSH_FLAG;

  if (!lvm->RunDmIoctl(DM_TABLE_STATUS, param)) {
    PLOG(ERROR) << "Failed to get pool status";
    return "";
  }

  struct dm_target_spec* spec =
      reinterpret_cast<struct dm_target_spec*>(buf.data() + param->data_start);
  char* status = buf.data() + param->data_start + sizeof(struct dm_target_spec);
  std::string result =
      base::StringPrintf("%llu %llu %s %s", spec->sector_start, spec->length,
                         spec->target_type, status);

  return result;
}

}  // namespace

PhysicalVolume::PhysicalVolume(const base::FilePath& device_path,
                               std::shared_ptr<LvmCommandRunner> lvm)
    : device_path_(device_path), lvm_(lvm) {}

bool PhysicalVolume::Check() {
  if (device_path_.empty() || !lvm_) {
    return false;
  }

  return lvm_->RunCommand({"pvck", device_path_.value()});
}

bool PhysicalVolume::Repair() {
  if (device_path_.empty() || !lvm_) {
    return false;
  }

  return lvm_->RunCommand({"pvck", "--yes", device_path_.value()});
}

bool PhysicalVolume::Remove() {
  if (device_path_.empty() || !lvm_) {
    return false;
  }

  bool ret = lvm_->RunCommand({"pvremove", "-ff", device_path_.value()});
  device_path_ = base::FilePath();
  return ret;
}

VolumeGroup::VolumeGroup(const std::string& volume_group_name,
                         std::shared_ptr<LvmCommandRunner> lvm)
    : volume_group_name_(volume_group_name), lvm_(lvm) {}

bool VolumeGroup::Check() {
  if (volume_group_name_.empty() || !lvm_) {
    return false;
  }

  return lvm_->RunCommand({"vgck", GetPath().value()});
}

bool VolumeGroup::Repair() {
  if (volume_group_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunCommand({"vgck", "--yes", GetPath().value()});
}

base::FilePath VolumeGroup::GetPath() const {
  if (volume_group_name_.empty() || !lvm_) {
    return base::FilePath();
  }
  return base::FilePath("/dev").Append(volume_group_name_);
}

bool VolumeGroup::Activate() {
  if (volume_group_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunCommand({"vgchange", "-ay", volume_group_name_});
}

bool VolumeGroup::Deactivate() {
  if (volume_group_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunCommand({"vgchange", "-an", volume_group_name_});
}

bool VolumeGroup::Remove() {
  if (volume_group_name_.empty() || !lvm_) {
    return false;
  }
  bool ret = lvm_->RunCommand({"vgremove", "-f", volume_group_name_});
  volume_group_name_ = "";
  return ret;
}

bool VolumeGroup::Rename(const std::string& new_name) {
  if (volume_group_name_.empty() || new_name.empty() || !lvm_) {
    return false;
  }
  if (!lvm_->RunCommand({"vgrename", volume_group_name_, new_name})) {
    return false;
  }
  volume_group_name_ = new_name;
  return true;
}

LogicalVolume::LogicalVolume(const std::string& logical_volume_name,
                             const std::string& volume_group_name,
                             std::shared_ptr<LvmCommandRunner> lvm)
    : logical_volume_name_(logical_volume_name),
      volume_group_name_(volume_group_name),
      lvm_(lvm) {}

base::FilePath LogicalVolume::GetPath() {
  if (logical_volume_name_.empty() || !lvm_) {
    return base::FilePath();
  }
  return base::FilePath("/dev")
      .Append(volume_group_name_)
      .Append(logical_volume_name_);
}

std::optional<int64_t> LogicalVolume::GetSize() {
  if (logical_volume_name_.empty() || !lvm_) {
    return std::nullopt;
  }

  std::string output;
  if (!lvm_->RunProcess({"/sbin/lvs", "-o", "lv_size", "--reportformat", "json",
                         "--unit", "m", "--nosuffix", GetPath().value()},
                        &output)) {
    return std::nullopt;
  }
  return ParseReportedSize(output, "lv_size");
}

std::optional<int64_t> LogicalVolume::GetBlockSize() {
  if (logical_volume_name_.empty() || !lvm_) {
    return std::nullopt;
  }

  std::string output;
  if (!lvm_->RunProcess(
          {"/sbin/lvs", "-o", "vg_extent_size", "--reportformat", "json",
           "--unit", "m", "--nosuffix", GetPath().value()},
          &output)) {
    return std::nullopt;
  }
  return ParseReportedSize(output, "vg_extent_size");
}

bool LogicalVolume::Activate() {
  if (logical_volume_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunCommand({"lvchange", "-ay", GetName()});
}

bool LogicalVolume::Deactivate() {
  if (logical_volume_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunCommand({"lvchange", "-an", GetName()});
}

bool LogicalVolume::Remove() {
  if (volume_group_name_.empty() || !lvm_) {
    return false;
  }
  bool ret = lvm_->RunCommand({"lvremove", "--force", GetName()});
  logical_volume_name_ = "";
  volume_group_name_ = "";
  return ret;
}

bool LogicalVolume::Resize(int64_t size) {
  if (logical_volume_name_.empty() || !lvm_) {
    return false;
  }
  const auto& size_str = base::StringPrintf("-L%" PRId64 "m", size);
  return lvm_->RunCommand({"lvresize", "--force", size_str, GetName()});
}

bool LogicalVolume::Rename(const std::string& new_name) {
  if (volume_group_name_.empty() || new_name.empty() || !lvm_) {
    return false;
  }
  bool ret = lvm_->RunCommand(
      {"lvrename", volume_group_name_, logical_volume_name_, new_name});
  if (ret) {
    logical_volume_name_ = new_name;
  }
  return ret;
}

std::optional<int64_t> LogicalVolume::ParseReportedSize(
    const std::string& report_json, const std::string& key) {
  std::optional<base::Value> lv_value =
      lvm_->UnwrapReportContents(report_json, "lv");

  if (!lv_value || !lv_value->is_dict()) {
    LOG(ERROR) << "Failed to get report contents";
    return std::nullopt;
  }
  const std::string* output_lv_size = lv_value->GetDict().FindString(key);
  if (!output_lv_size) {
    LOG(ERROR) << "Missing value=" << key;
    return std::nullopt;
  }
  double size;
  if (!base::StringToDouble(*output_lv_size, &size)) {
    LOG(ERROR) << "Failed to parse size, str:  " << *output_lv_size;
    return std::nullopt;
  }
  return size;
}

Thinpool::Thinpool(const std::string& thinpool_name,
                   const std::string& volume_group_name,
                   std::shared_ptr<LvmCommandRunner> lvm)
    : thinpool_name_(thinpool_name),
      volume_group_name_(volume_group_name),
      lvm_(lvm) {}

bool Thinpool::Check() {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }

  return lvm_->RunProcess({"thin_check", GetName()});
}

bool Thinpool::Repair() {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunProcess({"lvconvert", "--repair", GetName()});
}

bool Thinpool::Activate(bool check) {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }

  std::vector<std::string> command = {"lvchange", "-ay"};

  if (check) {
    command.push_back("--config");
    command.push_back(kCheckThinpoolMappingsConfig);
  }
  command.push_back(GetName());

  return lvm_->RunCommand(command);
}

bool Thinpool::Deactivate() {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }
  return lvm_->RunCommand({"lvchange", "-an", GetName()});
}

bool Thinpool::Remove() {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }

  bool ret = lvm_->RunCommand({"lvremove", "--force", GetName()});
  volume_group_name_ = "";
  thinpool_name_ = "";
  return ret;
}

bool Thinpool::GetTotalSpace(int64_t* size) {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }

  const std::string target =
      volume_group_name_ + "-" + thinpool_name_ + "-tpool";
  std::string output = GetPoolStatusString(target, lvm_);
  if (output.empty()) {
    LOG(ERROR) << "Failed to get output from dmsetup status for " << target;
    return false;
  }

  const std::vector<std::string_view> dmstatus_strs = base::SplitStringPiece(
      output, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (dmstatus_strs.size() < kLbaCountValIdx + 1) {
    LOG(ERROR) << "malformed dmsetup status, str:  " << output;
    return false;
  }

  int64_t total_lba;
  if (!base::StringToInt64(dmstatus_strs[kLbaCountValIdx], &total_lba)) {
    LOG(ERROR) << "Failed to parse total lba count, str:  "
               << dmstatus_strs[kLbaCountValIdx];
    return false;
  }

  *size = total_lba * kLbaSize;

  return true;
}

bool Thinpool::GetFreeSpace(int64_t* size) {
  if (thinpool_name_.empty() || !lvm_) {
    return false;
  }

  const std::string target =
      volume_group_name_ + "-" + thinpool_name_ + "-tpool";
  std::string output = GetPoolStatusString(target, lvm_);
  if (output.empty()) {
    LOG(ERROR) << "Failed to get output from dmsetup status for " << target;
    return false;
  }

  const std::vector<std::string_view> dmstatus_strs = base::SplitStringPiece(
      output, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (dmstatus_strs.size() < kDataAllocStatIdx + 1) {
    LOG(ERROR) << "malformed dmsetup status, str:  " << output;
    return false;
  }

  const std::vector<std::string_view> data_alloc_strs =
      base::SplitStringPiece(dmstatus_strs[kDataAllocStatIdx], "/",
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (data_alloc_strs.size() < kDataTotalBlocksStatIdx + 1) {
    LOG(ERROR) << "malformed data allocation value, str:  "
               << dmstatus_strs[kDataAllocStatIdx];
    return false;
  }

  int64_t total_lba;
  int64_t used_blocks_nr;
  int64_t total_blocks_nr;

  if (!base::StringToInt64(dmstatus_strs[kLbaCountValIdx], &total_lba)) {
    LOG(ERROR) << "Failed to parse total lba count, str:  "
               << dmstatus_strs[kLbaCountValIdx];
    return false;
  }

  if (!base::StringToInt64(data_alloc_strs[kDataUsedBlocksStatIdx],
                           &used_blocks_nr)) {
    LOG(ERROR) << "Failed to parse used data block count, str:  "
               << data_alloc_strs[kDataUsedBlocksStatIdx];
    return false;
  }

  if (!base::StringToInt64(data_alloc_strs[kDataTotalBlocksStatIdx],
                           &total_blocks_nr)) {
    LOG(ERROR) << "Failed to parse total data blockcount, str:  "
               << data_alloc_strs[kDataTotalBlocksStatIdx];
    return false;
  }

  // To avoid floating point operations, carry operations with fractions
  // multiplied by a large factor.
  int64_t total_size = total_lba * kLbaSize;
  int64_t free_blocks_nr = total_blocks_nr - used_blocks_nr;
  int64_t free_centi_percent = free_blocks_nr * kCentiFactor / total_blocks_nr;
  *size = total_size * free_centi_percent / kCentiFactor;

  return true;
}

LvmCommandRunner::LvmCommandRunner() {}

LvmCommandRunner::~LvmCommandRunner() {}

bool LvmCommandRunner::RunCommand(const std::vector<std::string>& cmd) {
  // lvm2_run() does not exec/fork a separate process, instead it parses the
  // command line and calls the relevant functions within liblvm2cmd directly.
  std::string lvm_cmd = base::JoinString(cmd, " ");

  // liblvm2cmd sets a global umask() but doesn't reset it.
  // Instead add a scoped umask here to reset the umask once we are done
  // executing.
  brillo::ScopedUmask lvm_umask(0);

  int rc = lvm2_run(nullptr, lvm_cmd.c_str());
  LogLvmError(rc, lvm_cmd);

  return rc == LVM2_COMMAND_SUCCEEDED;
}

bool LvmCommandRunner::RunProcess(const std::vector<std::string>& cmd,
                                  std::string* output) {
  brillo::ProcessImpl lvm_process;
  for (auto arg : cmd) {
    lvm_process.AddArg(arg);
  }
  lvm_process.SetCloseUnusedFileDescriptors(true);

  if (output) {
    lvm_process.RedirectUsingMemory(STDOUT_FILENO);
  }

  if (lvm_process.Run() != 0) {
    return false;
  }

  if (output) {
    *output = lvm_process.GetOutputString(STDOUT_FILENO);
  }

  return true;
}

// Disable int linter - argument to libc function.
// NOLINTNEXTLINE: (runtime/int)
bool LvmCommandRunner::RunDmIoctl(unsigned long ioctl_num,
                                  struct dm_ioctl* param) {
  const base::FilePath kDMControl("/dev/mapper/control");

  base::ScopedFD dm_control(
      open(kDMControl.value().c_str(), O_RDWR | O_CLOEXEC));
  if (!dm_control.is_valid()) {
    PLOG(ERROR) << "Failed to open " << kDMControl.value();
    return false;
  }

  if (ioctl(dm_control.get(), ioctl_num, param) != 0) {
    PLOG(ERROR) << "Failed to run dm ioctl: " << ioctl_num;
    return false;
  }

  return true;
}

// LVM reports are structured as:
//  {
//      "report": [
//          {
//              "lv": [
//                  {"lv_name":"foo", "vg_name":"bar", ...},
//                  {...}
//              ]
//          }
//      ]
//  }
//
// Common function to fetch the underlying dictionary (assume for now
// that the reports will be reporting just a single type (lv/vg/pv) for now).

std::optional<base::Value> LvmCommandRunner::UnwrapReportContents(
    const std::string& output, const std::string& key) {
  auto report =
      base::JSONReader::Read(output, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!report || !report->is_dict()) {
    LOG(ERROR) << "Failed to get report as dictionary";
    return std::nullopt;
  }

  base::Value::List* report_list = report->GetDict().FindList("report");
  if (!report_list) {
    LOG(ERROR) << "Failed to find 'report' list";
    return std::nullopt;
  }

  if (report_list->size() != 1) {
    LOG(ERROR) << "Unexpected size: " << report_list->size();
    return std::nullopt;
  }

  base::Value& report_dictionary = report_list->front();
  if (!report_dictionary.is_dict()) {
    LOG(ERROR) << "Failed to find 'report' dictionary";
    return std::nullopt;
  }

  base::Value::List* key_list = report_dictionary.GetDict().FindList(key);
  if (!key_list) {
    LOG(ERROR) << "Failed to find " << key << " list";
    return std::nullopt;
  }

  // If the list has just a single dictionary element, return it directly.
  if (key_list->size() == 1) {
    base::Value& key_dictionary = key_list->front();
    if (!key_dictionary.is_dict()) {
      LOG(ERROR) << "Failed to get " << key << " dictionary";
      return std::nullopt;
    }
    return std::move(key_dictionary);
  }

  return base::Value{std::move(*key_list)};
}

}  // namespace brillo
