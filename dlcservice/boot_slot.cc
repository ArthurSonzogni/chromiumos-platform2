// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/boot_slot.h"

#include <limits.h>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "dlcservice/boot_device.h"

using std::string;
using std::unique_ptr;

namespace dlcservice {

namespace {

constexpr char kChromeOSPartitionNameKernel[] = "kernel";
constexpr char kChromeOSPartitionNameRoot[] = "root";

}  // namespace

BootSlot::BootSlot(unique_ptr<BootDeviceInterface> boot_device)
    : boot_device_(std::move(boot_device)) {}

BootSlot::~BootSlot() {}

bool BootSlot::GetCurrentSlot(string* boot_disk_name_out,
                              int* num_slots_out,
                              int* current_slot_out) {
  if (!boot_disk_name_out || !num_slots_out || !current_slot_out)
    return false;

  string boot_device = boot_device_->GetBootDevice();
  if (boot_device.empty())
    return false;

  int partition_num;
  if (!SplitPartitionName(boot_device, boot_disk_name_out, &partition_num))
    return false;

  // All installed Chrome OS devices have two slots. We don't update removable
  // devices, so we will pretend we have only one slot in that case.
  if (boot_device_->IsRemovableDevice(*boot_disk_name_out)) {
    LOG(INFO)
        << "Booted from a removable device, pretending we have only one slot.";
    *num_slots_out = 1;
  } else {
    // TODO(xiaochu): Look at the actual number of slots reported in the GPT.
    *num_slots_out = 2;
  }

  // Search through the slots to see which slot has the partition_num we booted
  // from. This should map to one of the existing slots, otherwise something is
  // very wrong.
  *current_slot_out = 0;
  while (*current_slot_out < *num_slots_out &&
         partition_num != GetPartitionNumber(kChromeOSPartitionNameRoot,
                                             *current_slot_out,
                                             *num_slots_out)) {
    (*current_slot_out)++;
  }
  if (*current_slot_out >= *num_slots_out) {
    LOG(ERROR) << "Couldn't find the slot number corresponding to the "
                  "partition "
               << boot_device << ", number of slots: " << *num_slots_out
               << ". This device is not updateable.";
    *num_slots_out = 1;
    *current_slot_out = UINT_MAX;
    return false;
  }

  return true;
}

bool BootSlot::SplitPartitionName(string partition_name,
                                  string* disk_name_out,
                                  int* partition_num_out) {
  CHECK(disk_name_out);
  CHECK(partition_num_out);
  if (!base::StartsWith(partition_name, "/dev/",
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Invalid partition device name: " << partition_name;
    return false;
  }

  // Loop twice if we hit the '_' case to handle NAND block devices.
  for (int i = 0; i <= 1; ++i) {
    auto nondigit_pos = partition_name.find_last_not_of("0123456789");
    if (!isdigit(partition_name.back()) || nondigit_pos == string::npos) {
      LOG(ERROR) << "Unable to parse partition device name: " << partition_name;
      return false;
    }

    switch (partition_name[nondigit_pos]) {
      // NAND block devices have weird naming which could be something like
      // "/dev/ubiblock2_0". We discard "_0" in such a case.
      case '_':
        LOG(INFO) << "Shortening partition_name: " << partition_name;
        partition_name = partition_name.substr(0, nondigit_pos);
        break;
      // Special case for MMC devices which have the following naming scheme:
      //   mmcblk0p2
      case 'p':
        if (nondigit_pos != 0 && isdigit(partition_name[nondigit_pos - 1])) {
          *disk_name_out = partition_name.substr(0, nondigit_pos);
          base::StringToInt(partition_name.substr(nondigit_pos + 1),
                            partition_num_out);
          return true;
        }
        FALLTHROUGH;
      default:
        *disk_name_out = partition_name.substr(0, nondigit_pos + 1);
        base::StringToInt(partition_name.substr(nondigit_pos + 1),
                          partition_num_out);
        return true;
    }
  }
  LOG(ERROR) << "Unable to parse partition device name: " << partition_name;
  return false;
}

int BootSlot::GetPartitionNumber(const string& partition_name,
                                 int slot,
                                 int num_slots) {
  if (slot >= num_slots) {
    LOG(ERROR) << "Invalid slot number: " << slot << ", we only have "
               << num_slots << " slot(s)";
    return -1;
  }

  // In Chrome OS, the partition numbers are hard-coded:
  //   KERNEL-A=2, ROOT-A=3, KERNEL-B=4, ROOT-B=4, ...
  // To help compatibility between different we accept both lowercase and
  // uppercase names in the ChromeOS or Brillo standard names.
  // See http://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format
  string partition_lower = base::ToLowerASCII(partition_name);
  int base_part_num = 2 + 2 * slot;
  if (partition_lower == kChromeOSPartitionNameKernel)
    return base_part_num + 0;
  if (partition_lower == kChromeOSPartitionNameRoot)
    return base_part_num + 1;
  LOG(ERROR) << "Unknown Chrome OS partition name \"" << partition_name << "\"";
  return -1;
}

}  // namespace dlcservice
