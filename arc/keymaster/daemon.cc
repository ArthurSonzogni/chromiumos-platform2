// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/daemon.h"

#include <sysexits.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/task/single_thread_task_runner.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/self_owned_receiver.h>
#include <mojo/public/cpp/system/invitation.h>

#include "arc/keymaster/cert_store_instance.h"
#include "arc/keymaster/keymaster_server.h"

namespace arc {
namespace keymaster {

Daemon::Daemon() : weak_factory_(this) {}
Daemon::~Daemon() = default;

int Daemon::OnInit() {
  int exit_code = brillo::DBusDaemon::OnInit();
  if (exit_code != EX_OK) {
    return exit_code;
  }

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::SingleThreadTaskRunner::GetCurrentDefault(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  LOG(INFO) << "Mojo init succeeded.";

  InitDBus();
  return EX_OK;
}

void Daemon::InitDBus() {
  dbus::ExportedObject* exported_object =
      bus_->GetExportedObject(dbus::ObjectPath(kArcKeymasterServicePath));

  CHECK(exported_object);
  CHECK(exported_object->ExportMethodAndBlock(
      kArcKeymasterInterfaceName, kBootstrapMojoConnectionMethod,
      base::BindRepeating(&Daemon::BootstrapMojoConnection,
                          weak_factory_.GetWeakPtr())));
  CHECK(bus_->RequestOwnershipAndBlock(kArcKeymasterServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY));
  LOG(INFO) << "D-Bus registration succeeded";
}

void Daemon::BootstrapMojoConnection(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  LOG(INFO) << "Receiving bootstrap mojo call from D-Bus client.";

  if (is_bound_) {
    std::string err_mssg = "Trying to instantiate multiple Mojo proxies.";
    LOG(WARNING) << err_mssg;
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(method_call, DBUS_ERROR_FAILED,
                                                 err_mssg));
    return;
  }

  base::ScopedFD file_handle;
  dbus::MessageReader reader(method_call);

  if (!reader.PopFileDescriptor(&file_handle)) {
    std::string err_mssg = "Couldn't extract Mojo IPC handle.";
    LOG(ERROR) << err_mssg;
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(method_call, DBUS_ERROR_FAILED,
                                                 err_mssg));
    return;
  }

  if (!file_handle.is_valid()) {
    std::string err_mssg = "Couldn't get file handle sent over D-Bus.";
    LOG(ERROR) << err_mssg;
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(method_call, DBUS_ERROR_FAILED,
                                                 err_mssg));
    return;
  }

  if (!base::SetCloseOnExec(file_handle.get())) {
    std::string err_mssg = "Failed setting FD_CLOEXEC on fd.";
    PLOG(ERROR) << err_mssg;
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(method_call, DBUS_ERROR_FAILED,
                                                 err_mssg));
    return;
  }

  AcceptProxyConnection(std::move(file_handle));
  LOG(INFO) << "Mojo connection established.";
  std::move(response_sender).Run(dbus::Response::FromMethodCall(method_call));
}

void Daemon::AcceptProxyConnection(base::ScopedFD fd) {
#if defined(ENABLE_IPCZ_ON_CHROMEOS)
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannelEndpoint(mojo::PlatformHandle(std::move(fd))),
      MOJO_ACCEPT_INVITATION_FLAG_INHERIT_BROKER);
#else
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannelEndpoint(mojo::PlatformHandle(std::move(fd))));
#endif

  auto keymaster_server = std::make_unique<KeymasterServer>();
  auto cert_store_instance =
      std::make_unique<CertStoreInstance>(keymaster_server->GetWeakPtr());

  {
    mojo::ScopedMessagePipeHandle child_pipe;
    if (mojo::core::IsMojoIpczEnabled()) {
      constexpr uint64_t kKeymasterPipeAttachment = 0;
      child_pipe = invitation.ExtractMessagePipe(kKeymasterPipeAttachment);
    } else {
      child_pipe = invitation.ExtractMessagePipe("arc-keymaster-pipe");
    }
    if (!child_pipe.is_valid()) {
      LOG(ERROR) << "Could not extract KeymasterServer pipe.";
      return;
    }
    mojo::MakeSelfOwnedReceiver(
        std::move(keymaster_server),
        mojo::PendingReceiver<arc::mojom::KeymasterServer>(
            std::move(child_pipe)));
  }
  {
    mojo::ScopedMessagePipeHandle child_pipe;
    if (mojo::core::IsMojoIpczEnabled()) {
      constexpr uint64_t kCertStorePipeAttachment = 1;
      child_pipe = invitation.ExtractMessagePipe(kCertStorePipeAttachment);
    } else {
      child_pipe = invitation.ExtractMessagePipe("arc-cert-store-pipe");
    }

    // TODO(b/147573396): remove strong binding to be able to use cert store.
    if (!child_pipe.is_valid()) {
      LOG(ERROR) << "Could not extract CertStoreInstance pipe.";
      return;
    }
    mojo::MakeSelfOwnedReceiver(
        std::move(cert_store_instance),
        mojo::PendingReceiver<arc::keymaster::mojom::CertStoreInstance>(
            std::move(child_pipe)));
  }
  is_bound_ = true;
}

}  // namespace keymaster
}  // namespace arc
