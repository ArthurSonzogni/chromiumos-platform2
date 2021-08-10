// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbprovider/fake_kerberos_artifact_client.h"

#include <base/check.h>
#include <base/logging.h>
#include <dbus/authpolicy/dbus-constants.h>

namespace smbprovider {

FakeKerberosArtifactClient::FakeKerberosArtifactClient() = default;

void FakeKerberosArtifactClient::GetUserKerberosFiles(
    const std::string& object_guid, GetUserKerberosFilesCallback callback) {
  ++call_count_;

  if (!kerberos_files_map_.count(object_guid)) {
    LOG(ERROR) << "FakeKerberosArtifactClient: No Kerberos Files found";
    std::move(callback).Run(false, std::string(), std::string());
    return;
  }

  const authpolicy::KerberosFiles& files = kerberos_files_map_[object_guid];
  bool success = files.has_krb5cc() && files.has_krb5conf();
  std::move(callback).Run(success, files.krb5cc(), files.krb5conf());
}

void FakeKerberosArtifactClient::ConnectToKerberosFilesChangedSignal(
    dbus::ObjectProxy::SignalCallback signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) {
  signal_callback_ = std::move(signal_callback);

  std::move(on_connected_callback)
      .Run(authpolicy::kAuthPolicyInterface,
           authpolicy::kUserKerberosFilesChangedSignal, true /* success */);
}

void FakeKerberosArtifactClient::FireSignal() {
  DCHECK(IsConnected());

  dbus::Signal signal_to_send(authpolicy::kAuthPolicyInterface,
                              authpolicy::kUserKerberosFilesChangedSignal);

  signal_callback_.Run(&signal_to_send);
}

bool FakeKerberosArtifactClient::IsConnected() const {
  return !signal_callback_.is_null();
}

uint32_t FakeKerberosArtifactClient::GetFilesMethodCallCount() const {
  return call_count_;
}

void FakeKerberosArtifactClient::AddKerberosFiles(
    const std::string& account_guid,
    const authpolicy::KerberosFiles& kerberos_files) {
  kerberos_files_map_[account_guid] = kerberos_files;
}

void FakeKerberosArtifactClient::ResetKerberosFiles() {
  kerberos_files_map_.clear();
}

}  // namespace smbprovider
