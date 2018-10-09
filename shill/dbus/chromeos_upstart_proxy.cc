// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/chromeos_upstart_proxy.h"

#include <base/bind.h>

#include "shill/logging.h"

using std::string;
using std::vector;

namespace shill {

const char ChromeosUpstartProxy::kUpstartServiceName[] = "com.ubuntu.Upstart";

ChromeosUpstartProxy::ChromeosUpstartProxy(const scoped_refptr<dbus::Bus>& bus)
    : shill_event_proxy_(new com::ubuntu::Upstart0_6::JobProxy(
          bus, kUpstartServiceName)) {}

void ChromeosUpstartProxy::EmitEvent(
    const string& name, const vector<string>& env, bool wait) {
  vector<string> start_job_env = env;
  start_job_env.push_back("EVENT_NAME=" + name);
  shill_event_proxy_->StartAsync(
      start_job_env, wait, base::Bind([](const dbus::ObjectPath& path) {
        VLOG(2) << "Event emitted successful";
      }),
      base::Bind([](brillo::Error* error) {
        LOG(ERROR) << "Failed to emit event: " << error->GetCode() << " "
                   << error->GetMessage();
      }));
}

}  // namespace shill
