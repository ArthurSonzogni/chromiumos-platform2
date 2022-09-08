// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMASTER_CERT_STORE_INSTANCE_H_
#define ARC_KEYMASTER_CERT_STORE_INSTANCE_H_

#include <vector>

#include <base/memory/weak_ptr.h>
#include <mojo/cert_store.mojom.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "arc/keymaster/keymaster_server.h"

namespace arc {
namespace keymaster {

// Provides access to key pairs accessible from Chrome.
class CertStoreInstance : public mojom::CertStoreInstance {
 public:
  explicit CertStoreInstance(base::WeakPtr<KeymasterServer> keymaster_server);
  CertStoreInstance(const CertStoreInstance&) = delete;
  CertStoreInstance& operator=(const CertStoreInstance&) = delete;

  ~CertStoreInstance() override = default;

  // mojom::CertStoreInstance overrides.
  void Init(mojo::PendingRemote<mojom::CertStoreHost> host,
            InitCallback callback) override;

  void UpdatePlaceholderKeys(std::vector<mojom::ChromeOsKeyPtr> keys,
                             UpdatePlaceholderKeysCallback callback) override;

 private:
  // arc::mojom::CertStoreHost access methods.
  void RequestSecurityTokenOperation();

  void ResetSecurityTokenOperationProxy();
  void OnSecurityTokenOperationProxyReady();

  mojo::Remote<mojom::CertStoreHost> host_;
  // Use as proxy only when initialized:
  // |is_security_token_operation_proxy_ready_| is true.
  mojo::Remote<mojom::SecurityTokenOperation> security_token_operation_proxy_;
  bool is_security_token_operation_proxy_ready_ = false;

  base::WeakPtr<KeymasterServer> keymaster_server_;

  base::WeakPtrFactory<CertStoreInstance> weak_ptr_factory_{this};
};

}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_CERT_STORE_INSTANCE_H_
