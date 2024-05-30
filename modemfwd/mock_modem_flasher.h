// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MOCK_MODEM_FLASHER_H_
#define MODEMFWD_MOCK_MODEM_FLASHER_H_

#include <memory>

#include <gmock/gmock.h>

#include "modemfwd/modem_flasher.h"

namespace modemfwd {

class MockModemFlasher : public ModemFlasher {
 public:
  MockModemFlasher() = default;
  ~MockModemFlasher() override = default;

  MOCK_METHOD(bool, ShouldFlash, (Modem*, brillo::ErrorPtr*), (override));
  MOCK_METHOD(std::unique_ptr<FlashConfig>,
              BuildFlashConfig,
              (Modem*, brillo::ErrorPtr*),
              (override));
  MOCK_METHOD(bool,
              RunFlash,
              (Modem*, const FlashConfig&, base::TimeDelta*, brillo::ErrorPtr*),
              (override));
};

}  // namespace modemfwd

#endif  // MODEMFWD_MOCK_MODEM_FLASHER_H_
