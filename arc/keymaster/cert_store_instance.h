// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMASTER_CERT_STORE_INSTANCE_H_
#define ARC_KEYMASTER_CERT_STORE_INSTANCE_H_

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <mojo/cert_store.mojom.h>

namespace arc {
namespace keymaster {

// Provides access to key pairs accessible from Chrome.
class CertStoreInstance : public mojom::CertStoreInstance {
 public:
  CertStoreInstance() = default;

  ~CertStoreInstance() override = default;

  // mojom::CertStoreInstance overrides.
  void Init(mojom::CertStoreHostPtr host_ptr, InitCallback callback) override;

 private:
  // arc::mojom::CertStoreHost access methods.
  void RequestSecurityTokenOperation();

  void ResetSecurityTokenOperationProxy();
  void OnSecurityTokenOperationProxyReady();

  mojom::CertStoreHostPtr host_ptr_;
  // Use as proxy only when initialized:
  // |is_security_token_operation_proxy_ready_| is true.
  mojom::SecurityTokenOperationPtr security_token_operation_proxy_;
  bool is_security_token_operation_proxy_ready_ = false;

  base::WeakPtrFactory<CertStoreInstance> weak_ptr_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(CertStoreInstance);
};

}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_CERT_STORE_INSTANCE_H_
