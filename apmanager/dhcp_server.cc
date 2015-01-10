// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apmanager/dhcp_server.h"

#include <net/if.h>
#include <signal.h>

#include <base/strings/stringprintf.h>

#include "apmanager/daemon.h"

using std::string;

namespace apmanager {

// static.
const char DHCPServer::kDnsmasqPath[] = "/usr/sbin/dnsmasq";
const char DHCPServer::kDnsmasqConfigFilePathFormat[] =
    "/var/run/apmanager/dnsmasq/dhcpd-%d.conf";
const char DHCPServer::kDHCPLeasesFilePathFormat[] =
    "/var/run/apmanager/dnsmasq/dhcpd-%d.leases";
const char DHCPServer::kServerAddressFormat[] = "192.168.%d.254";
const char DHCPServer::kAddressRangeLowFormat[] = "192.168.%d.1";
const char DHCPServer::kAddressRangeHighFormat[] = "192.168.%d.128";
const int DHCPServer::kServerAddressPrefix = 24;
const int DHCPServer::kTerminationTimeoutSeconds = 2;

DHCPServer::DHCPServer(uint16_t server_address_index,
                       const string& interface_name)
    : server_address_index_(server_address_index),
      interface_name_(interface_name),
      server_address_(shill::IPAddress::kFamilyIPv4),
      rtnl_handler_(shill::RTNLHandler::GetInstance()),
      file_writer_(FileWriter::GetInstance()) {}

DHCPServer::~DHCPServer() {
  if (dnsmasq_process_) {
    // The destructor of the Process will send a SIGKILL signal if it is not
    // already terminated.
    dnsmasq_process_->Kill(SIGTERM, kTerminationTimeoutSeconds);
    dnsmasq_process_.reset();
    rtnl_handler_->RemoveInterfaceAddress(
        rtnl_handler_->GetInterfaceIndex(interface_name_), server_address_);
  }
}

bool DHCPServer::Start() {
  if (dnsmasq_process_) {
    LOG(ERROR) << "DHCP Server already running";
    return false;
  }

  // Generate dnsmasq config file.
  string config_str = GenerateConfigFile();
  string file_name = base::StringPrintf(kDnsmasqConfigFilePathFormat,
                                        server_address_index_);
  if (!file_writer_->Write(file_name, config_str)) {
    LOG(ERROR) << "Failed to write configuration to a file";
    return false;
  }

  // Setup local server address and bring up the interface in case it is down.
  server_address_.SetAddressFromString(
      base::StringPrintf(kServerAddressFormat, server_address_index_));
  server_address_.set_prefix(kServerAddressPrefix);
  int interface_index = rtnl_handler_->GetInterfaceIndex(interface_name_);
  rtnl_handler_->AddInterfaceAddress(
      interface_index,
      server_address_,
      server_address_.GetDefaultBroadcast(),
      shill::IPAddress(shill::IPAddress::kFamilyIPv4));
  rtnl_handler_->SetInterfaceFlags(interface_index, IFF_UP, IFF_UP);

  // TODO(zqiu): use ProcessFactory for better unit testing.
  // Start a dnsmasq process.
  dnsmasq_process_.reset(new chromeos::ProcessImpl());
  dnsmasq_process_->AddArg(kDnsmasqPath);
  dnsmasq_process_->AddArg(base::StringPrintf("--conf-file=%s",
                                              file_name.c_str()));
  if (!dnsmasq_process_->Start()) {
    rtnl_handler_->RemoveInterfaceAddress(interface_index, server_address_);
    dnsmasq_process_.reset();
    LOG(ERROR) << "Failed to start dnsmasq process";
    return false;
  }

  return true;
}

string DHCPServer::GenerateConfigFile() {
  string server_address = base::StringPrintf(kServerAddressFormat,
                                             server_address_index_);
  string address_low = base::StringPrintf(kAddressRangeLowFormat,
                                          server_address_index_);
  string address_high = base::StringPrintf(kAddressRangeHighFormat,
                                           server_address_index_);
  string lease_file_path = base::StringPrintf(kDHCPLeasesFilePathFormat,
                                              server_address_index_);
  string config;
  config += "port=0\n";
  config += "bind-interfaces\n";
  config += "log-dhcp\n";
  // By default, dnsmasq process will spawn off another process to run the
  // dnsmasq task in the "background" and exit the current process immediately.
  // This means the daemon would not have any knowledge of the background
  // dnsmasq process, and it will continue to run even after the AP service is
  // terminated. Configure dnsmasq to run in "foreground" so no extra process
  // will be spawned.
  config += "keep-in-foreground\n";
  // Explicitly set the user to apmanager. If not set, dnsmasq will default to
  // run as "nobody".
  base::StringAppendF(&config, "user=%s\n", Daemon::kAPManagerUserName);
  base::StringAppendF(
      &config, "dhcp-range=%s,%s\n", address_low.c_str(), address_high.c_str());
  base::StringAppendF(&config, "interface=%s\n", interface_name_.c_str());
  base::StringAppendF(&config, "dhcp-leasefile=%s\n", lease_file_path.c_str());
  return config;
}

}  // namespace apmanager
