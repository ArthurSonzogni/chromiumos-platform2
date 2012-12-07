// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dhcp_config.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <base/file_util.h>
#include <base/string_split.h>
#include <base/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/dhcpcd_proxy.h"
#include "shill/dhcp_provider.h"
#include "shill/event_dispatcher.h"
#include "shill/glib.h"
#include "shill/ip_address.h"
#include "shill/logging.h"
#include "shill/minijail.h"
#include "shill/proxy_factory.h"

using std::string;
using std::vector;

namespace shill {

// static
const char DHCPConfig::kConfigurationKeyBroadcastAddress[] = "BroadcastAddress";
const char DHCPConfig::kConfigurationKeyClasslessStaticRoutes[] =
    "ClasslessStaticRoutes";
const char DHCPConfig::kConfigurationKeyDNS[] = "DomainNameServers";
const char DHCPConfig::kConfigurationKeyDomainName[] = "DomainName";
const char DHCPConfig::kConfigurationKeyDomainSearch[] = "DomainSearch";
const char DHCPConfig::kConfigurationKeyIPAddress[] = "IPAddress";
const char DHCPConfig::kConfigurationKeyMTU[] = "InterfaceMTU";
const char DHCPConfig::kConfigurationKeyRouters[] = "Routers";
const char DHCPConfig::kConfigurationKeySubnetCIDR[] = "SubnetCIDR";
const int DHCPConfig::kDHCPCDExitPollMilliseconds = 50;
const int DHCPConfig::kDHCPCDExitWaitMilliseconds = 3000;
const char DHCPConfig::kDHCPCDPath[] = "/sbin/dhcpcd";
const char DHCPConfig::kDHCPCDPathFormatLease[] =
    "var/lib/dhcpcd/dhcpcd-%s.lease";
const char DHCPConfig::kDHCPCDPathFormatPID[] =
    "var/run/dhcpcd/dhcpcd-%s.pid";
const int DHCPConfig::kDHCPTimeoutSeconds = 30;
const char DHCPConfig::kDHCPCDUser[] = "dhcp";
const int DHCPConfig::kMinMTU = 576;
const char DHCPConfig::kReasonBound[] = "BOUND";
const char DHCPConfig::kReasonFail[] = "FAIL";
const char DHCPConfig::kReasonGatewayArp[] = "GATEWAY-ARP";
const char DHCPConfig::kReasonRebind[] = "REBIND";
const char DHCPConfig::kReasonReboot[] = "REBOOT";
const char DHCPConfig::kReasonRenew[] = "RENEW";
// static
const char DHCPConfig::kType[] = "dhcp";


DHCPConfig::DHCPConfig(ControlInterface *control_interface,
                       EventDispatcher *dispatcher,
                       DHCPProvider *provider,
                       const string &device_name,
                       const string &request_hostname,
                       const string &lease_file_suffix,
                       bool arp_gateway,
                       GLib *glib)
    : IPConfig(control_interface, device_name, kType),
      proxy_factory_(ProxyFactory::GetInstance()),
      provider_(provider),
      request_hostname_(request_hostname),
      lease_file_suffix_(lease_file_suffix),
      arp_gateway_(arp_gateway),
      pid_(0),
      child_watch_tag_(0),
      lease_acquisition_timeout_seconds_(kDHCPTimeoutSeconds),
      root_("/"),
      weak_ptr_factory_(this),
      dispatcher_(dispatcher),
      glib_(glib),
      minijail_(Minijail::GetInstance()) {
  SLOG(DHCP, 2) << __func__ << ": " << device_name;
  if (lease_file_suffix_.empty()) {
    lease_file_suffix_ = device_name;
  }
}

DHCPConfig::~DHCPConfig() {
  SLOG(DHCP, 2) << __func__ << ": " << device_name();

  // Don't leave behind dhcpcd running.
  Stop();

  // Make sure we don't get any callbacks to the destroyed instance.
  CleanupClientState();
}

bool DHCPConfig::RequestIP() {
  SLOG(DHCP, 2) << __func__ << ": " << device_name();
  if (!pid_) {
    return Start();
  }
  if (!proxy_.get()) {
    LOG(ERROR) << "Unable to request IP before acquiring destination.";
    return Restart();
  }
  return RenewIP();
}

bool DHCPConfig::RenewIP() {
  SLOG(DHCP, 2) << __func__ << ": " << device_name();
  if (!pid_) {
    return false;
  }
  if (!proxy_.get()) {
    LOG(ERROR) << "Unable to renew IP before acquiring destination.";
    return false;
  }
  proxy_->Rebind(device_name());
  StartDHCPTimeout();
  return true;
}

bool DHCPConfig::ReleaseIP() {
  SLOG(DHCP, 2) << __func__ << ": " << device_name();
  if (!pid_) {
    return true;
  }
  // If we are using gateway unicast ARP to speed up re-connect, don't
  // give up our leases when we disconnect.
  if (!arp_gateway_ && proxy_.get()) {
    proxy_->Release(device_name());
  }
  Stop();
  return true;
}

void DHCPConfig::InitProxy(const string &service) {
  if (!proxy_.get()) {
    LOG(INFO) << "Init DHCP Proxy: " << device_name() << " at " << service;
    proxy_.reset(proxy_factory_->CreateDHCPProxy(service));
  }
}

void DHCPConfig::ProcessEventSignal(const string &reason,
                                    const Configuration &configuration) {
  LOG(INFO) << "Event reason: " << reason;
  if (reason == kReasonFail) {
    LOG(ERROR) << "Received failure event from DHCP client.";
    UpdateProperties(IPConfig::Properties(), false);
    return;
  }
  if (reason != kReasonBound &&
      reason != kReasonRebind &&
      reason != kReasonReboot &&
      reason != kReasonRenew &&
      reason != kReasonGatewayArp) {
    LOG(WARNING) << "Event ignored.";
    return;
  }
  IPConfig::Properties properties;
  CHECK(ParseConfiguration(configuration, &properties));
  if (reason == kReasonGatewayArp) {
    // This is a non-authoritative confirmation that we or on the same
    // network as the one we received a lease on previously.  The DHCP
    // client is still running, so we should not cancel the timeout
    // until that completes.  In the meantime, however, we can tentatively
    // configure our network in anticipation of successful completion.
    IPConfig::UpdateProperties(properties, true);
  } else {
    UpdateProperties(properties, true);
  }
}

void DHCPConfig::UpdateProperties(const Properties &properties, bool success) {
  StopDHCPTimeout();
  IPConfig::UpdateProperties(properties, success);
}

bool DHCPConfig::Start() {
  SLOG(DHCP, 2) << __func__ << ": " << device_name();

  vector<char *> args;
  args.push_back(const_cast<char *>(kDHCPCDPath));
  args.push_back(const_cast<char *>("-B"));  // Run in foreground.
  args.push_back(const_cast<char *>("-q"));  // Only warnings+errors to stderr.
  if (!request_hostname_.empty()) {
    args.push_back(const_cast<char *>("-h"));  // Request hostname from server.
    args.push_back(const_cast<char *>(request_hostname_.c_str()));
  }
  if (arp_gateway_) {
    args.push_back(const_cast<char *>("-R"));  // ARP for default gateway.
    args.push_back(const_cast<char *>("-U"));  // Enable unicast ARP on renew.
  }
  string interface_arg(device_name());
  if (lease_file_suffix_ != device_name()) {
    interface_arg = base::StringPrintf("%s=%s", device_name().c_str(),
                                       lease_file_suffix_.c_str());
  }
  args.push_back(const_cast<char *>(interface_arg.c_str()));
  args.push_back(NULL);

  struct minijail *jail = minijail_->New();
  minijail_->DropRoot(jail, kDHCPCDUser);
  minijail_->UseCapabilities(jail,
                             CAP_TO_MASK(CAP_NET_BIND_SERVICE) |
                             CAP_TO_MASK(CAP_NET_BROADCAST) |
                             CAP_TO_MASK(CAP_NET_ADMIN) |
                             CAP_TO_MASK(CAP_NET_RAW));

  CHECK(!pid_);
  if (!minijail_->RunAndDestroy(jail, args, &pid_)) {
    LOG(ERROR) << "Unable to spawn " << kDHCPCDPath << " in a jail.";
    return false;
  }
  LOG(INFO) << "Spawned " << kDHCPCDPath << " with pid: " << pid_;
  provider_->BindPID(pid_, this);
  CHECK(!child_watch_tag_);
  child_watch_tag_ = glib_->ChildWatchAdd(pid_, ChildWatchCallback, this);
  StartDHCPTimeout();
  return true;
}

void DHCPConfig::Stop() {
  if (pid_) {
    SLOG(DHCP, 2) << "Terminating " << pid_;
    if (kill(pid_, SIGTERM) < 0) {
      PLOG(ERROR);
      return;
    }
    pid_t ret;
    int num_iterations =
        kDHCPCDExitWaitMilliseconds / kDHCPCDExitPollMilliseconds;
    for (int count = 0; count < num_iterations; ++count) {
      ret = waitpid(pid_, NULL, WNOHANG);
      if (ret == pid_ || ret == -1)
        break;
      usleep(kDHCPCDExitPollMilliseconds * 1000);
      if (count == num_iterations / 2)  // Make one last attempt to kill dhcpcd.
        kill(pid_, SIGKILL);
    }
    if (ret != pid_)
      PLOG(ERROR);
  }
  StopDHCPTimeout();
}

bool DHCPConfig::Restart() {
  // Check to ensure that this instance doesn't get destroyed in the middle of
  // this call. If stopping a running client while there's only one reference to
  // this instance, we will end up destroying it when the PID is unbound from
  // the Provider. Since the Provider doesn't invoke Restart, this would mean
  // that Restart was erroneously executed through a bare reference.
  CHECK(!pid_ || !HasOneRef());
  Stop();
  if (pid_) {
    provider_->UnbindPID(pid_);
  }
  CleanupClientState();
  return Start();
}

string DHCPConfig::GetIPv4AddressString(unsigned int address) {
  char str[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &address, str, arraysize(str))) {
    return str;
  }
  LOG(ERROR) << "Unable to convert IPv4 address to string: " << address;
  return "";
}

// static
bool DHCPConfig::ParseClasslessStaticRoutes(const string &classless_routes,
                                            IPConfig::Properties *properties) {
  if (classless_routes.empty()) {
    // It is not an error for this string to be empty.
    return true;
  }

  vector<string> route_strings;
  base::SplitString(classless_routes, ' ', &route_strings);
  if (route_strings.size() % 2) {
    LOG(ERROR) << "In " << __func__ << ": Size of route_strings array "
               << "is a non-even number: " << route_strings.size();
    return false;
  }

  vector<IPConfig::Route> routes;
  vector<string>::iterator route_iterator = route_strings.begin();
  // Classless routes are a space-delimited array of
  // "destination/prefix gateway" values.  As such, we iterate twice
  // for each pass of the loop below.
  while (route_iterator != route_strings.end()) {
    const string &destination_as_string(*route_iterator);
    route_iterator++;
    IPAddress destination(IPAddress::kFamilyIPv4);
    if (!destination.SetAddressAndPrefixFromString(
             destination_as_string)) {
      LOG(ERROR) << "In " << __func__ << ": Expected an IP address/prefix "
                 << "but got an unparsable: " << destination_as_string;
      return false;
    }

    CHECK(route_iterator != route_strings.end());
    const string &gateway_as_string(*route_iterator);
    route_iterator++;
    IPAddress gateway(IPAddress::kFamilyIPv4);
    if (!gateway.SetAddressFromString(gateway_as_string)) {
      LOG(ERROR) << "In " << __func__ << ": Expected a router IP address "
                 << "but got an unparsable: " << gateway_as_string;
      return false;
    }

    if (destination.prefix() == 0 && properties->gateway.empty()) {
      // If a default route is provided in the classless parameters and
      // we don't already have one, apply this as the default route.
      SLOG(DHCP, 2) << "In " << __func__ << ": Setting default gateway to "
                    << gateway_as_string;
      CHECK(gateway.IntoString(&properties->gateway));
    } else {
      IPConfig::Route route;
      CHECK(destination.IntoString(&route.host));
      IPAddress netmask(IPAddress::GetAddressMaskFromPrefix(
          destination.family(), destination.prefix()));
      CHECK(netmask.IntoString(&route.netmask));
      CHECK(gateway.IntoString(&route.gateway));
      routes.push_back(route);
      SLOG(DHCP, 2) << "In " << __func__ << ": Adding route to to "
                    << destination_as_string << " via " << gateway_as_string;
    }
  }

  if (!routes.empty()) {
    properties->routes.swap(routes);
  }

  return true;
}

bool DHCPConfig::ParseConfiguration(const Configuration &configuration,
                                    IPConfig::Properties *properties) {
  SLOG(DHCP, 2) << __func__;
  properties->method = flimflam::kTypeDHCP;
  properties->address_family = IPAddress::kFamilyIPv4;
  string classless_static_routes;
  bool default_gateway_parse_error = false;
  for (Configuration::const_iterator it = configuration.begin();
       it != configuration.end(); ++it) {
    const string &key = it->first;
    const DBus::Variant &value = it->second;
    SLOG(DHCP, 2) << "Processing key: " << key;
    if (key == kConfigurationKeyIPAddress) {
      properties->address = GetIPv4AddressString(value.reader().get_uint32());
      if (properties->address.empty()) {
        return false;
      }
    } else if (key == kConfigurationKeySubnetCIDR) {
      properties->subnet_prefix = value.reader().get_byte();
    } else if (key == kConfigurationKeyBroadcastAddress) {
      properties->broadcast_address =
          GetIPv4AddressString(value.reader().get_uint32());
      if (properties->broadcast_address.empty()) {
        return false;
      }
    } else if (key == kConfigurationKeyRouters) {
      vector<unsigned int> routers = value.operator vector<unsigned int>();
      if (routers.empty()) {
        LOG(ERROR) << "No routers provided.";
        default_gateway_parse_error = true;
      } else {
        properties->gateway = GetIPv4AddressString(routers[0]);
        if (properties->gateway.empty()) {
          LOG(ERROR) << "Failed to parse router parameter provided.";
          default_gateway_parse_error = true;
        }
      }
    } else if (key == kConfigurationKeyDNS) {
      vector<unsigned int> servers = value.operator vector<unsigned int>();
      for (vector<unsigned int>::const_iterator it = servers.begin();
           it != servers.end(); ++it) {
        string server = GetIPv4AddressString(*it);
        if (server.empty()) {
          return false;
        }
        properties->dns_servers.push_back(server);
      }
    } else if (key == kConfigurationKeyDomainName) {
      properties->domain_name = value.reader().get_string();
    } else if (key == kConfigurationKeyDomainSearch) {
      properties->domain_search = value.operator vector<string>();
    } else if (key == kConfigurationKeyMTU) {
      int mtu = value.reader().get_uint16();
      if (mtu >= kMinMTU) {
        properties->mtu = mtu;
      }
    } else if (key == kConfigurationKeyClasslessStaticRoutes) {
      classless_static_routes = value.reader().get_string();
    } else {
      SLOG(DHCP, 2) << "Key ignored.";
    }
  }
  ParseClasslessStaticRoutes(classless_static_routes, properties);
  if (default_gateway_parse_error && properties->gateway.empty()) {
    return false;
  }
  return true;
}

void DHCPConfig::ChildWatchCallback(GPid pid, gint status, gpointer data) {
  if (status == EXIT_SUCCESS) {
    SLOG(DHCP, 2) << "pid " << pid << " exit status " << status;
  } else {
    LOG(WARNING) << "pid " << pid << " exit status " << status;
  }
  DHCPConfig *config = reinterpret_cast<DHCPConfig *>(data);
  config->child_watch_tag_ = 0;
  CHECK_EQ(pid, config->pid_);
  config->CleanupClientState();

  // |config| instance may be destroyed after this call.
  config->provider_->UnbindPID(pid);
}

void DHCPConfig::CleanupClientState() {
  if (child_watch_tag_) {
    glib_->SourceRemove(child_watch_tag_);
    child_watch_tag_ = 0;
  }
  pid_ = 0;
  proxy_.reset();
  if (lease_file_suffix_ == device_name()) {
    // If the lease file suffix was left as default, clean it up at exit.
    file_util::Delete(root_.Append(
        base::StringPrintf(kDHCPCDPathFormatLease,
                           device_name().c_str())), false);
  }
  file_util::Delete(root_.Append(
      base::StringPrintf(kDHCPCDPathFormatPID, device_name().c_str())), false);
}

void DHCPConfig::StartDHCPTimeout() {
  lease_acquisition_timeout_callback_.Reset(
      Bind(&DHCPConfig::ProcessDHCPTimeout, weak_ptr_factory_.GetWeakPtr()));
  dispatcher_->PostDelayedTask(
      lease_acquisition_timeout_callback_.callback(),
      lease_acquisition_timeout_seconds_ * 1000);
}

void DHCPConfig::StopDHCPTimeout() {
  lease_acquisition_timeout_callback_.Cancel();
}

void DHCPConfig::ProcessDHCPTimeout() {
  LOG(ERROR) << "Timed out waiting for DHCP lease on " << device_name() << " "
             << "(after " << lease_acquisition_timeout_seconds_ << " seconds).";
  UpdateProperties(IPConfig::Properties(), false);
}

}  // namespace shill
