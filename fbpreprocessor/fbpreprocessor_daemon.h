// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_
#define FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "fbpreprocessor/configuration.h"
#include "fbpreprocessor/dbus_adaptor.h"
#include "fbpreprocessor/manager.h"

namespace fbpreprocessor {

class FbPreprocessorDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit FbPreprocessorDaemon(const Configuration& config);
  FbPreprocessorDaemon(const FbPreprocessorDaemon&) = delete;
  FbPreprocessorDaemon& operator=(const FbPreprocessorDaemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    adaptor_.reset(new DBusAdaptor(bus_, manager_.get()));
    adaptor_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() failed", true));
  }

 private:
  int OnInit() override;

  std::unique_ptr<DBusAdaptor> adaptor_;

  std::unique_ptr<Manager> manager_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_FBPREPROCESSOR_DAEMON_H_
