// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/static_ip_parameters.h"

#include <algorithm>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece_forward.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>

#include "shill/error.h"
#include "shill/network/network_config.h"
#include "shill/store/property_accessor.h"
#include "shill/store/property_store.h"
#include "shill/store/store_interface.h"

namespace shill {

namespace {

constexpr char kConfigKeyPrefix[] = "StaticIP.";

struct Property {
  enum class Type {
    kInt32,
    kString,
    // Properties of type "Strings" are stored as a comma-separated list in the
    // control interface and in the profile, but are stored as a vector of
    // strings in the IPConfig properties.
    kStrings
  };

  const char* name;
  Type type;
};

constexpr Property kProperties[] = {
    {kAddressProperty, Property::Type::kString},
    {kGatewayProperty, Property::Type::kString},
    {kMtuProperty, Property::Type::kInt32},
    {kNameServersProperty, Property::Type::kStrings},
    {kSearchDomainsProperty, Property::Type::kStrings},
    {kPrefixlenProperty, Property::Type::kInt32},
    {kIncludedRoutesProperty, Property::Type::kStrings},
    {kExcludedRoutesProperty, Property::Type::kStrings},
};

// Converts the StaticIPParameters from KeyValueStore to NetworkConfig.
// Errors are ignored if any value is not valid.
NetworkConfig KeyValuesToNetworkConfig(const KeyValueStore& kvs) {
  NetworkConfig ret;
  if (kvs.Contains<std::string>(kAddressProperty)) {
    const int prefix = kvs.Lookup<int32_t>(kPrefixlenProperty, 0);
    const std::string addr_str = kvs.Get<std::string>(kAddressProperty);
    ret.ipv4_address =
        net_base::IPv4CIDR::CreateFromStringAndPrefix(addr_str, prefix);
  }
  auto gateway_str = kvs.GetOptionalValue<std::string>(kGatewayProperty);
  if (gateway_str) {
    ret.ipv4_gateway = net_base::IPv4Address::CreateFromString(*gateway_str);
  }
  ret.included_route_prefixes = {};
  if (kvs.Contains<Strings>(kIncludedRoutesProperty)) {
    for (const auto& item : kvs.Get<Strings>(kIncludedRoutesProperty)) {
      auto cidr = net_base::IPCIDR::CreateFromCIDRString(item);
      if (cidr) {
        ret.included_route_prefixes.push_back(*cidr);
      }
    }
  }
  ret.excluded_route_prefixes = {};
  if (kvs.Contains<Strings>(kExcludedRoutesProperty)) {
    for (const auto& item : kvs.Get<Strings>(kExcludedRoutesProperty)) {
      auto cidr = net_base::IPCIDR::CreateFromCIDRString(item);
      if (cidr) {
        ret.excluded_route_prefixes.push_back(*cidr);
      }
    }
  }
  ret.mtu = kvs.GetOptionalValue<int32_t>(kMtuProperty);
  ret.dns_servers = {};
  if (kvs.Contains<Strings>(kNameServersProperty)) {
    for (const auto& item : kvs.Get<Strings>(kNameServersProperty)) {
      auto dns = net_base::IPAddress::CreateFromString(item);
      if (dns) {
        ret.dns_servers.push_back(*dns);
      }
    }
  }
  ret.dns_search_domains = kvs.GetOptionalValue<Strings>(kSearchDomainsProperty)
                               .value_or(std::vector<std::string>{});

  // TODO(b/269401899): Currently this is only used by VPN. Check that if the
  // Network class can make this decision by itself after finishing the
  // refactor.
  if (!ret.included_route_prefixes.empty()) {
    ret.ipv4_default_route = false;
  }

  return ret;
}

// Set a Strings property from a vector of objects, by calling ToString()
// function on each of the elements and adding the result to the property string
// vector. Remove the property if |input| is empty.
template <class T>
void SetStringsValueByObjectVector(KeyValueStore& kvs,
                                   std::string_view key,
                                   const std::vector<T>& input) {
  if (!input.empty()) {
    std::vector<std::string> strings;
    std::transform(input.begin(), input.end(), std::back_inserter(strings),
                   [](T item) { return item.ToString(); });
    kvs.Set<Strings>(key, strings);
  } else {
    kvs.Remove(key);
  }
}

}  // namespace

KeyValueStore StaticIPParameters::NetworkConfigToKeyValues(
    const NetworkConfig& props) {
  KeyValueStore kvs;
  if (props.ipv4_address.has_value()) {
    kvs.Set<std::string>(kAddressProperty,
                         props.ipv4_address->address().ToString());
    kvs.Set<int32_t>(kPrefixlenProperty, props.ipv4_address->prefix_length());
  }
  if (props.ipv4_gateway.has_value()) {
    kvs.Set<std::string>(kGatewayProperty, props.ipv4_gateway->ToString());
  }
  kvs.SetFromOptionalValue<int32_t>(kMtuProperty, props.mtu);
  if (!props.dns_search_domains.empty()) {
    kvs.Set<Strings>(kSearchDomainsProperty, props.dns_search_domains);
  }
  SetStringsValueByObjectVector(kvs, kNameServersProperty, props.dns_servers);
  SetStringsValueByObjectVector(kvs, kIncludedRoutesProperty,
                                props.included_route_prefixes);
  SetStringsValueByObjectVector(kvs, kExcludedRoutesProperty,
                                props.excluded_route_prefixes);
  return kvs;
}

StaticIPParameters::StaticIPParameters() = default;

StaticIPParameters::~StaticIPParameters() = default;

void StaticIPParameters::PlumbPropertyStore(PropertyStore* store) {
  // Register KeyValueStore for both static ip parameters.
  store->RegisterDerivedKeyValueStore(
      kStaticIPConfigProperty,
      KeyValueStoreAccessor(
          new CustomAccessor<StaticIPParameters, KeyValueStore>(
              this, &StaticIPParameters::GetStaticIPConfig,
              &StaticIPParameters::SetStaticIP)));
}

bool StaticIPParameters::Load(const StoreInterface* storage,
                              const std::string& storage_id) {
  KeyValueStore args;
  for (const auto& property : kProperties) {
    const std::string name(std::string(kConfigKeyPrefix) + property.name);
    switch (property.type) {
      case Property::Type::kInt32: {
        int32_t value;
        if (storage->GetInt(storage_id, name, &value)) {
          args.Set<int32_t>(property.name, value);
        } else {
          args.Remove(property.name);
        }
      } break;
      case Property::Type::kString: {
        std::string value;
        if (storage->GetString(storage_id, name, &value)) {
          args.Set<std::string>(property.name, value);
        } else {
          args.Remove(property.name);
        }
      } break;
      case Property::Type::kStrings: {
        // Name servers field is stored in storage as comma separated string.
        // Keep it as is to be backward compatible.
        std::string value;
        if (storage->GetString(storage_id, name, &value)) {
          std::vector<std::string> string_list = base::SplitString(
              value, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
          args.Set<Strings>(property.name, string_list);
        } else {
          args.Remove(property.name);
        }
      } break;
      default:
        NOTIMPLEMENTED();
        break;
    }
  }
  return SetStaticIP(args, nullptr);
}

void StaticIPParameters::Save(StoreInterface* storage,
                              const std::string& storage_id) {
  const auto args = NetworkConfigToKeyValues(config_);
  for (const auto& property : kProperties) {
    const std::string name(std::string(kConfigKeyPrefix) + property.name);
    bool property_exists = false;
    switch (property.type) {
      case Property::Type::kInt32:
        if (args.Contains<int32_t>(property.name)) {
          property_exists = true;
          storage->SetInt(storage_id, name, args.Get<int32_t>(property.name));
        }
        break;
      case Property::Type::kString:
        if (args.Contains<std::string>(property.name)) {
          property_exists = true;
          storage->SetString(storage_id, name,
                             args.Get<std::string>(property.name));
        }
        break;
      case Property::Type::kStrings:
        if (args.Contains<Strings>(property.name)) {
          property_exists = true;
          // Name servers field is stored in storage as comma separated string.
          // Keep it as is to be backward compatible.
          storage->SetString(
              storage_id, name,
              base::JoinString(args.Get<Strings>(property.name), ","));
        }
        break;
      default:
        NOTIMPLEMENTED();
        break;
    }
    if (!property_exists) {
      storage->DeleteKey(storage_id, name);
    }
  }
}

KeyValueStore StaticIPParameters::GetStaticIPConfig(Error* /*error*/) {
  return NetworkConfigToKeyValues(config_);
}

bool StaticIPParameters::SetStaticIP(const KeyValueStore& value,
                                     Error* /*error*/) {
  const auto current_args = NetworkConfigToKeyValues(config_);
  if (current_args == value) {
    return false;
  }
  config_ = KeyValuesToNetworkConfig(value);
  return true;
}

void StaticIPParameters::Reset() {
  config_ = NetworkConfig();
}

}  // namespace shill
