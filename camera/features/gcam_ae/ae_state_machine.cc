/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/gcam_ae/ae_state_machine.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kConvergingStepLog2[] = "converging_step_log2";
constexpr char kTetConvergeStabilizeDurationMs[] =
    "tet_converge_stabilize_duration_ms";
constexpr char kTetConvergeThresholdLog2[] = "tet_converge_threshold_log2";
constexpr char kTetRescanThresholdLog2[] = "tet_rescan_threshold_log2";
constexpr char kTetRetentionDurationMsDefault[] =
    "tet_retention_duration_ms_default";
constexpr char kTetRetentionDurationMsWithFace[] =
    "tet_retention_duration_ms_with_face";
constexpr char kTetTargetThresholdLog2[] = "tet_target_threshold_log2";

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

int ElapsedTimeMs(base::TimeTicks since) {
  return (base::TimeTicks::Now() - since).InMilliseconds();
}

}  // namespace

AeStateMachine::AeStateMachine() : camera_metrics_(CameraMetrics::New()) {}

AeStateMachine::~AeStateMachine() {
  UploadMetrics();
}

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

  const float hdr_ratio =
      current_ae_parameters_.long_tet / current_ae_parameters_.short_tet;
  VLOGFID(1, frame_info.frame_number)
      << "Filtered AE parameters:"
      << " short_tet=" << current_ae_parameters_.short_tet
      << " long_tet=" << current_ae_parameters_.long_tet
      << " hdr_ratio=" << hdr_ratio;

  gcam_ae_metrics_.accumulated_hdr_ratio += hdr_ratio;
  ++gcam_ae_metrics_.num_hdr_ratio_samples;
  gcam_ae_metrics_.accumulated_tet += current_ae_parameters_.short_tet;
  ++gcam_ae_metrics_.num_tet_samples;

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
  MaybeToggleAeLock(frame_info);
  State next_state;
  switch (current_state_) {
    case State::kInactive:
      next_state = State::kSearching;

      // For camera cold start.
      convergence_starting_frame_ = frame_info.frame_number;
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
        if (ae_locked_) {
          next_state = State::kLocked;
        } else {
          if (converged_start_time_ &&
              ElapsedTimeMs(*converged_start_time_) >
                  tuning_parameters_.tet_converge_stabilize_duration_ms) {
            next_state = State::kConverged;

            // Record convergence latency whenever we transition to the
            // Converged state. Only count the metrics here so that we exclude
            // the AE lock convergence latency.
            if (convergence_starting_frame_ != kInvalidFrame) {
              gcam_ae_metrics_.accumulated_convergence_latency_frames +=
                  frame_info.frame_number - convergence_starting_frame_;
              ++gcam_ae_metrics_.num_convergence_samples;
              convergence_starting_frame_ = kInvalidFrame;
            }
            break;
          }
          if (!converged_start_time_) {
            converged_start_time_ = base::TimeTicks::Now();
          }
          next_state = State::kConverging;
        }
      } else {
        converged_start_time_.reset();
        next_state = State::kConverging;
      }
      break;
    }

    case State::kConverged: {
      SearchTargetTet(frame_info, inputs, new_tet);
      if (ae_locked_) {
        next_state = State::kConverging;
        break;
      }
      if (target_tet_ &&
          std::fabs(std::log2f(*converged_tet_) - std::log2f(*target_tet_)) <=
              tuning_parameters_.tet_rescan_threshold_log2) {
        last_converged_time_ = base::TimeTicks::Now();
        next_state = State::kConverged;
        break;
      }
      if (ElapsedTimeMs(last_converged_time_) > *tet_retention_duration_ms_) {
        if (target_tet_) {
          next_state = State::kConverging;
        } else {
          next_state = State::kSearching;
        }
        // Start convergence timer whenever we transition out of the Converged
        // state.
        convergence_starting_frame_ = frame_info.frame_number;
        break;
      } else {
        next_state = State::kConverged;
      }
      break;
    }

    case State::kLocked:
      SearchTargetTet(frame_info, inputs, new_tet);
      if (ae_locked_) {
        DCHECK(target_tet_);
        if (std::fabs(std::log2f(actual_tet_set) - std::log2f(*target_tet_)) <=
            tuning_parameters_.tet_rescan_threshold_log2) {
          next_state = State::kLocked;
        } else {
          next_state = State::kConverging;
        }
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
      DCHECK(converged_tet_);
      next_tet_to_set_ = *converged_tet_;
      next_hdr_ratio_to_set_ = *locked_hdr_ratio_;
      break;
  }

  constexpr float kInvalidTet = -1.0f;
  constexpr float kInvalidHdrRatio = -1.0f;
  constexpr int kInvalidDuration = -1;
  VLOGFID(1, frame_info.frame_number)
      << "target_tet=" << (target_tet_ ? *target_tet_ : kInvalidTet);
  VLOGFID(1, frame_info.frame_number)
      << "target_hdr_ratio="
      << (target_hdr_ratio_ ? *target_hdr_ratio_ : kInvalidHdrRatio);
  VLOGFID(1, frame_info.frame_number)
      << "converged_tet=" << (converged_tet_ ? *converged_tet_ : kInvalidTet);
  VLOGFID(1, frame_info.frame_number)
      << "converged_hdr_ratio="
      << (converged_hdr_ratio_ ? *converged_hdr_ratio_ : kInvalidHdrRatio);
  VLOGFID(1, frame_info.frame_number)
      << "tet_retention_duration_ms="
      << (tet_retention_duration_ms_ ? *tet_retention_duration_ms_
                                     : kInvalidDuration);
  VLOGFID(1, frame_info.frame_number) << "ae_locked=" << ae_locked_;
  VLOGFID(1, frame_info.frame_number)
      << "locked_tet_=" << (locked_tet_ ? *locked_tet_ : kInvalidTet);
  VLOGFID(1, frame_info.frame_number)
      << "locked_hdr_ratio_="
      << (locked_hdr_ratio_ ? *locked_hdr_ratio_ : kInvalidHdrRatio);
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
  converged_start_time_.reset();
  tet_retention_duration_ms_.reset();
  locked_tet_.reset();
  locked_hdr_ratio_.reset();
  ae_locked_ = false;
}

void AeStateMachine::OnOptionsUpdated(const base::Value& json_values) {
  base::AutoLock lock(lock_);

  {
    auto v = json_values.FindDoubleKey(kTetTargetThresholdLog2);
    if (v) {
      tuning_parameters_.tet_target_threshold_log2 = *v;
    }
  }
  {
    auto v = json_values.FindDoubleKey(kConvergingStepLog2);
    if (v) {
      tuning_parameters_.converging_step_log2 = *v;
    }
  }
  {
    auto v = json_values.FindIntKey(kTetConvergeStabilizeDurationMs);
    if (v) {
      tuning_parameters_.tet_converge_stabilize_duration_ms = *v;
    }
  }
  {
    auto v = json_values.FindDoubleKey(kTetConvergeThresholdLog2);
    if (v) {
      tuning_parameters_.tet_converge_threshold_log2 = *v;
    }
  }
  {
    auto v = json_values.FindDoubleKey(kTetRescanThresholdLog2);
    if (v) {
      tuning_parameters_.tet_rescan_threshold_log2 = *v;
    }
  }
  {
    auto v = json_values.FindDoubleKey(kTetRetentionDurationMsDefault);
    if (v) {
      tuning_parameters_.tet_retention_duration_ms_default = *v;
    }
  }
  {
    auto v = json_values.FindDoubleKey(kTetRetentionDurationMsWithFace);
    if (v) {
      tuning_parameters_.tet_retention_duration_ms_with_face = *v;
    }
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "AeStateMachine tuning parameters:"
             << " tet_target_threshold_log2="
             << tuning_parameters_.tet_target_threshold_log2
             << " converging_step_log2="
             << tuning_parameters_.converging_step_log2
             << " tet_converge_stabilize_duration_ms="
             << tuning_parameters_.tet_converge_stabilize_duration_ms
             << " tet_converge_threshold_log2="
             << tuning_parameters_.tet_converge_threshold_log2
             << " tet_rescan_threshold_log2="
             << tuning_parameters_.tet_rescan_threshold_log2
             << " tet_retention_duration_ms_default="
             << tuning_parameters_.tet_retention_duration_ms_default
             << " tet_retention_duration_ms_with_face="
             << tuning_parameters_.tet_retention_duration_ms_with_face;
  }
}

float AeStateMachine::GetCaptureTet() {
  base::AutoLock lock(lock_);
  return next_tet_to_set_;
}

float AeStateMachine::GetFilteredHdrRatio() {
  base::AutoLock lock(lock_);
  return next_hdr_ratio_to_set_;
}

uint8_t AeStateMachine::GetAndroidAeState() {
  // We don't support flash, so there's no FLASH_REQUIRED state.
  switch (current_state_) {
    case AeStateMachine::State::kInactive:
      return ANDROID_CONTROL_AE_STATE_INACTIVE;
    case AeStateMachine::State::kSearching:
    case AeStateMachine::State::kConverging:
      return ANDROID_CONTROL_AE_STATE_SEARCHING;
    case AeStateMachine::State::kConverged:
      return ANDROID_CONTROL_AE_STATE_CONVERGED;
    case AeStateMachine::State::kLocked:
      return ANDROID_CONTROL_AE_STATE_LOCKED;
  }
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
  if (ae_locked_) {
    // AE compensation is still effective when AE is locked.
    target_tet_ = *locked_tet_ * std::exp2f(frame_info.target_ae_compensation);
    target_hdr_ratio_ = *locked_hdr_ratio_;
    return;
  }

  const float previous_log = std::log2f(previous_tet_);
  const float new_log = std::log2f(new_tet);
  const float search_tet_delta_log = std::fabs(previous_log - new_log);
  VLOGFID(1, frame_info.frame_number)
      << "search_tet_delta_log=" << search_tet_delta_log;
  if (search_tet_delta_log <= tuning_parameters_.tet_target_threshold_log2) {
    // Make sure we set a target TET that's achievable by the camera.
    target_tet_ = inputs.tet_range.Clamp(new_tet);
    target_hdr_ratio_ =
        current_ae_parameters_.long_tet / current_ae_parameters_.short_tet;
  } else {
    target_tet_.reset();
    target_hdr_ratio_.reset();
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
    converged_tet_ = *target_tet_;
    converged_hdr_ratio_ = *target_hdr_ratio_;
    tet_retention_duration_ms_ =
        frame_info.faces->empty()
            ? tuning_parameters_.tet_retention_duration_ms_default
            : tuning_parameters_.tet_retention_duration_ms_with_face;
  } else {
    converged_tet_.reset();
    converged_hdr_ratio_.reset();
    tet_retention_duration_ms_.reset();
  }
}

void AeStateMachine::MaybeToggleAeLock(const AeFrameInfo& frame_info) {
  if (frame_info.client_request_settings.ae_lock) {
    if (*frame_info.client_request_settings.ae_lock ==
            ANDROID_CONTROL_AE_LOCK_ON &&
        !ae_locked_) {
      ae_locked_ = true;
      locked_tet_ = next_tet_to_set_;
      locked_hdr_ratio_ = next_hdr_ratio_to_set_;
    } else if (*frame_info.client_request_settings.ae_lock ==
                   ANDROID_CONTROL_AE_LOCK_OFF &&
               ae_locked_) {
      ae_locked_ = false;
      locked_tet_.reset();
      locked_hdr_ratio_.reset();
    }
  }
}

void AeStateMachine::UploadMetrics() {
  base::AutoLock lock(lock_);
  if (gcam_ae_metrics_.num_convergence_samples > 0) {
    camera_metrics_->SendGcamAeAvgConvergenceLatency(
        gcam_ae_metrics_.accumulated_convergence_latency_frames /
        gcam_ae_metrics_.num_convergence_samples);
  }
  if (gcam_ae_metrics_.num_hdr_ratio_samples > 0) {
    camera_metrics_->SendGcamAeAvgHdrRatio(
        gcam_ae_metrics_.accumulated_hdr_ratio /
        gcam_ae_metrics_.num_hdr_ratio_samples);
  }
  if (gcam_ae_metrics_.num_tet_samples > 0) {
    camera_metrics_->SendGcamAeAvgTet(gcam_ae_metrics_.accumulated_tet /
                                      gcam_ae_metrics_.num_tet_samples);
  }
}

}  // namespace cros
