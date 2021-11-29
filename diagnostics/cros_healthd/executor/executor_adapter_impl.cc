// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/executor/executor_adapter_impl.h"

#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

#include "diagnostics/cros_healthd/executor/executor_constants.h"

namespace diagnostics {

namespace {

namespace executor_ipc = ::chromeos::cros_healthd_executor::mojom;

}  // namespace

ExecutorAdapterImpl::ExecutorAdapterImpl() = default;
ExecutorAdapterImpl::~ExecutorAdapterImpl() = default;

void ExecutorAdapterImpl::Connect(mojo::PlatformChannelEndpoint endpoint) {
  DCHECK(endpoint.is_valid());

  mojo::OutgoingInvitation invitation;
  // Attach a message pipe to be extracted by the receiver. The other end of the
  // pipe is returned for us to use locally.
  mojo::ScopedMessagePipeHandle pipe =
      invitation.AttachMessagePipe(kExecutorPipeName);

  executor_.Bind(
      executor_ipc::ExecutorPtrInfo(std::move(pipe), 0u /* version */));

  mojo::OutgoingInvitation::Send(std::move(invitation),
                                 base::kNullProcessHandle, std::move(endpoint));
}

void ExecutorAdapterImpl::GetFanSpeed(Executor::GetFanSpeedCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetFanSpeed(std::move(callback));
}

void ExecutorAdapterImpl::GetInterfaces(
    Executor::GetInterfacesCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetInterfaces(std::move(callback));
}

void ExecutorAdapterImpl::GetLink(const std::string& interface_name,
                                  Executor::GetLinkCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetLink(interface_name, std::move(callback));
}

void ExecutorAdapterImpl::GetInfo(const std::string& interface_name,
                                  Executor::GetInfoCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetInfo(interface_name, std::move(callback));
}

void ExecutorAdapterImpl::GetScanDump(const std::string& interface_name,
                                      Executor::GetScanDumpCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetScanDump(interface_name, std::move(callback));
}

void ExecutorAdapterImpl::RunMemtester(
    Executor::RunMemtesterCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->RunMemtester(std::move(callback));
}

void ExecutorAdapterImpl::KillMemtester() {
  DCHECK(executor_.is_bound());

  executor_->KillMemtester();
}

void ExecutorAdapterImpl::GetProcessIOContents(
    const pid_t pid, Executor::GetProcessIOContentsCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetProcessIOContents(base::checked_cast<uint32_t>(pid),
                                  std::move(callback));
}

void ExecutorAdapterImpl::RunModetest(executor_ipc::ModetestOptionEnum option,
                                      Executor::RunModetestCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->RunModetest(option, std::move(callback));
}

void ExecutorAdapterImpl::ReadMsr(const uint32_t msr_reg,
                                  Executor::ReadMsrCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->ReadMsr(base::checked_cast<uint32_t>(msr_reg),
                     std::move(callback));
}

void ExecutorAdapterImpl::GetUEFISecureBootContent(
    Executor::GetUEFISecureBootContentCallback callback) {
  DCHECK(executor_.is_bound());

  executor_->GetUEFISecureBootContent(std::move(callback));
}

}  // namespace diagnostics
