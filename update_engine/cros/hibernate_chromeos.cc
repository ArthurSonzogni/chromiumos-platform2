//
// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "update_engine/cros/dbus_connection.h"
#include "update_engine/cros/hibernate_chromeos.h"

#include <utility>

#include <base/files/file_util.h>
#include <dbus/hiberman/dbus-constants.h>

namespace chromeos_update_engine {

std::unique_ptr<HibernateInterface> CreateHibernateService() {
  std::unique_ptr<HibernateChromeOS> hibernate(new HibernateChromeOS());
  hibernate->Init();
  return std::move(hibernate);
}

void HibernateChromeOS::Init() {
  hiberman_resume_proxy_.reset(new org::chromium::HibernateResumeInterfaceProxy(
      DBusConnection::Get()->GetDBus()));
}

bool HibernateChromeOS::IsResuming() {
  if (not_resuming_from_hibernate_)
    return false;

  // This file is created by hiberman's resume_init function, which is initiated
  // during chromeos_startup very early in the boot process (before the stateful
  // partition is mounted). Hiberman's resume process removes it if resume is
  // aborted.
  if (base::PathExists(
          base::FilePath(hiberman::kHibernateResumeInProgressFile)))
    return true;

  // The system only ever starts as resuming from hibernate, it never
  // transitions there. Cache a negative result.
  not_resuming_from_hibernate_ = true;
  return false;
}

bool HibernateChromeOS::AbortResume(const std::string& reason) {
  brillo::ErrorPtr err;

  if (!hiberman_resume_proxy_) {
    LOG(ERROR) << "Hibernate resume proxy unavailable.";
    return false;
  }

  if (!hiberman_resume_proxy_->AbortResume(reason, &err)) {
    LOG(ERROR) << "Failed to abort resume from hibernate: "
               << "ErrorCode=" << err->GetCode()
               << ", ErrMsg=" << err->GetMessage();
    return false;
  }

  return true;
}

}  // namespace chromeos_update_engine
