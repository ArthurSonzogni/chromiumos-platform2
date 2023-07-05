// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/fake_kerberos_artifact_client.h"

#include <base/check.h>
#include <base/logging.h>
#include <dbus/kerberos/dbus-constants.h>
#include <kerberos/proto_bindings/kerberos_service.pb.h>

namespace smbfs {

FakeKerberosArtifactClient::FakeKerberosArtifactClient() = default;

void FakeKerberosArtifactClient::GetKerberosFiles(
    const std::string& principal_name, GetKerberosFilesCallback callback) {
  ++call_count_;

  if (!kerberos_files_map_.count(principal_name)) {
    LOG(ERROR) << "FakeKerberosArtifactClient: No Kerberos Files found";
    std::move(callback).Run(/*success=*/false, "", "");
    return;
  }

  const kerberos::KerberosFiles& files = kerberos_files_map_[principal_name];
  std::move(callback).Run(/*success=*/true, files.krb5cc(), files.krb5conf());
}

void FakeKerberosArtifactClient::ConnectToKerberosFilesChangedSignal(
    dbus::ObjectProxy::SignalCallback signal_callback,
    dbus::ObjectProxy::OnConnectedCallback on_connected_callback) {
  signal_callback_ = std::move(signal_callback);

  std::move(on_connected_callback)
      .Run(kerberos::kKerberosInterface, kerberos::kKerberosFilesChangedSignal,
           /*success=*/true);
}

void FakeKerberosArtifactClient::FireSignal() {
  DCHECK(IsConnected());

  dbus::Signal signal_to_send(kerberos::kKerberosInterface,
                              kerberos::kKerberosFilesChangedSignal);

  signal_callback_.Run(&signal_to_send);
}

bool FakeKerberosArtifactClient::IsConnected() const {
  return !signal_callback_.is_null();
}

uint32_t FakeKerberosArtifactClient::GetFilesMethodCallCount() const {
  return call_count_;
}

void FakeKerberosArtifactClient::AddKerberosFiles(
    const std::string& principal_name,
    const kerberos::KerberosFiles& kerberos_files) {
  kerberos_files_map_[principal_name] = kerberos_files;
}

void FakeKerberosArtifactClient::ResetKerberosFiles() {
  kerberos_files_map_.clear();
}

}  // namespace smbfs
