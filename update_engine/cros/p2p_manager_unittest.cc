// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/p2p_manager.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/test/simple_test_clock.h>
#include <brillo/message_loops/fake_message_loop.h>
#if BASE_VER < 780000  // Android
#include <base/message_loop/message_loop.h>
#endif  // BASE_VER < 780000
#include <base/strings/stringprintf.h>
#if BASE_VER >= 780000  // CrOS
#include <base/task/single_thread_task_executor.h>
#endif  // BASE_VER >= 780000
#include <brillo/asynchronous_signal_handler.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/message_loops/message_loop_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "update_engine/common/mock_call_wrapper.h"
#include "update_engine/common/prefs.h"
#include "update_engine/common/subprocess.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/common/utils.h"
#include "update_engine/cros/fake_p2p_manager_configuration.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/fake_update_manager.h"

using base::TimeDelta;
using brillo::MessageLoop;
using std::string;
using std::unique_ptr;
using std::vector;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace chromeos_update_engine {

class P2PManagerSimpleTest : public testing::Test {
 protected:
  P2PManagerSimpleTest() = default;
  ~P2PManagerSimpleTest() override = default;

  // Derived from testing::Test.
  void SetUp() override {
    FakeSystemState::CreateInstance();
    test_conf_ = new FakeP2PManagerConfiguration();
    fake_um_ = FakeSystemState::Get()->fake_update_manager();
    mock_call_wrapper_ = FakeSystemState::Get()->mock_call_wrapper();

    // Construct the P2P manager under test.
    manager_.reset(P2PManager::Construct(test_conf_, fake_um_, "cros_au", 3,
                                         base::Days(5)));
  }

  base::SimpleTestClock test_clock_;
  brillo::FakeMessageLoop fake_loop_{&test_clock_};

  // The P2PManager::Configuration instance used for testing.
  FakeP2PManagerConfiguration* test_conf_;

  chromeos_update_manager::FakeUpdateManager* fake_um_;

  unique_ptr<P2PManager> manager_;

  MockCallWrapper* mock_call_wrapper_;
};

// Check that |IsP2PEnabled()| polls the policy correctly, with the value not
// changing between calls.
TEST_F(P2PManagerSimpleTest, P2PEnabledInitAndNotChangedAndChanged) {
  fake_loop_.SetAsCurrent();

  EXPECT_FALSE(manager_->IsP2PEnabled());
  brillo::MessageLoopRunMaxIterations(MessageLoop::current(), 100);
  EXPECT_FALSE(manager_->IsP2PEnabled());
  brillo::MessageLoopRunMaxIterations(MessageLoop::current(), 100);

  // Move clock a few minutes so the timeout causes the policy be re-evaluated.
  test_clock_.Advance(base::Minutes(6));

  fake_um_->state()->updater_provider()->var_p2p_enabled()->reset(
      new bool(true));
  brillo::MessageLoopRunMaxIterations(MessageLoop::current(), 1);
  fake_um_->state()->updater_provider()->var_p2p_enabled()->reset(
      new bool(true));
  EXPECT_TRUE(manager_->IsP2PEnabled());
  // This is not a duplicate test. We need to make sure the value is not changed
  // between consecutive calls.
  fake_um_->state()->updater_provider()->var_p2p_enabled()->reset(
      new bool(true));
  brillo::MessageLoopRunMaxIterations(MessageLoop::current(), 1);
  EXPECT_TRUE(manager_->IsP2PEnabled());
  fake_loop_.Run();
  EXPECT_FALSE(fake_loop_.PendingTasks());
}

// Test fixture that sets up a testing configuration (with e.g. a
// temporary p2p dir) for P2PManager and cleans up when the test is
// done.
class P2PManagerTest : public P2PManagerSimpleTest {
 protected:
  P2PManagerTest() = default;
  ~P2PManagerTest() override = default;

  // Derived from testing::Test.
  void SetUp() override {
    loop_.SetAsCurrent();
    P2PManagerSimpleTest::SetUp();
    async_signal_handler_.Init();
    subprocess_.Init(&async_signal_handler_);
  }

#if BASE_VER < 780000  // Android
  base::MessageLoopForIO base_loop_;
  brillo::BaseMessageLoop loop_{&base_loop_};
#else   // CrOS
  base::SingleThreadTaskExecutor base_loop_{base::MessagePumpType::IO};
  brillo::BaseMessageLoop loop_{base_loop_.task_runner()};
#endif  // BASE_VER < 780000
  brillo::AsynchronousSignalHandler async_signal_handler_;
  Subprocess subprocess_;
};

// Check that we keep the $N newest files with the .$EXT.p2p extension.
TEST_F(P2PManagerTest, HousekeepingCountLimit) {
  // Specifically pass 0 for |max_file_age| to allow files of any age. Note that
  // we need to reallocate the test_conf_ member, whose currently aliased object
  // will be freed.
  test_conf_ = new FakeP2PManagerConfiguration();
  manager_.reset(P2PManager::Construct(test_conf_, fake_um_, "cros_au", 3,
                                       /*max_file_age=*/TimeDelta()));
  EXPECT_EQ(manager_->CountSharedFiles(), 0);

  base::Time start_time = base::Time::FromSecondsSinceUnixEpoch(1246996800.);
  // Generate files with different timestamps matching our pattern and generate
  // other files not matching the pattern.
  for (int n = 0; n < 5; n++) {
    base::FilePath path = test_conf_->GetP2PDir().Append(
        base::StringPrintf("file_%d.cros_au.p2p", n));
    base::Time file_time = start_time + base::Minutes(n);
    EXPECT_TRUE(base::WriteFile(path, std::string_view()));
    EXPECT_TRUE(base::TouchFile(path, file_time, file_time));

    path = test_conf_->GetP2PDir().Append(
        base::StringPrintf("file_%d.OTHER.p2p", n));
    EXPECT_TRUE(base::WriteFile(path, std::string_view()));
    EXPECT_TRUE(base::TouchFile(path, file_time, file_time));
  }
  // CountSharedFiles() only counts 'cros_au' files.
  EXPECT_EQ(manager_->CountSharedFiles(), 5);

  EXPECT_TRUE(manager_->PerformHousekeeping());

  // At this point - after HouseKeeping - we should only have
  // eight files left.
  for (int n = 0; n < 5; n++) {
    string file_name;
    bool expect;

    expect = (n >= 2);
    file_name = base::StringPrintf("%s/file_%d.cros_au.p2p",
                                   test_conf_->GetP2PDir().value().c_str(), n);
    EXPECT_EQ(expect, utils::FileExists(file_name.c_str()));

    file_name = base::StringPrintf("%s/file_%d.OTHER.p2p",
                                   test_conf_->GetP2PDir().value().c_str(), n);
    EXPECT_TRUE(utils::FileExists(file_name.c_str()));
  }
  // CountSharedFiles() only counts 'cros_au' files.
  EXPECT_EQ(manager_->CountSharedFiles(), 3);
}

// Check that we keep files with the .$EXT.p2p extension not older
// than some specific age (5 days, in this test).
TEST_F(P2PManagerTest, HousekeepingAgeLimit) {
  // We set the cutoff time to be 1 billion seconds (01:46:40 UTC on 9
  // September 2001 - arbitrary number, but constant to avoid test
  // flakiness) since the epoch and then we put two files before that
  // date and three files after.
  base::Time cutoff_time = base::Time::FromTimeT(1000000000);
  TimeDelta age_limit = base::Days(5);

  // Set the clock just so files with a timestamp before |cutoff_time|
  // will be deleted at housekeeping.
  FakeSystemState::Get()->fake_clock()->SetWallclockTime(cutoff_time +
                                                         age_limit);

  // Specifically pass 0 for |num_files_to_keep| to allow any number of files.
  // Note that we need to reallocate the test_conf_ member, whose currently
  // aliased object will be freed.
  test_conf_ = new FakeP2PManagerConfiguration();
  manager_.reset(P2PManager::Construct(test_conf_, fake_um_, "cros_au",
                                       /*num_files_to_keep=*/0, age_limit));
  EXPECT_EQ(manager_->CountSharedFiles(), 0);

  // Generate files with different timestamps matching our pattern and generate
  // other files not matching the pattern.
  for (int n = 0; n < 5; n++) {
    base::FilePath path = test_conf_->GetP2PDir().Append(
        base::StringPrintf("file_%d.cros_au.p2p", n));

    // With five files and aiming for two of them to be before
    // |cutoff_time|, we distribute it like this:
    //
    //  -------- 0 -------- 1 -------- 2 -------- 3 -------- 4 --------
    //                            |
    //                       cutoff_time
    //
    base::Time file_date =
        cutoff_time + (n - 2) * base::Days(1) + base::Hours(12);

    EXPECT_TRUE(base::WriteFile(path, std::string_view()));
    EXPECT_TRUE(base::TouchFile(path, file_date, file_date));

    path = test_conf_->GetP2PDir().Append(
        base::StringPrintf("file_%d.OTHER.p2p", n));
    EXPECT_TRUE(base::WriteFile(path, std::string_view()));
    EXPECT_TRUE(base::TouchFile(path, file_date, file_date));
  }
  // CountSharedFiles() only counts 'cros_au' files.
  EXPECT_EQ(manager_->CountSharedFiles(), 5);

  EXPECT_TRUE(manager_->PerformHousekeeping());

  // At this point - after HouseKeeping - we should only have
  // eight files left.
  for (int n = 0; n < 5; n++) {
    string file_name;
    bool expect;

    expect = (n >= 2);
    file_name = base::StringPrintf("%s/file_%d.cros_au.p2p",
                                   test_conf_->GetP2PDir().value().c_str(), n);
    EXPECT_EQ(expect, utils::FileExists(file_name.c_str()));

    file_name = base::StringPrintf("%s/file_%d.OTHER.p2p",
                                   test_conf_->GetP2PDir().value().c_str(), n);
    EXPECT_TRUE(utils::FileExists(file_name.c_str()));
  }
  // CountSharedFiles() only counts 'cros_au' files.
  EXPECT_EQ(manager_->CountSharedFiles(), 3);
}

static bool CheckP2PFile(const string& p2p_dir,
                         const string& file_name,
                         ssize_t expected_size,
                         ssize_t expected_size_xattr) {
  string path = p2p_dir + "/" + file_name;
  char ea_value[64] = {0};
  ssize_t ea_size;

  off_t p2p_size = utils::FileSize(path);
  if (p2p_size < 0) {
    LOG(ERROR) << "File " << path << " does not exist";
    return false;
  }

  if (expected_size != 0) {
    if (p2p_size != expected_size) {
      LOG(ERROR) << "Expected size " << expected_size << " but size was "
                 << p2p_size;
      return false;
    }
  }

  if (expected_size_xattr == 0) {
    ea_size = getxattr(path.c_str(), "user.cros-p2p-filesize", &ea_value,
                       sizeof ea_value - 1);
    if (ea_size == -1 && errno == ENODATA) {
      // This is valid behavior as we support files without the xattr set.
    } else {
      PLOG(ERROR) << "getxattr() didn't fail with ENODATA as expected, "
                  << "ea_size=" << ea_size << ", errno=" << errno;
      return false;
    }
  } else {
    ea_size = getxattr(path.c_str(), "user.cros-p2p-filesize", &ea_value,
                       sizeof ea_value - 1);
    if (ea_size < 0) {
      LOG(ERROR) << "Error getting xattr attribute";
      return false;
    }
    char* endp = nullptr;
    long long int val = strtoll(ea_value, &endp, 0);  // NOLINT(runtime/int)
    if (endp == nullptr || *endp != '\0') {
      LOG(ERROR) << "Error parsing xattr '" << ea_value << "' as an integer";
      return false;
    }
    if (val != expected_size_xattr) {
      LOG(ERROR) << "Expected xattr size " << expected_size_xattr
                 << " but size was " << val;
      return false;
    }
  }

  return true;
}

static bool CreateP2PFile(string p2p_dir,
                          string file_name,
                          size_t size,
                          size_t size_xattr) {
  string path = p2p_dir + "/" + file_name;

  int fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
  if (fd == -1) {
    PLOG(ERROR) << "Error creating file with path " << path;
    return false;
  }
  if (ftruncate(fd, size) != 0) {
    PLOG(ERROR) << "Error truncating " << path << " to size " << size;
    close(fd);
    return false;
  }

  if (size_xattr != 0) {
    string decimal_size = std::to_string(size_xattr);
    if (fsetxattr(fd, "user.cros-p2p-filesize", decimal_size.c_str(),
                  decimal_size.size(), 0) != 0) {
      PLOG(ERROR) << "Error setting xattr on " << path;
      close(fd);
      return false;
    }
  }

  close(fd);
  return true;
}

// Check that sharing a *new* file works.
TEST_F(P2PManagerTest, ShareFile) {
  const int kP2PTestFileSize = 1000 * 8;  // 8 KB
  EXPECT_CALL(*mock_call_wrapper_, AmountOfFreeDiskSpace(_))
      .WillOnce(Return(kP2PTestFileSize * 2));

  EXPECT_TRUE(manager_->FileShare("foo", kP2PTestFileSize));
  EXPECT_EQ(manager_->FileGetPath("foo"),
            test_conf_->GetP2PDir().Append("foo.cros_au.p2p.tmp"));
  EXPECT_TRUE(CheckP2PFile(test_conf_->GetP2PDir().value(),
                           "foo.cros_au.p2p.tmp", 0, kP2PTestFileSize));

  // Sharing it again - with the same expected size - should return true
  EXPECT_TRUE(manager_->FileShare("foo", kP2PTestFileSize));

  // ... but if we use the wrong size, it should fail
  EXPECT_FALSE(manager_->FileShare("foo", kP2PTestFileSize + 1));
}

// Check that making a shared file visible, does what is expected.
TEST_F(P2PManagerTest, MakeFileVisible) {
  const int kP2PTestFileSize = 1000 * 8;  // 8 KB
  EXPECT_CALL(*mock_call_wrapper_, AmountOfFreeDiskSpace(_))
      .WillOnce(Return(kP2PTestFileSize * 2));

  // First, check that it's not visible.
  manager_->FileShare("foo", kP2PTestFileSize);
  EXPECT_EQ(manager_->FileGetPath("foo"),
            test_conf_->GetP2PDir().Append("foo.cros_au.p2p.tmp"));
  EXPECT_TRUE(CheckP2PFile(test_conf_->GetP2PDir().value(),
                           "foo.cros_au.p2p.tmp", 0, kP2PTestFileSize));
  // Make the file visible and check that it changed its name. Do it
  // twice to check that FileMakeVisible() is idempotent.
  for (int n = 0; n < 2; n++) {
    manager_->FileMakeVisible("foo");
    EXPECT_EQ(manager_->FileGetPath("foo"),
              test_conf_->GetP2PDir().Append("foo.cros_au.p2p"));
    EXPECT_TRUE(CheckP2PFile(test_conf_->GetP2PDir().value(), "foo.cros_au.p2p",
                             0, kP2PTestFileSize));
  }
}

TEST_F(P2PManagerTest, SharingFileBytesMoreThanNecessaryStorageSpace) {
  const int kP2PTestFileSize = 16 * (1 << 10);  // 16 KB
  EXPECT_CALL(*mock_call_wrapper_, AmountOfFreeDiskSpace(_))
      .WillOnce(Return(kP2PTestFileSize + 1));
  EXPECT_FALSE(manager_->FileShare("foo", kP2PTestFileSize));
}

TEST_F(P2PManagerTest, SharingFileBytesLessThanNecessaryStorageSpace) {
  const int kP2PTestFileSize = 16 * (1 << 10);  // 16 KB
  EXPECT_CALL(*mock_call_wrapper_, AmountOfFreeDiskSpace(_))
      .WillOnce(Return(kP2PTestFileSize * 2));
  EXPECT_TRUE(manager_->FileShare("foo", kP2PTestFileSize));
}

// Check that we return the right values for existing files in P2P_DIR.
TEST_F(P2PManagerTest, ExistingFiles) {
  bool visible;

  // Check that errors are returned if the file does not exist
  EXPECT_EQ(manager_->FileGetPath("foo"), base::FilePath());
  EXPECT_EQ(manager_->FileGetSize("foo"), -1);
  EXPECT_EQ(manager_->FileGetExpectedSize("foo"), -1);
  EXPECT_FALSE(manager_->FileGetVisible("foo", nullptr));
  // ... then create the file ...
  EXPECT_TRUE(CreateP2PFile(test_conf_->GetP2PDir().value(), "foo.cros_au.p2p",
                            42, 43));
  // ... and then check that the expected values are returned
  EXPECT_EQ(manager_->FileGetPath("foo"),
            test_conf_->GetP2PDir().Append("foo.cros_au.p2p"));
  EXPECT_EQ(manager_->FileGetSize("foo"), 42);
  EXPECT_EQ(manager_->FileGetExpectedSize("foo"), 43);
  EXPECT_TRUE(manager_->FileGetVisible("foo", &visible));
  EXPECT_TRUE(visible);

  // One more time, this time with a .tmp variant. First ensure it errors out..
  EXPECT_EQ(manager_->FileGetPath("bar"), base::FilePath());
  EXPECT_EQ(manager_->FileGetSize("bar"), -1);
  EXPECT_EQ(manager_->FileGetExpectedSize("bar"), -1);
  EXPECT_FALSE(manager_->FileGetVisible("bar", nullptr));
  // ... then create the file ...
  EXPECT_TRUE(CreateP2PFile(test_conf_->GetP2PDir().value(),
                            "bar.cros_au.p2p.tmp", 44, 45));
  // ... and then check that the expected values are returned
  EXPECT_EQ(manager_->FileGetPath("bar"),
            test_conf_->GetP2PDir().Append("bar.cros_au.p2p.tmp"));
  EXPECT_EQ(manager_->FileGetSize("bar"), 44);
  EXPECT_EQ(manager_->FileGetExpectedSize("bar"), 45);
  EXPECT_TRUE(manager_->FileGetVisible("bar", &visible));
  EXPECT_FALSE(visible);
}

// This is a little bit ugly but short of mocking a 'p2p' service this
// will have to do. E.g. we essentially simulate the various
// behaviours of initctl(8) that we rely on.
TEST_F(P2PManagerTest, StartP2P) {
  // Check that we can start the service
  test_conf_->SetInitctlStartCommand({"true"});
  EXPECT_TRUE(manager_->EnsureP2PRunning());
  test_conf_->SetInitctlStartCommand({"false"});
  EXPECT_FALSE(manager_->EnsureP2PRunning());
  test_conf_->SetInitctlStartCommand(
      {"sh", "-c", "echo \"initctl: Job is already running: p2p\" >&2; false"});
  EXPECT_TRUE(manager_->EnsureP2PRunning());
  test_conf_->SetInitctlStartCommand(
      {"sh", "-c", "echo something else >&2; false"});
  EXPECT_FALSE(manager_->EnsureP2PRunning());
}

// Same comment as for StartP2P
TEST_F(P2PManagerTest, StopP2P) {
  // Check that we can start the service
  test_conf_->SetInitctlStopCommand({"true"});
  EXPECT_TRUE(manager_->EnsureP2PNotRunning());
  test_conf_->SetInitctlStopCommand({"false"});
  EXPECT_FALSE(manager_->EnsureP2PNotRunning());
  test_conf_->SetInitctlStopCommand(
      {"sh", "-c", "echo \"initctl: Unknown instance \" >&2; false"});
  EXPECT_TRUE(manager_->EnsureP2PNotRunning());
  test_conf_->SetInitctlStopCommand(
      {"sh", "-c", "echo something else >&2; false"});
  EXPECT_FALSE(manager_->EnsureP2PNotRunning());
}

static void ExpectUrl(const string& expected_url, const string& url) {
  EXPECT_EQ(url, expected_url);
  MessageLoop::current()->BreakLoop();
}

// Like StartP2P, we're mocking the different results that p2p-client
// can return. It's not pretty but it works.
TEST_F(P2PManagerTest, LookupURL) {
  // Emulate p2p-client returning valid URL with "fooX", 42 and "cros_au"
  // being propagated in the right places.
  test_conf_->SetP2PClientCommand(
      {"echo", "http://1.2.3.4/{file_id}_{minsize}"});
  manager_->LookupUrlForFile(
      "fooX", 42, TimeDelta(),
      base::BindOnce(ExpectUrl, "http://1.2.3.4/fooX.cros_au_42"));
  loop_.Run();

  // Emulate p2p-client returning invalid URL.
  test_conf_->SetP2PClientCommand({"echo", "not_a_valid_url"});
  manager_->LookupUrlForFile("foobar", 42, TimeDelta(),
                             base::BindOnce(ExpectUrl, ""));
  loop_.Run();

  // Emulate p2p-client conveying failure.
  test_conf_->SetP2PClientCommand({"false"});
  manager_->LookupUrlForFile("foobar", 42, TimeDelta(),
                             base::BindOnce(ExpectUrl, ""));
  loop_.Run();

  // Emulate p2p-client not existing.
  test_conf_->SetP2PClientCommand({"/path/to/non/existent/helper/program"});
  manager_->LookupUrlForFile("foobar", 42, TimeDelta(),
                             base::BindOnce(ExpectUrl, ""));
  loop_.Run();

  // Emulate p2p-client crashing.
  test_conf_->SetP2PClientCommand({"sh", "-c", "kill -SEGV $$"});
  manager_->LookupUrlForFile("foobar", 42, TimeDelta(),
                             base::BindOnce(ExpectUrl, ""));
  loop_.Run();

  // Emulate p2p-client exceeding its timeout.
  test_conf_->SetP2PClientCommand(
      {"sh", "-c",
       // The 'sleep' launched below could be left behind as an orphaned
       // process when the 'sh' process is terminated by SIGTERM. As a
       // remedy, trap SIGTERM and kill the 'sleep' process, which requires
       // launching 'sleep' in background and then waiting for it.
       "cleanup() { kill \"${sleep_pid}\"; exit 0; }; "
       "trap cleanup TERM; "
       "sleep 5 & "
       "sleep_pid=$!; "
       "echo http://1.2.3.4/; "
       "wait"});
  manager_->LookupUrlForFile("foobar", 42, base::Milliseconds(500),
                             base::BindOnce(ExpectUrl, ""));
  loop_.Run();
}

}  // namespace chromeos_update_engine
