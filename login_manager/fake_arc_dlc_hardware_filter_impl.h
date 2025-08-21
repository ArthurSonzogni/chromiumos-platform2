// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_FAKE_ARC_DLC_HARDWARE_FILTER_IMPL_H_
#define LOGIN_MANAGER_FAKE_ARC_DLC_HARDWARE_FILTER_IMPL_H_

#include "login_manager/arc_dlc_hardware_filter.h"

namespace login_manager {

class FakeArcDlcHardwareFilterImpl : public ArcDlcHardwareFilter {
 public:
  FakeArcDlcHardwareFilterImpl() = default;
  FakeArcDlcHardwareFilterImpl(const FakeArcDlcHardwareFilterImpl&) = delete;
  FakeArcDlcHardwareFilterImpl& operator=(const FakeArcDlcHardwareFilterImpl&) =
      delete;
  ~FakeArcDlcHardwareFilterImpl() override = default;

  void set_all_checks_pass(bool pass) { all_checks_pass_ = pass; }

  bool IsArcDlcHardwareRequirementSatisfied() const override {
    return all_checks_pass_;
  }

 private:
  bool all_checks_pass_ = true;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_FAKE_ARC_DLC_HARDWARE_FILTER_IMPL_H_
