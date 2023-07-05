// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_KERBEROS_ARTIFACT_CLIENT_INTERFACE_H_
#define SMBFS_KERBEROS_ARTIFACT_CLIENT_INTERFACE_H_

#include <string>

#include <base/functional/callback.h>
#include <dbus/object_proxy.h>

namespace smbfs {

class KerberosArtifactClientInterface {
 public:
  using GetKerberosFilesCallback =
      base::OnceCallback<void(bool success,
                              const std::string& krb5_ccache_data,
                              const std::string& krb5_conf_data)>;

  KerberosArtifactClientInterface() = default;
  virtual ~KerberosArtifactClientInterface() = default;

  KerberosArtifactClientInterface(const KerberosArtifactClientInterface&) =
      delete;
  KerberosArtifactClientInterface& operator=(
      const KerberosArtifactClientInterface&) = delete;

  // Gets Kerberos files for the user determined by `principal_name`. The files
  // come from kerberosd and they are the credential cache and the krb5 config
  // files.
  virtual void GetKerberosFiles(const std::string& principal_name,
                                GetKerberosFilesCallback callback) = 0;

  // Connects callbacks to OnKerberosFilesChanged D-Bus signal.
  virtual void ConnectToKerberosFilesChangedSignal(
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;
};

}  // namespace smbfs

#endif  // SMBFS_KERBEROS_ARTIFACT_CLIENT_INTERFACE_H_
