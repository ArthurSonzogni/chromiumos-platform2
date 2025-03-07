// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/uploader/upload_service.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <base/check.h>
#include <gtest/gtest.h>

#include "base/at_exit.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/strings/strcat.h"
#include "base/test/scoped_chromeos_version_info.h"
#include "metrics/metrics_library_mock.h"
#include "metrics/serialization/metric_sample.h"
#include "metrics/serialization/serialization_utils.h"
#include "metrics/uploader/metrics_log.h"
#include "metrics/uploader/metrics_log_base.h"
#include "metrics/uploader/mock/mock_system_profile_setter.h"
#include "metrics/uploader/mock/sender_mock.h"
#include "metrics/uploader/proto/chrome_user_metrics_extension.pb.h"
#include "metrics/uploader/proto/histogram_event.pb.h"
#include "metrics/uploader/proto/system_profile.pb.h"
#include "metrics/uploader/system_profile_cache.h"

namespace {
const char kMetricsServer[] = "https://clients4.google.com/uma/v2";
const char kMetricsFilePath[] = "/run/metrics/uma-events";
const char kMetricsDirPath[] = "/run/metrics/uma-events.d";
const char kUMAEarlyEventsDirPath[] = "/run/early-metrics";
using metrics::MetricSample;
}  // namespace

class UploadServiceTest : public testing::Test {
 protected:
  UploadServiceTest()
      : cache_(true, "/"),
        upload_service_(
            new MockSystemProfileSetter(), &metrics_lib_, kMetricsServer, true),
        exit_manager_(new base::AtExitManager()) {
    sender_ = new SenderMock;
    upload_service_.sender_.reset(sender_);
    upload_service_.Init(base::Minutes(30), kMetricsFilePath, kMetricsDirPath,
                         kUMAEarlyEventsDirPath, true);
  }

  virtual void SetUp() {
    CHECK(dir_.CreateUniqueTempDir());
    upload_service_.GatherHistograms();
    upload_service_.Reset();
    metrics_file_ = dir_.GetPath().Append("uma-events").value();
    metrics_dir_ = dir_.GetPath().Append("uma-events.d").value();
    early_metrics_dir_ = dir_.GetPath().Append("early-metrics").value();
    CHECK(base::CreateDirectory(base::FilePath(metrics_dir_)));
    CHECK(base::CreateDirectory(base::FilePath(early_metrics_dir_)));
    upload_service_.SetPathsForTesting(metrics_file_, metrics_dir_,
                                       early_metrics_dir_);
    sender_->Reset();
    base::FilePath path = dir_.GetPath().Append("session_id");
    cache_.session_id_.reset(new chromeos_metrics::PersistentInteger(path));
  }

  MetricSample Crash(const std::string& name) {
    return MetricSample::CrashSample(name, /*num_samples=*/1);
  }

  base::ScopedTempDir dir_;
  SenderMock* sender_;
  SystemProfileCache cache_;
  UploadService upload_service_;
  MetricsLibraryMock metrics_lib_;
  std::string metrics_file_;
  std::string metrics_dir_;
  std::string early_metrics_dir_;

  std::unique_ptr<base::AtExitManager> exit_manager_;
};

// Tests that UploadService properly reads from both the single file, per-pid
// uma-events.d, and the per-pid early metrics directory.
TEST_F(UploadServiceTest, ReadMetrics) {
  std::vector<MetricSample> output_samples = {
      MetricSample::HistogramSample("myhist", 3, 1, 10, 5, /*num_samples=*/1),
  };

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(output_samples,
                                                              metrics_file_));

  std::vector<MetricSample> output_samples1 = {
      MetricSample::HistogramSample("myhist1", 3, 1, 10, 5, /*num_samples=*/1),
  };

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples1, base::StrCat({metrics_dir_, "/1"})));

  std::vector<MetricSample> output_samples2 = {
      MetricSample::HistogramSample("myhist2", 3, 1, 10, 5, /*num_samples=*/1),
  };

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples2, base::StrCat({metrics_dir_, "/2"})));

  std::vector<MetricSample> output_samples3 = {
      MetricSample::HistogramSample("myhist3", 3, 1, 10, 5, /*num_samples=*/1),
  };

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples3, base::StrCat({early_metrics_dir_, "/1"})));

  std::vector<MetricSample> output_samples4 = {
      MetricSample::HistogramSample("myhist4", 3, 1, 10, 5, /*num_samples=*/1),
  };

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples4, base::StrCat({early_metrics_dir_, "/2"})));

  EXPECT_TRUE(upload_service_.ReadMetrics());

  upload_service_.GatherHistograms();

  MetricsLog* log = upload_service_.current_log_.get();
  ASSERT_NE(log, nullptr);
  metrics::ChromeUserMetricsExtension* proto = log->uma_proto();
  ASSERT_NE(proto, nullptr);
  EXPECT_EQ(5, proto->histogram_event().size());

  std::vector<uint64_t> expected_hashes = {
      metrics::MetricsLogBase::Hash("myhist"),
      metrics::MetricsLogBase::Hash("myhist1"),
      metrics::MetricsLogBase::Hash("myhist2"),
      metrics::MetricsLogBase::Hash("myhist3"),
      metrics::MetricsLogBase::Hash("myhist4"),
  };
  std::vector<uint64_t> actual_hashes = {
      proto->histogram_event(0).name_hash(),
      proto->histogram_event(1).name_hash(),
      proto->histogram_event(2).name_hash(),
      proto->histogram_event(3).name_hash(),
      proto->histogram_event(4).name_hash(),
  };
  EXPECT_THAT(actual_hashes,
              testing::UnorderedElementsAreArray(expected_hashes));

  // Verify the metrics file is empty after being successfully read.
  ASSERT_EQ(0, base::GetFileSize(base::FilePath(metrics_file_)));

  // Verify the metrics directory and early metrics directories are empty after
  // being successfully read.
  EXPECT_TRUE(base::IsDirectoryEmpty(base::FilePath(metrics_dir_)));
  EXPECT_TRUE(base::IsDirectoryEmpty(base::FilePath(early_metrics_dir_)));
}

// Test that UploadMetrics only reads the maximum amount specified, even if that
// amount is spread across multiple files.
TEST_F(UploadServiceTest, ReadMetrics_TooLargeFiles) {
  std::vector<MetricSample> output_samples = {
      MetricSample::HistogramSample("myhist", 4, 1, 10, 5, /*num_samples=*/1),
  };
  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples, base::StrCat({metrics_dir_, "/1"})));

  std::optional<int64_t> size =
      base::GetFileSize(base::FilePath(metrics_dir_).Append("1"));
  ASSERT_TRUE(size.has_value());
  // Only allow for 2.
  size_t sample_batch_max_length = size.value() * 2;

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples, base::StrCat({metrics_dir_, "/2"})));

  EXPECT_TRUE(metrics::SerializationUtils::WriteMetricsToFile(
      output_samples, base::StrCat({metrics_dir_, "/3"})));

  EXPECT_FALSE(upload_service_.ReadMetrics(sample_batch_max_length));

  upload_service_.GatherHistograms();

  MetricsLog* log = upload_service_.current_log_.get();
  ASSERT_NE(log, nullptr);
  metrics::ChromeUserMetricsExtension* proto = log->uma_proto();
  ASSERT_NE(proto, nullptr);
  EXPECT_EQ(1, proto->histogram_event().size());

  EXPECT_EQ(metrics::MetricsLogBase::Hash("myhist"),
            proto->histogram_event(0).name_hash());
  // Should be 2 samples of value 4.
  EXPECT_EQ(proto->histogram_event(0).sum(), 4 * 2);

  EXPECT_FALSE(base::IsDirectoryEmpty(base::FilePath(metrics_dir_)));
}

// Tests that the right crash increments a values.
TEST_F(UploadServiceTest, LogUserCrash) {
  upload_service_.AddSample(Crash("user"));

  MetricsLog* log = upload_service_.current_log_.get();
  metrics::ChromeUserMetricsExtension* proto = log->uma_proto();

  EXPECT_EQ(1, proto->system_profile().stability().other_user_crash_count());
}

TEST_F(UploadServiceTest, LogUncleanShutdown) {
  upload_service_.AddSample(Crash("uncleanshutdown"));

  EXPECT_EQ(1, upload_service_.current_log_->uma_proto()
                   ->system_profile()
                   .stability()
                   .unclean_system_shutdown_count());
}

TEST_F(UploadServiceTest, LogKernelCrash) {
  upload_service_.AddSample(Crash("kernel"));

  EXPECT_EQ(1, upload_service_.current_log_->uma_proto()
                   ->system_profile()
                   .stability()
                   .kernel_crash_count());
}

TEST_F(UploadServiceTest, UnknownCrashIgnored) {
  upload_service_.AddSample(Crash("foo"));

  // The log should be empty.
  EXPECT_FALSE(upload_service_.current_log_);
}

TEST_F(UploadServiceTest, FailedSendAreRetried) {
  sender_->set_should_succeed(false);

  upload_service_.AddSample(Crash("user"));
  upload_service_.UploadEvent();
  EXPECT_EQ(1, sender_->send_call_count());
  std::string sent_string = sender_->last_message();

  upload_service_.UploadEvent();
  EXPECT_EQ(2, sender_->send_call_count());
  EXPECT_EQ(sent_string, sender_->last_message());
}

TEST_F(UploadServiceTest, DiscardLogsAfterTooManyFailedUpload) {
  sender_->set_should_succeed(false);
  upload_service_.AddSample(Crash("user"));

  for (int i = 0; i < UploadService::kMaxFailedUpload; i++) {
    upload_service_.UploadEvent();
  }

  EXPECT_TRUE(upload_service_.staged_log_.get());
  upload_service_.UploadEvent();
  EXPECT_FALSE(upload_service_.staged_log_);
}

TEST_F(UploadServiceTest, EmptyLogsAreNotSent) {
  upload_service_.UploadEvent();
  EXPECT_FALSE(upload_service_.current_log_);
  EXPECT_EQ(0, sender_->send_call_count());
}

TEST_F(UploadServiceTest, LogEmptyByDefault) {
  UploadService upload_service(new MockSystemProfileSetter(), &metrics_lib_,
                               kMetricsServer);

  // current_log_ should be initialized later as it needs AtExitManager to exit
  // in order to gather system information from SysInfo.
  EXPECT_FALSE(upload_service.current_log_);
}

TEST_F(UploadServiceTest, CanSendMultipleTimes) {
  upload_service_.AddSample(Crash("user"));
  upload_service_.UploadEvent();

  std::string first_message = sender_->last_message();

  upload_service_.AddSample(Crash("kernel"));
  upload_service_.UploadEvent();

  EXPECT_NE(first_message, sender_->last_message());
}

TEST_F(UploadServiceTest, LogEmptyAfterUpload) {
  upload_service_.AddSample(Crash("user"));

  EXPECT_TRUE(upload_service_.current_log_.get());

  upload_service_.UploadEvent();
  EXPECT_FALSE(upload_service_.current_log_);
}

TEST_F(UploadServiceTest, LogContainsAggregatedValues) {
  MetricSample histogram =
      MetricSample::HistogramSample("foo", 10, 0, 42, 10, /*num_samples=*/1);
  upload_service_.AddSample(histogram);

  MetricSample histogram2 =
      MetricSample::HistogramSample("foo", 11, 0, 42, 10, /*num_samples=*/1);
  upload_service_.AddSample(histogram2);

  upload_service_.GatherHistograms();
  metrics::ChromeUserMetricsExtension* proto =
      upload_service_.current_log_->uma_proto();
  EXPECT_EQ(1, proto->histogram_event().size());
}

TEST_F(UploadServiceTest, ExtractChannelFromString) {
  EXPECT_EQ(SystemProfileCache::ProtoChannelFromString("developer-build"),
            metrics::SystemProfileProto::CHANNEL_UNKNOWN);

  EXPECT_EQ(metrics::SystemProfileProto::CHANNEL_DEV,
            SystemProfileCache::ProtoChannelFromString("dev-channel"));

  EXPECT_EQ(metrics::SystemProfileProto::CHANNEL_UNKNOWN,
            SystemProfileCache::ProtoChannelFromString("dev-channel test"));

  EXPECT_EQ(metrics::SystemProfileProto::CHANNEL_STABLE,
            SystemProfileCache::ProtoChannelFromString("lts-channel"));
  EXPECT_EQ(metrics::SystemProfileProto::CHANNEL_STABLE,
            SystemProfileCache::ProtoChannelFromString("ltc-channel"));
}

TEST_F(UploadServiceTest, ValuesInConfigFileAreSent) {
  std::string name("os name");
  std::string content(
      "CHROMEOS_RELEASE_NAME=" + name +
      "\nCHROMEOS_RELEASE_VERSION=version\n"
      "CHROMEOS_RELEASE_DESCRIPTION=description beta-channel test\n"
      "CHROMEOS_RELEASE_TRACK=beta-channel\n"
      "CHROMEOS_RELEASE_BUILD_TYPE=developer build\n"
      "CHROMEOS_RELEASE_BOARD=myboard");

  base::test::ScopedChromeOSVersionInfo version(content, base::Time());
  MetricSample histogram =
      MetricSample::SparseHistogramSample("myhistogram", 1,
                                          /*num_samples=*/1);
  SystemProfileCache* local_cache_ = new SystemProfileCache(true, "/");
  base::FilePath path = dir_.GetPath().Append("session_id");
  local_cache_->session_id_.reset(
      new chromeos_metrics::PersistentInteger(path));

  upload_service_.system_profile_setter_.reset(local_cache_);
  // Reset to create the new log with the profile setter.
  upload_service_.Reset();
  upload_service_.AddSample(histogram);
  upload_service_.UploadEvent();

  EXPECT_EQ(1, sender_->send_call_count());
  EXPECT_TRUE(sender_->is_good_proto());
  EXPECT_EQ(1, sender_->last_message_proto().histogram_event().size());

  EXPECT_EQ(name, sender_->last_message_proto().system_profile().os().name());
  EXPECT_EQ(metrics::SystemProfileProto::CHANNEL_BETA,
            sender_->last_message_proto().system_profile().channel());
  EXPECT_NE(0, sender_->last_message_proto().client_id());
  EXPECT_NE(0,
            sender_->last_message_proto().system_profile().build_timestamp());
  EXPECT_NE(0, sender_->last_message_proto().session_id());
}

TEST_F(UploadServiceTest, PersistentGUID) {
  std::string tmp_file = dir_.GetPath().Append("tmpfile").value();

  std::string first_guid = SystemProfileCache::GetPersistentGUID(tmp_file);
  std::string second_guid = SystemProfileCache::GetPersistentGUID(tmp_file);

  // The GUID are cached.
  EXPECT_EQ(first_guid, second_guid);

  base::DeleteFile(base::FilePath(tmp_file));

  first_guid = SystemProfileCache::GetPersistentGUID(tmp_file);
  base::DeleteFile(base::FilePath(tmp_file));
  second_guid = SystemProfileCache::GetPersistentGUID(tmp_file);

  // Random GUIDs are generated (not all the same).
  EXPECT_NE(first_guid, second_guid);
}

TEST_F(UploadServiceTest, SessionIdIncrementedAtInitialization) {
  std::string content(
      "CHROMEOS_RELEASE_NAME=os name\n"
      "CHROMEOS_RELEASE_VERSION=version\n"
      "CHROMEOS_RELEASE_DESCRIPTION=description beta-channel test\n"
      "CHROMEOS_RELEASE_TRACK=beta-channel\n"
      "CHROMEOS_RELEASE_BUILD_TYPE=developer build\n"
      "CHROMEOS_RELEASE_BOARD=myboard");

  base::test::ScopedChromeOSVersionInfo version(content, base::Time());
  cache_.Initialize();
  int session_id = cache_.profile_.session_id;
  cache_.initialized_ = false;
  cache_.Initialize();
  EXPECT_EQ(cache_.profile_.session_id, session_id + 1);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
