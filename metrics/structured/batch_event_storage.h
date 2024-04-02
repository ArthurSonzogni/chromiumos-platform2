// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_BATCH_EVENT_STORAGE_H_
#define METRICS_STRUCTURED_BATCH_EVENT_STORAGE_H_

#include <optional>
#include <string>

#include <base/time/time.h>
#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <google/protobuf/message_lite.h>

#include "metrics/structured/proto/storage.pb.h"

namespace metrics::structured {

class EventsProto;

// Event storage that periodically flushes events to disk based on certain
// conditions. Currently implemented conditions are:
//
//  1. If |this| is holding more than |params_.max_event_bytes_size| worth of
//  serialized data, then a flush will be triggered.
//  2. If the time since the last write has exceeded |params_.flush_time_limit|,
//  then a flush will be triggered on the next call to AddEvent.
//  3. Explicit call to Flush() or the destructor. If this object will not be
//  destroyed during exit, then an explicit call to Flush() should be added to
//  the shutdown sequence.
class BatchEventStorage {
 public:
  // Parameters that will configure the behavior of BatchEventStorage. Read
  // field documentation for more details.
  struct StorageParams {
    // The maximum time duration before events are flushed to disk. A flush will
    // trigger on the next event write.
    //
    // This uses uptime to compute the time elapsed since the last write. If
    // uptime cannot be retrieved, this condition will be ignored.
    base::TimeDelta flush_time_limit;

    // The maximum number of bytes before a flush to disk occurs. A flush will
    // be triggered on the current event write if this threshold is exceeded.
    //
    // This uses the estimated serialized size of the message rather than the
    // actual in-memory size since message_lite only provides the serialized
    // message size.
    int max_event_bytes_size;
  };

  BatchEventStorage(const base::FilePath& events_directory,
                    StorageParams params);

  // Calls Flush() to write any remaining events to disk.
  ~BatchEventStorage();

  BatchEventStorage(const BatchEventStorage&) = delete;
  BatchEventStorage& operator=(const BatchEventStorage&) = delete;

  // Adds an event to the storage.
  void AddEvent(StructuredEventProto event);

  // Explicit flush call to be made before the process is torn down to save
  // events.
  void Flush();

  // (TEST ONLY): Sets the current uptime and last_write_uptime_ explicitly.
  void SetUptimeForTesting(base::TimeDelta curr_uptime,
                           base::TimeDelta last_write_uptime);

  // (TEST ONLY): Returns the in-memory event count. This should only be needed
  // for testing.
  int GetInMemoryEventCountForTesting() const;

 private:
  bool IsMaxByteSize();
  bool IsMaxTimer();

  // Purges all events in-memory but does not purge events that have already
  // been flushed.
  void Purge();

  // Retrieves the current uptime.
  base::TimeDelta GetUptime();

  // Will check the criteria configured by |params_| before an explicit write
  // call is made. If none of the criteria are met, then no write will happen.
  void MaybeWrite();

  // Directory to write events.
  base::FilePath events_directory_;

  // Time since the last write occurred. Will be set to the time when this
  // object was created at first.
  base::TimeDelta last_write_uptime_;

  // In-memory copy of current events.
  EventsProto events_;

  // Batch event storage parameters to control the behavior of |this|. Refer to
  // documentation in BatchEventStorageParams for more details.
  StorageParams params_;

  std::optional<base::TimeDelta> uptime_for_test_;
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_BATCH_EVENT_STORAGE_H_
