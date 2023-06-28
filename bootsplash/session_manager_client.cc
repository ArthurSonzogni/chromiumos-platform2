// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <chromeos/dbus/service_constants.h>

#include "bootsplash/session_manager_client.h"

namespace bootsplash {

std::unique_ptr<SessionManagerClientInterface> SessionManagerClient::Create(
    const scoped_refptr<dbus::Bus>& bus) {
  return base::WrapUnique(new SessionManagerClient(bus));
}

SessionManagerClient::SessionManagerClient(
    const scoped_refptr<dbus::Bus>& bus) {
  proxy_ = std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus);
  // Register Input Event signal Handler.
  proxy_->RegisterLoginPromptVisibleSignalHandler(
      base::BindRepeating(&SessionManagerClient::LoginPromptVisible,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&SessionManagerClient::OnSignalConnected,
                     weak_factory_.GetWeakPtr()));
}

void SessionManagerClient::AddObserver(SessionEventObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

bool SessionManagerClient::HasObserver(SessionEventObserver* observer) {
  DCHECK(observer);
  return observers_.HasObserver(observer);
}

void SessionManagerClient::RemoveObserver(SessionEventObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void SessionManagerClient::LoginPromptVisible() {
  for (auto& observer : observers_) {
    observer.SessionManagerLoginPromptVisibleEventReceived();
  }
}

void SessionManagerClient::OnSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success) {
  LOG(INFO) << __func__ << " interface: " << interface_name
            << " signal: " << signal_name << " success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name << " to interface "
               << interface_name;
  }
}

}  // namespace bootsplash
