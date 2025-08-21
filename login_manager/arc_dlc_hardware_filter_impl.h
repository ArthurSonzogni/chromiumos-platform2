// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_IMPL_H_
#define LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_IMPL_H_

#include "login_manager/arc_dlc_hardware_filter.h"
#include "login_manager/arc_dlc_platform_info.h"

namespace base {
class FilePath;
}  // namespace base

namespace login_manager {

// A concrete implementation of ArcDlcHardwareFilter.
class ArcDlcHardwareFilterImpl : public ArcDlcHardwareFilter {
 public:
  // The |helper| must outlive this class.
  explicit ArcDlcHardwareFilterImpl(const base::FilePath& root_dir,
                                    ArcDlcPlatformInfo* platform_info);
  ArcDlcHardwareFilterImpl(const ArcDlcHardwareFilterImpl&) = delete;
  ArcDlcHardwareFilterImpl& operator=(const ArcDlcHardwareFilterImpl&) = delete;
  ~ArcDlcHardwareFilterImpl() override = default;

  // ArcDlcHardwareFilter overrides:
  bool IsArcDlcHardwareRequirementSatisfied() const override;

 private:
  // Checks if KVM virtualization is supported.
  bool IsCpuSupportArcDlc() const;

  // Checks if the GPU is on the supported chipset list.
  bool IsGpuSupportArcDlc() const;

  // Checks if the system has at least 4 GB of RAM.
  bool IsRamSupportArcDlc() const;

  // Checks if the boot disk is non-rotational and has at least 32GB of space.
  bool IsBootDiskSupportArcDlc() const;

  const base::FilePath root_dir_;
  ArcDlcPlatformInfo* const platform_info_;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_DLC_HARDWARE_FILTER_IMPL_H_
