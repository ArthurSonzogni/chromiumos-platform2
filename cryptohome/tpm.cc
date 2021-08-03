// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm.h"

#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <inttypes.h>
#include <libhwsec-foundation/tpm/tpm_version.h>

#include <string>

#include "cryptohome/crypto/sha.h"

#if USE_TPM2
#include "cryptohome/tpm2_impl.h"
#endif

#if USE_TPM1
#include "cryptohome/tpm_impl.h"
#endif

#include "cryptohome/stub_tpm.h"

namespace cryptohome {

namespace {
constexpr TpmKeyHandle kInvalidKeyHandle = 0;
}

Tpm* Tpm::singleton_ = NULL;
base::Lock Tpm::singleton_lock_;

ScopedKeyHandle::ScopedKeyHandle()
    : tpm_(nullptr), handle_(kInvalidKeyHandle) {}

ScopedKeyHandle::~ScopedKeyHandle() {
  if (tpm_ != nullptr && handle_ != kInvalidKeyHandle) {
    tpm_->CloseHandle(handle_);
  }
}

TpmKeyHandle ScopedKeyHandle::value() {
  return handle_;
}

TpmKeyHandle ScopedKeyHandle::release() {
  TpmKeyHandle return_handle = handle_;
  tpm_ = nullptr;
  handle_ = kInvalidKeyHandle;
  return return_handle;
}

void ScopedKeyHandle::reset(Tpm* tpm, TpmKeyHandle handle) {
  if ((tpm_ != tpm) || (handle_ != handle)) {
    if ((tpm_ != nullptr) && (handle_ != kInvalidKeyHandle)) {
      tpm_->CloseHandle(handle_);
    }
    tpm_ = tpm;
    handle_ = handle;
  }
}

bool ScopedKeyHandle::has_value() {
  return tpm_ != nullptr && handle_ != kInvalidKeyHandle;
}

int Tpm::TpmVersionInfo::GetFingerprint() const {
  // The exact encoding doesn't matter as long as its unambiguous, stable and
  // contains all information present in the version fields.
  std::string encoded_parameters =
      base::StringPrintf("%08" PRIx32 "%016" PRIx64 "%08" PRIx32 "%08" PRIx32
                         "%016" PRIx64 "%016zx",
                         family, spec_level, manufacturer, tpm_model,
                         firmware_version, vendor_specific.size());
  encoded_parameters.append(vendor_specific);
  brillo::SecureBlob hash = Sha256(
      brillo::SecureBlob(encoded_parameters.begin(), encoded_parameters.end()));

  // Return the first 31 bits from |hash|.
  int result = hash[0] | hash[1] << 8 | hash[2] << 16 | hash[3] << 24;
  return result & 0x7fffffff;
}

Tpm* Tpm::GetSingleton() {
  // TODO(fes): Replace with a better atomic operation
  singleton_lock_.Acquire();
  if (!singleton_) {
    TPM_SELECT_BEGIN;
    TPM2_SECTION({ singleton_ = new Tpm2Impl(); });
    TPM1_SECTION({ singleton_ = new TpmImpl(); });
    OTHER_TPM_SECTION({
      LOG(WARNING) << "Unknown TPM";
      singleton_ = new StubTpm();
    });
    TPM_SELECT_END;
  }
  singleton_lock_.Release();
  return singleton_;
}

}  // namespace cryptohome
