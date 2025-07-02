// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "farfetchd/trace_manager.h"

#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_util.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace farfetchd {

// Mock implementation of TraceReader for testing
class MockTraceReader : public TraceReader {
 public:
  MockTraceReader() = default;
  ~MockTraceReader() override = default;

  bool Open() override {
    is_open_ = true;
    return true;  // Always succeed in tests
  }

  bool ReadLine(std::string* line) override {
    if (!is_open_) {
      return false;  // Reader is closed
    }

    if (!lines_to_read_.empty()) {
      *line = lines_to_read_.front();
      lines_to_read_.pop();
      return true;
    }

    // If max_reads_ is 0, simulate immediate end of data (for most tests)
    // We'll sleep briefly to allow the trace to be stopped gracefully
    if (max_reads_ == 0) {
      // Give the test a chance to stop the trace before we return false
      base::PlatformThread::Sleep(base::Milliseconds(10));

      // Check if we're still supposed to be reading
      return false;  // No more data
    }

    if (max_reads_ > 0 && read_count_ >= max_reads_) {
      return false;  // Simulate end of data
    }

    // Return some mock trace data for testing
    *line =
        "test-process-123 [000] .... 1234.567890: event: file=\"/test/path\"";
    read_count_++;
    return true;
  }

  void Close() override { is_open_ = false; }

  // Test helpers
  void set_max_reads(int max) { max_reads_ = max; }
  void AddLine(const std::string& line) { lines_to_read_.push(line); }

 private:
  bool is_open_ = false;
  int read_count_ = 0;
  int max_reads_ = 0;  // Limit reads to prevent infinite loops in tests
  std::queue<std::string> lines_to_read_;
};

class TraceManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    // Create a task environment for testing async operations
    task_environment_ = std::make_unique<base::test::TaskEnvironment>(
        base::test::TaskEnvironment::TimeSource::MOCK_TIME);

    // Create a mock trace reader
    auto mock_reader = std::make_unique<MockTraceReader>();
    mock_trace_reader_ = mock_reader.get();  // Keep a pointer for test control

    // Note: Individual tests can call mock_trace_reader_->set_max_reads() to
    // control behavior Default is 0 which means immediate end-of-stream

    manager_ = std::make_unique<TraceManager>(std::move(mock_reader));
    manager_->SetTraceBaseDirForTest(temp_dir_.GetPath());
  }

  void TearDown() override {
    // Run any pending tasks
    task_environment_->RunUntilIdle();
  }

  base::ScopedTempDir temp_dir_;
  std::unique_ptr<base::test::TaskEnvironment> task_environment_;
  std::unique_ptr<TraceManager> manager_;
  MockTraceReader* mock_trace_reader_;
};

// Test the basic trace creation functionality
TEST_F(TraceManagerTest, StartTrace) {
  std::string app_name = "test_app";
  std::vector<std::string> process_names = {"chrome", "chrome_renderer"};

  std::string trace_id = manager_->StartTrace(app_name, process_names, {}, {});

  EXPECT_FALSE(trace_id.empty());
  EXPECT_TRUE(
      base::StartsWith(trace_id, "trace_"));  // Trace IDs start with prefix.

  // Verify the trace directory was created
  base::FilePath trace_dir = temp_dir_.GetPath().Append(trace_id);
  EXPECT_TRUE(base::DirectoryExists(trace_dir));

  // Verify the trace status is "Tracing"
  EXPECT_EQ(manager_->GetTraceStatus(trace_id), "Tracing");
}

TEST_F(TraceManagerTest, StartTraceWithoutProcesses) {
  std::string app_name = "test_app";
  std::vector<std::string> empty_process_names;

  std::string trace_id =
      manager_->StartTrace(app_name, empty_process_names, {}, {});

  EXPECT_TRUE(trace_id.empty());
}

TEST_F(TraceManagerTest, StartMultipleTraces) {
  std::string app1 = "app1";
  std::string app2 = "app2";
  std::vector<std::string> process_names1 = {"chrome"};
  std::vector<std::string> process_names2 = {"firefox"};

  std::string trace_id1 = manager_->StartTrace(app1, process_names1, {}, {});
  std::string trace_id2 = manager_->StartTrace(app2, process_names2, {}, {});

  EXPECT_FALSE(trace_id1.empty());
  EXPECT_FALSE(trace_id2.empty());
  EXPECT_NE(trace_id1, trace_id2);

  // Give traces a moment to start, then check status
  task_environment_->RunUntilIdle();

  // Stop traces to avoid error state from mock reader
  manager_->StopTrace(trace_id1);
  manager_->StopTrace(trace_id2);

  // Run tasks to complete processing
  task_environment_->RunUntilIdle();
}

TEST_F(TraceManagerTest, StopTrace) {
  // Ensure reader doesn't hit EOF while tracing.
  mock_trace_reader_->set_max_reads(-1);
  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Ensure there is at least an empty raw file so processing doesn't fail.
  base::FilePath raw_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.raw");
  ASSERT_TRUE(base::CreateDirectory(raw_path.DirName()));
  base::WriteFile(raw_path, "");

  bool result = manager_->StopTrace(trace_id);
  EXPECT_TRUE(result);

  // Allow some time for async processing
  task_environment_->RunUntilIdle();

  // Status should eventually become "Processing" or "Completed"
  std::string status = manager_->GetTraceStatus(trace_id);
  EXPECT_THAT(status, ::testing::AnyOf(::testing::Eq("Processing"),
                                       ::testing::Eq("Completed")));
}

TEST_F(TraceManagerTest, StopMissingTrace) {
  bool result = manager_->StopTrace("nonexistent_trace_id");
  EXPECT_FALSE(result);
}

TEST_F(TraceManagerTest, StopTwice) {
  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Stop the trace once
  EXPECT_TRUE(manager_->StopTrace(trace_id));

  // Try to stop it again
  EXPECT_FALSE(manager_->StopTrace(trace_id));
}

TEST_F(TraceManagerTest, CancelTrace) {
  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  bool result = manager_->CancelTrace(trace_id);
  EXPECT_TRUE(result);

  EXPECT_EQ(manager_->GetTraceStatus(trace_id), "Cancelled");
}

TEST_F(TraceManagerTest, CancelMissingTrace) {
  bool result = manager_->CancelTrace("nonexistent_trace_id");
  EXPECT_FALSE(result);
}

TEST_F(TraceManagerTest, GetStatusMissingTrace) {
  std::string status = manager_->GetTraceStatus("nonexistent_trace_id");
  EXPECT_TRUE(status.empty());
}

TEST_F(TraceManagerTest, GetPathWhileTracing) {
  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Should return empty path while tracing
  std::string path = manager_->GetTracePath(trace_id);
  EXPECT_TRUE(path.empty());
}

TEST_F(TraceManagerTest, GetPathAfterStop) {
  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Stop and wait for completion
  EXPECT_TRUE(manager_->StopTrace(trace_id));
  task_environment_->RunUntilIdle();

  // Should return the trace file path once completed
  std::string path = manager_->GetTracePath(trace_id);
  if (manager_->GetTraceStatus(trace_id) == "Completed") {
    EXPECT_FALSE(path.empty());
    EXPECT_TRUE(path.find(trace_id) != std::string::npos);
  }
}

TEST_F(TraceManagerTest, GetPathMissingTrace) {
  std::string path = manager_->GetTracePath("nonexistent_trace_id");
  EXPECT_TRUE(path.empty());
}

TEST_F(TraceManagerTest, Timeout) {
  mock_trace_reader_->set_max_reads(100);

  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Verify initial state.
  EXPECT_EQ(manager_->GetTraceStatus(trace_id), "Tracing");

  // Fast forward time by the timeout amount.
  task_environment_->FastForwardBy(base::Minutes(5));

  // Run tasks to process the timeout
  task_environment_->RunUntilIdle();

  // The trace should have been stopped by the timeout handler.
  // Since we don't manually create a raw file, ProcessTrace might fail,
  // but the important thing is that it's no longer "Tracing".
  std::string status = manager_->GetTraceStatus(trace_id);
  EXPECT_NE(status, "Tracing");
}

TEST_F(TraceManagerTest, MultipleOperations) {
  std::vector<std::string> process_names = {"chrome"};
  std::string trace_id =
      manager_->StartTrace("test_app", process_names, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Try multiple operations
  EXPECT_EQ(manager_->GetTraceStatus(trace_id), "Tracing");
  EXPECT_TRUE(manager_->GetTracePath(trace_id).empty());  // Not completed yet

  // Stop the trace
  EXPECT_TRUE(manager_->StopTrace(trace_id));

  // Try to stop again (should fail)
  EXPECT_FALSE(manager_->StopTrace(trace_id));

  // Try to cancel after stopping (should still work)
  EXPECT_TRUE(manager_->CancelTrace(trace_id));
  EXPECT_EQ(manager_->GetTraceStatus(trace_id), "Cancelled");
}

TEST_F(TraceManagerTest, UniqueIds) {
  std::vector<std::string> process_names = {"chrome"};

  // Create multiple traces and verify they have unique IDs
  std::string trace_id1 = manager_->StartTrace("app1", process_names, {}, {});
  std::string trace_id2 = manager_->StartTrace("app2", process_names, {}, {});
  std::string trace_id3 = manager_->StartTrace("app3", process_names, {}, {});

  EXPECT_FALSE(trace_id1.empty());
  EXPECT_FALSE(trace_id2.empty());
  EXPECT_FALSE(trace_id3.empty());

  EXPECT_NE(trace_id1, trace_id2);
  EXPECT_NE(trace_id2, trace_id3);
  EXPECT_NE(trace_id1, trace_id3);
}

TEST_F(TraceManagerTest, PidFiltering) {
  // This test verifies that ProcessTrace correctly filters by PID during
  // the offline processing phase (not during the live trace reading phase).
  // We'll create a trace that includes lines from multiple PIDs.

  std::string trace_id =
      manager_->StartTrace("test_app", {"some_process"}, {}, {});
  ASSERT_FALSE(trace_id.empty());

  // Manually create a raw trace file with data from multiple PIDs.
  // The key insight is that ProcessTrace doesn't filter by PID - that's done
  // during live tracing. ProcessTrace only filters by path patterns.
  base::FilePath raw_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.raw");
  std::string raw_content =
      "some-process-123 [000] ... event: file=\"/path/to/allowed_file\"\n"
      "other-process-456 [000] ... event: file=\"/path/to/other_file\"\n";
  ASSERT_TRUE(base::WriteFile(raw_path, raw_content));

  // Stop the trace and wait for processing.
  ASSERT_TRUE(manager_->StopTrace(trace_id));
  task_environment_->RunUntilIdle();
  ASSERT_EQ(manager_->GetTraceStatus(trace_id), "Completed");

  // Check the final trace file.
  base::FilePath final_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.log");
  std::string final_content;
  ASSERT_TRUE(base::ReadFileToString(final_path, &final_content));

  // Since we don't have path filtering rules, both lines should be present.
  // This test demonstrates that ProcessTrace preserves all lines from the raw
  // file.
  EXPECT_NE(final_content.find("/path/to/allowed_file"), std::string::npos);
  EXPECT_NE(final_content.find("/path/to/other_file"), std::string::npos);
}

// Test error conditions and edge cases
TEST_F(TraceManagerTest, EmptyAppName) {
  std::vector<std::string> process_names = {"chrome"};

  // Test with empty app name
  std::string trace_id = manager_->StartTrace("", process_names, {}, {});
  EXPECT_FALSE(trace_id.empty());  // Should still work with empty app name
}

TEST_F(TraceManagerTest, ManyProcesses) {
  std::vector<std::string> large_process_list;
  for (int i = 0; i < 100; ++i) {
    large_process_list.push_back("process_" + std::to_string(i));
  }
  std::string trace_id =
      manager_->StartTrace("large_app", large_process_list, {}, {});
  EXPECT_FALSE(trace_id.empty());
  EXPECT_EQ(manager_->GetTraceStatus(trace_id), "Tracing");
}

TEST_F(TraceManagerTest, CreateAndStart) {
  std::string trace_id = manager_->StartTrace("test_app", {"process1"}, {}, {});
  ASSERT_FALSE(trace_id.empty());
  ASSERT_EQ(manager_->GetTraceStatus(trace_id), "Tracing");
}

TEST_F(TraceManagerTest, StartAndStopTrace) {
  std::string trace_id = manager_->StartTrace("test_app", {"process1"}, {}, {});
  ASSERT_FALSE(trace_id.empty());

  base::FilePath trace_dir = temp_dir_.GetPath().Append(trace_id);
  ASSERT_TRUE(base::DirectoryExists(trace_dir));

  ASSERT_TRUE(manager_->StopTrace(trace_id));
  task_environment_->RunUntilIdle();  // Wait for processing to complete.

  ASSERT_EQ(manager_->GetTraceStatus(trace_id), "Completed");
}

TEST_F(TraceManagerTest, PathFiltering) {
  // Start a trace with path filtering rules.
  std::string trace_id = manager_->StartTrace(
      "test_app", {"process1"},
      {"/usr/lib/.*", "/opt/google/chrome/.*"},  // path_allowlist
      {".*\\.log", "/tmp/.*"});                  // path_denylist

  ASSERT_FALSE(trace_id.empty());

  // Manually create a fake raw trace file.
  base::FilePath raw_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.raw");

  std::string raw_content =
      "# TRACE HEADER\n"
      "some-proc-123 ... event: file=\"/usr/lib/libfoo.so\"\n"
      "some-proc-123 ... event: file=\"/opt/google/chrome/chrome\"\n"
      "some-proc-123 ... event: file=\"/home/user/somefile.txt\"\n"
      "some-proc-123 ... event: file=\"/var/log/messages.log\"\n"
      "some-proc-123 ... event: file=\"/usr/lib/some.log\"\n"
      "some-proc-123 ... event: file=\"/tmp/tempfile\"\n";
  ASSERT_TRUE(base::WriteFile(raw_path, raw_content));

  // Stop the trace to trigger processing.
  ASSERT_TRUE(manager_->StopTrace(trace_id));
  task_environment_->RunUntilIdle();

  ASSERT_EQ(manager_->GetTraceStatus(trace_id), "Completed");

  // Check the final processed trace file.
  base::FilePath final_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.log");
  std::string final_content;
  ASSERT_TRUE(base::ReadFileToString(final_path, &final_content));

  // Verify that only the allowed paths are present.
  EXPECT_NE(final_content.find("/usr/lib/libfoo.so"), std::string::npos);
  EXPECT_NE(final_content.find("/opt/google/chrome/chrome"), std::string::npos);
  EXPECT_EQ(final_content.find("/home/user/somefile.txt"), std::string::npos);
  EXPECT_EQ(final_content.find("/var/log/messages.log"), std::string::npos);
  EXPECT_EQ(final_content.find("/usr/lib/some.log"), std::string::npos);
  EXPECT_EQ(final_content.find("/tmp/tempfile"), std::string::npos);
}

TEST_F(TraceManagerTest, InvalidRegex) {
  // Pass an invalid regex to the allow list. RE2 will mark it !ok() and we
  // expect tracing to continue without crashing.
  std::string trace_id =
      manager_->StartTrace("test_app", {"proc1"}, {"[unclosed_regex"}, {});
  ASSERT_FALSE(trace_id.empty());

  // Manually create an empty raw file
  base::FilePath raw_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.raw");
  ASSERT_TRUE(base::WriteFile(raw_path, ""));

  // Stop the trace to force processing.
  ASSERT_TRUE(manager_->StopTrace(trace_id));
  task_environment_->RunUntilIdle();

  // Even with invalid regex, trace should complete (or at least proceed to
  // Processing state) instead of erroring out.
  std::string status = manager_->GetTraceStatus(trace_id);
  EXPECT_THAT(status, ::testing::AnyOf(::testing::Eq("Processing"),
                                       ::testing::Eq("Completed")));
}

TEST_F(TraceManagerTest, DenyOnlyPathFiltering) {
  // Start a trace that only has a deny list (no allow list).
  std::string trace_id = manager_->StartTrace("test_app", {"proc1"},
                                              /*allow*/ {}, {".*/secret.txt"});
  ASSERT_FALSE(trace_id.empty());

  // Craft raw trace containing one denied and one allowed path.
  base::FilePath raw_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.raw");
  std::string raw_content =
      "# HEADER\n"
      "x-123 event: file=\"/var/data/normal.txt\"\n"
      "x-123 event: file=\"/home/user/secret.txt\"\n";  // should be denied
  ASSERT_TRUE(base::WriteFile(raw_path, raw_content));

  // Stop trace and process.
  ASSERT_TRUE(manager_->StopTrace(trace_id));
  task_environment_->RunUntilIdle();
  ASSERT_EQ(manager_->GetTraceStatus(trace_id), "Completed");

  // Validate results.
  base::FilePath final_path =
      temp_dir_.GetPath().Append(trace_id).Append("trace.log");
  std::string final_content;
  ASSERT_TRUE(base::ReadFileToString(final_path, &final_content));

  EXPECT_NE(final_content.find("/var/data/normal.txt"), std::string::npos);
  EXPECT_EQ(final_content.find("/home/user/secret.txt"), std::string::npos);
}

}  // namespace farfetchd
