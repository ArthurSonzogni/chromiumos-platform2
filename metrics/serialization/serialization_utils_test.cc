// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/serialization/serialization_utils.h"

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/thread_pool.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/threading/platform_thread.h>
#include <brillo/process/process.h>
#include <gtest/gtest.h>

#include "metrics/serialization/metric_sample.h"

namespace metrics {
namespace {

class SerializationUtilsTest : public testing::Test {
 protected:
  SerializationUtilsTest() {
    bool success = temporary_dir_.CreateUniqueTempDir();
    if (success) {
      base::FilePath dir_path = temporary_dir_.GetPath();
      filepath_ = dir_path.Append("chromeossampletest");
      filename_ = filepath_.value();
    }

    base::FilePath my_executable_path =
        base::CommandLine::ForCurrentProcess()->GetProgram();
    build_directory_ = base::FilePath(my_executable_path.DirName());
  }
  SerializationUtilsTest(const SerializationUtilsTest&) = delete;
  SerializationUtilsTest& operator=(const SerializationUtilsTest&) = delete;

  void SetUp() override { base::DeleteFile(filepath_); }

  void TestSerialization(const MetricSample& sample) {
    std::string serialized(sample.ToString());
    ASSERT_EQ('\0', serialized.back());
    MetricSample deserialized = SerializationUtils::ParseSample(serialized);
    EXPECT_TRUE(sample.IsEqual(deserialized));
  }

  // Lock the indicated file |file_name| using flock so that
  // WriteMetricsToFile() will fail to acquire it. File will be created if
  // it doesn't exist. Returns when the file is actually locked. Since locks are
  // per-process, in order to prevent this process from locking the file, we
  // have to spawn a separate process to hold the lock; the process holding the
  // lock is returned. It can be killed to release the lock.
  std::unique_ptr<brillo::Process> LockFile(const base::FilePath& file_name) {
    base::Time start_time = base::Time::Now();
    auto lock_process = std::make_unique<brillo::ProcessImpl>();
    CHECK(!build_directory_.empty());
    base::FilePath lock_file_holder = build_directory_.Append("hold_lock_file");
    lock_process->AddArg(lock_file_holder.value());
    lock_process->AddArg(file_name.value());
    CHECK(lock_process->Start());

    // Wait for the file to actually be locked. Don't wait forever in case the
    // subprocess fails in some way.
    base::Time stop_time = base::Time::Now() + base::Seconds(30);
    bool success = false;
    base::Time wait_start_time = base::Time::Now();
    LOG(INFO) << "Took " << wait_start_time - start_time
              << " to start subprocess";
    while (!success && base::Time::Now() < stop_time) {
      base::File lock_file(file_name, base::File::FLAG_OPEN |
                                          base::File::FLAG_READ |
                                          base::File::FLAG_WRITE);
      if (lock_file.IsValid()) {
        if (HANDLE_EINTR(
                flock(lock_file.GetPlatformFile(), LOCK_EX | LOCK_NB)) < 0 &&
            errno == EWOULDBLOCK) {
          success = true;
        }
      }

      if (!success) {
        base::PlatformThread::Sleep(base::Seconds(1));
      }
    }
    LOG(INFO) << "Took " << base::Time::Now() - wait_start_time
              << " to verify file lock";

    CHECK(success) << "Subprocess did not lock " << file_name.value();
    return lock_process;
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MULTIPLE_THREADS};
  std::string filename_;
  base::ScopedTempDir temporary_dir_;
  base::FilePath filepath_;
  // Directory that the test executable lives in.
  base::FilePath build_directory_;
};

TEST_F(SerializationUtilsTest, CrashSerializeTest) {
  // Should work with both 1 and non-1 values
  TestSerialization(MetricSample::CrashSample("test", /*num_samples=*/1));
  TestSerialization(MetricSample::CrashSample("test", /*num_samples=*/10));
}

TEST_F(SerializationUtilsTest, HistogramSerializeTest) {
  TestSerialization(MetricSample::HistogramSample(
      "myhist", /*sample=*/13, /*min=*/1, /*max=*/100,
      /*bucket_count=*/10, /*num_samples=*/1));
  TestSerialization(MetricSample::HistogramSample(
      "myhist", /*sample=*/13, /*min=*/1, /*max=*/100,
      /*bucket_count=*/10, /*num_samples=*/2));
}

TEST_F(SerializationUtilsTest, LinearSerializeTest) {
  TestSerialization(
      MetricSample::LinearHistogramSample("linearhist", /*sample=*/12,
                                          /*max=*/30, /*num_samples=*/1));
  TestSerialization(
      MetricSample::LinearHistogramSample("linearhist", /*sample=*/12,
                                          /*max=*/30, /*num_samples=*/10));
}

TEST_F(SerializationUtilsTest, SparseSerializeTest) {
  TestSerialization(MetricSample::SparseHistogramSample(
      "mysparse", /*sample=*/30, /*num_samples=*/1));
  TestSerialization(MetricSample::SparseHistogramSample(
      "mysparse", /*sample=*/30, /*num_samples=*/10));
}

TEST_F(SerializationUtilsTest, UserActionSerializeTest) {
  TestSerialization(
      MetricSample::UserActionSample("myaction", /*num_samples=*/1));
  TestSerialization(
      MetricSample::UserActionSample("myaction", /*num_samples=*/10));
}

TEST_F(SerializationUtilsTest, InvalidCrashSerialize) {
  // No name
  EXPECT_EQ(MetricSample::INVALID, MetricSample::ParseCrash("").type());
  // Empty name
  EXPECT_EQ(MetricSample::INVALID, MetricSample::ParseCrash(" ").type());
  // num_samples is not a number
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseCrash("kernel asdf").type());
  // Too many numbers
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseCrash("kernel 1 2").type());
  // Negative num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseCrash("kernel -1").type());
}

TEST_F(SerializationUtilsTest, InvalidHistogramSample) {
  // Too few parts
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 2 3").type());
  // Too many parts
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 2 3 4 5 6").type());
  // Empty hist name
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram(" 1 2 3 4 5").type());
  // sample is not a number
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist a 2 3 4 5").type());
  // min is not a number
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 a 3 4 5").type());
  // max is not a number
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 2 a 4 5").type());
  // buckets is not a number
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 2 3 a 5").type());
  // num_samples is not a number
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 2 3 4 a").type());
  // Negative num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseHistogram("hist 1 2 3 4 -1").type());
}

TEST_F(SerializationUtilsTest, InvalidSparseHistogramSample) {
  // Too few fields
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseSparseHistogram("name").type());
  // Too many fields
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseSparseHistogram("name 1 2 3").type());
  // No name
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseSparseHistogram(" 1 2").type());
  // Invalid sample
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseSparseHistogram("name a 2").type());
  // Invalid num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseSparseHistogram("name 1 a").type());
  // Negative num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseSparseHistogram("name 1 -1").type());
}

TEST_F(SerializationUtilsTest, InvalidLinearHistogramSample) {
  // Too few fields
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram("name 1").type());
  // Too many fields
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram("name 1 2 3 4").type());
  // No name
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram(" 1 2 3").type());
  // Invalid sample
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram("name a 2 3").type());
  // Invalid max
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram("name 1 a 3").type());
  // Invalid num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram("name 1 2 a").type());
  // Negative num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseLinearHistogram("name 1 2 -1").type());
}

TEST_F(SerializationUtilsTest, InvalidUserAction) {
  // Too few fields
  EXPECT_EQ(MetricSample::INVALID, MetricSample::ParseUserAction("").type());
  // Too many fields
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseUserAction("name 1 2").type());
  // No name
  EXPECT_EQ(MetricSample::INVALID, MetricSample::ParseUserAction(" 1").type());
  // Invalid num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseUserAction("name a").type());
  // Negative num_samples
  EXPECT_EQ(MetricSample::INVALID,
            MetricSample::ParseUserAction("name -1").type());
}

TEST_F(SerializationUtilsTest, IllegalNameAreFilteredTest) {
  EXPECT_FALSE(SerializationUtils::WriteMetricsToFile(
      {MetricSample::SparseHistogramSample("no space", 10, /*num_samples=*/1),
       MetricSample::LinearHistogramSample(
           base::StringPrintf("here%cbhe", '\0'), 1, 3, /*num_samples=*/1)},
      filename_));

  ASSERT_FALSE(PathExists(filepath_));
  EXPECT_FALSE(base::GetFileSize(filepath_).has_value());
}

TEST_F(SerializationUtilsTest, BadHistogramsTest) {
  EXPECT_FALSE(SerializationUtils::WriteMetricsToFile(
      {MetricSample::HistogramSample("myhist", 5, 1, 10, 100,
                                     /*num_samples=*/1)},
      filename_));
  EXPECT_FALSE(SerializationUtils::WriteMetricsToFile(
      {MetricSample::LinearHistogramSample("alsomyhist", 0, 1,
                                           /*num_samples=*/1)},
      filename_));
}

TEST_F(SerializationUtilsTest, BadInputIsCaughtTest) {
  std::string input(
      base::StringPrintf("sparsehistogram%cname foo%c", '\0', '\0'));
  EXPECT_FALSE(MetricSample::ParseSparseHistogram(input).IsValid());
}

TEST_F(SerializationUtilsTest, MessageSeparatedByZero) {
  EXPECT_TRUE(SerializationUtils::WriteMetricsToFile(
      {MetricSample::CrashSample("mycrash", /*num_samples=*/1)}, filename_));
  // 4 bytes for the size
  // 5 bytes for crash
  // 1 byte for \0
  // 7 bytes for mycrash
  // 1 bytes for \0
  // -> total of 18
  EXPECT_EQ(base::GetFileSize(filepath_), 18);
}

TEST_F(SerializationUtilsTest, MessageSeparatedByZero_WithSamples) {
  EXPECT_TRUE(SerializationUtils::WriteMetricsToFile(
      {MetricSample::CrashSample("mycrash", /*num_samples=*/10)}, filename_));
  // 4 bytes for the size
  // 5 bytes for crash
  // 1 byte for \0
  // 7 bytes for mycrash
  // 3 bytes for " 10"
  // 1 byte for \0
  // -> total of 21
  EXPECT_EQ(base::GetFileSize(filepath_), 21);
}

TEST_F(SerializationUtilsTest, MessagesTooLongAreDiscardedTest) {
  // Creates a message that is bigger than the maximum allowed size.
  // As we are adding extra character (crash, \0s, etc), if the name is
  // kMessageMaxLength long, it will be too long.
  std::string name(SerializationUtils::kMessageMaxLength, 'c');

  EXPECT_FALSE(SerializationUtils::WriteMetricsToFile(
      {MetricSample::CrashSample(name, /*num_samples=*/1)}, filename_));
  EXPECT_FALSE(base::PathExists(filepath_));
}

TEST_F(SerializationUtilsTest, ReadLongMessageTest) {
  base::File test_file(filepath_,
                       base::File::FLAG_OPEN_ALWAYS | base::File::FLAG_APPEND);
  std::string message(SerializationUtils::kMessageMaxLength + 1, 'c');

  int32_t message_size = message.length() + sizeof(int32_t);
  test_file.WriteAtCurrentPos(reinterpret_cast<const char*>(&message_size),
                              sizeof(message_size));
  test_file.WriteAtCurrentPos(message.c_str(), message.length());
  test_file.Close();

  MetricSample crash = MetricSample::CrashSample("test", /*num_samples=*/1);
  EXPECT_TRUE(SerializationUtils::WriteMetricsToFile({crash}, filename_));

  std::vector<MetricSample> samples;

  size_t bytes_read = 0;
  SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, SerializationUtils::kSampleBatchMaxLength,
      bytes_read);

  // Shouldn't count the bytes we ignored.
  EXPECT_EQ(bytes_read, crash.ToString().length() + sizeof(int32_t));

  ASSERT_EQ(1U, samples.size());
  EXPECT_TRUE(crash.IsEqual(samples.front()));
}

TEST_F(SerializationUtilsTest, NegativeLengthTest) {
  // This input is specifically constructed to yield a single crash sample when
  // parsed by a buggy version of the code but fails to parse and doesn't yield
  // samples when parsed by a correct implementation.
  constexpr uint8_t kInput[] = {
      // Length indicating that next length field is the negative one below.
      // This sample is invalid as it contains more than three null bytes.
      0x14,
      0x00,
      0x00,
      0x00,
      // Encoding of a valid crash sample.
      0x0c,
      0x00,
      0x00,
      0x00,
      0x63,
      0x72,
      0x61,
      0x73,
      0x68,
      0x00,
      0x61,
      0x00,
      // Invalid sample that jumps past the negative length bytes below.
      0x08,
      0x00,
      0x00,
      0x00,
      // This is -16 in two's complement interpretation, pointing to the valid
      // crash sample before.
      0xf0,
      0xff,
      0xff,
      0xff,
  };
  CHECK(base::WriteFile(filepath_, base::make_span(kInput, sizeof(kInput))));

  std::vector<MetricSample> samples;
  size_t bytes_read;
  SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, SerializationUtils::kSampleBatchMaxLength,
      bytes_read);
  // Read should ignore the -16.
  EXPECT_EQ(sizeof(kInput) - sizeof(int32_t), bytes_read);

  ASSERT_EQ(0U, samples.size());
}

TEST_F(SerializationUtilsTest, WriteReadTest) {
  std::vector<MetricSample> output_samples = {
      MetricSample::HistogramSample("myhist", 3, 1, 10, 5, /*num_samples=*/1),
      MetricSample::CrashSample("mycrash", /*num_samples=*/2),
      MetricSample::LinearHistogramSample("linear", 1, 10, /*num_samples=*/3),
      MetricSample::SparseHistogramSample("mysparse", 30, /*num_samples=*/4),
      MetricSample::UserActionSample("myaction", /*num_samples=*/5),
      MetricSample::HistogramSample("myrepeatedhist", 3, 1, 10, 5,
                                    /*num_samples=*/10),
  };

  EXPECT_TRUE(
      SerializationUtils::WriteMetricsToFile(output_samples, filename_));
  std::optional<int64_t> size = base::GetFileSize(filepath_);
  std::vector<MetricSample> samples;
  size_t bytes_read;
  SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, SerializationUtils::kSampleBatchMaxLength,
      bytes_read);
  EXPECT_EQ(size, bytes_read);

  ASSERT_EQ(output_samples.size(), samples.size());
  for (size_t i = 0; i < output_samples.size(); ++i) {
    EXPECT_TRUE(output_samples[i].IsEqual(samples[i]));
  }

  ASSERT_EQ(0, base::GetFileSize(filepath_));
}

// Check that WriteMetricsToFile doesn't write to a dangling (deleted) file.
TEST_F(SerializationUtilsTest, LockDeleteRace) {
  int fd =
      open(filename_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0777);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(HANDLE_EINTR(flock(fd, LOCK_EX)), 0);
  scoped_refptr<base::SequencedTaskRunner> task_runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});

  base::RunLoop loop;
  // Post a thread that waits with file locked (to make race more likely) then
  // deletes the file, as chrome would, and unlocks the file.
  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](int fd, const std::string& filename, base::OnceClosure cb) {
            base::PlatformThread::Sleep(base::Seconds(5));
            ASSERT_EQ(unlink(filename.c_str()), 0);
            ASSERT_EQ(HANDLE_EINTR(flock(fd, LOCK_UN)), 0);
            ASSERT_EQ(close(fd), 0);
            std::move(cb).Run();
          },
          fd, filename_, loop.QuitClosure()));

  std::vector<MetricSample> output_samples = {
      MetricSample::HistogramSample("myhist", 3, 1, 10, 5, /*num_samples=*/1),
  };
  EXPECT_TRUE(
      SerializationUtils::WriteMetricsToFile(output_samples, filename_));

  // Ensure thread is done to make sure that it's not about to delete the file
  // (e.g. if ReadAndTruncateMetricsFromFile didn't wait for the lock).
  loop.Run();

  std::vector<MetricSample> samples;
  size_t bytes_read;
  SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, SerializationUtils::kSampleBatchMaxLength,
      bytes_read);

  ASSERT_EQ(output_samples.size(), samples.size());
  for (size_t i = 0; i < output_samples.size(); ++i) {
    EXPECT_TRUE(output_samples[i].IsEqual(samples[i]));
  }

  ASSERT_EQ(0, base::GetFileSize(filepath_));
}

// Same as above, but re-create the file to make sure that inode checking works.
TEST_F(SerializationUtilsTest, LockDeleteRecreateRace) {
  int fd =
      open(filename_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0777);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(HANDLE_EINTR(flock(fd, LOCK_EX)), 0);
  scoped_refptr<base::SequencedTaskRunner> task_runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});

  base::RunLoop loop;
  // Post a thread that waits with file locked (to make race more likely) then
  // deletes the file, as chrome would, and unlocks the file.
  task_runner->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](int fd, const std::string& filename, base::OnceClosure cb) {
            base::PlatformThread::Sleep(base::Seconds(5));
            ASSERT_EQ(unlink(filename.c_str()), 0);

            // Recreate the file to make sure inode checking works.
            ASSERT_TRUE(base::WriteFile(base::FilePath(filename), ""));

            ASSERT_EQ(HANDLE_EINTR(flock(fd, LOCK_UN)), 0);
            ASSERT_EQ(close(fd), 0);
            std::move(cb).Run();
          },
          fd, filename_, loop.QuitClosure()));

  std::vector<MetricSample> output_samples = {
      MetricSample::HistogramSample("myhist", 3, 1, 10, 5, /*num_samples=*/1),
  };
  EXPECT_TRUE(
      SerializationUtils::WriteMetricsToFile(output_samples, filename_));
  std::optional<int64_t> size = base::GetFileSize(filepath_);

  // Ensure thread is done to make sure that it's not about to delete the file
  // (e.g. if ReadAndTruncateMetricsFromFile didn't wait for the lock).
  loop.Run();

  std::vector<MetricSample> samples;
  size_t bytes_read;
  SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, SerializationUtils::kSampleBatchMaxLength,
      bytes_read);
  EXPECT_EQ(bytes_read, size);

  ASSERT_EQ(output_samples.size(), samples.size());
  for (size_t i = 0; i < output_samples.size(); ++i) {
    EXPECT_TRUE(output_samples[i].IsEqual(samples[i]));
  }

  ASSERT_EQ(0, base::GetFileSize(filepath_));
}

TEST_F(SerializationUtilsTest, WriteReadDeleteTest) {
  std::vector<MetricSample> output_samples = {
      MetricSample::HistogramSample("myhist", 3, 1, 10, 5, /*num_samples=*/1),
      MetricSample::CrashSample("mycrash", /*num_samples=*/2),
      MetricSample::LinearHistogramSample("linear", 1, 10, /*num_samples=*/3),
      MetricSample::SparseHistogramSample("mysparse", 30, /*num_samples=*/4),
      MetricSample::UserActionSample("myaction", /*num_samples=*/5),
      MetricSample::HistogramSample("myrepeatedhist", 3, 1, 10, 5,
                                    /*num_samples=*/10),
  };

  EXPECT_TRUE(
      SerializationUtils::WriteMetricsToFile(output_samples, filename_));
  std::optional<int64_t> size = base::GetFileSize(filepath_);

  std::vector<MetricSample> samples;
  size_t bytes_read;
  SerializationUtils::ReadAndDeleteMetricsFromFile(
      filename_, &samples, SerializationUtils::kSampleBatchMaxLength,
      bytes_read);
  EXPECT_EQ(bytes_read, size);

  ASSERT_EQ(output_samples.size(), samples.size());
  for (size_t i = 0; i < output_samples.size(); ++i) {
    EXPECT_TRUE(output_samples[i].IsEqual(samples[i]));
  }

  ASSERT_FALSE(base::PathExists(filepath_));
}

// Test of batched upload.  Creates a metrics log with enough samples to
// trigger two uploads.
TEST_F(SerializationUtilsTest, BatchedUploadTest) {
  MetricSample hist = MetricSample::HistogramSample("Boring.Histogram", 3, 1,
                                                    10, 5, /*num_samples=*/1);
  // The serialized MetricSample does not contain the header size (4 bytes for
  // the total sample length).
  size_t serialized_sample_length = hist.ToString().length() + 4;
  // Make the max batch size a multiple of the filesystem block size so we can
  // test the hole-punching optimization (maybe overkill, but fun).
  const size_t sample_batch_max_length = 10 * 4096;
  // Write enough samples for two passes.
  const int sample_count =
      1.5 * sample_batch_max_length / serialized_sample_length;

  EXPECT_TRUE(SerializationUtils::WriteMetricsToFile(
      std::vector<MetricSample>(sample_count, hist), filename_));

  std::vector<MetricSample> samples;
  size_t bytes_read;
  bool first_pass_status = SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, sample_batch_max_length, bytes_read);
  // subtlety: We can overflow by at most one sample; we only stop when a sample
  // causes us to *exceed* sample_batch_max_length.
  EXPECT_GT(bytes_read, 0);
  EXPECT_GE(sample_batch_max_length + serialized_sample_length, bytes_read);

  ASSERT_FALSE(first_pass_status);  // means: more samples remain
  int first_pass_count = samples.size();
  ASSERT_LT(first_pass_count, sample_count);

  // There is nothing in the base library which returns the actual file
  // allocation (size - holes).
  struct stat stat_buf;
  // Check that stat() is successful.
  ASSERT_EQ(::stat(filename_.c_str(), &stat_buf), 0);
  // Check that the file is not truncated to zero.
  ASSERT_GT(stat_buf.st_size, 0);
  // Check that the file has holes.
  ASSERT_LT(stat_buf.st_blocks * 512, stat_buf.st_size);

  size_t second_bytes_read;
  bool second_pass_status = SerializationUtils::ReadAndTruncateMetricsFromFile(
      filename_, &samples, sample_batch_max_length, second_bytes_read);
  EXPECT_GT(second_bytes_read, 0);
  EXPECT_GE(sample_batch_max_length + serialized_sample_length,
            second_bytes_read);
  EXPECT_EQ(1.5 * sample_batch_max_length, second_bytes_read + bytes_read);

  ASSERT_TRUE(second_pass_status);  // no more samples.
  // Check that stat() is successful.
  ASSERT_EQ(::stat(filename_.c_str(), &stat_buf), 0);
  // Check that the file is empty.
  ASSERT_EQ(stat_buf.st_size, 0);
  // Check that we read all samples.
  ASSERT_EQ(samples.size(), sample_count);
}

TEST_F(SerializationUtilsTest, ParseInvalidType) {
  // Verify that parsing of an invalid sample type fails.
  EXPECT_EQ(MetricSample::INVALID,
            SerializationUtils::ParseSample(
                base::StringPrintf("not_a_type%cvalue%c", '\0', '\0'))
                .type());
}

}  // namespace
}  // namespace metrics
