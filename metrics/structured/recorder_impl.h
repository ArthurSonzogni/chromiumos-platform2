// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_RECORDER_IMPL_H_
#define METRICS_STRUCTURED_RECORDER_IMPL_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/time/time.h>

#include "metrics/metrics_library.h"
#include "metrics/structured/batch_event_storage.h"
#include "metrics/structured/key_data.h"
#include "metrics/structured/recorder.h"
#include "metrics/structured/recorder_singleton.h"

namespace metrics::structured {

class EventBase;

// State to represent when the counter file has not been read.
constexpr int kCounterFileUnread = -1;

// Writes metrics to disk for collection and upload by chrome. A singleton
// returned by GetInstance should be used for this purpose, and can be passed an
// event via Record. Record processes the event, including adding identifiers
// and computing HMAC metrics.
//
// Note that a call to Flush() is made on the destructor. If this object will
// not be destroyed during exit, then an explicit call to Flush() should be
// added to the shutdown sequence to ensure events are properly saved onto disk.
class RecorderImpl : public Recorder {
 public:
  // Calls Flush() to write any remaining events to disk.
  ~RecorderImpl() override;

  // Returns false if the event will definitely not be recorded, eg. due to
  // consent. Returns true if the event will likely be reported, though this
  // may fail if, for example, chrome fails to upload the log after collection.
  bool Record(const EventBase& event) override;

  // Explicit call to flush to be made before the process is torn down to save
  // events.
  void Flush() override;

 private:
  friend class FakeRecorder;
  // TODO(b/333781135): Remove RecorderSingleton friend once all users of SM
  // have begun to use CreateRecorder() and manage their own recorder lifetime.
  friend class RecorderSingleton;
  friend class RecorderTest;

  friend std::unique_ptr<Recorder> RecorderSingleton::CreateRecorder(
      Recorder::RecorderParams params);

  RecorderImpl(const std::string& events_directory,
               const std::string& keys_path,
               Recorder::RecorderParams params);

  RecorderImpl(const std::string& events_directory,
               const std::string& keys_path,
               Recorder::RecorderParams params,
               const base::FilePath& reset_counter_file,
               std::unique_ptr<MetricsLibraryInterface> metrics_library);

  RecorderImpl(const RecorderImpl&) = delete;
  RecorderImpl& operator=(const RecorderImpl&) = delete;

  // Loads the reset counter if it hasn't been read yet.
  int GetResetCounter();

  // Get system uptime.
  std::optional<base::TimeDelta> GetUptime();

  // Where to save event protos.
  const std::string events_directory_;

  platform::KeyData key_data_;

  const base::FilePath reset_counter_file_;

  // -1 represents an uninitialized state.
  int reset_counter_ = kCounterFileUnread;

  // Used for checking the UMA consent.
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;

  // Used to batch write events to disk.
  BatchEventStorage event_storage_;
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_RECORDER_IMPL_H_
