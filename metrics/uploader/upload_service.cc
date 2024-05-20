// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/uploader/upload_service.h"

#include <memory>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/files/dir_reader_posix.h>
#include <base/functional/bind.h>
#include <base/hash/sha1.h>
#include <base/logging.h>
#include <base/metrics/histogram.h>
#include <base/metrics/histogram_base.h>
#include <base/metrics/histogram_snapshot_manager.h>
#include <base/metrics/sparse_histogram.h>
#include <base/metrics/statistics_recorder.h>
#include <base/task/single_thread_task_runner.h>

#include "metrics/serialization/metric_sample.h"
#include "metrics/serialization/serialization_utils.h"
#include "metrics/uploader/metrics_log.h"
#include "metrics/uploader/sender_http.h"
#include "metrics/uploader/system_profile_cache.h"

namespace {

bool ReadAndProcessMetricsFromDirectory(
    const std::string& dir_path,
    size_t sample_batch_max_length,
    std::vector<metrics::MetricSample>& samples,
    size_t& bytes_read_total) {
  base::DirReaderPosix reader(dir_path.c_str());
  if (!reader.IsValid()) {
    LOG(ERROR) << "Failed to create DirReaderPosix for directory: " << dir_path;
    return false;
  }

  bool result = true;
  while (bytes_read_total < sample_batch_max_length && reader.Next()) {
    std::string filename(reader.name());
    if (filename == "." || filename == "..") {
      continue;
    }
    size_t bytes_read = 0;
    if (!metrics::SerializationUtils::ReadAndDeleteMetricsFromFile(
            base::StrCat({dir_path, "/", filename}), &samples,
            sample_batch_max_length - bytes_read_total, bytes_read)) {
      result = false;
    }
    bytes_read_total += bytes_read;
  }

  return result && !reader.Next();  // If there's more to read, return false.
}

}  // namespace

const int UploadService::kMaxFailedUpload = 10;

UploadService::UploadService(SystemProfileSetter* setter,
                             MetricsLibraryInterface* metrics_lib,
                             const std::string& server)
    : system_profile_setter_(setter),
      metrics_lib_(metrics_lib),
      histogram_snapshot_manager_(this),
      sender_(new HttpSender(server)),
      testing_(false) {}

UploadService::UploadService(SystemProfileSetter* setter,
                             MetricsLibraryInterface* metrics_lib,
                             const std::string& server,
                             bool testing)
    : UploadService(setter, metrics_lib, server) {
  testing_ = testing;
}

void UploadService::Init(const base::TimeDelta& upload_interval,
                         const std::string& metrics_file,
                         const std::string& metrics_dir,
                         const std::string& early_metrics_dir,
                         bool uploads_enabled) {
  metrics_file_ = metrics_file;
  metrics_dir_ = metrics_dir;
  early_metrics_dir_ = early_metrics_dir;
  skip_upload_ = !uploads_enabled;

  if (!testing_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&UploadService::UploadEventCallback,
                       base::Unretained(this), upload_interval),
        upload_interval);
  }
}

void UploadService::StartNewLog() {
  CHECK(!staged_log_) << "the staged log should be discarded before starting "
                         "a new metrics log";
  MetricsLog* log = new MetricsLog();
  log->PopulateSystemProfile(system_profile_setter_.get());
  current_log_.reset(log);
}

void UploadService::UploadEventCallback(const base::TimeDelta& interval) {
  UploadEvent();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&UploadService::UploadEventCallback,
                     base::Unretained(this), interval),
      interval);
}

void UploadService::UploadEvent() {
  bool all_metrics_processed;

  if (skip_upload_) {
    // Process incoming samples as if we were going to upload them, but discard
    // them instead.  Consuming the samples has the side effect of truncating
    // the uma-events file, which otherwise grows indefinitely.
    while (true) {
      // ReadMetrics() may have to be called multiple times, because if
      // uma-events is too large it processes samples in batches.
      Reset();
      if (ReadMetrics())
        break;
    }
    return;
  }

  // If staged_log_ is not empty, it means that the previous upload failed.
  // Retry sending the logs, then return. (QUESTION: why return, instead of
  // continuing and sending the recent metrics as well?  It's not critical
  // since they'll be sent later, but why not now?)
  if (staged_log_) {
    SendStagedLog();
    return;
  }

  // The previous upload was successful. Read the new metrics samples from the
  // uma-events file and ship them.  If the file is too large, ReadMetrics()
  // updates the file to reflect which samples were read and returns false.
  // Loop until all samples are processed (or an error occurs).
  while (true) {
    all_metrics_processed = ReadMetrics();
    GatherHistograms();

    // No samples found. Exit to avoid sending an empty log.
    if (!current_log_)
      break;

    // Stage and send the logs.
    StageCurrentLog();
    SendStagedLog();

    // If staged_logs_ is not empty, SendStagedLog failed.  Try later.
    if (staged_log_)
      break;
    if (all_metrics_processed)
      break;
  }
}

void UploadService::SendStagedLog() {
  CHECK(staged_log_) << "staged_log_ must exist to be sent";

  // If metrics are not enabled, discard the log and exit.
  if (!metrics_lib_->AreMetricsEnabled()) {
    LOG(INFO) << "Metrics disabled. Don't upload metrics samples.";
    staged_log_.reset();
    return;
  }

  std::string log_text;
  staged_log_->GetEncodedLog(&log_text);
  if (!sender_->Send(log_text, base::SHA1HashString(log_text))) {
    ++failed_upload_count_;
    if (failed_upload_count_ <= kMaxFailedUpload) {
      LOG(WARNING) << "log upload failed " << failed_upload_count_
                   << " times. It will be retried later.";
      return;
    }
    LOG(WARNING) << "log failed more than " << kMaxFailedUpload << " times.";
  } else {
    LOG(INFO) << "uploaded " << log_text.length() << " bytes";
  }
  // Discard staged log.
  staged_log_.reset();
}

void UploadService::Reset() {
  staged_log_.reset();
  current_log_.reset();
  failed_upload_count_ = 0;
}

void UploadService::SetPathsForTesting(const std::string& metrics_file,
                                       const std::string& metrics_dir,
                                       const std::string& early_metrics_dir) {
  metrics_file_ = metrics_file;
  metrics_dir_ = metrics_dir;
  early_metrics_dir_ = early_metrics_dir;
}

bool UploadService::ReadMetrics(size_t sample_batch_max_length) {
  CHECK(!staged_log_)
      << "cannot read metrics until the old logs have been discarded";

  std::vector<metrics::MetricSample> samples;
  size_t bytes_read_total = 0;

  // Read from metrics file for devices that are transitioning
  // from metrics_file_ to metrics_dir_. This was done to reduce read/write
  // contention on the metrics file.
  bool result = metrics::SerializationUtils::ReadAndTruncateMetricsFromFile(
      metrics_file_, &samples, sample_batch_max_length, bytes_read_total);

  // Read from early metrics directory.
  if (!ReadAndProcessMetricsFromDirectory(early_metrics_dir_,
                                          sample_batch_max_length, samples,
                                          bytes_read_total)) {
    result = false;
  }

  // Read from metrics directory.
  if (!ReadAndProcessMetricsFromDirectory(metrics_dir_, sample_batch_max_length,
                                          samples, bytes_read_total)) {
    result = false;
  }

  // Add the samples to the current log.
  for (const auto& sample : samples) {
    AddSample(sample);
  }

  DLOG(INFO) << samples.size() << " samples found in uma-events, "
             << "early metrics dir and uma-events.d";

  return result;
}

void UploadService::AddSample(const metrics::MetricSample& sample) {
  base::HistogramBase* counter;
  switch (sample.type()) {
    case metrics::MetricSample::CRASH:
      AddCrash(sample.name());
      break;
    case metrics::MetricSample::HISTOGRAM:
      counter = base::Histogram::FactoryGet(
          sample.name(), sample.min(), sample.max(), sample.bucket_count(),
          base::Histogram::kUmaTargetedHistogramFlag);
      CHECK(counter) << "FactoryGet failed for " << sample.name();
      counter->AddCount(sample.sample(), sample.num_samples());
      break;
    case metrics::MetricSample::SPARSE_HISTOGRAM:
      counter = base::SparseHistogram::FactoryGet(
          sample.name(), base::HistogramBase::kUmaTargetedHistogramFlag);
      CHECK(counter) << "FactoryGet failed for " << sample.name();
      counter->Add(sample.sample());
      break;
    case metrics::MetricSample::LINEAR_HISTOGRAM:
      counter = base::LinearHistogram::FactoryGet(
          sample.name(), 1, sample.max(), sample.max() + 1,
          base::Histogram::kUmaTargetedHistogramFlag);
      CHECK(counter) << "FactoryGet failed for " << sample.name();
      counter->Add(sample.sample());
      break;
    case metrics::MetricSample::USER_ACTION:
      GetOrCreateCurrentLog()->RecordUserAction(sample.name());
      break;
    default:
      break;
  }
}

void UploadService::AddCrash(const std::string& crash_name) {
  if (crash_name == "user") {
    GetOrCreateCurrentLog()->IncrementUserCrashCount();
  } else if (crash_name == "kernel") {
    GetOrCreateCurrentLog()->IncrementKernelCrashCount();
  } else if (crash_name == "uncleanshutdown") {
    GetOrCreateCurrentLog()->IncrementUncleanShutdownCount();
  } else {
    DLOG(ERROR) << "crash name unknown" << crash_name;
  }
}

void UploadService::GatherHistograms() {
  auto histograms = base::StatisticsRecorder::GetHistograms();

  histogram_snapshot_manager_.PrepareDeltas(
      histograms, base::Histogram::kNoFlags,
      base::Histogram::kUmaTargetedHistogramFlag);
}

void UploadService::RecordDelta(const base::HistogramBase& histogram,
                                const base::HistogramSamples& snapshot) {
  GetOrCreateCurrentLog()->RecordHistogramDelta(histogram.histogram_name(),
                                                snapshot);
}

void UploadService::StageCurrentLog() {
  CHECK(!staged_log_)
      << "staged logs must be discarded before another log can be staged";

  if (!current_log_)
    return;

  staged_log_.swap(current_log_);
  staged_log_->CloseLog();
  failed_upload_count_ = 0;
}

MetricsLog* UploadService::GetOrCreateCurrentLog() {
  if (!current_log_) {
    StartNewLog();
  }
  return current_log_.get();
}
