// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/zram_recompression.h"

#include <absl/cleanup/cleanup.h>
#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/logging.h>

#include "swap_management/power_manager_proxy.h"
#include "swap_management/suspend_history.h"
#include "swap_management/zram_idle.h"

namespace swap_management {

namespace {
base::RepeatingTimer recompression_timer_;
}  // namespace

ZramRecompression* ZramRecompression::Get() {
  return *GetSingleton<ZramRecompression>();
}

ZramRecompression::~ZramRecompression() {
  Stop();
}

void ZramRecompression::Start() {
  // Start periodic recompression.
  recompression_timer_.Start(
      FROM_HERE, params_.periodic_time,
      base::BindRepeating(&ZramRecompression::PeriodicRecompress,
                          weak_factory_.GetWeakPtr()));
}

void ZramRecompression::Stop() {
  recompression_timer_.Stop();
}

absl::Status ZramRecompression::SetZramRecompressionConfigIfOverriden(
    const std::string& key, const std::string& value) {
  if (key == "recomp_algorithm") {
    params_.recomp_algorithm = value;
  } else if (key == "periodic_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.periodic_time = base::Seconds(*buf);
  } else if (key == "backoff_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.backoff_time = base::Seconds(*buf);
  } else if (key == "threshold_mib") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.threshold_mib = *buf;
  } else if (key == "recompression_huge") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.recompression_huge = *buf;
  } else if (key == "recompression_huge_idle") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.recompression_huge_idle = *buf;
  } else if (key == "recompression_idle") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.recompression_idle = *buf;
  } else if (key == "idle_min_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.idle_min_time = base::Seconds(*buf);
  } else if (key == "idle_max_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok()) {
      return buf.status();
    }
    params_.idle_max_time = base::Seconds(*buf);
  } else {
    return absl::InvalidArgumentError("Unknown key " + key);
  }

  return absl::OkStatus();
}

absl::Status ZramRecompression::EnableRecompression() {
  LOG(INFO) << "Zram recompression params: " << params_;

  // Basic sanity check on our configuration.
  if (!params_.recompression_huge && !params_.recompression_idle &&
      !params_.recompression_huge_idle) {
    return absl::InvalidArgumentError("No setup for recompression page type.");
  }

  // Register for suspend signal from power manager.
  PowerManagerProxy::Get()->RegisterSuspendSignal();

  // Program recomp_algorithm for enabling recompression.
  // We only support single recompression algorithm at this point. No need to
  // program priority.
  return Utils::Get()->WriteFile(
      base::FilePath(kZramSysfsDir).Append("recomp_algorithm"),
      "algo=" + params_.recomp_algorithm);
}

absl::Status ZramRecompression::InitiateRecompression(
    ZramRecompressionMode mode) {
  // If currently working on huge_idle or idle mode, mark idle for pages.
  if (mode == RECOMPRESSION_HUGE_IDLE || mode == RECOMPRESSION_IDLE) {
    uint64_t idle_age_sec = GetCurrentIdleTimeSec(
        params_.idle_min_time.InSeconds(), params_.idle_max_time.InSeconds());
    absl::Status status = MarkIdle(idle_age_sec);
    if (!status.ok()) {
      return status;
    }
  }

  base::FilePath filepath = base::FilePath(kZramSysfsDir).Append("recompress");
  std::stringstream ss;
  if (mode == RECOMPRESSION_IDLE) {
    ss << "type=idle";
  } else if (mode == RECOMPRESSION_HUGE) {
    ss << "type=huge";
  } else if (mode == RECOMPRESSION_HUGE_IDLE) {
    ss << "type=huge_idle";
  } else {
    return absl::InvalidArgumentError("Invalid mode");
  }

  if (params_.threshold_mib != 0) {
    ss << " threshold=" << std::to_string(params_.threshold_mib);
  }

  absl::Status status = Utils::Get()->WriteFile(filepath, ss.str());
  if (!status.ok()) {
    return status;
  }

  last_recompression_ = base::Time::Now();

  return absl::OkStatus();
}

void ZramRecompression::PeriodicRecompress() {
  // If device is suspended and not on AC, recompression should be skipped
  // to save battery power.
  if (SuspendHistory::Get()->IsSuspended()) {
    absl::StatusOr<bool> on = PowerManagerProxy::Get()->IsACConnected();
    if (!on.ok()) {
      LOG(WARNING) << on.status();
    } else if (!*on) {
      return;
    }
  }

  // Is recompression ongoing? If not then set the flag.
  if (is_currently_recompressing_.exchange(true)) {
    return;
  }

  absl::Cleanup cleanup = [&] { is_currently_recompressing_ = false; };
  absl::Status status = absl::OkStatus();

  // Did we recompress too recently?
  const auto time_since_recompression = base::Time::Now() - last_recompression_;
  if (time_since_recompression < params_.backoff_time) {
    return;
  }

  if (params_.recompression_huge_idle) {
    absl::Status status = InitiateRecompression(RECOMPRESSION_HUGE_IDLE);
    if (!status.ok()) {
      LOG(ERROR) << "Can not initiate zram recompression for huge idle pages: "
                 << status;
      return;
    }
  }
  if (params_.recompression_idle) {
    absl::Status status = InitiateRecompression(RECOMPRESSION_IDLE);
    if (!status.ok()) {
      LOG(ERROR) << "Can not initiate zram recompression for idle pages: "
                 << status;
      return;
    }
  }
  if (params_.recompression_huge) {
    absl::Status status = InitiateRecompression(RECOMPRESSION_HUGE);
    if (!status.ok()) {
      LOG(ERROR) << "Can not initiate zram recompression for huge pages: "
                 << status;
      return;
    }
  }
}

// Return true if recomp_algorithm exists. otherwise return false.
bool ZramRecompression::KernelSupportsZramRecompression() {
  return Utils::Get()
      ->PathExists(base::FilePath(kZramSysfsDir).Append("recomp_algorithm"))
      .ok();
}

}  // namespace swap_management
