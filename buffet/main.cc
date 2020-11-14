// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <signal.h>

#include <base/files/file_path.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/flag_helper.h>
#include <brillo/strings/string_utils.h>
#include <brillo/syslog_logging.h>

#include "buffet/buffet_config.h"
#include "buffet/dbus_constants.h"
#include "buffet/manager.h"

using brillo::DBusServiceDaemon;
using brillo::dbus_utils::AsyncEventSequencer;
using buffet::dbus_constants::kRootServicePath;
using buffet::dbus_constants::kServiceName;

namespace buffet {

class Daemon final : public DBusServiceDaemon {
 public:
  explicit Daemon(const Manager::Options& options)
      : DBusServiceDaemon(kServiceName, kRootServicePath), options_{options} {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) override {
    manager_.reset(new Manager(options_, object_manager_->AsWeakPtr()));
    manager_->Start(sequencer);
  }

  void OnShutdown(int* return_code) override { manager_->Stop(); }

 private:
  Manager::Options options_;

  std::unique_ptr<buffet::Manager> manager_;
};

}  // namespace buffet

namespace {

const char kDefaultConfigFilePath[] = "/etc/buffet/buffet.conf";
const char kDefaultStateFilePath[] = "/var/lib/buffet/device_reg_info";

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(log_to_stderr, false, "log trace messages to stderr as well");
  DEFINE_string(config_path, kDefaultConfigFilePath,
                "Path to file containing config information.");
  DEFINE_string(state_path, kDefaultStateFilePath,
                "Path to file containing state information.");
  DEFINE_bool(enable_xmpp, true,
              "Connect to GCD via a persistent XMPP connection.");
  DEFINE_bool(disable_privet, false, "disable Privet protocol");
  DEFINE_bool(enable_ping, false, "enable test HTTP handler at /privet/ping");
  DEFINE_string(device_whitelist, "",
                "Comma separated list of network interfaces to monitor for "
                "connectivity (an empty list enables all interfaces).");

  DEFINE_bool(disable_security, false,
              "disable Privet security for tests. For test only.");
  DEFINE_string(test_privet_ssid, "",
                "Fixed SSID for WiFi bootstrapping. For test only.");
  DEFINE_string(test_definitions_path, "",
                "Path to directory containing additional command "
                "and state definitions. For test only.");

  brillo::FlagHelper::Init(argc, argv, "Privet protocol handler daemon");
  if (FLAGS_config_path.empty())
    FLAGS_config_path = kDefaultConfigFilePath;
  if (FLAGS_state_path.empty())
    FLAGS_state_path = kDefaultStateFilePath;
  int flags = brillo::kLogToSyslog | brillo::kLogHeader;
  if (FLAGS_log_to_stderr)
    flags |= brillo::kLogToStderr;
  brillo::InitLog(flags);

  auto device_whitelist =
      brillo::string_utils::Split(FLAGS_device_whitelist, ",", true, true);

  // We are handling write errors on closed sockets correctly and not relying on
  // (nor handling) SIGPIPE signal, which just kills the process.
  // Mark it to be ignored.
  signal(SIGPIPE, SIG_IGN);

  buffet::Manager::Options options;
  options.xmpp_enabled = FLAGS_enable_xmpp;
  options.disable_privet = FLAGS_disable_privet;
  options.enable_ping = FLAGS_enable_ping;
  options.device_whitelist = {device_whitelist.begin(), device_whitelist.end()};

  options.config_options.defaults = base::FilePath{FLAGS_config_path};
  options.config_options.settings = base::FilePath{FLAGS_state_path};
  options.config_options.definitions = base::FilePath{"/etc/buffet"};
  options.config_options.test_definitions =
      base::FilePath{FLAGS_test_definitions_path};
  options.config_options.disable_security = FLAGS_disable_security;
  options.config_options.test_privet_ssid = FLAGS_test_privet_ssid;

  buffet::Daemon daemon{options};
  return daemon.Run();
}
