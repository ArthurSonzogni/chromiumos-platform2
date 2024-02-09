// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_HIBERNATE_CHROMEOS_H_
#define UPDATE_ENGINE_CROS_HIBERNATE_CHROMEOS_H_

#include <memory>
#include <string>

#include <hibernate/dbus-proxies.h>

#include "update_engine/common/hibernate_interface.h"

namespace chromeos_update_engine {

// The Chrome OS implementation of the HibernateInterface. This interface
// provides information about the state of hibernate and resume.
class HibernateChromeOS : public HibernateInterface {
 public:
  HibernateChromeOS() {}
  HibernateChromeOS(const HibernateChromeOS&) = delete;
  HibernateChromeOS& operator=(const HibernateChromeOS&) = delete;

  ~HibernateChromeOS() override = default;

  void Init();

  // HibernateInterface overrides.

  // Returns true if the system is resuming from hibernate.
  bool IsResuming() override;

  // Aborts a resume from hibernate, if one is in progress.
  bool AbortResume(const std::string& reason) override;

 private:
  bool not_resuming_from_hibernate_ = false;

  // Real DBus proxy using the DBus connection.
  std::unique_ptr<org::chromium::HibernateResumeInterfaceProxy>
      hiberman_resume_proxy_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_HIBERNATE_CHROMEOS_H_
