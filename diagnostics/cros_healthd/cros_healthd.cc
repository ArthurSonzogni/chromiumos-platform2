// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd.h"

#include <cstdlib>
#include <memory>
#include <utility>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/unguessable_token.h>
#include <brillo/udev/udev_monitor.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <dbus/object_path.h>
#include <mojo/public/cpp/platform/platform_channel_endpoint.h>
#include <mojo/public/cpp/system/invitation.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/events/audio_events_impl.h"
#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"
#include "diagnostics/cros_healthd/events/lid_events_impl.h"
#include "diagnostics/cros_healthd/events/power_events_impl.h"
#include "diagnostics/cros_healthd/events/udev_events_impl.h"

namespace diagnostics {

CrosHealthd::CrosHealthd(mojo::PlatformChannelEndpoint endpoint,
                         std::unique_ptr<brillo::UdevMonitor>&& udev_monitor)
    : DBusServiceDaemon(kCrosHealthdServiceName /* service_name */) {
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  context_ = Context::Create(std::move(endpoint), std::move(udev_monitor));
  CHECK(context_) << "Failed to initialize context.";

  fetch_aggregator_ = std::make_unique<FetchAggregator>(context_.get());

  bluetooth_events_ = std::make_unique<BluetoothEventsImpl>(context_.get());

  lid_events_ = std::make_unique<LidEventsImpl>(context_.get());

  power_events_ = std::make_unique<PowerEventsImpl>(context_.get());

  audio_events_ = std::make_unique<AudioEventsImpl>(context_.get());

  udev_events_ = std::make_unique<UdevEventsImpl>(context_.get());

  routine_factory_ =
      std::make_unique<CrosHealthdRoutineFactoryImpl>(context_.get());

  routine_service_ = std::make_unique<CrosHealthdRoutineService>(
      context_.get(), routine_factory_.get());

  mojo_service_ = std::make_unique<CrosHealthdMojoService>(
      context_.get(), fetch_aggregator_.get(), bluetooth_events_.get(),
      lid_events_.get(), power_events_.get(), audio_events_.get(),
      udev_events_.get());

  service_factory_receiver_set_.set_disconnect_handler(
      base::Bind(&CrosHealthd::OnDisconnect, base::Unretained(this)));
}

CrosHealthd::~CrosHealthd() = default;

int CrosHealthd::OnInit() {
  VLOG(0) << "Starting";
  return DBusServiceDaemon::OnInit();
}

void CrosHealthd::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus_,
      dbus::ObjectPath(kCrosHealthdServicePath));
  brillo::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_->AddOrGetInterface(kCrosHealthdServiceInterface);
  DCHECK(dbus_interface);
  dbus_interface->AddSimpleMethodHandler(
      kCrosHealthdBootstrapMojoConnectionMethod, base::Unretained(this),
      &CrosHealthd::BootstrapMojoConnection);
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));
}

std::string CrosHealthd::BootstrapMojoConnection(const base::ScopedFD& mojo_fd,
                                                 bool is_chrome) {
  VLOG(1) << "Received BootstrapMojoConnection D-Bus request";

  if (!mojo_fd.is_valid()) {
    constexpr char kInvalidFileDescriptorError[] =
        "Invalid Mojo file descriptor";
    LOG(ERROR) << kInvalidFileDescriptorError;
    return kInvalidFileDescriptorError;
  }

  // We need a file descriptor that stays alive after the current method
  // finishes, but libbrillo's D-Bus wrappers currently don't support passing
  // base::ScopedFD by value.
  base::ScopedFD mojo_fd_copy(HANDLE_EINTR(dup(mojo_fd.get())));
  if (!mojo_fd_copy.is_valid()) {
    constexpr char kFailedDuplicationError[] =
        "Failed to duplicate the Mojo file descriptor";
    PLOG(ERROR) << kFailedDuplicationError;
    return kFailedDuplicationError;
  }

  if (!base::SetCloseOnExec(mojo_fd_copy.get())) {
    constexpr char kFailedSettingFdCloexec[] =
        "Failed to set FD_CLOEXEC on Mojo file descriptor";
    PLOG(ERROR) << kFailedSettingFdCloexec;
    return kFailedSettingFdCloexec;
  }

  std::string token;

  mojo::PendingReceiver<
      chromeos::cros_healthd::mojom::CrosHealthdServiceFactory>
      receiver;
  if (is_chrome) {
    if (mojo_service_bind_attempted_) {
      // This should not normally be triggered, since the other endpoint - the
      // browser process - should bootstrap the Mojo connection only once, and
      // when that process is killed the Mojo shutdown notification should have
      // been received earlier. But handle this case to be on the safe side.
      // After we restart, the browser process is expected to invoke the
      // bootstrapping again.
      ShutDownDueToMojoError(
          "Repeated Mojo bootstrap request received" /* debug_reason */);
      // It doesn't matter what we return here, this is just to satisfy the
      // compiler. ShutDownDueToMojoError will kill cros_healthd.
      return "";
    }

    // Connect to mojo in the requesting process.
    mojo::IncomingInvitation invitation =
        mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
            mojo::PlatformHandle(std::move(mojo_fd_copy))));
    receiver = mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdServiceFactory>(
        invitation.ExtractMessagePipe(kCrosHealthdMojoConnectionChannelToken));
    mojo_service_bind_attempted_ = true;
  } else {
    // Create a unique token which will allow the requesting process to connect
    // to us via mojo.
    mojo::OutgoingInvitation invitation;
    token = base::UnguessableToken::Create().ToString();
    mojo::ScopedMessagePipeHandle pipe = invitation.AttachMessagePipe(token);

    mojo::OutgoingInvitation::Send(
        std::move(invitation), base::kNullProcessHandle,
        mojo::PlatformChannelEndpoint(
            mojo::PlatformHandle(std::move(mojo_fd_copy))));
    receiver = mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdServiceFactory>(
        std::move(pipe));
  }
  service_factory_receiver_set_.Add(this /* impl */, std::move(receiver),
                                    is_chrome);

  VLOG(1) << "Successfully bootstrapped Mojo connection";
  return token;
}

void CrosHealthd::GetProbeService(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdProbeService> service) {
  mojo_service_->AddProbeReceiver(std::move(service));
}

void CrosHealthd::GetDiagnosticsService(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsService> service) {
  diagnostics_receiver_set_.Add(routine_service_.get(), std::move(service));
}

void CrosHealthd::GetEventService(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdEventService> service) {
  mojo_service_->AddEventReceiver(std::move(service));
}

void CrosHealthd::GetSystemService(
    mojo::PendingReceiver<
        chromeos::cros_healthd::mojom::CrosHealthdSystemService> service) {
  mojo_service_->AddSystemReceiver(std::move(service));
}

void CrosHealthd::SendNetworkHealthService(
    mojo::PendingRemote<chromeos::network_health::mojom::NetworkHealthService>
        remote) {
  context_->network_health_adapter()->SetServiceRemote(std::move(remote));
}

void CrosHealthd::SendNetworkDiagnosticsRoutines(
    mojo::PendingRemote<
        chromeos::network_diagnostics::mojom::NetworkDiagnosticsRoutines>
        network_diagnostics_routines) {
  context_->network_diagnostics_adapter()->SetNetworkDiagnosticsRoutines(
      std::move(network_diagnostics_routines));
}

void CrosHealthd::ShutDownDueToMojoError(const std::string& debug_reason) {
  // Our daemon has to be restarted to be prepared for future Mojo connection
  // bootstraps. We can't do this without a restart since Mojo EDK gives no
  // guarantees it will support repeated bootstraps. Therefore, tear down and
  // exit from our process and let upstart restart us again.
  LOG(ERROR) << "Shutting down due to: " << debug_reason;
  mojo_service_.reset();
  Quit();
}

void CrosHealthd::OnDisconnect() {
  // Only respond to disconnects caused by the browser. All others are
  // recoverable.
  if (service_factory_receiver_set_.current_context())
    ShutDownDueToMojoError("Lost mojo connection to browser.");
}

}  // namespace diagnostics
