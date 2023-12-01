// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/zram_idle.h"
#include "swap_management/zram_recompression.h"

#include <absl/cleanup/cleanup.h>
#include <absl/status/status.h>
#include <base/logging.h>

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
    if (!buf.ok())
      return buf.status();
    params_.periodic_time = base::Seconds(*buf);
  } else if (key == "backoff_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.backoff_time = base::Seconds(*buf);
  } else if (key == "threshold_mib") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.threshold_mib = *buf;
  } else if (key == "recompression_huge") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok())
      return buf.status();
    params_.recompression_huge = *buf;
  } else if (key == "recompression_huge_idle") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok())
      return buf.status();
    params_.recompression_huge_idle = *buf;
  } else if (key == "recompression_idle") {
    auto buf = Utils::Get()->SimpleAtob(value);
    if (!buf.ok())
      return buf.status();
    params_.recompression_idle = *buf;
  } else if (key == "idle_min_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
    params_.idle_min_time = base::Seconds(*buf);
  } else if (key == "idle_max_time_sec") {
    auto buf = Utils::Get()->SimpleAtoi<uint32_t>(value);
    if (!buf.ok())
      return buf.status();
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
      !params_.recompression_huge_idle)
    return absl::InvalidArgumentError("No setup for recompression page type.");

  // Program recomp_algorithm for enabling recompression.
  // We only support single recompression algorithm at this point. No need to
  // program priority.
  return Utils::Get()->WriteFile(
      base::FilePath(kZramSysfsDir).Append("recomp_algorithm"),
      "algo=" + params_.recomp_algorithm);
}

absl::Status ZramRecompression::InitiateRecompression(
    ZramRecompressionMode mode) {
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

  if (params_.threshold_mib != 0)
    ss << " threshold=" << std::to_string(params_.threshold_mib);

  return Utils::Get()->WriteFile(filepath, ss.str());
}

void ZramRecompression::PeriodicRecompress() {
  // Is recompression ongoing?
  if (is_currently_recompressing_)
    return;
  absl::Cleanup cleanup = [&] { is_currently_recompressing_ = false; };
  absl::Status status = absl::OkStatus();

  // Did we recompress too recently?
  const auto time_since_recompression = base::Time::Now() - last_recompression_;
  if (time_since_recompression < params_.backoff_time)
    return;

  // We started on huge idle page recompression, then idle, then huge pages, if
  // enabled accordingly.
  ZramRecompressionMode current_recompression_mode = RECOMPRESSION_HUGE_IDLE;
  while (current_recompression_mode != RECOMPRESSION_NONE) {
    // Is recompression enabled at current mode?
    if ((current_recompression_mode == RECOMPRESSION_HUGE_IDLE &&
         params_.recompression_huge_idle) ||
        (current_recompression_mode == RECOMPRESSION_IDLE &&
         params_.recompression_idle) ||
        (current_recompression_mode == RECOMPRESSION_HUGE &&
         params_.recompression_huge)) {
      // If currently working on huge_idle or idle mode, mark idle for pages.
      if (current_recompression_mode == RECOMPRESSION_HUGE_IDLE ||
          current_recompression_mode == RECOMPRESSION_IDLE) {
        std::optional<uint64_t> idle_age_sec =
            GetCurrentIdleTimeSec(params_.idle_min_time.InSeconds(),
                                  params_.idle_max_time.InSeconds());
        if (!idle_age_sec.has_value()) {
          // Failed to calculate idle age, directly move to huge page.
          current_recompression_mode = RECOMPRESSION_HUGE;
          continue;
        }
        status = MarkIdle(*idle_age_sec);
        if (!status.ok()) {
          LOG(ERROR) << "Can not mark zram idle:" << status;
          return;
        }
      }

      // Then we initiate recompression.
      status = InitiateRecompression(current_recompression_mode);
      if (!status.ok()) {
        LOG(ERROR) << "Can not initiate zram recompression" << status;
        return;
      }
      last_recompression_ = base::Time::Now();
    }

    // Move to the next stage.
    if (current_recompression_mode == RECOMPRESSION_HUGE_IDLE)
      current_recompression_mode = RECOMPRESSION_IDLE;
    else if (current_recompression_mode == RECOMPRESSION_IDLE)
      current_recompression_mode = RECOMPRESSION_HUGE;
    else
      current_recompression_mode = RECOMPRESSION_NONE;
  }
}

}  // namespace swap_management
