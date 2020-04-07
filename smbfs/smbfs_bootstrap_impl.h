// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_SMBFS_BOOTSTRAP_IMPL_H_
#define SMBFS_SMBFS_BOOTSTRAP_IMPL_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/callback.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "smbfs/mojom/smbfs.mojom.h"
#include "smbfs/smb_filesystem.h"

namespace smbfs {

class Filesystem;
struct SmbCredential;

// Implements mojom::SmbFsBootstrap to mount an SMB share.
class SmbFsBootstrapImpl : public mojom::SmbFsBootstrap {
 public:
  // Delegate interface used for actions that need to persist after the
  // bootstrap process has completed.
  class Delegate {
   public:
    // Sets up Kerberos authentication.
    virtual void SetupKerberos(
        mojom::KerberosConfigPtr kerberos_config,
        base::OnceCallback<void(bool success)> callback) = 0;
  };

  // Factory function to create an SmbFilesystem instance.
  using SmbFilesystemFactory =
      base::RepeatingCallback<std::unique_ptr<SmbFilesystem>(
          SmbFilesystem::Options)>;

  using BootstrapCompleteCallback =
      base::OnceCallback<void(std::unique_ptr<SmbFilesystem> fs,
                              mojom::SmbFsRequest smbfs_request,
                              mojom::SmbFsDelegatePtr delegate_ptr)>;

  SmbFsBootstrapImpl(mojom::SmbFsBootstrapRequest request,
                     SmbFilesystemFactory smb_filesystem_factory,
                     Delegate* delegate);
  ~SmbFsBootstrapImpl() override;

  // Start the bootstrap process and run |callback| when finished or the Mojo
  // channel is disconnected. If the bootstrap process completed successfully,
  // |callback| will be called with a valid SmbFilesystem object. If the Mojo
  // channel is disconnected, |callback| will be run with nullptr.
  void Start(BootstrapCompleteCallback callback);

 private:
  // mojom::SmbFsBootstrap overrides.
  void MountShare(mojom::MountOptionsPtr options,
                  mojom::SmbFsDelegatePtr smbfs_delegate,
                  const MountShareCallback& callback) override;

  // Callback to continue MountShare after setting up credentials
  // (username/password, or kerberos).
  void OnCredentialsSetup(mojom::MountOptionsPtr options,
                          mojom::SmbFsDelegatePtr smbfs_delegate,
                          const MountShareCallback& callback,
                          std::unique_ptr<SmbCredential> credential,
                          bool use_kerberos,
                          bool setup_success);

  // Mojo connection error handler.
  void OnMojoConnectionError();

  mojo::Binding<mojom::SmbFsBootstrap> binding_;
  base::OnceClosure disconnect_callback_;

  const SmbFilesystemFactory smb_filesystem_factory_;
  Delegate* const delegate_;
  BootstrapCompleteCallback completion_callback_;

  DISALLOW_COPY_AND_ASSIGN(SmbFsBootstrapImpl);
};

}  // namespace smbfs

#endif  // SMBFS_SMBFS_BOOTSTRAP_IMPL_H_
