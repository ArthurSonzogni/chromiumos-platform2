// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>

#include "base/at_exit.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/logging.h"
#include "base/test/scoped_chromeos_version_info.h"
#include "metrics/metrics_library_mock.h"
#include "metrics/serialization/metric_sample.h"
#include "metrics/uploader/metrics_log.h"
#include "metrics/uploader/mock/mock_system_profile_setter.h"
#include "metrics/uploader/mock/sender_mock.h"
#include "metrics/uploader/proto/chrome_user_metrics_extension.pb.h"
#include "metrics/uploader/proto/histogram_event.pb.h"
#include "metrics/uploader/proto/system_profile.pb.h"
#include "metrics/uploader/system_profile_cache.h"
#include "metrics/uploader/upload_service.h"

#include <base/check.h>

namespace {
const char kMetricsServer[] = "https://clients4.google.com/uma/v2";
const char kMetricsFilePath[] = "/run/metrics/uma-events";
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
    upload_service_.Init(base::Minutes(30), kMetricsFilePath, true);
  }

  virtual void SetUp() {
    CHECK(dir_.CreateUniqueTempDir());
    upload_service_.GatherHistograms();
    upload_service_.Reset();
    sender_->Reset();
    base::FilePath path = dir_.GetPath().Append("session_id");
    cache_.session_id_.reset(new chromeos_metrics::PersistentInteger(path));
  }

  metrics::MetricSample Crash(const std::string& name) {
    return metrics::MetricSample::CrashSample(name, /*num_samples=*/1);
  }

  base::ScopedTempDir dir_;
  SenderMock* sender_;
  SystemProfileCache cache_;
  UploadService upload_service_;
  MetricsLibraryMock metrics_lib_;

  std::unique_ptr<base::AtExitManager> exit_manager_;
};

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
  metrics::MetricSample histogram = metrics::MetricSample::HistogramSample(
      "foo", 10, 0, 42, 10, /*num_samples=*/1);
  upload_service_.AddSample(histogram);

  metrics::MetricSample histogram2 = metrics::MetricSample::HistogramSample(
      "foo", 11, 0, 42, 10, /*num_samples=*/1);
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
  metrics::MetricSample histogram =
      metrics::MetricSample::SparseHistogramSample("myhistogram", 1,
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
