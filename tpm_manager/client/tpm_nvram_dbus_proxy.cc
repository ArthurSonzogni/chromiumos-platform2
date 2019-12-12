// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/client/tpm_nvram_dbus_proxy.h"

#include <base/bind.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

#include "tpm_manager/common/tpm_nvram_dbus_interface.h"

namespace {

// Use a two minute timeout because TPM operations can take a long time.
const int kDBusTimeoutMS = 2 * 60 * 1000;

}  // namespace

namespace tpm_manager {

TpmNvramDBusProxy::~TpmNvramDBusProxy() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

bool TpmNvramDBusProxy::Initialize() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  object_proxy_ = bus_->GetObjectProxy(
      tpm_manager::kTpmManagerServiceName,
      dbus::ObjectPath(tpm_manager::kTpmManagerServicePath));
  return (object_proxy_ != nullptr);
}

void TpmNvramDBusProxy::DefineSpace(const DefineSpaceRequest& request,
                                    const DefineSpaceCallback& callback) {
  CallMethod<DefineSpaceReply>(tpm_manager::kDefineSpace, request, callback);
}

void TpmNvramDBusProxy::DestroySpace(const DestroySpaceRequest& request,
                                     const DestroySpaceCallback& callback) {
  CallMethod<DestroySpaceReply>(tpm_manager::kDestroySpace, request, callback);
}

void TpmNvramDBusProxy::WriteSpace(const WriteSpaceRequest& request,
                                   const WriteSpaceCallback& callback) {
  CallMethod<WriteSpaceReply>(tpm_manager::kWriteSpace, request, callback);
}

void TpmNvramDBusProxy::ReadSpace(const ReadSpaceRequest& request,
                                  const ReadSpaceCallback& callback) {
  CallMethod<ReadSpaceReply>(tpm_manager::kReadSpace, request, callback);
}

void TpmNvramDBusProxy::LockSpace(const LockSpaceRequest& request,
                                  const LockSpaceCallback& callback) {
  CallMethod<LockSpaceReply>(tpm_manager::kLockSpace, request, callback);
}

void TpmNvramDBusProxy::ListSpaces(const ListSpacesRequest& request,
                                   const ListSpacesCallback& callback) {
  CallMethod<ListSpacesReply>(tpm_manager::kListSpaces, request, callback);
}

void TpmNvramDBusProxy::GetSpaceInfo(const GetSpaceInfoRequest& request,
                                     const GetSpaceInfoCallback& callback) {
  CallMethod<GetSpaceInfoReply>(tpm_manager::kGetSpaceInfo, request, callback);
}

template <typename ReplyProtobufType,
          typename RequestProtobufType,
          typename CallbackType>
void TpmNvramDBusProxy::CallMethod(const std::string& method_name,
                                   const RequestProtobufType& request,
                                   const CallbackType& callback) {
  auto on_error = [](CallbackType callback, brillo::Error* error) {
    ReplyProtobufType reply;
    reply.set_result(NVRAM_RESULT_IPC_ERROR);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, tpm_manager::kTpmNvramInterface,
      method_name, callback, base::Bind(on_error, callback), request);
}

}  // namespace tpm_manager
