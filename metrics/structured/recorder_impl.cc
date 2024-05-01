// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/recorder_impl.h"

#include <errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>

#include <memory>
#include <sstream>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <base/uuid.h>

#include "metrics/structured/batch_event_storage.h"
#include "metrics/structured/event_base.h"
#include "metrics/structured/proto/storage.pb.h"
#include "metrics/structured/recorder.h"
#include "metrics/structured/recorder_singleton.h"
#include "metrics/structured/structured_events.h"

namespace metrics::structured {
namespace {

// Path to the reset counter path. This should be always be synced with the path
// in reset_counter_updater.cc.
const char kResetCounterPath[] = "/var/lib/metrics/structured/reset-counter";

}  // namespace

RecorderImpl::RecorderImpl(const std::string& events_directory,
                           const std::string& keys_path,
                           Recorder::RecorderParams params)
    : RecorderImpl(events_directory,
                   keys_path,
                   params,
                   base::FilePath(kResetCounterPath),
                   std::make_unique<MetricsLibrary>()) {}

RecorderImpl::RecorderImpl(
    const std::string& events_directory,
    const std::string& keys_path,
    Recorder::RecorderParams params,
    const base::FilePath& reset_counter_file,
    std::unique_ptr<MetricsLibraryInterface> metrics_library)
    : events_directory_(events_directory),
      key_data_(keys_path),
      reset_counter_file_(reset_counter_file),
      metrics_library_(std::move(metrics_library)),
      event_storage_(
          base::FilePath(events_directory_),
          BatchEventStorage::StorageParams{
              .flush_time_limit = params.write_cadence,
              .max_event_bytes_size = params.max_in_memory_size_bytes}) {
  RecorderSingleton::GetInstance()->SetGlobalRecorder(this);
}

RecorderImpl::~RecorderImpl() {
  RecorderSingleton::GetInstance()->UnsetGlobalRecorder(this);
}

bool RecorderImpl::Record(const EventBase& event) {
  // Do not record if the UMA consent is opted out, except for metrics for the
  // rmad project, usb projects, and rollback project.
  //
  // rmad metrics skip this check because, at the time of recording, the UMA
  // consent status is undetermined. The same applies to usb and rollback
  // metrics.
  //
  // These metrics will be discarded if needed by the consent check in chromium,
  // which happens when the events are read from disk.
  //
  // kProjectNameHash is common to all events in a same project so any event
  // belonging to the project can be used for the following check.
  if (event.project_name_hash() !=
          events::rmad::ShimlessRmaReport::kProjectNameHash &&
      event.project_name_hash() !=
          events::rollback_enterprise::RollbackPolicyActivated::
              kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_camera_module::UsbCameraModuleInfo::kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_device::UsbDeviceInfo::kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_session::UsbSessionEvent::kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_quality::UsbBusConnect::kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_error::HubError::kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_error::XhciError::kProjectNameHash &&
      event.project_name_hash() !=
          events::usb_pd_device::UsbPdDeviceInfo::kProjectNameHash &&
      event.project_name_hash() !=
          events::audio_peripheral_info::Info::kProjectNameHash &&
      event.project_name_hash() !=
          events::audio_peripheral::Close::kProjectNameHash &&
      event.project_name_hash() !=
          events::guest_usb_device::UsbDeviceInfo::kProjectNameHash &&
      !metrics_library_->AreMetricsEnabled()) {
    return false;
  }

  StructuredEventProto event_proto;

  // Set the ID for this event, if any.
  switch (event.id_type()) {
    case EventBase::IdType::kProjectId:
      event_proto.set_profile_event_id(key_data_.Id(event.project_name_hash()));
      break;
    case EventBase::IdType::kUnidentified:
      // Do nothing since there should be no ID attached to the event.
      break;
    case EventBase::IdType::kUmaId:
    default:
      LOG(ERROR) << "Attempting to record event of unsupported id type.";
      return false;
  }

  event_proto.set_project_name_hash(event.project_name_hash());

  // Set the event type. Do this with a switch statement to catch when the event
  // type is UNKNOWN or uninitialized.
  switch (event.event_type()) {
    case StructuredEventProto_EventType_REGULAR:
    case StructuredEventProto_EventType_RAW_STRING:
    case StructuredEventProto_EventType_SEQUENCE:
      event_proto.set_event_type(event.event_type());
      break;
    default:
      LOG(ERROR) << "Attempting to record event of unsupported event type";
      return false;
  }

  if (event_proto.event_type() == StructuredEventProto_EventType_SEQUENCE) {
    int reset_counter = GetResetCounter();
    std::optional<base::TimeDelta> uptime = GetUptime();

    // Only populate the fields if both are valid.
    if (reset_counter != kCounterFileUnread && uptime.has_value()) {
      event_proto.mutable_event_sequence_metadata()->set_reset_counter(
          reset_counter);
      event_proto.mutable_event_sequence_metadata()->set_system_uptime(
          uptime.value().InMilliseconds());
    }
  }

  event_proto.set_event_name_hash(event.name_hash());

  // Set each metric's name hash and value.
  for (const auto& metric : event.metrics()) {
    auto* metric_proto = event_proto.add_metrics();
    metric_proto->set_name_hash(metric.name_hash);

    switch (metric.type) {
      case EventBase::MetricType::kHmac:
        metric_proto->set_value_hmac(key_data_.HmacMetric(
            event.project_name_hash(), metric.name_hash, metric.hmac_value));
        break;
      case EventBase::MetricType::kInt:
        metric_proto->set_value_int64(metric.int_value);
        break;
      case EventBase::MetricType::kRawString:
        metric_proto->set_value_string(metric.string_value);
        break;
      case EventBase::MetricType::kDouble:
        metric_proto->set_value_double(metric.double_value);
        break;
      case EventBase::MetricType::kIntArray:
        StructuredEventProto::Metric::RepeatedInt64* repeated_int64 =
            metric_proto->mutable_value_repeated_int64();
        repeated_int64->mutable_values()->Assign(metric.int_array_value.begin(),
                                                 metric.int_array_value.end());
        break;
    }
  }

  event_storage_.AddEvent(std::move(event_proto));
  return true;
}

void RecorderImpl::Flush() {
  event_storage_.Flush();
}

int RecorderImpl::GetResetCounter() {
  if (reset_counter_ == kCounterFileUnread) {
    std::string content;
    if (base::ReadFileToString(reset_counter_file_, &content)) {
      std::stringstream ss(content);
      ss >> reset_counter_;
    } else {
      PLOG(ERROR) << "Unable to read reset counter file at "
                  << kResetCounterPath;
    }
  }

  return reset_counter_;
}

std::optional<base::TimeDelta> RecorderImpl::GetUptime() {
  timespec boot_time;
  if (clock_gettime(CLOCK_BOOTTIME, &boot_time) != 0) {
    PLOG(ERROR) << "Failed to get boot time.";
    return std::nullopt;
  }

  return base::Seconds(boot_time.tv_sec) + base::Nanoseconds(boot_time.tv_nsec);
}

}  // namespace metrics::structured
