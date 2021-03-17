// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_RECORDER_H_
#define METRICS_STRUCTURED_RECORDER_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <metrics/structured/key_data.h>
#include <base/no_destructor.h>
#include <metrics/metrics_library.h>

namespace metrics {
namespace structured {

class EventBase;
class EventsProto;

// Writes metrics to disk for collection and upload by chrome. A singleton
// returned by GetInstance should be used for this purpose, and can be passed an
// event via Record. Record processes the event, including adding identifiers
// and computing HMAC metrics.
class Recorder {
 public:
  static Recorder* GetInstance();
  // Returns false if the event will definitely not be recorded, eg. due to
  // consent. Returns true if the event will likely be reported, though this
  // may fail if, for example, chrome fails to upload the log after collection.
  bool Record(const EventBase& event);

 private:
  friend class base::NoDestructor<Recorder>;

  Recorder(const std::string& events_directory, const std::string& keys_path);
  ~Recorder();
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

  // Where to save event protos.
  const std::string events_directory_;

  // Used for checking the UMA consent.
  MetricsLibrary metrics_library_;

  KeyData key_data_;
};

}  // namespace structured
}  // namespace metrics

#endif  // METRICS_STRUCTURED_RECORDER_H_
