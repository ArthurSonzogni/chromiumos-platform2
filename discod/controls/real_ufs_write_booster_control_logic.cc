// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "discod/controls/real_ufs_write_booster_control_logic.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <brillo/blkdev_utils/disk_iostat.h>

#include "discod/controls/binary_control.h"
#include "discod/controls/ufs_write_booster_control_logic.h"
#include "discod/utils/libhwsec_status_import.h"

namespace discod {

namespace {

// Threshold for considering io pattern "intensive". MiB/sec
constexpr uint64_t kWriteBwThreshold = 50 * 1024 * 1024;
// Amount of evaluation cycles over threshold to enable WriteBooster.
// The cycle period is determined by the caller.
constexpr uint64_t kWriteBwThresholdEnableHysteresis = 3;
// Amount of evaluation cycles under threshold to disable WriteBooster.
// The cycle period is determined by the caller.
constexpr uint64_t kWriteBwThresholdDisableHysteresis = 5;
// Amount of evaluation cycles under threshold to disable WriteBooster after
// an explicit Enable() call.
// The cycle period is determined by the caller.
constexpr uint64_t kWriteBwThresholdDisableExplicitHysteresis = 60;

constexpr uint64_t kUfsBlockSize = 4096;

}  // namespace

RealUfsWriteBoosterControlLogic::RealUfsWriteBoosterControlLogic(
    std::unique_ptr<BinaryControl> control)
    : UfsWriteBoosterControlLogic(), control_(std::move(control)) {}

Status RealUfsWriteBoosterControlLogic::Reset() {
  RETURN_IF_ERROR(control_->Toggle(BinaryControl::State::kOff));

  cycles_over_write_threshold_ = 0;
  cycles_under_write_threshold_ = 0;
  explicit_trigger_ = false;
  last_decision_ = BinaryControl::State::kOff;

  return OkStatus();
}

void RealUfsWriteBoosterControlLogic::UpdateStatistics(uint64_t bw) {
  if (bw >= kWriteBwThreshold) {
    ++cycles_over_write_threshold_;
    cycles_under_write_threshold_ = 0;
  } else {
    cycles_over_write_threshold_ = 0;
    ++cycles_under_write_threshold_;
  }
}

BinaryControl::State RealUfsWriteBoosterControlLogic::CalculateTargetState() {
  BinaryControl::State target = last_decision_;

  if (cycles_over_write_threshold_ >= kWriteBwThresholdEnableHysteresis) {
    target = BinaryControl::State::kOn;
  }

  if (!explicit_trigger_ &&
      cycles_under_write_threshold_ >= kWriteBwThresholdDisableHysteresis) {
    target = BinaryControl::State::kOff;
  }

  if (explicit_trigger_ && cycles_under_write_threshold_ >=
                               kWriteBwThresholdDisableExplicitHysteresis) {
    target = BinaryControl::State::kOff;
  }

  return target;
}

Status RealUfsWriteBoosterControlLogic::UpdateState(
    BinaryControl::State target) {
  RETURN_IF_ERROR(control_->Toggle(target));
  last_decision_ = target;
  if (last_decision_ == BinaryControl::State::kOff) {
    explicit_trigger_ = false;
  }

  return OkStatus();
}

Status RealUfsWriteBoosterControlLogic::Update(
    const brillo::DiskIoStat::Delta& delta) {
  uint64_t written_bytes = delta->GetWrittenSectors() * kUfsBlockSize;
  uint64_t ts_delta = delta->GetTimestamp().InMilliseconds();

  VLOG(2) << "RealUfsWriteBoosterControlLogic::Update"
          << "  written_bytes_delta=" << written_bytes
          << "  timestamp_delta=" << ts_delta;

  uint64_t bw = written_bytes * 1000 / ts_delta;

  VLOG(2) << "  bw=" << bw;

  UpdateStatistics(bw);

  VLOG(2) << "  cycles_under_write_threshold_=" << cycles_under_write_threshold_
          << "  cycles_over_write_threshold_=" << cycles_over_write_threshold_
          << "  explicit_trigger_=" << explicit_trigger_
          << "  last_decision_=" << static_cast<int32_t>(last_decision_);

  BinaryControl::State target = CalculateTargetState();

  VLOG(2) << "  decision target=" << static_cast<int32_t>(target);

  if (target != last_decision_) {
    VLOG(1) << "  toggle target=" << static_cast<int32_t>(target);
    RETURN_IF_ERROR(UpdateState(target));
  }

  return OkStatus();
}

Status RealUfsWriteBoosterControlLogic::Enable() {
  VLOG(2) << "RealUfsWriteBoosterControlLogic::Enable";

  explicit_trigger_ = true;
  cycles_over_write_threshold_ = 0;
  cycles_under_write_threshold_ = 0;
  RETURN_IF_ERROR(control_->Toggle(BinaryControl::State::kOn));
  last_decision_ = BinaryControl::State::kOn;

  return OkStatus();
}

}  // namespace discod
