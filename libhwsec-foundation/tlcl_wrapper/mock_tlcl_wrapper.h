// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TLCL_WRAPPER_MOCK_TLCL_WRAPPER_H_
#define LIBHWSEC_FOUNDATION_TLCL_WRAPPER_MOCK_TLCL_WRAPPER_H_

#include <gmock/gmock.h>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"
#include "libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h"

namespace hwsec_foundation {

class HWSEC_FOUNDATION_EXPORT MockTlclWrapper : public TlclWrapper {
 public:
  MockTlclWrapper() = default;

  MockTlclWrapper(const MockTlclWrapper&) = delete;
  MockTlclWrapper& operator=(const MockTlclWrapper&) = delete;

  MOCK_METHOD(uint32_t, Init, (), (override));

  MOCK_METHOD(uint32_t, Close, (), (override));

  MOCK_METHOD(uint32_t,
              Extend,
              (int pcr_num, const brillo::Blob&, brillo::Blob*),
              (override));
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TLCL_WRAPPER_MOCK_TLCL_WRAPPER_H_
