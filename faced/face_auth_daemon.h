// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACE_AUTH_DAEMON_H_
#define FACED_FACE_AUTH_DAEMON_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>

#include "faced/dbus_adaptor.h"
#include "faced/face_auth_service.h"

namespace faced {

class FaceAuthDaemon : public brillo::DBusServiceDaemon {
 public:
  FaceAuthDaemon();
  ~FaceAuthDaemon() override = default;

  // Disallow copy and move.
  FaceAuthDaemon(const FaceAuthDaemon&) = delete;
  FaceAuthDaemon& operator=(const FaceAuthDaemon&) = delete;

 protected:
  // brillo::DBusServiceDaemon:
  int OnInit() override;

 private:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

  void ShutdownOnConnectionError(std::string error_message);

  std::unique_ptr<DBusAdaptor> adaptor_;

  std::unique_ptr<FaceAuthServiceInterface> face_auth_service_;

  base::WeakPtrFactory<FaceAuthDaemon> weak_ptr_factory_{this};
};

}  // namespace faced

#endif  // FACED_FACE_AUTH_DAEMON_H_
