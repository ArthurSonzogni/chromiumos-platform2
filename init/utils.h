// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_UTILS_H_
#define INIT_UTILS_H_

#include <string>
#include <vector>
#include <libstorage/platform/platform.h>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace utils {

// Try to set root to the root device filepath, optionally removing the
// partition number
bool BRILLO_EXPORT GetRootDevice(base::FilePath* root, bool strip_partition);

// Helper function to read a file to int
bool BRILLO_EXPORT ReadFileToInt(const base::FilePath& path, int* value);

// Run shutdown.
void BRILLO_EXPORT Reboot();

void BRILLO_EXPORT Restorecon(libstorage::Platform* platform_,
                              const base::FilePath& path,
                              const std::vector<base::FilePath>& exclude,
                              bool is_recursive,
                              bool set_digests);

// Searches `drive_name` for the partition labeled `partition_label` and
// returns its partition number if exactly one partition was found. Returns
// -1 on error.
int BRILLO_EXPORT GetPartitionNumber(const base::FilePath& drive_name,
                                     const std::string& partition_label);

// Splits a device path, for example /dev/mmcblk0p1, /dev/sda3,
// into the base device and partition numbers,
// which would be respectively /dev/mmcblk0p, 1; and /dev/sda, 3;
// Returns true on success.
bool BRILLO_EXPORT GetDevicePathComponents(const base::FilePath& device,
                                           std::string* base_device_out,
                                           int* partition_out);

// Reads successful and priority metadata from partition numbered
// `partition_number` on `disk`, storing the results in `successful_out` and
// `priority_out`, respectively. Returns true on success.
//
// successful is a 1 bit value indicating if a kernel partition
// has been successfully booted, while priority is a 4 bit value
// indicating what order the kernel partitions should be booted in, 15 being
// the highest, 1 the lowest, and 0 meaning not bootable.
// More information on partition metadata is available at.
// https://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format
bool BRILLO_EXPORT ReadPartitionMetadata(const base::FilePath& disk,
                                         int partition_number,
                                         bool* successful_out,
                                         int* priority_out);

// Make sure the kernel partition numbered `kernel_partition` is still
// bootable after being wiped. The system may be in AU state that active
// kernel does not have "successful" bit set to 1, but the kernel has been
// successfully booted.
void BRILLO_EXPORT EnsureKernelIsBootable(const base::FilePath root_disk,
                                          int kernel_partition);

}  // namespace utils

#endif  // INIT_UTILS_H_
