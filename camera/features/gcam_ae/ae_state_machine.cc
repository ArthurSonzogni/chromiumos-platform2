/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/ae_state_machine.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <base/timer/elapsed_timer.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

// The log2 IIR filter strength for the long/short TET computed by Gcam AE.
constexpr float kFilterStrength = 0.85f;

constexpr float kTetEpsilon = 1.0e-8f;

// IIR filter on log2 space:
//   exp2(|strength| * log2(current_value) + (1 - |strength|) * log2(new_value))
float IirFilterLog2(float current_value, float new_value, float strength) {
  if (current_value > kTetEpsilon && new_value > kTetEpsilon) {
    const float curr_log = std::log2f(current_value);
    const float new_log = std::log2f(new_value);
    const float next_log = strength * curr_log + (1 - strength) * new_log;
    return std::max(std::exp2f(next_log), kTetEpsilon);
  }
  return current_value;
}

// Gets a smoothed TET value moving from |previous| to |target| with no more
// than |step_log2| difference in the log2 space.
float SmoothTetTransition(const float target,
                          const float previous,
                          const float step_log2) {
  if (target > kTetEpsilon && previous > kTetEpsilon) {
    float prev_log = std::log2f(previous);
    if (target > previous) {
      return std::min(target, std::exp2f(prev_log + step_log2));
    } else {
      return std::max(target, std::exp2f(prev_log - step_log2));
    }
  }
  return target;
}

}  // namespace

void AeStateMachine::OnNewAeParameters(InputParameters inputs,
                                       MetadataLogger* metadata_logger) {
  base::AutoLock lock(lock_);
  const AeFrameInfo& frame_info = inputs.ae_frame_info;
  const AeParameters& raw_ae_parameters = inputs.ae_parameters;

  VLOGFID(1, frame_info.frame_number)
      << "Raw AE parameters:"
      << " short_tet=" << raw_ae_parameters.short_tet
      << " long_tet=" << raw_ae_parameters.long_tet;

  // Filter the TET transition to avoid AE fluctuations or hunting.
  if (!current_ae_parameters_.IsValid()) {
    // This is the first set of AE parameters we get.
    current_ae_parameters_ = raw_ae_parameters;
  } else {
    current_ae_parameters_.long_tet =
        IirFilterLog2(current_ae_parameters_.long_tet,
                      raw_ae_parameters.long_tet, kFilterStrength);
    current_ae_parameters_.short_tet =
        IirFilterLog2(current_ae_parameters_.short_tet,
                      raw_ae_parameters.short_tet, kFilterStrength);
  }

  VLOGFID(1, frame_info.frame_number)
      << "Filtered AE parameters:"
      << " short_tet=" << current_ae_parameters_.short_tet
      << " long_tet=" << current_ae_parameters_.long_tet << " hdr_ratio="
      << current_ae_parameters_.long_tet / current_ae_parameters_.short_tet;

  if (metadata_logger) {
    metadata_logger->Log(frame_info.frame_number, kTagShortTet,
                         raw_ae_parameters.short_tet);
    metadata_logger->Log(frame_info.frame_number, kTagLongTet,
                         raw_ae_parameters.long_tet);
    metadata_logger->Log(frame_info.frame_number, kTagFilteredShortTet,
                         current_ae_parameters_.short_tet);
    metadata_logger->Log(frame_info.frame_number, kTagFilteredLongTet,
                         current_ae_parameters_.long_tet);
  }

  const float new_tet = current_ae_parameters_.short_tet;
  const float actual_tet_set = frame_info.exposure_time_ms *
                               frame_info.analog_gain * frame_info.digital_gain;

  // Compute state transition.
  State next_state;
  switch (current_state_) {
    case State::kInactive:
      next_state = State::kSearching;
      break;

    case State::kSearching:
      SearchTargetTet(frame_info, inputs, new_tet);
      if (target_tet_) {
        next_state = State::kConverging;
      } else {
        next_state = State::kSearching;
      }
      break;

    case State::kConverging: {
      SearchTargetTet(frame_info, inputs, new_tet);
      if (!target_tet_) {
        next_state = State::kSearching;
        break;
      }
      ConvergeToTargetTet(frame_info, inputs, actual_tet_set);
      if (converged_tet_) {
        next_state = State::kConverged;
      } else {
        next_state = State::kConverging;
      }
      break;
    }

    case State::kConverged: {
      SearchTargetTet(frame_info, inputs, new_tet);
      if (target_tet_ &&
          std::fabs(std::log2f(*converged_tet_) - std::log2f(*target_tet_)) <=
              tuning_parameters_.tet_rescan_threshold_log2) {
        last_converged_time_ = base::TimeTicks::Now();
        next_state = State::kConverged;
        break;
      }
      if ((base::TimeTicks::Now() - last_converged_time_).InMilliseconds() >
          *tet_retention_duration_ms_) {
        if (target_tet_) {
          next_state = State::kConverging;
        } else {
          next_state = State::kSearching;
        }
        break;
      } else {
        next_state = State::kConverged;
      }
      break;
    }

    case State::kLocked:
      // TODO(jcliang): Handle transitioning into the locked state.
      SearchTargetTet(frame_info, inputs, new_tet);
      if (ae_locked_) {
        next_state = State::kLocked;
      } else {
        if (!target_tet_) {
          next_state = State::kSearching;
        } else {
          next_state = State::kConverging;
        }
      }
      break;
  }

  VLOGFID(1, frame_info.frame_number)
      << "state=" << current_state_ << " next_state=" << next_state
      << " actual_tet_set=" << actual_tet_set;

  // Execute state entry actions.
  switch (next_state) {
    case State::kInactive:
      break;

    case State::kSearching: {
      next_tet_to_set_ = SmoothTetTransition(
          new_tet, next_tet_to_set_, tuning_parameters_.converging_step_log2);
      next_hdr_ratio_to_set_ =
          current_ae_parameters_.long_tet / current_ae_parameters_.short_tet;
      break;
    }

    case State::kConverging: {
      next_tet_to_set_ =
          SmoothTetTransition(*target_tet_, next_tet_to_set_,
                              tuning_parameters_.converging_step_log2);
      // TODO(jcliang): Test using |target_hdr_ratio_| here.
      next_hdr_ratio_to_set_ =
          current_ae_parameters_.long_tet / current_ae_parameters_.short_tet;
      break;
    }

    case State::kConverged:
      next_tet_to_set_ = *converged_tet_;
      next_hdr_ratio_to_set_ = *converged_hdr_ratio_;
      break;

    case State::kLocked:
      // Keep |next_tet_to_set_| unchanged.
      // TODO(jcliang): Handle transitioning into the locked state.
      break;
  }
  VLOGFID(1, frame_info.frame_number) << "next_tet_to_set=" << next_tet_to_set_;
  VLOGFID(1, frame_info.frame_number)
      << "next_hdr_ratio_to_set=" << next_hdr_ratio_to_set_;

  previous_tet_ = new_tet;
  current_state_ = next_state;
}

void AeStateMachine::OnReset() {
  base::AutoLock lock(lock_);
  current_state_ = State::kInactive;
  previous_tet_ = 0;
  next_tet_to_set_ = 0;
  target_tet_.reset();
  target_hdr_ratio_.reset();
  converged_tet_.reset();
  converged_hdr_ratio_.reset();
  tet_retention_duration_ms_.reset();
}

float AeStateMachine::GetCaptureTet() {
  base::AutoLock lock(lock_);
  return next_tet_to_set_;
}

float AeStateMachine::GetFilteredHdrRatio() {
  base::AutoLock lock(lock_);
  return next_hdr_ratio_to_set_;
}

std::ostream& operator<<(std::ostream& os, AeStateMachine::State state) {
  std::string state_str;
  switch (state) {
    case AeStateMachine::State::kInactive:
      state_str = "Inactive";
      break;
    case AeStateMachine::State::kSearching:
      state_str = "Searching";
      break;
    case AeStateMachine::State::kConverging:
      state_str = "Converging";
      break;
    case AeStateMachine::State::kConverged:
      state_str = "Converged";
      break;
    case AeStateMachine::State::kLocked:
      state_str = "Locked";
      break;
  }
  return os << state_str;
}

void AeStateMachine::SearchTargetTet(const AeFrameInfo& frame_info,
                                     const InputParameters& inputs,
                                     const float new_tet) {
  const float previous_log = std::log2f(previous_tet_);
  const float new_log = std::log2f(new_tet);
  const float search_tet_delta_log = std::fabs(previous_log - new_log);
  VLOGFID(1, frame_info.frame_number)
      << "search_tet_delta_log=" << search_tet_delta_log;
  if (search_tet_delta_log <= tuning_parameters_.tet_stabilize_threshold_log2) {
    // Make sure we set a target TET that's achievable by the camera.
    target_tet_ = inputs.tet_range.Clamp(new_tet);
    target_hdr_ratio_ =
        current_ae_parameters_.long_tet / current_ae_parameters_.short_tet;
    VLOGFID(1, frame_info.frame_number) << "target_tet=" << *target_tet_;
    VLOGFID(1, frame_info.frame_number)
        << "target_hdr_ratio=" << *target_hdr_ratio_;
  } else {
    target_tet_.reset();
    target_hdr_ratio_.reset();
    VLOGFID(1, frame_info.frame_number) << "target_tet=none";
    VLOGFID(1, frame_info.frame_number) << "target_hdr_ratio=none";
  }
}

void AeStateMachine::ConvergeToTargetTet(const AeFrameInfo& frame_info,
                                         const InputParameters& inputs,
                                         const float actual_tet_set) {
  const float actual_tet_set_log = std::log2f(actual_tet_set);
  const float converge_tet_delta_log =
      std::fabs(actual_tet_set_log - std::log2f(*target_tet_));
  VLOGFID(1, frame_info.frame_number)
      << "converge_tet_delta_log=" << converge_tet_delta_log;
  if (converge_tet_delta_log < tuning_parameters_.tet_converge_threshold_log2) {
    if (!converged_tet_) {
      converged_tet_ = actual_tet_set;
      converged_hdr_ratio_ = current_ae_parameters_.long_tet / actual_tet_set;
      tet_retention_duration_ms_ =
          frame_info.faces->empty()
              ? tuning_parameters_.tet_retention_duration_ms_default
              : tuning_parameters_.tet_retention_duration_ms_with_face;
    }
    VLOGFID(1, frame_info.frame_number) << "converged_tet=" << *converged_tet_;
    VLOGFID(1, frame_info.frame_number)
        << "converged_hdr_ratio=" << *converged_hdr_ratio_;
    VLOGFID(1, frame_info.frame_number)
        << "tet_retention_duration_ms=" << *tet_retention_duration_ms_;
  } else {
    converged_tet_.reset();
    converged_hdr_ratio_.reset();
    tet_retention_duration_ms_.reset();
    VLOGFID(1, frame_info.frame_number) << "converged_tet=none";
    VLOGFID(1, frame_info.frame_number) << "converged_hdr_ratio=none";
    VLOGFID(1, frame_info.frame_number) << "tet_retention_duration_ms=none";
  }
}

}  // namespace cros
