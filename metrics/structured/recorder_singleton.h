// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_STRUCTURED_RECORDER_SINGLETON_H_
#define METRICS_STRUCTURED_RECORDER_SINGLETON_H_

#include <memory>

#include <base/no_destructor.h>
#include <brillo/brillo_export.h>

#include "metrics/structured/recorder.h"

namespace metrics::structured {

// RecorderSingleton provides a way to set MockRecorder or FakeRecorder for
// testing. This is used internally by events, but shouldn't need to be
// explicitly called by clients in non-test code.
//
// Example Usage:
//   RecorderSingleton::GetInstance()->SetRecorderForTest(
//         std::move(your_mock_recorder));
class BRILLO_EXPORT RecorderSingleton {
 public:
  RecorderSingleton();
  ~RecorderSingleton();
  RecorderSingleton(const RecorderSingleton&) = delete;
  RecorderSingleton& operator=(const RecorderSingleton&) = delete;

  static RecorderSingleton* GetInstance();
  Recorder* GetRecorder();

  // Creates and returns a handle to the recorder. Note that calling this
  // function will set the global recorder to the returned instance. Its
  // destruction will unset the global recorder.
  //
  // It is up to the caller to properly manage the lifetime.
  std::unique_ptr<Recorder> CreateRecorder(Recorder::RecorderParams params);

  void SetRecorderForTest(std::unique_ptr<Recorder> recorder);
  void DestroyRecorderForTest();

 private:
  // Implementations.
  friend class RecorderImpl;

  void SetGlobalRecorder(Recorder* recorder);
  void UnsetGlobalRecorder(Recorder* recorder);

  Recorder* g_recorder_;

  // TODO(b/333781135): Remove this once all users of SM have begun to use
  // CreateRecorder() and manage their own recorder lifetime.
  //
  // Note that this instance is never destroyed, because GetInstance() creates a
  // base::NoDestructor instance of |this|. The unique_ptr is used to document
  // ownership.
  std::unique_ptr<Recorder> owned_recorder_;
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_RECORDER_SINGLETON_H_
