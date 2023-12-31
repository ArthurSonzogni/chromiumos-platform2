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
  CertStoreInstance() = delete;
  CertStoreInstance(const CertStoreInstance&) = delete;
  CertStoreInstance& operator=(const CertStoreInstance&) = delete;

  ~CertStoreInstance() override = default;

  void UpdatePlaceholderKeys(std::vector<mojom::ChromeOsKeyPtr> keys,
                             UpdatePlaceholderKeysCallback callback) override;

 private:
  base::WeakPtr<KeymasterServer> keymaster_server_;

  base::WeakPtrFactory<CertStoreInstance> weak_ptr_factory_{this};
};

}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_CERT_STORE_INSTANCE_H_
