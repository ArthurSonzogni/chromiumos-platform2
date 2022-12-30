// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/tracing.h>

#include <string>
#include <vector>

#include <base/test/task_environment.h>
#include <base/trace_event/trace_log.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <perfetto/perfetto.h>

namespace brillo {

class TracingTest : public ::testing::Test {
 public:
  TracingTest() = default;
  ~TracingTest() override = default;

  void TearDown() override { perfetto::Tracing::ResetForTesting(); }

 private:
  base::test::TaskEnvironment task_environment_;
};

TEST_F(TracingTest, Init) {
  EXPECT_FALSE(perfetto::Tracing::IsInitialized());
  brillo::InitPerfettoTracing();
  EXPECT_TRUE(perfetto::Tracing::IsInitialized());
}

TEST_F(TracingTest, RunLoopTracing) {
  // Use an in-process tracing backend for testing, since the system tracing
  // service won't necessarily be running.
  perfetto::TracingInitArgs init_args;
  init_args.backends = perfetto::BackendType::kInProcessBackend;
  perfetto::Tracing::Initialize(init_args);

  // Also initialize tracing through brillo so libchrome's tracing categories
  // get registered.
  brillo::InitPerfettoTracing();

  perfetto::TraceConfig cfg;
  cfg.add_buffers()->set_size_kb(1024);
  auto* ds_cfg = cfg.add_data_sources()->mutable_config();
  ds_cfg->set_name("track_event");

  auto tracing_session = perfetto::Tracing::NewTrace();
  tracing_session->Setup(cfg);
  tracing_session->StartBlocking();

  // Start and immediately stop a run loop to cause libchrome to emit some trace
  // events (e.g., "RunLoop::Run").
  base::RunLoop run_loop;
  run_loop.RunUntilIdle();

  tracing_session->StopBlocking();
  std::vector<char> trace_data(tracing_session->ReadTraceBlocking());

  // Check that the track event emitted in RunLoop::Run is in the trace.
  std::string trace_str(trace_data.data(), trace_data.size());
  EXPECT_THAT(trace_str, testing::HasSubstr("RunLoop::Run"));
}

}  // namespace brillo
