// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/daemon.h"

#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/memory/ptr_util.h>
#include <base/memory/ref_counted.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/edk/embedder/process_delegate.h>

#include "ml/machine_learning_service_impl.h"

namespace ml {

namespace {

// A ProcessDelegate that does nothing upon IPC system shutdown.
class DoNothingProcessDelegate : public mojo::edk::ProcessDelegate {
 public:
  void OnShutdownComplete() override {}
};

void InitMojo() {
  mojo::edk::Init();
  static DoNothingProcessDelegate process_delegate;
  mojo::edk::InitIPCSupport(&process_delegate,
                            base::ThreadTaskRunnerHandle::Get());
}

}  // namespace

Daemon::Daemon() : weak_ptr_factory_(this) {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  InitMojo();
  InitDBus();

  return 0;
}

void Daemon::InitDBus() {
  // Get or create the ExportedObject for the ML service.
  dbus::ExportedObject* const ml_service_exported_object =
      bus_->GetExportedObject(dbus::ObjectPath(kMachineLearningServicePath));
  CHECK(ml_service_exported_object);

  // Register a handler of the BootstrapMojoConnection method.
  CHECK(ml_service_exported_object->ExportMethodAndBlock(
      kMachineLearningInterfaceName, kBootstrapMojoConnectionMethod,
      base::Bind(&Daemon::BootstrapMojoConnection,
                 weak_ptr_factory_.GetWeakPtr())));

  // Take ownership of the ML service.
  CHECK(bus_->RequestOwnershipAndBlock(kMachineLearningServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));
}

void Daemon::BootstrapMojoConnection(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  if (machine_learning_service_) {
    LOG(ERROR) << "MachineLearningService already instantiated";
    response_sender.Run(dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_FAILED, "Bootstrap already completed"));
    return;
  }

  base::ScopedFD file_handle;
  dbus::MessageReader reader(method_call);

  if (!reader.PopFileDescriptor(&file_handle)) {
    LOG(ERROR) << "Couldn't extract file descriptor from D-Bus call";
    response_sender.Run(dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_INVALID_ARGS, "Expected file descriptor"));
    return;
  }

  if (!file_handle.is_valid()) {
    LOG(ERROR) << "ScopedFD extracted from D-Bus call was invalid (i.e. empty)";
    response_sender.Run(dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_INVALID_ARGS,
        "Invalid (empty) file descriptor"));
    return;
  }

  if (!base::SetCloseOnExec(file_handle.get())) {
    PLOG(ERROR) << "Failed setting FD_CLOEXEC on file descriptor";
    response_sender.Run(dbus::ErrorResponse::FromMethodCall(
        method_call, DBUS_ERROR_FAILED,
        "Failed setting FD_CLOEXEC on file descriptor"));
    return;
  }

  // Connect to mojo in the requesting process.
  mojo::edk::SetParentPipeHandle(mojo::edk::ScopedPlatformHandle(
      mojo::edk::PlatformHandle(file_handle.release())));

  // Bind primordial message pipe to a MachineLearningService implementation.
  machine_learning_service_ = base::MakeUnique<MachineLearningServiceImpl>(
      mojo::edk::CreateChildMessagePipe(kBootstrapMojoConnectionChannelToken),
      base::Bind(&Daemon::OnConnectionError, base::Unretained(this)));

  // Send success response.
  response_sender.Run(dbus::Response::FromMethodCall(method_call));
}

void Daemon::OnConnectionError() {
  // Die upon Mojo error. Reconnection can occur when the daemon is restarted.
  // (A future Mojo API may enable Mojo re-bootstrap without a process restart.)
  Quit();
}

}  // namespace ml
