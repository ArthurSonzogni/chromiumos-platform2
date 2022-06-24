// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_KEY_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_TPM2_KEY_MANAGEMENT_H_

#include <memory>
#include <optional>
#include <utility>

#include <trunks/tpm_generated.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/no_default_init.h"

namespace hwsec {

struct KeyReloadDataTpm2 {
  OperationPolicy policy;
  brillo::Blob key_blob;
};

struct KeyTpm2 {
  enum class Type {
    kPersistentKey,
    kTransientKey,
    kReloadableTransientKey,
  };

  struct Cache {
    NoDefault<trunks::TPMT_PUBLIC> public_area;
  };

  NoDefault<Type> type;
  NoDefault<trunks::TPM_HANDLE> key_handle;
  NoDefault<Cache> cache;
  std::optional<KeyReloadDataTpm2> reload_data;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_KEY_MANAGEMENT_H_
