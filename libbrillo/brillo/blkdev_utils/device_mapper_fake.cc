// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check_op.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace brillo {
namespace fake {

namespace {

// Parses the parameters of a DmTarget, clears the key field and returns the
// updated parameters as a SecureBlob.
SecureBlob ClearKeysParameter(const DmTarget& dmt) {
  std::string cipher;
  base::FilePath device;
  int iv_offset, device_offset;
  uint64_t allow_discard;
  // Parse dmt parameters, copy existing values and only remove key reference
  SecureBlobTokenizer tokenizer(dmt.parameters.begin(), dmt.parameters.end(),
                                " ");

  // First field is the cipher.
  if (!tokenizer.GetNext())
    return SecureBlob();
  cipher = std::string(tokenizer.token_begin(), tokenizer.token_end());

  // The key is stored in the second field, skip this
  if (!tokenizer.GetNext())
    return SecureBlob();

  // The next field is iv_offset
  if (!tokenizer.GetNext() ||
      !base::StringToInt(
          std::string(tokenizer.token_begin(), tokenizer.token_end()),
          &iv_offset))
    return SecureBlob();

  // The next field is const base::FilePath& device
  if (!tokenizer.GetNext())
    return SecureBlob();
  device = base::FilePath(
      std::string(tokenizer.token_begin(), tokenizer.token_end()));

  // The next field is int device_offset
  if (!tokenizer.GetNext() ||
      !base::StringToInt(
          std::string(tokenizer.token_begin(), tokenizer.token_end()),
          &device_offset))
    return SecureBlob();

  // The next field is bool allow_discard
  if (!tokenizer.GetNext() ||
      !base::StringToUint64(
          std::string(tokenizer.token_begin(), tokenizer.token_end()),
          &allow_discard))
    return SecureBlob();

  // Construct one SecureBlob from the parameters and return it.
  return DevmapperTable::CryptCreateParameters(
      cipher, /*encryption_key=*/SecureBlob(), iv_offset, device, device_offset,
      allow_discard);
}

// Parses the parameters of a DmTarget, sets the key field and returns the
// updated parameters as a SecureBlob.
SecureBlob SetKeysParameter(const DmTarget& dmt,
                            const std::string& key_descriptor) {
  std::string cipher;
  brillo::SecureBlob enc_key(key_descriptor);
  base::FilePath device;
  int iv_offset, device_offset;
  uint64_t allow_discard;
  // Parse dmt parameters, copy existing values and only remove key reference
  SecureBlobTokenizer tokenizer(dmt.parameters.begin(), dmt.parameters.end(),
                                " ");

  // First field is the cipher.
  if (!tokenizer.GetNext())
    return SecureBlob();
  cipher = std::string(tokenizer.token_begin(), tokenizer.token_end());

  // The key should be cleared, the next field should be iv_offset.
  if (!tokenizer.GetNext() ||
      !base::StringToInt(
          std::string(tokenizer.token_begin(), tokenizer.token_end()),
          &iv_offset))
    return SecureBlob();

  // The next field is const base::FilePath& device
  if (!tokenizer.GetNext())
    return SecureBlob();
  device = base::FilePath(
      std::string(tokenizer.token_begin(), tokenizer.token_end()));

  // The next field is int device_offset
  if (!tokenizer.GetNext() ||
      !base::StringToInt(
          std::string(tokenizer.token_begin(), tokenizer.token_end()),
          &device_offset))
    return SecureBlob();

  // The next field is bool allow_discard
  if (!tokenizer.GetNext() ||
      !base::StringToUint64(
          std::string(tokenizer.token_begin(), tokenizer.token_end()),
          &allow_discard))
    return SecureBlob();

  // Construct one SecureBlob from the parameters and return it.
  return DevmapperTable::CryptCreateParameters(
      cipher, enc_key, iv_offset, device, device_offset, allow_discard);
}

// Stub DmTask runs into a map for easy reference.
bool StubDmRunTask(DmTask* task, bool udev_sync) {
  std::string dev_name = task->name;
  std::string params;
  int type = task->type;
  static auto& dm_target_map_ =
      *new std::unordered_map<std::string, std::vector<DmTarget>>();

  switch (type) {
    case DM_DEVICE_CREATE:
      CHECK_EQ(udev_sync, true);
      if (dm_target_map_.find(dev_name) != dm_target_map_.end())
        return false;
      dm_target_map_.insert(std::make_pair(dev_name, task->targets));
      break;
    case DM_DEVICE_REMOVE:
      CHECK_EQ(udev_sync, true);
      if (dm_target_map_.find(dev_name) == dm_target_map_.end())
        return false;
      if (!task->deferred) {
        dm_target_map_.erase(dev_name);
      }
      break;
    case DM_DEVICE_TABLE:
      CHECK_EQ(udev_sync, false);
      if (dm_target_map_.find(dev_name) == dm_target_map_.end())
        return false;
      task->targets = dm_target_map_[dev_name];
      break;
    case DM_DEVICE_RELOAD:
      CHECK_EQ(udev_sync, false);
      if (dm_target_map_.find(dev_name) == dm_target_map_.end())
        return false;
      dm_target_map_.erase(dev_name);
      dm_target_map_.insert(std::make_pair(dev_name, task->targets));
      break;
    case DM_DEVICE_TARGET_MSG: {
      CHECK_EQ(udev_sync, false);
      // Parse message, written to mimic behaviours of the following:
      // dmsetup message <device> 0 key wipe
      // dmsetup message <device> 0 key set <key_reference>

      if (dm_target_map_.find(dev_name) == dm_target_map_.end())
        return false;

      if (task->message.starts_with("key wipe")) {
        // Fetch the DmTarget from the dm_target_map and wipe the key from
        // that target.
        DmTarget dmt = dm_target_map_.find(dev_name)->second.front();
        dmt.parameters = ClearKeysParameter(dmt);
        // Update the DmTargets within |task| as well.
        task->targets = std::vector<DmTarget>{dmt};
        // Clear and refresh dm_target_map_ to reflect changes.
        dm_target_map_.erase(dev_name);
        dm_target_map_.insert(std::make_pair(dev_name, task->targets));
      } else if (task->message.starts_with("key set ") &&
                 task->message.size() > 8) {
        std::string key_desc = task->message.substr(8);
        // Fetch the DmTarget from the dm_target_map and set the key for
        // that target.
        DmTarget dmt = dm_target_map_.find(dev_name)->second.front();
        dmt.parameters = SetKeysParameter(dmt, key_desc);
        // Update the DmTargets within |task| as well.
        task->targets = std::vector<DmTarget>{dmt};
        // Update dm_target_map_ to reflect changes.
        dm_target_map_.erase(dev_name);
        dm_target_map_.insert(std::make_pair(dev_name, task->targets));
      }

      break;
    }
    case DM_DEVICE_SUSPEND:
    case DM_DEVICE_RESUME:
      CHECK_EQ(udev_sync, false);
      if (dm_target_map_.find(dev_name) == dm_target_map_.end())
        return false;
      break;
    default:
      return false;
  }
  return true;
}

std::unique_ptr<DmTask> DmTaskCreate(int type) {
  auto t = std::make_unique<DmTask>();
  t->type = type;
  t->deferred = false;
  return t;
}

}  // namespace

FakeDevmapperTask::FakeDevmapperTask(int type) : task_(DmTaskCreate(type)) {}

bool FakeDevmapperTask::SetName(const std::string& name) {
  task_->name = std::string(name);
  return true;
}

bool FakeDevmapperTask::AddTarget(uint64_t start,
                                  uint64_t sectors,
                                  const std::string& type,
                                  const SecureBlob& parameters) {
  DmTarget dmt;
  dmt.start = start;
  dmt.size = sectors;
  dmt.type = type;
  dmt.parameters = parameters;
  task_->targets.push_back(dmt);
  return true;
}

bool FakeDevmapperTask::GetNextTarget(uint64_t* start,
                                      uint64_t* sectors,
                                      std::string* type,
                                      SecureBlob* parameters) {
  if (task_->targets.empty())
    return false;

  DmTarget dmt = task_->targets[0];
  *start = dmt.start;
  *sectors = dmt.size;
  *type = dmt.type;
  *parameters = dmt.parameters;
  task_->targets.erase(task_->targets.begin());

  return !task_->targets.empty();
}

bool FakeDevmapperTask::Run(bool udev_sync) {
  return StubDmRunTask(task_.get(), udev_sync);
}

bool FakeDevmapperTask::SetDeferredRemove() {
  // Make sure that deferred remove is only set for remove tasks.
  if (task_->type != DM_DEVICE_REMOVE)
    return false;

  task_->deferred = true;
  return true;
}

std::unique_ptr<DevmapperTask> CreateDevmapperTask(int type) {
  return std::make_unique<FakeDevmapperTask>(type);
}

DeviceMapperVersion FakeDevmapperTask::GetVersion() {
  DeviceMapperVersion version({1, 21, 0});
  return version;
}

bool FakeDevmapperTask::SetMessage(const std::string& msg) {
  // Make sure that message is only set for message tasks.
  if (task_->type != DM_DEVICE_TARGET_MSG)
    return false;

  task_->message = std::string(msg);
  return true;
}

}  // namespace fake
}  // namespace brillo
