// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/download_action_chromeos.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/location.h>
#include <base/strings/stringprintf.h>
#include <base/test/simple_test_clock.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <brillo/message_loops/message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "update_engine/common/action_pipe.h"
#include "update_engine/common/hash_calculator.h"
#include "update_engine/common/mock_download_action.h"
#include "update_engine/common/mock_http_fetcher.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/fake_p2p_manager_configuration.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/payload_consumer/install_plan.h"
#include "update_engine/payload_consumer/mock_file_writer.h"
#include "update_engine/update_manager/fake_update_manager.h"

namespace chromeos_update_engine {

using base::FilePath;
using base::ReadFileToString;
using base::WriteFile;
using chromeos_update_manager::WeeklyTime;
using chromeos_update_manager::WeeklyTimeInterval;
using chromeos_update_manager::WeeklyTimeIntervalVector;
using std::string;
using std::unique_ptr;
using testing::_;
using testing::AtLeast;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;

namespace {
constexpr base::TimeDelta kHour = base::Hours(1);
constexpr base::TimeDelta kMinute = base::Minutes(1);

class DownloadActionTestProcessorDelegate : public ActionProcessorDelegate {
 public:
  DownloadActionTestProcessorDelegate()
      : processing_done_called_(false), expected_code_(ErrorCode::kSuccess) {}
  ~DownloadActionTestProcessorDelegate() override {
    EXPECT_TRUE(processing_done_called_);
  }
  void ProcessingDone(const ActionProcessor* processor,
                      ErrorCode code) override {
    brillo::MessageLoop::current()->BreakLoop();
    brillo::Blob found_data;
    ASSERT_TRUE(utils::ReadFile(path_, &found_data));
    if (expected_code_ == ErrorCode::kSuccess) {
      ASSERT_EQ(expected_data_.size(), found_data.size());
      for (unsigned i = 0; i < expected_data_.size(); i++) {
        EXPECT_EQ(expected_data_[i], found_data[i]);
      }
    }
    processing_done_called_ = true;
  }

  void ActionCompleted(ActionProcessor* processor,
                       AbstractAction* action,
                       ErrorCode code) override {
    const string type = action->Type();
    if (type == DownloadActionChromeos::StaticType()) {
      EXPECT_EQ(expected_code_, code);
      p2p_file_id_ =
          static_cast<DownloadActionChromeos*>(action)->p2p_file_id();
    } else {
      EXPECT_EQ(ErrorCode::kSuccess, code);
    }
  }

  string path_;
  brillo::Blob expected_data_;
  bool processing_done_called_;
  ErrorCode expected_code_;
  string p2p_file_id_;
};

class TestDirectFileWriter : public DirectFileWriter {
 public:
  TestDirectFileWriter() : fail_write_(0), current_write_(0) {}
  void set_fail_write(int fail_write) { fail_write_ = fail_write; }

  virtual bool Write(const void* bytes, size_t count) {
    if (++current_write_ == fail_write_) {
      return false;
    }
    return DirectFileWriter::Write(bytes, count);
  }

 private:
  // If positive, fail on the |fail_write_| call to Write.
  int fail_write_;
  int current_write_;
};

void StartProcessorInRunLoop(ActionProcessor* processor,
                             MockHttpFetcher* http_fetcher) {
  processor->StartProcessing();
  http_fetcher->SetOffset(1);
}

void TestWithData(const brillo::Blob& data,
                  int fail_write,
                  bool use_download_delegate) {
  FakeSystemState::CreateInstance();
  brillo::FakeMessageLoop loop(nullptr);
  loop.SetAsCurrent();

  ScopedTempFile output_temp_file;
  TestDirectFileWriter writer;
  EXPECT_EQ(
      0, writer.Open(output_temp_file.path().c_str(), O_WRONLY | O_CREAT, 0));
  writer.set_fail_write(fail_write);

  uint64_t size = data.size() - 1;
  InstallPlan install_plan;
  install_plan.payloads.push_back(
      {.size = size, .type = InstallPayloadType::kDelta});
  // We pull off the first byte from data and seek past it.
  EXPECT_TRUE(HashCalculator::RawHashOfBytes(&data[1], data.size() - 1,
                                             &install_plan.payloads[0].hash));
  install_plan.source_slot = 0;
  install_plan.target_slot = 1;
  // We mark both slots as bootable. Only the target slot should be unbootable
  // after the download starts.
  FakeSystemState::Get()->fake_boot_control()->SetSlotBootable(
      install_plan.source_slot, true);
  FakeSystemState::Get()->fake_boot_control()->SetSlotBootable(
      install_plan.target_slot, true);
  auto* fake_prefs = FakeSystemState::Get()->prefs();
  fake_prefs->SetString(kPrefsUpdateCompletedOnBootId, "boot-id");
  fake_prefs->SetString(kPrefsLastFp, "last-fp");
  fake_prefs->SetString(kPrefsPreviousVersion, "prev-version");

  auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
  feeder_action->set_obj(install_plan);
  auto http_fetcher =
      std::make_unique<MockHttpFetcher>(data.data(), data.size(), nullptr);
  auto http_fetcher_ptr = http_fetcher.get();
  auto download_action = std::make_unique<DownloadActionChromeos>(
      std::move(http_fetcher), /*interactive=*/false);
  download_action->SetTestFileWriter(&writer);
  BondActions(feeder_action.get(), download_action.get());
  MockDownloadActionDelegate download_delegate;
  if (use_download_delegate) {
    InSequence s;
    download_action->set_delegate(&download_delegate);
    if (data.size() > kMockHttpFetcherChunkSize) {
      EXPECT_CALL(download_delegate,
                  BytesReceived(_, kMockHttpFetcherChunkSize, _));
    }
    EXPECT_CALL(download_delegate, BytesReceived(_, _, _)).Times(AtLeast(1));
    EXPECT_CALL(download_delegate, DownloadComplete())
        .Times(fail_write == 0 ? 1 : 0);
  }
  DownloadActionTestProcessorDelegate delegate;
  delegate.expected_code_ =
      (fail_write > 0) ? ErrorCode::kDownloadWriteError : ErrorCode::kSuccess;
  delegate.expected_data_ = brillo::Blob(data.begin() + 1, data.end());
  delegate.path_ = output_temp_file.path();
  ActionProcessor processor;
  processor.set_delegate(&delegate);
  processor.EnqueueAction(std::move(feeder_action));
  processor.EnqueueAction(std::move(download_action));

  loop.PostTask(FROM_HERE, base::BindOnce(&StartProcessorInRunLoop, &processor,
                                          http_fetcher_ptr));
  loop.Run();
  EXPECT_FALSE(loop.PendingTasks());

  EXPECT_TRUE(FakeSystemState::Get()->fake_boot_control()->IsSlotBootable(
      install_plan.source_slot));
  EXPECT_FALSE(FakeSystemState::Get()->fake_boot_control()->IsSlotBootable(
      install_plan.target_slot));

  EXPECT_FALSE(fake_prefs->Exists(kPrefsUpdateCompletedOnBootId));
  EXPECT_FALSE(fake_prefs->Exists(kPrefsLastFp));
  EXPECT_FALSE(fake_prefs->Exists(kPrefsPreviousVersion));
}
}  // namespace

TEST(DownloadActionTest, SimpleTest) {
  brillo::Blob small;
  const char* foo = "foo";
  small.insert(small.end(), foo, foo + strlen(foo));
  TestWithData(small,
               0,      // fail_write
               true);  // use_download_delegate
}

TEST(DownloadActionTest, LargeTest) {
  brillo::Blob big(5 * kMockHttpFetcherChunkSize);
  char c = '0';
  for (unsigned int i = 0; i < big.size(); i++) {
    big[i] = c;
    c = ('9' == c) ? '0' : c + 1;
  }
  TestWithData(big,
               0,      // fail_write
               true);  // use_download_delegate
}

TEST(DownloadActionTest, FailWriteTest) {
  brillo::Blob big(5 * kMockHttpFetcherChunkSize);
  char c = '0';
  for (unsigned int i = 0; i < big.size(); i++) {
    big[i] = c;
    c = ('9' == c) ? '0' : c + 1;
  }
  TestWithData(big,
               2,      // fail_write
               true);  // use_download_delegate
}

TEST(DownloadActionTest, NoDownloadDelegateTest) {
  brillo::Blob small;
  const char* foo = "foofoo";
  small.insert(small.end(), foo, foo + strlen(foo));
  TestWithData(small,
               0,       // fail_write
               false);  // use_download_delegate
}

TEST(DownloadActionTest, MultiPayloadProgressTest) {
  brillo::FakeMessageLoop loop{nullptr};
  loop.SetAsCurrent();
  FakeSystemState::CreateInstance();

  std::vector<brillo::Blob> payload_datas;
  // the first payload must be the largest, as it's the actual payload used by
  // the MockHttpFetcher for all downloaded data.
  payload_datas.emplace_back(4 * kMockHttpFetcherChunkSize + 256);
  payload_datas.emplace_back(2 * kMockHttpFetcherChunkSize);
  EXPECT_CALL(*FakeSystemState::Get()->mock_payload_state(), NextPayload())
      .WillOnce(Return(true));

  MockFileWriter mock_file_writer;
  EXPECT_CALL(mock_file_writer, Close()).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_file_writer, Write(_, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(ErrorCode::kSuccess), Return(true)));

  InstallPlan install_plan;
  uint64_t total_expected_download_size{0};
  for (const auto& data : payload_datas) {
    uint64_t size = data.size();
    install_plan.payloads.push_back(
        {.size = size, .type = InstallPayloadType::kFull});
    total_expected_download_size += size;
  }
  auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
  feeder_action->set_obj(install_plan);

  auto download_action = std::make_unique<DownloadActionChromeos>(
      std::make_unique<MockHttpFetcher>(payload_datas[0].data(),
                                        payload_datas[0].size(), nullptr),
      /*interactive=*/false);
  download_action->SetTestFileWriter(&mock_file_writer);
  BondActions(feeder_action.get(), download_action.get());
  MockDownloadActionDelegate download_delegate;
  {
    InSequence s;
    download_action->set_delegate(&download_delegate);
    // these are hand-computed based on the payloads specified above
    EXPECT_CALL(download_delegate, BytesReceived(kMockHttpFetcherChunkSize,
                                                 kMockHttpFetcherChunkSize,
                                                 total_expected_download_size));
    EXPECT_CALL(download_delegate, BytesReceived(kMockHttpFetcherChunkSize,
                                                 kMockHttpFetcherChunkSize * 2,
                                                 total_expected_download_size));
    EXPECT_CALL(download_delegate, BytesReceived(kMockHttpFetcherChunkSize,
                                                 kMockHttpFetcherChunkSize * 3,
                                                 total_expected_download_size));
    EXPECT_CALL(download_delegate, BytesReceived(kMockHttpFetcherChunkSize,
                                                 kMockHttpFetcherChunkSize * 4,
                                                 total_expected_download_size));
    EXPECT_CALL(download_delegate,
                BytesReceived(256, kMockHttpFetcherChunkSize * 4 + 256,
                              total_expected_download_size));
    EXPECT_CALL(download_delegate,
                BytesReceived(kMockHttpFetcherChunkSize,
                              kMockHttpFetcherChunkSize * 5 + 256,
                              total_expected_download_size));
    EXPECT_CALL(download_delegate, BytesReceived(kMockHttpFetcherChunkSize,
                                                 total_expected_download_size,
                                                 total_expected_download_size));
  }
  ActionProcessor processor;
  processor.EnqueueAction(std::move(feeder_action));
  processor.EnqueueAction(std::move(download_action));

  loop.PostTask(FROM_HERE, base::BindOnce(
                               [](ActionProcessor* processor) {
                                 processor->StartProcessing();
                               },
                               base::Unretained(&processor)));
  loop.Run();
  EXPECT_FALSE(loop.PendingTasks());
}

namespace {
class TerminateEarlyTestProcessorDelegate : public ActionProcessorDelegate {
 public:
  void ProcessingStopped(const ActionProcessor* processor) {
    brillo::MessageLoop::current()->BreakLoop();
  }
};

void TerminateEarlyTestStarter(ActionProcessor* processor) {
  processor->StartProcessing();
  CHECK(processor->IsRunning());
  processor->StopProcessing();
}

void TestTerminateEarly(bool use_download_delegate) {
  FakeSystemState::CreateInstance();
  brillo::FakeMessageLoop loop(nullptr);
  loop.SetAsCurrent();

  brillo::Blob data(kMockHttpFetcherChunkSize + kMockHttpFetcherChunkSize / 2);
  memset(data.data(), 0, data.size());

  ScopedTempFile temp_file;
  {
    DirectFileWriter writer;
    EXPECT_EQ(0, writer.Open(temp_file.path().c_str(), O_WRONLY | O_CREAT, 0));

    // takes ownership of passed in HttpFetcher
    auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
    InstallPlan install_plan;
    install_plan.payloads.resize(1);
    feeder_action->set_obj(install_plan);

    auto download_action = std::make_unique<DownloadActionChromeos>(
        std::make_unique<MockHttpFetcher>(data.data(), data.size(), nullptr),
        /*interactive=*/false);
    download_action->SetTestFileWriter(&writer);
    MockDownloadActionDelegate download_delegate;
    if (use_download_delegate) {
      download_action->set_delegate(&download_delegate);
      EXPECT_CALL(download_delegate, BytesReceived(_, _, _)).Times(0);
    }
    TerminateEarlyTestProcessorDelegate delegate;
    ActionProcessor processor;
    processor.set_delegate(&delegate);
    BondActions(feeder_action.get(), download_action.get());
    processor.EnqueueAction(std::move(feeder_action));
    processor.EnqueueAction(std::move(download_action));

    loop.PostTask(FROM_HERE,
                  base::BindOnce(&TerminateEarlyTestStarter, &processor));
    loop.Run();
    EXPECT_FALSE(loop.PendingTasks());
  }

  // 1 or 0 chunks should have come through
  const off_t resulting_file_size(utils::FileSize(temp_file.path()));
  EXPECT_GE(resulting_file_size, 0);
  if (resulting_file_size != 0) {
    EXPECT_EQ(kMockHttpFetcherChunkSize,
              static_cast<size_t>(resulting_file_size));
  }
}

}  // namespace

TEST(DownloadActionTest, TerminateEarlyTest) {
  TestTerminateEarly(true);
}

TEST(DownloadActionTest, TerminateEarlyNoDownloadDelegateTest) {
  TestTerminateEarly(false);
}

class DownloadActionTestAction;

template <>
class ActionTraits<DownloadActionTestAction> {
 public:
  typedef InstallPlan OutputObjectType;
  typedef InstallPlan InputObjectType;
};

// This is a simple Action class for testing.
class DownloadActionTestAction : public Action<DownloadActionTestAction> {
 public:
  DownloadActionTestAction() = default;
  typedef InstallPlan InputObjectType;
  typedef InstallPlan OutputObjectType;
  ActionPipe<InstallPlan>* in_pipe() { return in_pipe_.get(); }
  ActionPipe<InstallPlan>* out_pipe() { return out_pipe_.get(); }
  ActionProcessor* processor() { return processor_; }
  void PerformAction() {
    ASSERT_TRUE(HasInputObject());
    EXPECT_TRUE(expected_input_object_ == GetInputObject());
    ASSERT_TRUE(processor());
    processor()->ActionComplete(this, ErrorCode::kSuccess);
  }
  static std::string StaticType() { return "DownloadActionTestAction"; }
  string Type() const { return StaticType(); }
  InstallPlan expected_input_object_;
};

namespace {
// This class is an ActionProcessorDelegate that simply terminates the
// run loop when the ActionProcessor has completed processing. It's used
// only by the test PassObjectOutTest.
class PassObjectOutTestProcessorDelegate : public ActionProcessorDelegate {
 public:
  void ProcessingDone(const ActionProcessor* processor,
                      ErrorCode code) override {
    brillo::MessageLoop::current()->BreakLoop();
  }
  void ActionCompleted(ActionProcessor* processor,
                       AbstractAction* action,
                       ErrorCode code) override {
    if (action->Type() == DownloadActionTestAction::StaticType()) {
      did_test_action_run_ = true;
    }
  }

  bool did_test_action_run_ = false;
};

}  // namespace

TEST(DownloadActionTest, PassObjectOutTest) {
  FakeSystemState::CreateInstance();
  brillo::FakeMessageLoop loop(nullptr);
  loop.SetAsCurrent();

  DirectFileWriter writer;
  EXPECT_EQ(0, writer.Open("/dev/null", O_WRONLY | O_CREAT, 0));

  // takes ownership of passed in HttpFetcher
  InstallPlan install_plan;
  install_plan.payloads.push_back({.size = 1});
  EXPECT_TRUE(
      HashCalculator::RawHashOfData({'x'}, &install_plan.payloads[0].hash));
  auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
  feeder_action->set_obj(install_plan);

  auto download_action = std::make_unique<DownloadActionChromeos>(
      std::make_unique<MockHttpFetcher>("x", 1, nullptr),
      /*interactive=*/false);
  download_action->SetTestFileWriter(&writer);

  auto test_action = std::make_unique<DownloadActionTestAction>();
  test_action->expected_input_object_ = install_plan;
  BondActions(feeder_action.get(), download_action.get());
  BondActions(download_action.get(), test_action.get());

  ActionProcessor processor;
  PassObjectOutTestProcessorDelegate delegate;
  processor.set_delegate(&delegate);
  processor.EnqueueAction(std::move(feeder_action));
  processor.EnqueueAction(std::move(download_action));
  processor.EnqueueAction(std::move(test_action));

  loop.PostTask(FROM_HERE, base::BindOnce(
                               [](ActionProcessor* processor) {
                                 processor->StartProcessing();
                               },
                               base::Unretained(&processor)));
  loop.Run();
  EXPECT_FALSE(loop.PendingTasks());

  EXPECT_EQ(true, delegate.did_test_action_run_);
}

// Test fixture for P2P tests.
class P2PDownloadActionTest : public testing::Test {
 protected:
  P2PDownloadActionTest() : start_at_offset_(0) {}

  ~P2PDownloadActionTest() override {}

  // Derived from testing::Test.
  void SetUp() override {
    loop_.SetAsCurrent();
    FakeSystemState::CreateInstance();
  }

  // Derived from testing::Test.
  void TearDown() override { EXPECT_FALSE(loop_.PendingTasks()); }

  // To be called by tests to setup the download. The
  // |starting_offset| parameter is for where to resume.
  void SetupDownload(off_t starting_offset) {
    start_at_offset_ = starting_offset;
    // Prepare data 10 kB of data.
    data_.resize(10 * 1000);
    std::generate(data_.begin(), data_.end(),
                  [i = 0]() mutable { return 'a' + (i++ % 26); });

    // Setup p2p.
    FakeP2PManagerConfiguration* test_conf = new FakeP2PManagerConfiguration();
    p2p_manager_.reset(P2PManager::Construct(test_conf, &fake_um_, "cros_au", 3,
                                             base::Days(5)));
    FakeSystemState::Get()->set_p2p_manager(p2p_manager_.get());
  }

  // To be called by tests to perform the download. The
  // |use_p2p_to_share| parameter is used to indicate whether the
  // payload should be shared via p2p.
  void StartDownload(bool use_p2p_to_share) {
    EXPECT_CALL(*FakeSystemState::Get()->mock_payload_state(),
                GetUsingP2PForSharing())
        .WillRepeatedly(Return(use_p2p_to_share));
    EXPECT_CALL(*FakeSystemState::Get()->mock_call_wrapper(),
                AmountOfFreeDiskSpace(_))
        .WillRepeatedly(Return(data_.length() * 2));

    ScopedTempFile output_temp_file;
    TestDirectFileWriter writer;
    EXPECT_EQ(
        0, writer.Open(output_temp_file.path().c_str(), O_WRONLY | O_CREAT, 0));
    InstallPlan install_plan;
    install_plan.payloads.push_back(
        {.size = data_.length(),
         .hash = {'1', '2', '3', '4', 'h', 'a', 's', 'h'}});
    auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
    feeder_action->set_obj(install_plan);
    MockPrefs prefs;
    auto download_action = std::make_unique<DownloadActionChromeos>(
        std::make_unique<MockHttpFetcher>(data_.c_str(), data_.length(),
                                          nullptr),
        /*interactive=*/false);
    auto http_fetcher = download_action->http_fetcher();
    download_action->SetTestFileWriter(&writer);
    BondActions(feeder_action.get(), download_action.get());
    delegate_.expected_data_ =
        brillo::Blob(data_.begin() + start_at_offset_, data_.end());
    delegate_.path_ = output_temp_file.path();
    processor_.set_delegate(&delegate_);
    processor_.EnqueueAction(std::move(feeder_action));
    processor_.EnqueueAction(std::move(download_action));

    loop_.PostTask(
        FROM_HERE,
        base::BindOnce(
            [](P2PDownloadActionTest* action_test, HttpFetcher* http_fetcher) {
              action_test->processor_.StartProcessing();
              http_fetcher->SetOffset(action_test->start_at_offset_);
            },
            base::Unretained(this), base::Unretained(http_fetcher)));
    loop_.Run();
  }

  // Mainloop used to make StartDownload() synchronous.
  brillo::FakeMessageLoop loop_{nullptr};

  // Delegate that is passed to the ActionProcessor.
  DownloadActionTestProcessorDelegate delegate_;

  // The P2PManager used in the test.
  unique_ptr<P2PManager> p2p_manager_;

  // The ActionProcessor used for running the actions.
  ActionProcessor processor_;

  // The data being downloaded.
  string data_;

 private:
  // The requested starting offset passed to SetupDownload().
  off_t start_at_offset_;

  chromeos_update_manager::FakeUpdateManager fake_um_;
};

TEST_F(P2PDownloadActionTest, IsWrittenTo) {
  SetupDownload(0);     // starting_offset
  StartDownload(true);  // use_p2p_to_share

  // Check the p2p file and its content matches what was sent.
  string file_id = delegate_.p2p_file_id_;
  EXPECT_NE("", file_id);
  EXPECT_EQ(static_cast<int>(data_.length()),
            p2p_manager_->FileGetSize(file_id));
  EXPECT_EQ(static_cast<int>(data_.length()),
            p2p_manager_->FileGetExpectedSize(file_id));
  string p2p_file_contents;
  EXPECT_TRUE(
      ReadFileToString(p2p_manager_->FileGetPath(file_id), &p2p_file_contents));
  EXPECT_EQ(data_, p2p_file_contents);
}

TEST_F(P2PDownloadActionTest, DeleteIfHoleExists) {
  SetupDownload(1000);  // starting_offset
  StartDownload(true);  // use_p2p_to_share

  // DownloadAction should convey that the file is not being shared.
  // and that we don't have any p2p files.
  EXPECT_EQ(delegate_.p2p_file_id_, "");
  EXPECT_EQ(p2p_manager_->CountSharedFiles(), 0);
}

TEST_F(P2PDownloadActionTest, CanAppend) {
  SetupDownload(1000);  // starting_offset
  EXPECT_CALL(*FakeSystemState::Get()->mock_call_wrapper(),
              AmountOfFreeDiskSpace(_))
      .WillOnce(Return(data_.length() * 2));

  // Prepare the file with existing data before starting to write to
  // it via DownloadAction.
  string file_id = utils::CalculateP2PFileId(
      {'1', '2', '3', '4', 'h', 'a', 's', 'h'}, data_.length());
  ASSERT_TRUE(p2p_manager_->FileShare(file_id, data_.length()));
  string existing_data;
  for (unsigned int i = 0; i < 1000; i++) {
    existing_data += '0' + (i % 10);
  }
  ASSERT_TRUE(WriteFile(p2p_manager_->FileGetPath(file_id), existing_data));

  StartDownload(true);  // use_p2p_to_share

  // DownloadAction should convey the same file_id and the file should
  // have the expected size.
  EXPECT_EQ(delegate_.p2p_file_id_, file_id);
  EXPECT_EQ(static_cast<ssize_t>(data_.length()),
            p2p_manager_->FileGetSize(file_id));
  EXPECT_EQ(static_cast<ssize_t>(data_.length()),
            p2p_manager_->FileGetExpectedSize(file_id));
  string p2p_file_contents;
  // Check that the first 1000 bytes wasn't touched and that we
  // appended the remaining as appropriate.
  EXPECT_TRUE(
      ReadFileToString(p2p_manager_->FileGetPath(file_id), &p2p_file_contents));
  EXPECT_EQ(existing_data, p2p_file_contents.substr(0, 1000));
  EXPECT_EQ(data_.substr(1000), p2p_file_contents.substr(1000));
}

TEST_F(P2PDownloadActionTest, DeletePartialP2PFileIfResumingWithoutP2P) {
  SetupDownload(1000);  // starting_offset
  EXPECT_CALL(*FakeSystemState::Get()->mock_call_wrapper(),
              AmountOfFreeDiskSpace(_))
      .WillOnce(Return(data_.length() * 2));

  // Prepare the file with all existing data before starting to write
  // to it via DownloadAction.
  string file_id = utils::CalculateP2PFileId(
      {'1', '2', '3', '4', 'h', 'a', 's', 'h'}, data_.length());
  ASSERT_TRUE(p2p_manager_->FileShare(file_id, data_.length()));
  string existing_data;
  for (unsigned int i = 0; i < 1000; i++) {
    existing_data += '0' + (i % 10);
  }
  ASSERT_TRUE(WriteFile(p2p_manager_->FileGetPath(file_id), existing_data));

  // Check that the file is there.
  EXPECT_EQ(1000, p2p_manager_->FileGetSize(file_id));
  EXPECT_EQ(1, p2p_manager_->CountSharedFiles());

  StartDownload(false);  // use_p2p_to_share

  // DownloadAction should have deleted the p2p file. Check that it's gone.
  EXPECT_EQ(-1, p2p_manager_->FileGetSize(file_id));
  EXPECT_EQ(0, p2p_manager_->CountSharedFiles());
}

TEST_F(P2PDownloadActionTest, MultiplePayload) {
  SetupDownload(0);
  EXPECT_CALL(*FakeSystemState::Get()->mock_payload_state(),
              GetUsingP2PForSharing())
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*FakeSystemState::Get()->mock_call_wrapper(),
              AmountOfFreeDiskSpace(_))
      .WillRepeatedly(Return(data_.length() * 2));

  EXPECT_CALL(*FakeSystemState::Get()->mock_payload_state(), NextPayload())
      .WillOnce(Return(true));

  MockFileWriter mock_file_writer;
  EXPECT_CALL(mock_file_writer, Close()).WillRepeatedly(Return(0));
  EXPECT_CALL(mock_file_writer, Write(_, _, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<2>(ErrorCode::kSuccess), Return(true)));

  InstallPlan install_plan;
  install_plan.payloads.push_back(
      {.size = data_.length() / 4,
       .hash = {'1', '1', '1', '1', 'h', 'a', 's', 'h'}});
  install_plan.payloads.push_back(
      {.size = (data_.length() / 4) * 3,
       .hash = {'2', '2', '2', '2', 'h', 'a', 's', 'h'}});

  auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
  feeder_action->set_obj(install_plan);
  auto download_action = std::make_unique<DownloadActionChromeos>(
      std::make_unique<MockHttpFetcher>(data_.c_str(), data_.length(), nullptr),
      /*interactive=*/false);

  download_action->SetTestFileWriter(&mock_file_writer);
  BondActions(feeder_action.get(), download_action.get());
  processor_.EnqueueAction(std::move(feeder_action));
  processor_.EnqueueAction(std::move(download_action));

  loop_.PostTask(FROM_HERE, base::BindOnce(
                                [](ActionProcessor* processor) {
                                  processor->StartProcessing();
                                },
                                base::Unretained(&processor_)));
  loop_.Run();
  EXPECT_FALSE(loop_.PendingTasks());

  EXPECT_EQ(2, p2p_manager_->CountSharedFiles());
  for (auto& payload : install_plan.payloads) {
    string file_id = utils::CalculateP2PFileId(payload.hash, payload.size);
    EXPECT_EQ(payload.size, p2p_manager_->FileGetSize(file_id));
    string file_content;
    EXPECT_TRUE(
        ReadFileToString(p2p_manager_->FileGetPath(file_id), &file_content));
    EXPECT_EQ(data_.substr(0, payload.size), file_content);
  }

  // We don't use the |delegate_| in this test. So just sets it's processing
  // done to true so it doesn't complain on destruction.
  delegate_.processing_done_called_ = true;
}

class RestrictedTimeIntervalDownloadActionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake_loop_.SetAsCurrent();
    FakeSystemState::CreateInstance();

    // Setup restricted interval policy.
    auto* var_disallowed_time_intervals = FakeSystemState::Get()
                                              ->fake_update_manager()
                                              ->state()
                                              ->device_policy_provider()
                                              ->var_disallowed_time_intervals();
    var_disallowed_time_intervals->reset(
        new WeeklyTimeIntervalVector(restricted_time_intervals_));
    // Sunday, May 3th 2020 7:51 PM.
    ASSERT_TRUE(SetNow(base::Time::Exploded{2020, 5, 0, 3, 19, 51, 0, 0}));
  }

  void TearDown() override { EXPECT_FALSE(fake_loop_.PendingTasks()); }

  bool SetNow(const base::Time::Exploded& exploded_now) {
    base::Time now;
    if (!base::Time::FromLocalExploded(exploded_now, &now)) {
      return false;
    }

    test_clock_.SetNow(now);
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(now);
    return true;
  }

  void AdvanceTime(const base::TimeDelta& duration) {
    test_clock_.Advance(duration);
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(test_clock_.Now());
  }

  void StartDownloadAction(const brillo::Blob& data,
                           const ErrorCode& expected_error_code,
                           base::TimeDelta advance_time);

  brillo::Blob CreateBigData() {
    brillo::Blob big(3 * kMockHttpFetcherChunkSize);
    return big;
  }

  base::SimpleTestClock test_clock_;
  // For EvaluationContext inside monitor.
  brillo::FakeMessageLoop fake_loop_{&test_clock_};
  static WeeklyTimeIntervalVector restricted_time_intervals_;
};

WeeklyTimeIntervalVector
    RestrictedTimeIntervalDownloadActionTest::restricted_time_intervals_ =
        WeeklyTimeIntervalVector{
            // Monday 10:15 AM to Monday 3:30 PM.
            WeeklyTimeInterval(WeeklyTime(1, kHour * 10 + kMinute * 15),
                               WeeklyTime(1, kHour * 15 + kMinute * 30)),
            // Wednesday 8:30 PM to Thursday 8:40 AM.
            WeeklyTimeInterval(WeeklyTime(3, kHour * 20 + kMinute * 30),
                               WeeklyTime(4, kHour * 8 + kMinute * 40))};

void RestrictedTimeIntervalDownloadActionTest::StartDownloadAction(
    const brillo::Blob& data,
    const ErrorCode& expected_error_code,
    base::TimeDelta advance_time) {
  ScopedTempFile output_temp_file;
  TestDirectFileWriter writer;
  EXPECT_EQ(
      0, writer.Open(output_temp_file.path().c_str(), O_WRONLY | O_CREAT, 0));

  InstallPlan install_plan;
  install_plan.can_download_be_canceled = true;
  install_plan.payloads.push_back(
      {.size = data.size(), .type = InstallPayloadType::kFull});

  EXPECT_TRUE(HashCalculator::RawHashOfBytes(&data[0], data.size(),
                                             &install_plan.payloads[0].hash));
  auto feeder_action = std::make_unique<ObjectFeederAction<InstallPlan>>();
  feeder_action->set_obj(install_plan);

  auto http_fetcher =
      std::make_unique<MockHttpFetcher>(data.data(), data.size(), nullptr);
  auto http_fetcher_ptr = http_fetcher.get();
  auto download_action =
      std::make_unique<DownloadActionChromeos>(std::move(http_fetcher),
                                               /*interactive=*/false);
  download_action->SetTestFileWriter(&writer);
  BondActions(feeder_action.get(), download_action.get());

  DownloadActionTestProcessorDelegate delegate;
  delegate.expected_code_ = expected_error_code;
  delegate.expected_data_ = brillo::Blob(data.begin(), data.end());
  delegate.path_ = output_temp_file.path();

  ActionProcessor processor;
  processor.set_delegate(&delegate);
  processor.EnqueueAction(std::move(feeder_action));
  processor.EnqueueAction(std::move(download_action));
  processor.StartProcessing();

  http_fetcher_ptr->Pause();
  AdvanceTime(advance_time);
  http_fetcher_ptr->Unpause();
  fake_loop_.Run();
}

TEST_F(RestrictedTimeIntervalDownloadActionTest,
       DownloadCancelledDueToRestrictedInterval) {
  const base::TimeDelta duration_till_interval =
      WeeklyTime::FromTime(test_clock_.Now())
          .GetDurationTo(restricted_time_intervals_[0].start());
  StartDownloadAction(CreateBigData(), ErrorCode::kDownloadCancelledPerPolicy,
                      duration_till_interval + kMinute);
}

TEST_F(RestrictedTimeIntervalDownloadActionTest,
       DownloadCompletedOutsideRestrictedInterval) {
  const base::TimeDelta duration_till_interval =
      WeeklyTime::FromTime(test_clock_.Now())
          .GetDurationTo(restricted_time_intervals_[0].start());
  StartDownloadAction(CreateBigData(), ErrorCode::kSuccess,
                      duration_till_interval - kMinute);
}

}  // namespace chromeos_update_engine
