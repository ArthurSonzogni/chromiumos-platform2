// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "installer/chromeos_install_config.h"

#include <stdio.h>

#include "installer/cgpt_manager.h"

#include <base/logging.h>

using std::string;

bool StrToBiosType(string name, BiosType* bios_type) {
  if (name == "secure") {
    *bios_type = kBiosTypeSecure;
  } else if (name == "uboot") {
    *bios_type = kBiosTypeUBoot;
  } else if (name == "legacy") {
    *bios_type = kBiosTypeLegacy;
  } else if (name == "efi") {
    *bios_type = kBiosTypeEFI;
  } else {
    LOG(INFO) << "Bios type " << name
              << " is not one of secure, legacy, efi, or uboot.";
    return false;
  }

  return true;
}

// TODO(jaysri): Unduplicate these methods.
//
// This #define and this function are copied from cgpt.h and
// cgpt_common.c because they aren't currently exported in a
// way we can use.
//
// chromium-os:29457 covers exporting these methods. We should
// use the exported ones as soon as they are available.
#define GUID_STRLEN 37
void GuidToStr(const Guid* guid, char* str, unsigned int buflen) {
  snprintf(str, buflen, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
           le32toh(guid->u.Uuid.time_low), le16toh(guid->u.Uuid.time_mid),
           le16toh(guid->u.Uuid.time_high_and_version),
           guid->u.Uuid.clock_seq_high_and_reserved, guid->u.Uuid.clock_seq_low,
           guid->u.Uuid.node[0], guid->u.Uuid.node[1], guid->u.Uuid.node[2],
           guid->u.Uuid.node[3], guid->u.Uuid.node[4], guid->u.Uuid.node[5]);
}

string Partition::uuid() const {
  CgptManager cgpt;

  if (cgpt.Initialize(base_device()) != kCgptSuccess) {
    LOG(ERROR) << "CgptManager failed to initialize for " << base_device();
    return "";
  }

  Guid guid;

  if (cgpt.GetPartitionUniqueId(number(), &guid) != kCgptSuccess) {
    LOG(ERROR) << "CgptManager failed to get guid for " << number();
    return "";
  }

  char guid_str[GUID_STRLEN];
  GuidToStr(&guid, guid_str, GUID_STRLEN);
  return guid_str;
}
