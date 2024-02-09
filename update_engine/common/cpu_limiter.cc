// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/cpu_limiter.h"

#include <string>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>

#include "update_engine/common/utils.h"

namespace {

// Cgroup container is created in update-engine's upstart script located at
// /etc/init/update-engine.conf.
const char kCGroupSharesPath[] = "/sys/fs/cgroup/cpu/update-engine/cpu.shares";

}  // namespace

namespace chromeos_update_engine {

CPULimiter::~CPULimiter() {
  // Set everything back to normal on destruction.
  CPULimiter::SetCpuShares(CpuShares::kNormal);
}

void CPULimiter::StartLimiter() {
  if (manage_shares_id_ != brillo::MessageLoop::kTaskIdNull) {
    LOG(ERROR) << "Cpu shares timeout source hasn't been destroyed.";
    StopLimiter();
  }
  manage_shares_id_ = brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&CPULimiter::StopLimiterCallback, base::Unretained(this)),
      base::Hours(2));
  SetCpuShares(CpuShares::kLow);
}

void CPULimiter::StopLimiter() {
  if (manage_shares_id_ != brillo::MessageLoop::kTaskIdNull) {
    // If the shares were never set and there isn't a message loop instance,
    // we avoid calling CancelTask(), which otherwise would have been a no-op.
    brillo::MessageLoop::current()->CancelTask(manage_shares_id_);
    manage_shares_id_ = brillo::MessageLoop::kTaskIdNull;
  }
  SetCpuShares(CpuShares::kNormal);
}

bool CPULimiter::SetCpuShares(CpuShares shares) {
  // Short-circuit to avoid re-setting the shares.
  if (shares_ == shares)
    return true;

  std::string string_shares = base::NumberToString(static_cast<int>(shares));
  LOG(INFO) << "Setting cgroup cpu shares to  " << string_shares;
  if (!utils::WriteFile(kCGroupSharesPath, string_shares.c_str(),
                        string_shares.size())) {
    LOG(ERROR) << "Failed to change cgroup cpu shares to " << string_shares
               << " using " << kCGroupSharesPath;
    return false;
  }
  shares_ = shares;
  LOG(INFO) << "CPU shares = " << static_cast<int>(shares_);
  return true;
}

void CPULimiter::StopLimiterCallback() {
  SetCpuShares(CpuShares::kNormal);
  manage_shares_id_ = brillo::MessageLoop::kTaskIdNull;
}

}  // namespace chromeos_update_engine
