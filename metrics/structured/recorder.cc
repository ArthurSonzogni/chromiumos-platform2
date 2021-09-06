// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/recorder.h"

#include <memory>
#include <sys/file.h>
#include <utility>

#include <base/bind.h>
#include <base/macros.h>
#include <base/logging.h>
#include <base/guid.h>
#include <metrics/structured/structured_events.h>
#include <metrics/structured/event_base.h>
#include <metrics/structured/proto/storage.pb.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/strcat.h>

namespace metrics {
namespace structured {
namespace {

constexpr char kEventsPath[] = "/var/lib/metrics/structured/events";

constexpr char kKeysPath[] = "/var/lib/metrics/structured/keys";

constexpr mode_t kFilePermissions = 0660;

// Writes |events| to a file within |directory|. Fails if |directory| doesn't
// exist. Returns whether the write was successful.
bool WriteEventsProtoToDir(const std::string& directory,
                           const EventsProto& events) {
  const std::string guid = base::GenerateGUID();
  if (guid.empty())
    return false;
  const std::string filepath = base::StrCat({directory, "/", guid});

  base::ScopedFD file_descriptor(
      open(filepath.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600));
  if (file_descriptor.get() < 0) {
    PLOG(ERROR) << filepath << " cannot open";
    return false;
  }

  if (!events.SerializeToFileDescriptor(file_descriptor.get())) {
    PLOG(ERROR) << filepath << " write error";
    return false;
  }

  // Explicitly set permissions on the created event file. This is done
  // separately to the open call to be independent of the umask.
  if (fchmod(file_descriptor.get(), kFilePermissions) < 0) {
    PLOG(ERROR) << filepath << " cannot chmod";
    return false;
  }

  return true;
}

}  // namespace

// static
Recorder* Recorder::GetInstance() {
  static base::NoDestructor<Recorder> recorder{kEventsPath, kKeysPath};
  return recorder.get();
}

Recorder::Recorder(const std::string& events_directory,
                   const std::string& keys_path)
    : events_directory_(events_directory), key_data_(keys_path) {}

Recorder::~Recorder() = default;

bool Recorder::Record(const EventBase& event) {
  if (!metrics_library_.AreMetricsEnabled())
    return false;

  EventsProto events_proto;
  StructuredEventProto* event_proto;
  if (event.id_type() == EventBase::IdType::kUmaId) {
    // TODO(crbug.com/1148168): Unimplemented.
    NOTREACHED();
    return false;
  } else {
    event_proto = events_proto.add_non_uma_events();
  }

  // Set the ID for this event, if any.
  switch (event.id_type()) {
    case EventBase::IdType::kProjectId:
      event_proto->set_profile_event_id(
          key_data_.Id(event.project_name_hash()));
      break;
    case EventBase::IdType::kUmaId:
      // TODO(crbug.com/1148168): Unimplemented.
      NOTREACHED();
      break;
    case EventBase::IdType::kUnidentified:
      // Do nothing.
      break;
    default:
      // In case id_type is uninitialized.
      NOTREACHED();
      break;
  }

  // Set the event type. Do this with a switch statement to catch when the event
  // type is UNKNOWN or uninitialized.
  switch (event.event_type()) {
    case StructuredEventProto_EventType_REGULAR:
    case StructuredEventProto_EventType_RAW_STRING:
      event_proto->set_event_type(event.event_type());
      break;
    default:
      NOTREACHED();
      break;
  }

  event_proto->set_event_name_hash(event.name_hash());

  // Set each metric's name hash and value.
  for (const auto& metric : event.metrics()) {
    auto* metric_proto = event_proto->add_metrics();
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
    }
  }

  return WriteEventsProtoToDir(events_directory_, events_proto);
}

}  // namespace structured
}  // namespace metrics
