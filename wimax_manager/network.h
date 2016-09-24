// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WIMAX_MANAGER_NETWORK_H_
#define WIMAX_MANAGER_NETWORK_H_

#include <stdint.h>

#include <map>
#include <string>

#include <base/macros.h>
#include <base/memory/ref_counted.h>

#include "wimax_manager/dbus_adaptable.h"

namespace wimax_manager {

enum NetworkType {
  kNetworkHome,
  kNetworkPartner,
  kNetworkRoamingParnter,
  kNetworkUnknown,
};

class NetworkDBusAdaptor;

class Network : public base::RefCounted<Network>,
                public DBusAdaptable<Network, NetworkDBusAdaptor> {
 public:
  using Identifier = uint32_t;

  static const int kMaxCINR;
  static const int kMinCINR;
  static const int kMaxRSSI;
  static const int kMinRSSI;
  static const Identifier kInvalidIdentifier;

  Network(Identifier identifier, const std::string &name, NetworkType type,
          int cinr, int rssi);

  static int DecodeCINR(int encoded_cinr);
  static int DecodeRSSI(int encoded_rssi);

  void UpdateFrom(const Network &network);
  int GetSignalStrength() const;

  // Returns a string description that comprises |name_| and |identifier_|.
  // If |name_| is empty, returns "network (<8-digit hexadecimal identifier>)".
  // Otherwise, returns "network '<name>' (<8-digit hexadecimal identifier>)".
  std::string GetNameWithIdentifier() const;

  Identifier identifier() const { return identifier_; }
  const std::string &name() const { return name_; }
  NetworkType type() const { return type_; }
  int cinr() const { return cinr_; }
  int rssi() const { return rssi_; }

 private:
  friend class base::RefCounted<Network>;

  ~Network();

  Identifier identifier_;
  std::string name_;
  NetworkType type_;
  int cinr_;
  int rssi_;

  DISALLOW_COPY_AND_ASSIGN(Network);
};

using NetworkRefPtr = scoped_refptr<Network>;
using NetworkMap = std::map<Network::Identifier, NetworkRefPtr>;

}  // namespace wimax_manager

#endif  // WIMAX_MANAGER_NETWORK_H_
