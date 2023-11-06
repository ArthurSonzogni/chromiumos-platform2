// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_MODEM_H_
#define MODEMLOGGERD_MODEM_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <brillo/process/process.h>

#include "modemloggerd/adaptor_factory_interface.h"
#include "modemloggerd/adaptor_interfaces.h"
#include "modemloggerd/helper_manifest.pb.h"
#include "modemloggerd/logger_interface.h"

namespace modemloggerd {

class Modem : public LoggerInterface {
 public:
  Modem(dbus::Bus* bus,
        AdaptorFactoryInterface* adaptor_factory,
        HelperEntry logging_helper);
  Modem(const Modem&) = delete;
  Modem& operator=(const Modem&) = delete;

  brillo::ErrorPtr Start() override;
  brillo::ErrorPtr Stop() override;
  brillo::ErrorPtr SetOutputDir(const std::string& output_dir) override;

  dbus::ObjectPath GetDBusPath() override;

 private:
  bool RunHelperProcessWithLogs();

  std::string output_dir_;
  brillo::ProcessImpl process_;

  std::unique_ptr<ModemAdaptorInterface> dbus_adaptor_;
  HelperEntry logging_helper_;
};

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_MODEM_H_
