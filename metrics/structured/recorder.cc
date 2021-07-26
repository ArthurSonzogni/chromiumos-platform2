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

  // Grab a lock to avoid chrome deleting the file while we're writing. Keep the
  // file locked as briefly as possible. Freeing file_descriptor will close the
  // file and remove the lock.
  if (HANDLE_EINTR(flock(file_descriptor.get(), LOCK_EX)) < 0) {
    PLOG(ERROR) << filepath << " cannot lock";
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

  EventsProto events;

  // TODO(crbug.com/1148168): use the identifier type for an event to choose
  // which list of events to save to: uma or non-uma.
  auto* event_proto = events.add_uma_events();

  event_proto->set_profile_event_id(key_data_.Id(event.project_name_hash()));
  event_proto->set_event_name_hash(event.name_hash());
  for (const auto& metric : event.metrics()) {
    auto* metric_proto = event_proto->add_metrics();
    metric_proto->set_name_hash(metric.name_hash);

    switch (metric.type) {
      case EventBase::MetricType::kInt:
        metric_proto->set_value_int64(metric.int_value);
        break;
      case EventBase::MetricType::kString:
        const int64_t hmac = key_data_.HmacMetric(
            event.project_name_hash(), metric.name_hash, metric.string_value);
        metric_proto->set_value_hmac(hmac);
        break;
    }
  }

  return WriteEventsProtoToDir(events_directory_, events);
}

}  // namespace structured
}  // namespace metrics
