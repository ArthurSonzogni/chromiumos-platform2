// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include <limits>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/flag_helper.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include <grpc++/grpc++.h>

#include "guest.grpc.pb.h"  // NOLINT(build/include)

using std::string;

namespace pb = google::protobuf;

namespace {
bool ParseFileToProto(base::FilePath path, pb::Message* msg) {
  if (!base::PathExists(path)) {
    LOG(ERROR) << path.value() << " does not exist";
    return false;
  }

  base::ScopedFD fd(open(path.value().c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Unable to open file at " << path.value();
    return false;
  }

  pb::io::FileInputStream stream(fd.get());
  return pb::TextFormat::Parse(&stream, msg);
}

bool ConfigureNetwork(vm_tools::Maitred::Stub* stub, base::FilePath path) {
  LOG(INFO) << "Attempting to configure VM network";

  vm_tools::NetworkConfigRequest request;
  if (!ParseFileToProto(path, &request)) {
    LOG(ERROR) << "Unable to parse proto file";
    return false;
  }

  // Make the RPC.
  grpc::ClientContext ctx;
  vm_tools::EmptyMessage empty;

  grpc::Status status = stub->ConfigureNetwork(&ctx, request, &empty);

  if (status.ok()) {
    LOG(INFO) << "Successfully configured network";
  } else {
    LOG(ERROR) << "Failed to configure network: " << status.error_message();
  }

  return true;
}

void Shutdown(vm_tools::Maitred::Stub* stub) {
  LOG(INFO) << "Shutting down VM";

  grpc::ClientContext ctx;
  vm_tools::EmptyMessage empty;

  grpc::Status status = stub->Shutdown(&ctx, empty, &empty);

  if (status.ok()) {
    LOG(INFO) << "Successfully shut down VM";
  } else {
    LOG(ERROR) << "Failed to shut down VM: " << status.error_message();
  }
}

bool LaunchProcess(vm_tools::Maitred::Stub* stub, base::FilePath path) {
  LOG(INFO) << "Attempting to launch process";

  vm_tools::LaunchProcessRequest request;
  if (!ParseFileToProto(path, &request)) {
    LOG(ERROR) << "Unable to parse proto file";
    return false;
  }

  // Make the RPC.
  grpc::ClientContext ctx;
  vm_tools::LaunchProcessResponse response;

  grpc::Status status = stub->LaunchProcess(&ctx, request, &response);
  if (status.ok()) {
    LOG(INFO) << "Successfully launched process " << request.argv()[0];
  } else {
    LOG(ERROR) << "Failed to launch process " << request.argv()[0] << ": "
               << status.error_message();
  }

  if (!status.ok() || !request.wait_for_exit()) {
    return true;
  }

  if (response.reason() == vm_tools::EXITED) {
    LOG(INFO) << "Process exited with status " << response.status();
  } else if (response.reason() == vm_tools::SIGNALED) {
    LOG(INFO) << "Process killed by signal " << response.status();
  } else {
    LOG(WARNING) << "Process exited with unknown status";
  }

  return true;
}
}  // namespace

int main(int argc, char* argv[]) {
  base::AtExitManager at_exit;

  DEFINE_uint64(cid, 0, "Cid of VM");
  DEFINE_uint64(port, 0, "Port number where maitred is listening");
  DEFINE_string(configure_network, "",
                "Path to NetworkConfigRequest text proto file");
  DEFINE_string(launch_process, "",
                "Path to LaunchProcessRequest text proto file");
  DEFINE_bool(shutdown, false, "Shutdown the VM");
  brillo::FlagHelper::Init(argc, argv, "maitred client tool");

  if (FLAGS_cid == 0) {
    LOG(ERROR) << "--cid flag is required";
    return EXIT_FAILURE;
  }
  if (FLAGS_port == 0) {
    LOG(ERROR) << "--port flag is required";
    return EXIT_FAILURE;
  }

  unsigned int cid = FLAGS_cid;
  if (static_cast<uint64_t>(cid) != FLAGS_cid) {
    LOG(ERROR) << "Cid value (" << FLAGS_cid << ") is too large.  Largest "
               << "valid value is " << std::numeric_limits<unsigned int>::max();
    return EXIT_FAILURE;
  }

  unsigned int port = FLAGS_port;
  if (static_cast<uint64_t>(port) != FLAGS_port) {
    LOG(ERROR) << "Port value (" << FLAGS_port << ") is too large.  Largest "
               << "valid value is " << std::numeric_limits<unsigned int>::max();
    return EXIT_FAILURE;
  }

  vm_tools::Maitred::Stub stub(
      grpc::CreateChannel(base::StringPrintf("vsock:%u:%u", cid, port),
                          grpc::InsecureChannelCredentials()));

  bool success = true;
  if (!FLAGS_configure_network.empty()) {
    success = ConfigureNetwork(&stub, base::FilePath(FLAGS_configure_network));
  } else if (!FLAGS_launch_process.empty()) {
    success = LaunchProcess(&stub, base::FilePath(FLAGS_launch_process));
  } else if (FLAGS_shutdown) {
    Shutdown(&stub);
  } else {
    LOG(WARNING) << "No commands specified";
  }

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
