// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DLP_DLP_DAEMON_H_
#define DLP_DLP_DAEMON_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

namespace brillo {
namespace dbus_utils {
class AsyncEventSequencer;
}
}  // namespace brillo

namespace dlp {

class DlpAdaptor;

class DlpDaemon : public brillo::DBusServiceDaemon {
 public:
  DlpDaemon();
  DlpDaemon(const DlpDaemon&) = delete;
  DlpDaemon& operator=(const DlpDaemon&) = delete;
  ~DlpDaemon();

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  std::unique_ptr<DlpAdaptor> adaptor_;
};

}  // namespace dlp
#endif  // DLP_DLP_DAEMON_H_
