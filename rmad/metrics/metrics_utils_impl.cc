// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/metrics/metrics_utils_impl.h"

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <metrics/structured/structured_events.h>

#include "rmad/constants.h"
#include "rmad/metrics/metrics_constants.h"
#include "rmad/utils/json_store.h"

using StructuredShimlessRmaReport =
    metrics::structured::events::rmad::ShimlessRmaReport;
using StructuredReplacedComponent =
    metrics::structured::events::rmad::ReplacedComponent;
using StructuredOccurredError =
    metrics::structured::events::rmad::OccurredError;
using StructuredAdditionalActivity =
    metrics::structured::events::rmad::AdditionalActivity;

namespace rmad {

MetricsUtilsImpl::MetricsUtilsImpl(bool record_to_system)
    : record_to_system_(record_to_system) {}

bool MetricsUtilsImpl::Record(scoped_refptr<JsonStore> json_store,
                              bool is_complete) {
  if (!RecordShimlessRmaReport(json_store, is_complete) ||
      !RecordOccurredErrors(json_store) ||
      !RecordReplacedComponents(json_store) ||
      !RecordAdditionalActivities(json_store)) {
    return false;
  }
  return true;
}

bool MetricsUtilsImpl::RecordShimlessRmaReport(
    scoped_refptr<JsonStore> json_store, bool is_complete) {
  auto report = StructuredShimlessRmaReport();
  double current_timestamp = base::Time::Now().ToDoubleT();
  double first_setup_timestamp;
  if (!json_store->GetValue(kFirstSetupTimestamp, &first_setup_timestamp)) {
    LOG(ERROR) << "Failed to get timestamp of the first setup.";
    return false;
  }
  report.SetOverallTime(
      static_cast<int>(current_timestamp - first_setup_timestamp));

  double setup_timestamp;
  if (!json_store->GetValue(kSetupTimestamp, &setup_timestamp) ||
      !json_store->SetValue(kSetupTimestamp, current_timestamp)) {
    LOG(ERROR) << "Failed to get and reset setup timestamp for measuring "
                  "running time.";
    return false;
  }
  double running_time = 0.0;
  // It could be the first time we have calculated the running time, thus the
  // return value is ignored.
  json_store->GetValue(kRunningTime, &running_time);
  running_time += current_timestamp - setup_timestamp;
  report.SetRunningTime(static_cast<int>(running_time));

  report.SetIsComplete(is_complete);

  if (bool is_ro_verified;
      json_store->GetValue(kRoFirmwareVerified, &is_ro_verified)) {
    if (is_ro_verified) {
      report.SetRoVerification(static_cast<int64_t>(RoVerification::PASS));
    } else {
      report.SetRoVerification(
          static_cast<int64_t>(RoVerification::UNSUPPORTED));
    }
  } else {
    report.SetRoVerification(static_cast<int64_t>(RoVerification::UNKNOWN));
  }

  ReturningOwner returning_owner = ReturningOwner::UNKNOWN;
  // Ignore the else part and leave it as the default value, because we may
  // abort earlier than we know it.
  if (bool is_same_owner; json_store->GetValue(kSameOwner, &is_same_owner)) {
    if (is_same_owner) {
      returning_owner = ReturningOwner::SAME_OWNER;
    } else {
      returning_owner = ReturningOwner::DIFFERENT_OWNER;
    }
  }
  report.SetReturningOwner(static_cast<int64_t>(returning_owner));

  MainboardReplacement mlb_replacement = MainboardReplacement::UNKNOWN;
  // Ignore the else part and leave it as the default value, because we may
  // abort earlier than we know it.
  if (bool is_mlb_replaced;
      json_store->GetValue(kMlbRepair, &is_mlb_replaced)) {
    if (is_mlb_replaced) {
      mlb_replacement = MainboardReplacement::REPLACED;
    } else {
      mlb_replacement = MainboardReplacement::ORIGINAL;
    }
  }
  report.SetMainboardReplacement(static_cast<int64_t>(mlb_replacement));

  int wp_disable_method = static_cast<int>(WriteProtectDisableMethod::UNKNOWN);
  // Ignore the else part, because we may not have decided on it.
  if (json_store->GetValue(kWriteProtectDisableMethod, &wp_disable_method)) {
    if (std::find(kValidWpDisableMethods.begin(), kValidWpDisableMethods.end(),
                  static_cast<WriteProtectDisableMethod>(wp_disable_method)) ==
        kValidWpDisableMethods.end()) {
      LOG(ERROR) << "Failed to parse [" << wp_disable_method
                 << "] as write protect disable method to append to metrics.";
      return false;
    }
  }
  report.SetWriteProtectDisableMethod(wp_disable_method);

  if (record_to_system_ && !report.Record()) {
    LOG(ERROR) << "Failed to record shimless rma report to metrics.";
    return false;
  }

  return true;
}

bool MetricsUtilsImpl::RecordReplacedComponents(
    scoped_refptr<JsonStore> json_store) {
  // Ignore the else part, because we may not replace anything.
  if (std::vector<std::string> replace_component_names;
      json_store->GetValue(kReplacedComponentNames, &replace_component_names)) {
    for (const std::string& component_name : replace_component_names) {
      if (RmadComponent component;
          RmadComponent_Parse(component_name, &component)) {
        auto structured_replace_component = StructuredReplacedComponent();
        structured_replace_component.SetComponentCategory(component);
        if (record_to_system_ && !structured_replace_component.Record()) {
          LOG(ERROR) << "Failed to record replaced component to metrics.";
          return false;
        }
      } else {
        LOG(ERROR) << "Failed to parse [" << component_name
                   << "] as component to append to metrics.";
        return false;
      }
    }
  }
  return true;
}

bool MetricsUtilsImpl::RecordOccurredErrors(
    scoped_refptr<JsonStore> json_store) {
  // Ignore the else part, because we may have no errors.
  if (std::vector<std::string> occurred_errors;
      json_store->GetValue(kOccurredErrors, &occurred_errors)) {
    for (const std::string& occurred_error : occurred_errors) {
      if (RmadErrorCode error_code;
          RmadErrorCode_Parse(occurred_error, &error_code)) {
        auto structured_occurred_error = StructuredOccurredError();
        structured_occurred_error.SetErrorType(error_code);
        if (record_to_system_ && !structured_occurred_error.Record()) {
          LOG(ERROR) << "Failed to record error code to metrics.";
          return false;
        }
      } else {
        LOG(ERROR) << "Failed to parse [" << occurred_error
                   << "] as error code to append to metrics.";
        return false;
      }
    }
  }
  return true;
}

bool MetricsUtilsImpl::RecordAdditionalActivities(
    scoped_refptr<JsonStore> json_store) {
  // Ignore the else part, because we may have no additional activities.
  if (std::vector<int> additional_activities;
      json_store->GetValue(kAdditionalActivities, &additional_activities)) {
    for (int additional_activity : additional_activities) {
      if (std::find(kValidAdditionalActivities.begin(),
                    kValidAdditionalActivities.end(),
                    static_cast<AdditionalActivity>(additional_activity)) !=
          kValidAdditionalActivities.end()) {
        auto structured_additional_activity = StructuredAdditionalActivity();
        structured_additional_activity.SetActivityType(additional_activity);
        if (record_to_system_ && !structured_additional_activity.Record()) {
          LOG(ERROR) << "Failed to record additional activity to metrics.";
          return false;
        }
      } else {
        LOG(ERROR) << "Failed to parse [" << additional_activity
                   << "] as additional activity to append to metrics.";
        return false;
      }
    }
  }
  return true;
}

}  // namespace rmad
