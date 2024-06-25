// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_ASYNC_MODEM_FLASHER_H_
#define MODEMFWD_ASYNC_MODEM_FLASHER_H_

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include <base/functional/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/thread.h>
#include <base/time/time.h>

#include "modemfwd/modem_flasher.h"

namespace modemfwd {

// AsyncModemFlasher runs ModemFlasher functions on a separate thread
// to avoid blocking the main thread.
class AsyncModemFlasher : public base::RefCountedThreadSafe<AsyncModemFlasher> {
 public:
  explicit AsyncModemFlasher(std::unique_ptr<ModemFlasher> flasher);

  void ShouldFlash(Modem* modem,
                   base::OnceCallback<void(bool, brillo::ErrorPtr)> callback);
  void BuildFlashConfig(Modem* modem,
                        std::optional<std::string> carrier_override_uuid,
                        base::OnceCallback<void(std::unique_ptr<FlashConfig>,
                                                brillo::ErrorPtr)> callback);
  void RunFlash(
      Modem* modem,
      std::unique_ptr<FlashConfig> flash_cfg,
      base::OnceCallback<void(bool, base::TimeDelta, brillo::ErrorPtr)>
          callback);

 private:
  friend class base::RefCountedThreadSafe<AsyncModemFlasher>;
  ~AsyncModemFlasher() = default;

  void ShouldFlashOnThread(
      Modem* modem, std::shared_ptr<std::pair<bool, brillo::ErrorPtr>> result);
  void OnShouldFlashResult(
      base::OnceCallback<void(bool, brillo::ErrorPtr)> callback,
      std::shared_ptr<std::pair<bool, brillo::ErrorPtr>> result);

  void BuildFlashConfigOnThread(
      Modem* modem,
      std::optional<std::string> carrier_override_uuid,
      std::shared_ptr<std::pair<std::unique_ptr<FlashConfig>, brillo::ErrorPtr>>
          result);
  void OnBuildFlashConfigResult(
      base::OnceCallback<void(std::unique_ptr<FlashConfig>, brillo::ErrorPtr)>
          callback,
      std::shared_ptr<std::pair<std::unique_ptr<FlashConfig>, brillo::ErrorPtr>>
          result);

  void RunFlashOnThread(
      Modem* modem,
      std::unique_ptr<FlashConfig> flash_cfg,
      std::shared_ptr<std::tuple<bool, base::TimeDelta, brillo::ErrorPtr>>
          result);
  void OnRunFlashResult(
      base::OnceCallback<void(bool, base::TimeDelta, brillo::ErrorPtr)>
          callback,
      std::shared_ptr<std::tuple<bool, base::TimeDelta, brillo::ErrorPtr>>
          result);

  base::Thread thread_;
  std::unique_ptr<ModemFlasher> flasher_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_ASYNC_MODEM_FLASHER_H_
