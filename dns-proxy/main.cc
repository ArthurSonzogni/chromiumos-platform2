// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "dns-proxy/controller.h"
#include "dns-proxy/proxy.h"

int main(int argc, char* argv[]) {
  DEFINE_bool(log_to_stderr, false, "Log to both syslog and stderr");
  DEFINE_bool(root_ns, false, "Run DNS proxy on the root namespace");
  DEFINE_string(t, "", "The proxy type or empty to run the controller");
  DEFINE_string(i, "", "The outbound network interface");
  DEFINE_int32(
      fd, -1,
      "File descriptor for the proxies to communicate with the controller");
  brillo::FlagHelper::Init(argc, argv, "DNS Proxy daemon");

  int flags = brillo::kLogToSyslog | brillo::kLogHeader;
  if (FLAGS_log_to_stderr) {
    flags |= brillo::kLogToStderr;
  }

  brillo::InitLog(flags);

  if (FLAGS_t.empty()) {
    // Get the VLOG arg from the cmdline args. This function will return an
    // empty string if the flag does not exist so it will always be safe.
    const std::string vmodule =
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII("vmodule");
    dns_proxy::Controller controller(argv[0], vmodule);
    return controller.Run();
  }

  if (auto t = dns_proxy::Proxy::StringToType(FLAGS_t)) {
    dns_proxy::Proxy proxy({.type = t.value(), .ifname = FLAGS_i}, FLAGS_fd,
                           FLAGS_root_ns);
    return proxy.Run();
  }

  LOG(ERROR) << "Cannot launch proxy for unknown type [" << FLAGS_t << "]";
  return EX_USAGE;
}
