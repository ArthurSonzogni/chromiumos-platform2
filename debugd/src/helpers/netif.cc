// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Netif helper - emits information about network interfaces as json.
// Here's an example of output from my system:
// {
//    "eth0": {
//       "flags": [ "up", "broadcast", "running", "multi", "lower-up" ],
//       "ipv4": {
//          "addrs": [ "172.31.197.126" ],
//          "destination": "172.31.197.255",
//          "mask": "255.255.254.0"
//       },
//       "ipv6": {
//          "addrs": [ "2620:0:1004:1:198:42c6:435c:aa09",
// "2620:0:1004:1:210:60ff:fe3b:c2d0", "fe80::210:60ff:fe3b:c2d0" ]
//       },
//       "mac": "0010603BC2D0"
//    },
//    "lo": {
//       "flags": [ "up", "loopback", "running", "lower-up" ],
//       "ipv4": {
//          "addrs": [ "127.0.0.1" ],
//          "destination": "127.0.0.1",
//          "mask": "255.0.0.0"
//       },
//       "ipv6": {
//          "addrs": [ "::1" ]
//       },
//       "mac": "000000000000"
//    },
//    "wlan0": {
//       "flags": [ "broadcast", "multi" ],
//       "mac": "68A3C41B264C",
//       "signal-strengths": {
//          "A9F1BDF1DAB1NVT4F4F59": 62
//       }
//    },
//    "wwan0": {
//       "flags": [ "broadcast", "multi" ],
//       "mac": "020010ABA636"
//    }
// }
// The meanings of the individual flags are up to Linux's networking stack (and
// sometimes up to the individual cards' drivers); "up" indicates that the
// interface is up.

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <linux/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/json/json_writer.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <chromeos/dbus/service_constants.h>
#include <debugd/src/dbus_utils.h>
#include <shill/dbus_proxies/org.chromium.flimflam.Manager.h>
#include <shill/dbus_proxies/org.chromium.flimflam.Service.h>

using base::DictionaryValue;
using base::ListValue;
using base::StringValue;
using base::Value;

std::string getmac(int fd, const char *ifname) {
  struct ifreq ifr;
  int ret;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_addr.sa_family = AF_PACKET;
  strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);
  ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
  if (ret < 0)
    return "<can't fetch>";
  return base::HexEncode(ifr.ifr_hwaddr.sa_data, 6);
}

std::string sockaddr2str(struct sockaddr *sa) {
  char *buf = new char[INET6_ADDRSTRLEN];
  void *addr;
  // These need NOLINT because cpplint thinks we're taking the address of a
  // cast, which we aren't - we're taking the address of a member after casting
  // a pointer to a different type.
  if (sa->sa_family == AF_INET)
    addr = &((struct sockaddr_in *)sa)->sin_addr; // NOLINT
  else if (sa->sa_family == AF_INET6)
    addr = &((struct sockaddr_in6 *)sa)->sin6_addr; // NOLINT
  else
    return "unknown";
  return inet_ntop(sa->sa_family, addr, buf, INET6_ADDRSTRLEN);
}

struct ifflag {
  unsigned int bit;
  const char *name;
} ifflags[] = {
  { IFF_UP, "up" },
  { IFF_BROADCAST, "broadcast" },
  { IFF_DEBUG, "debug" },
  { IFF_LOOPBACK, "loopback" },
  { IFF_POINTOPOINT, "point-to-point" },
  { IFF_RUNNING, "running" },
  { IFF_NOARP, "noarp" },
  { IFF_PROMISC, "promisc" },
  { IFF_NOTRAILERS, "notrailers" },
  { IFF_ALLMULTI, "allmulti" },
  { IFF_MASTER, "master" },
  { IFF_SLAVE, "slave" },
  { IFF_MULTICAST, "multi" },
  { IFF_PORTSEL, "portsel" },
  { IFF_AUTOMEDIA, "automedia" },
  { IFF_DYNAMIC, "dynamic" },
  { IFF_LOWER_UP, "lower-up" },
  { IFF_DORMANT, "dormant" },
  { IFF_ECHO, "echo" }
};

ListValue *flags2list(unsigned int flags) {
  ListValue *lv = new ListValue();
  for (unsigned int i = 0; i < arraysize(ifflags); ++i)
    if (flags & ifflags[i].bit)
      lv->Append(new StringValue(ifflags[i].name));
  if (lv->empty()) {
    delete lv;
    return NULL;
  }
  return lv;
}

// Duplicated from </src/helpers/network_status.cc>. Need to figure out how to
// refactor these.
class ManagerProxy : public org::chromium::flimflam::Manager_proxy,
                     public DBus::ObjectProxy {
 public:
  ManagerProxy(DBus::Connection* connection,
               const char* path,
               const char* service)
      : DBus::ObjectProxy(*connection, path, service) {}
  ~ManagerProxy() override = default;
  void PropertyChanged(const std::string&, const DBus::Variant&) override {}
  void StateChanged(const std::string&) override {}
};

class ServiceProxy : public org::chromium::flimflam::Service_proxy,
                     public DBus::ObjectProxy {
 public:
  ServiceProxy(DBus::Connection* connection,
               const char* path,
               const char* service)
      : DBus::ObjectProxy(*connection, path, service) {}
  ~ServiceProxy() override = default;
  void PropertyChanged(const std::string&, const DBus::Variant&) override {}
};

class NetInterface {
 public:
  NetInterface(int fd, const char *name);
  ~NetInterface() = default;

  bool Init();
  void AddAddress(struct ifaddrs *ifa);
  void AddSignalStrength(const std::string& name, int strength);
  Value *ToValue();

 private:
  int fd_;
  const char *name_;
  DictionaryValue *ipv4_;
  DictionaryValue *ipv6_;
  ListValue *flags_;
  std::string mac_;
  DictionaryValue *signal_strengths_;

  void AddAddressTo(DictionaryValue *dv, struct sockaddr *sa);
};

NetInterface::NetInterface(int fd, const char *name)
    : fd_(fd),
      name_(name),
      ipv4_(NULL),
      ipv6_(NULL),
      flags_(NULL),
      signal_strengths_(NULL) {}

bool NetInterface::Init() {
  mac_ = getmac(fd_, name_);
  return true;
}

void NetInterface::AddSignalStrength(const std::string& name, int strength) {
  if (!signal_strengths_)
    signal_strengths_ = new DictionaryValue();
  signal_strengths_->SetInteger(name, strength);
}

void NetInterface::AddAddressTo(DictionaryValue *dv, struct sockaddr *sa) {
  if (!dv->HasKey("addrs"))
    dv->Set("addrs", new ListValue());
  ListValue *lv;
  dv->Get("addrs", reinterpret_cast<Value**>(&lv));
  lv->Append(new StringValue(sockaddr2str(sa)));
}

void NetInterface::AddAddress(struct ifaddrs *ifa) {
  if (!flags_)
    flags_ = flags2list(ifa->ifa_flags);
  if (!ifa->ifa_addr)
    return;
  if (ifa->ifa_addr->sa_family == AF_INET) {
    // An IPv4 address.
    if (!ipv4_)
      ipv4_ = new DictionaryValue();
    AddAddressTo(ipv4_, ifa->ifa_addr);
    if (!ipv4_->HasKey("mask"))
      ipv4_->Set("mask", new StringValue(sockaddr2str(ifa->ifa_netmask)));
    if (!ipv4_->HasKey("destination"))
      ipv4_->Set("destination",
                 new StringValue(sockaddr2str(ifa->ifa_broadaddr)));
  } else if (ifa->ifa_addr->sa_family == AF_INET6) {
    // An IPv6 address.
    if (!ipv6_)
      ipv6_ = new DictionaryValue();
    AddAddressTo(ipv6_, ifa->ifa_addr);
  }
}

Value *NetInterface::ToValue() {
  DictionaryValue *dv = new DictionaryValue();
  if (ipv4_)
    dv->Set("ipv4", ipv4_);
  if (ipv6_)
    dv->Set("ipv6", ipv6_);
  if (flags_)
    dv->Set("flags", flags_);
  if (signal_strengths_)
    dv->Set("signal-strengths", signal_strengths_);
  dv->Set("mac", new StringValue(mac_));
  return dv;
}

std::string DevicePathToName(const std::string& path) {
  const char *kPrefix = "/device/";
  if (path.find(kPrefix) == 0)
    return path.substr(strlen(kPrefix));
  return "?";
}

void AddSignalStrengths(std::map<std::string, NetInterface*> *interfaces) {
  DBus::BusDispatcher dispatcher;
  DBus::default_dispatcher = &dispatcher;
  DBus::Connection connection = DBus::Connection::SystemBus();
  ManagerProxy manager(&connection,
                       shill::kFlimflamServicePath,
                       shill::kFlimflamServiceName);

  std::map<std::string, DBus::Variant> props = manager.GetProperties();
  if (props.count("Services") != 1)
    return;
  DBus::Variant& devices = props["Services"];
  if (strcmp(devices.signature().c_str(), "ao"))
    return;
  std::vector<DBus::Path> paths = devices;
  for (std::vector<DBus::Path>::iterator it = paths.begin();
       it != paths.end(); ++it) {
    ServiceProxy service(&connection, it->c_str(), shill::kFlimflamServiceName);
    std::map<std::string, DBus::Variant> props = service.GetProperties();
    if (   props.count("Strength") != 1
        || props.count("Name") != 1
        || props.count("Device") != 1)
      continue;
    uint8_t strength = props["Strength"];
    std::string name = props["Name"];
    DBus::Variant& varpath = props["Device"];
    DBus::Path devpath = varpath;
    std::string devname = DevicePathToName(devpath);
    if (interfaces->count(devname)) {
      interfaces->find(devname)->second->AddSignalStrength(name, strength);
    }
  }
}

int main() {
  struct ifaddrs *ifaddrs;
  int fd;
  DictionaryValue result;
  std::map<std::string, NetInterface*> interfaces;

  if (getifaddrs(&ifaddrs) == -1) {
    perror("getifaddrs");
    exit(1);
  }

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket");
    exit(1);
  }

  for (struct ifaddrs *ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
    if (!interfaces.count(ifa->ifa_name)) {
      interfaces[ifa->ifa_name] = new NetInterface(fd, ifa->ifa_name);
      interfaces[ifa->ifa_name]->Init();
    }
    interfaces[ifa->ifa_name]->AddAddress(ifa);
  }

  AddSignalStrengths(&interfaces);

  for (std::map<std::string, NetInterface*>::iterator it = interfaces.begin();
       it != interfaces.end(); ++it)
    result.Set(it->first, it->second->ToValue());

  std::string json;
  base::JSONWriter::WriteWithOptions(
      result, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  printf("%s\n", json.c_str());
  return 0;
}
