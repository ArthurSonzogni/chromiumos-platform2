// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ANALYTICS_METRICS_H_
#define MISSIVE_ANALYTICS_METRICS_H_

#include <memory>
#include <string>

#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/thread_pool.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>

namespace reporting::analytics {

// Provides access to `MetricsLibrary`. Guarantees that all calls to Send*ToUMA
// happen on the same task sequence.
//
// To use this class, call its Send*ToUMA methods just like `MetricsLibrary`:
//
//   Metrics::Get().SendToUMA(....);
//   Metrics::Get().SendLinearToUMA(....);
class Metrics {
 public:
  class TestEnvironment;

  Metrics();
  Metrics(const Metrics&) = delete;
  Metrics& operator=(const Metrics&) = delete;

  // Gets the metrics instance.
  static Metrics& Get();

  // Proxy of `MetricsLibraryInterface::SendPercentageToUMA`.
  bool SendPercentageToUMA(const std::string& name, int sample);

  // Proxy of `MetricsLibraryInterface::SendLinearToUMA`.
  bool SendLinearToUMA(const std::string& name, int sample, int max);

  // Proxy of `MetricsLibraryInterface::SendToUMA`.
  bool SendToUMA(
      const std::string& name, int sample, int min, int max, int nbuckets);

  // Add new proxy methods here when you need to use
  // `MetricsLibrary::Send*ToUMA` methods that are not proxied above.

 private:
  friend class TestEnvironment;

  // Sends data to UMA.
  template <typename FuncType, typename... ArgTypes>
  bool PostUMATask(FuncType send_to_uma_func, ArgTypes... args);

  // The task runner on which metrics sends data. In production code, it never
  // changes once set.
  scoped_refptr<base::SequencedTaskRunner> metrics_task_runner_{
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})};

  // The only `MetricsLibrary` instance. In production code, it never changes
  // once set. Its instantiation is thread-safe because `Metrics` is initialized
  // as a static variable inside a function, which is thread-safe since C++ 11.
  std::unique_ptr<MetricsLibraryInterface> metrics_{
      std::make_unique<MetricsLibrary>()};
};
}  // namespace reporting::analytics

#endif  // MISSIVE_ANALYTICS_METRICS_H_
