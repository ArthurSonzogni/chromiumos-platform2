// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/at_exit.h>
#include <base/check.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/message_loop/message_pump_type.h>
#include <base/run_loop.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/single_thread_task_executor.h>
#include <base/task/thread_pool.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_writer.h>

#include "vm_tools/concierge/service.h"

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  brillo::FlagHelper::Init(argc, argv, "vm_concierge service");

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("vm_concierge");
  scoped_refptr<base::SequencedTaskRunner> sequenced_task_runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
  scoped_refptr<AsynchronousMetricsWriter> metrics_writer =
      base::MakeRefCounted<AsynchronousMetricsWriter>(sequenced_task_runner,
                                                      false);

  if (argc != 1) {
    LOG(ERROR) << "Unexpected command line arguments";
    return EXIT_FAILURE;
  }

  base::RunLoop run_loop;

  auto service = vm_tools::concierge::Service::Create(
      run_loop.QuitClosure(), std::make_unique<MetricsLibrary>(metrics_writer));
  CHECK(service);

  run_loop.Run();

  metrics_writer->WaitUntilFlushed();

  return 0;
}
