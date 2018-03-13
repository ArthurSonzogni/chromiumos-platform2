// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <memory>
#include <string>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/scoped_file.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread.h>
#include <grpc++/grpc++.h>

#include "vm_tools/common/constants.h"
#include "vm_tools/maitred/init.h"
#include "vm_tools/maitred/service_impl.h"

#include "vm_host.grpc.pb.h"  // NOLINT(build/include)

using std::string;

namespace {

// Path to logging file.
constexpr char kDevKmsg[] = "/dev/kmsg";

// Prefix inserted before every log message.
constexpr char kLogPrefix[] = "maitred: ";

// File descriptor that points to /dev/kmsg.  Needs to be a global variable
// because logging::LogMessageHandlerFunction is just a function pointer so we
// can't bind any variables to it via base::Bind.
int g_kmsg_fd = -1;

bool LogToKmsg(logging::LogSeverity severity,
               const char* file,
               int line,
               size_t message_start,
               const string& message) {
  DCHECK_NE(g_kmsg_fd, -1);

  const char* priority = nullptr;
  switch (severity) {
    case logging::LOG_VERBOSE:
      priority = "<7>";
      break;
    case logging::LOG_INFO:
      priority = "<6>";
      break;
    case logging::LOG_WARNING:
      priority = "<4>";
      break;
    case logging::LOG_ERROR:
      priority = "<3>";
      break;
    case logging::LOG_FATAL:
      priority = "<2>";
      break;
    default:
      priority = "<5>";
      break;
  }

  const struct iovec iovs[] = {
      {
          .iov_base = static_cast<void*>(const_cast<char*>(priority)),
          .iov_len = strlen(priority),
      },
      {
          .iov_base = static_cast<void*>(const_cast<char*>(kLogPrefix)),
          .iov_len = sizeof(kLogPrefix) - 1,
      },
      {
          .iov_base = static_cast<void*>(
              const_cast<char*>(message.c_str() + message_start)),
          .iov_len = message.length() - message_start,
      },
  };

  ssize_t count = 0;
  for (const struct iovec& iov : iovs) {
    count += iov.iov_len;
  }

  ssize_t ret = HANDLE_EINTR(
      writev(g_kmsg_fd, iovs, sizeof(iovs) / sizeof(struct iovec)));

  // Even if the write wasn't successful, we can't log anything here because
  // this _is_ the logging function.  Just return whether the write succeeded.
  return ret == count;
}

}  // namespace

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  logging::InitLogging(logging::LoggingSettings());

  // Make sure that stdio is set up correctly.
  for (int fd = 0; fd < 3; ++fd) {
    if (fcntl(fd, F_GETFD) >= 0) {
      continue;
    }

    CHECK_EQ(errno, EBADF);

    int newfd = open("/dev/null", O_RDWR);
    CHECK_EQ(fd, newfd);
  }

  // Set up logging to /dev/kmsg.
  base::ScopedFD kmsg_fd(open(kDevKmsg, O_WRONLY | O_CLOEXEC));
  PCHECK(kmsg_fd.is_valid()) << "Failed to open " << kDevKmsg;

  g_kmsg_fd = kmsg_fd.get();
  logging::SetLogMessageHandler(LogToKmsg);

  // Do init setup if we are running as init.
  std::unique_ptr<vm_tools::maitred::Init> init;
  if (strcmp(program_invocation_short_name, "init") == 0) {
    init = vm_tools::maitred::Init::Create();
    CHECK(init);
  }

  // Build the server.
  grpc::ServerBuilder builder;
  builder.AddListeningPort(
      base::StringPrintf("vsock:%u:%u", VMADDR_CID_ANY, vm_tools::kMaitredPort),
      grpc::InsecureServerCredentials());

  vm_tools::maitred::ServiceImpl maitred_service(std::move(init));
  builder.RegisterService(&maitred_service);

  std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
  CHECK(server);

  // Due to restrictions in the gRPC API, there is no way to stop a server from
  // the same thread on which it is running.  It has to be stopped from a
  // different thread.  So we spawn a new thread here that sits around doing
  // nothing and give the maitre'd service a callback, which it will run when it
  // receives a Shutdown rpc.  This callback will post a task to the idle thread
  // to stop the gRPC server.  Once the server is stopped, it will return from
  // the Wait() call below and we can shut down the whole system by issuing a
  // reboot().
  base::Thread shutdown_thread("shutdown thread");
  CHECK(shutdown_thread.Start());

  // The following line is very confusing but is equivalent to this code:
  //
  // maitred_service.set_shutdown_cb(base::Bind(
  //     [](scoped_refptr<base::SingleThreadTaskRunner> runner,
  //        grpc::Server* server) {
  //       runner->PostTask(
  //           FROM_HERE,
  //           base::Bind([](grpc::Server* s) { s->Shutdown(); }, server));
  //     },
  //     shutdown_thread.task_runner(), server.get()));
  //
  // Admittedly, that's not much better but the only other option is to move
  // the code into a separate function, which would break up the flow of logic
  // and be arguably less readable than this code + comment.
  //
  // Once base::Bind in chrome os has been updated to handle lambdas, we should
  // consider replacing this with the above code instead.
  maitred_service.set_shutdown_cb(base::Bind(
      &base::TaskRunner::PostTask, shutdown_thread.task_runner(), FROM_HERE,
      base::Bind(
          static_cast<void (grpc::Server::*)(void)>(&grpc::Server::Shutdown),
          base::Unretained(server.get()))));

  LOG(INFO) << "Server listening on port " << vm_tools::kMaitredPort;

  // Notify the host system that we are ready.
  vm_tools::StartupListener::Stub stub(
      grpc::CreateChannel(base::StringPrintf("vsock:%u:%u", VMADDR_CID_HOST,
                                             vm_tools::kStartupListenerPort),
                          grpc::InsecureChannelCredentials()));
  grpc::ClientContext ctx;
  vm_tools::EmptyMessage empty;
  grpc::Status status = stub.VmReady(&ctx, empty, &empty);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to notify host system that VM is ready: "
                 << status.error_message();
  }

  // The following call will return once the server has been stopped.
  server->Wait();

  LOG(INFO) << "Shutting down system NOW";

  reboot(RB_AUTOBOOT);

  return 0;
}
