// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/structured/recorder_singleton.h"

#include <memory>
#include <utility>

#include <base/time/time.h>
#include <base/no_destructor.h>

#include "metrics/structured/recorder_impl.h"

namespace metrics::structured {
namespace {

constexpr char kEventsPath[] = "/var/lib/metrics/structured/events";

constexpr char kKeysPath[] = "/var/lib/metrics/structured/keys";

// Max bytes size for event proto in-memory before a flush is triggered.
constexpr int kMaxEventBytesSize = 10000;  // 10KB

// Max time elapsed since last write before a flush of events is triggered. This
// is currently set to 0 while users of Structured metrics are migrated to
// explicitly call Flush() at the end of their programs. Otherwise, events
// in-memory at the end of a process will not be flushed to disk and lost.
//
// TODO(b/333781135): Change this to an actual value once all users have been
// migrated.
constexpr base::TimeDelta kFlushTimeLimitSeconds = base::Seconds(0);

}  // namespace

RecorderSingleton* RecorderSingleton::GetInstance() {
  static base::NoDestructor<RecorderSingleton> recorder_singleton{};
  return recorder_singleton.get();
}

Recorder* RecorderSingleton::GetRecorder() {
  // Create a default recorder if one does not exist.
  if (!g_recorder_) {
    owned_recorder_ = std::unique_ptr<RecorderImpl>(
        new RecorderImpl(kEventsPath, kKeysPath,
                         Recorder::RecorderParams{
                             .write_cadence = kFlushTimeLimitSeconds,
                             .max_in_memory_size_bytes = kMaxEventBytesSize}));
  }
  return g_recorder_;
}

std::unique_ptr<Recorder> RecorderSingleton::CreateRecorder(
    Recorder::RecorderParams params) {
  return std::unique_ptr<RecorderImpl>(
      new RecorderImpl(kEventsPath, kKeysPath, params));
}

void RecorderSingleton::SetRecorderForTest(std::unique_ptr<Recorder> recorder) {
  owned_recorder_ = std::move(recorder);
  g_recorder_ = owned_recorder_.get();
}

void RecorderSingleton::DestroyRecorderForTest() {
  owned_recorder_.reset();
}

RecorderSingleton::RecorderSingleton() = default;
RecorderSingleton::~RecorderSingleton() = default;

void RecorderSingleton::SetGlobalRecorder(Recorder* recorder) {
  g_recorder_ = recorder;
}

void RecorderSingleton::UnsetGlobalRecorder(Recorder* recorder) {
  if (g_recorder_ == recorder) {
    g_recorder_ = nullptr;
  }
}

}  // namespace metrics::structured
