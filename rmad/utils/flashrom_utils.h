// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_UTILS_FLASHROM_UTILS_H_
#define RMAD_UTILS_FLASHROM_UTILS_H_

namespace rmad {

class FlashromUtils {
 public:
  FlashromUtils() = default;
  virtual ~FlashromUtils() = default;

  virtual bool GetSoftwareWriteProtectionStatus(bool* enabled) = 0;
  virtual bool EnableSoftwareWriteProtection() = 0;
  virtual bool DisableSoftwareWriteProtection() = 0;
};

}  // namespace rmad

#endif  // RMAD_UTILS_FLASHROM_UTILS_H_
