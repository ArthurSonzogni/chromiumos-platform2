// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_DAEMON_DELEGATE_H_
#define MODEMFWD_DAEMON_DELEGATE_H_

#include <string>

#include <base/functional/callback.h>

namespace modemfwd {

class Delegate {
 public:
  virtual ~Delegate() = default;

  virtual bool ForceFlashForTesting(const std::string& device_id,
                                    const std::string& carrier_uuid,
                                    const std::string& variant,
                                    bool use_modems_fw_info) = 0;

  virtual bool ResetModem(const std::string& device_id) = 0;

  virtual void RegisterOnModemReappearanceCallback(
      const std::string& equipment_id, base::OnceClosure callback) = 0;
};

}  // namespace modemfwd

#endif  // MODEMFWD_DAEMON_DELEGATE_H_
