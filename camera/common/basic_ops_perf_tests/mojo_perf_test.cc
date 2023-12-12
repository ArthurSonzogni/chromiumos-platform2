/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "camera/common/basic_ops_perf_tests/mojo_perf_test.h"

#include <cstdint>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <base/threading/thread.h>
#include <benchmark/benchmark.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

namespace {

constexpr char kMessagePipeName[] = "mojo_perf_test";

using cros::mojom::MojoPerfTest;

mojo::Remote<MojoPerfTest> g_perf_test;

base::Process SetUpRemoteAndGetChild() {
  mojo::PlatformChannel channel;
  mojo::OutgoingInvitation invitation;
  mojo::ScopedMessagePipeHandle pipe =
      invitation.AttachMessagePipe(kMessagePipeName);

  base::LaunchOptions options;
  base::CommandLine command_line(
      base::CommandLine::ForCurrentProcess()->GetProgram());
  channel.PrepareToPassRemoteEndpoint(&options, &command_line);
  base::Process child_process = base::LaunchProcess(command_line, options);
  CHECK(child_process.IsValid()) << "Failed launching subprocess";
  channel.RemoteProcessLaunchAttempted();

  mojo::OutgoingInvitation::Send(std::move(invitation), child_process.Handle(),
                                 channel.TakeLocalEndpoint());
  g_perf_test = mojo::Remote<MojoPerfTest>(
      mojo::PendingRemote<MojoPerfTest>(std::move(pipe), 0));
  return child_process;
}

mojo::PendingReceiver<MojoPerfTest> GetPendingReceiver() {
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannel::RecoverPassedEndpointFromCommandLine(
          *base::CommandLine::ForCurrentProcess()));
  mojo::ScopedMessagePipeHandle pipe =
      invitation.ExtractMessagePipe(kMessagePipeName);
  return mojo::PendingReceiver<MojoPerfTest>(std::move(pipe));
}

}  // namespace

namespace cros::tests {

MojoPerfTestImpl::MojoPerfTestImpl(mojo::PendingReceiver<MojoPerfTest> receiver,
                                   base::OnceClosure disconnect_handler)
    : receiver_(this, std::move(receiver)) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

void MojoPerfTestImpl::CallWithBuffer(const std::vector<uint8_t>&,
                                      CallWithBufferCallback callback) {
  std::move(callback).Run();
}

static void BM_CallWithBuffer(benchmark::State& state) {
  std::vector<uint8_t> buf(state.range(0));
  for (auto _ : state) {
    base::RunLoop run_loop;
    g_perf_test->CallWithBuffer(buf, run_loop.QuitClosure());
    run_loop.Run();
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
}

// Test with buffer sizes ranging from 1B to 16MB
BENCHMARK(BM_CallWithBuffer)
    ->UseRealTime()
    ->RangeMultiplier(16)
    ->Range(1, 16 * 1024 * 1024)
    ->Unit(benchmark::kMillisecond);

}  // namespace cros::tests

// Use our own main function instead of BENCHMARK_MAIN() because we want to
// setup mojo connection first.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  base::CommandLine::Init(argc, argv);
  base::SingleThreadTaskExecutor main_task_executor;

  mojo::core::Init();
  base::Thread ipc_thread("ipc_thread");
  ipc_thread.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));

  mojo::core::ScopedIPCSupport ipc_support(
      ipc_thread.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

  if (mojo::PlatformChannel::CommandLineHasPassedEndpoint(
          *base::CommandLine::ForCurrentProcess())) {
    auto receiver = GetPendingReceiver();

    base::RunLoop run_loop;
    auto test_impl = cros::tests::MojoPerfTestImpl(std::move(receiver),
                                                   run_loop.QuitClosure());
    run_loop.Run();
    return 0;
  }

  base::Process child = SetUpRemoteAndGetChild();
  g_perf_test.FlushForTesting();
  CHECK(g_perf_test.is_connected()) << "Cannot connect to child process";

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  g_perf_test.reset();
  CHECK(child.WaitForExit(nullptr));
  return 0;
}
