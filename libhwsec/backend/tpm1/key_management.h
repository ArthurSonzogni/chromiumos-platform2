// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_KEY_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_TPM1_KEY_MANAGEMENT_H_

#include <memory>
#include <optional>
#include <utility>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/no_default_init.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

namespace hwsec {

struct KeyReloadDataTpm1 {
  OperationPolicy policy;
  brillo::Blob key_blob;
};

struct KeyTpm1 {
  enum class Type {
    kPersistentKey,
    kTransientKey,
    kReloadableTransientKey,
  };

  struct Cache {
    brillo::Blob pubkey_blob;
  };

  NoDefault<Type> type;
  NoDefault<TSS_HKEY> key_handle;
  NoDefault<Cache> cache;
  std::optional<ScopedTssKey> scoped_key;
  std::optional<KeyReloadDataTpm1> reload_data;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_KEY_MANAGEMENT_H_
