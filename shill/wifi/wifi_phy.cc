// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <numeric>
#include <utility>

#include <base/containers/contains.h>
#include <base/rand_util.h>
#include <net-base/attribute_list.h>
#include <net-base/netlink_attribute.h>

#include "shill/logging.h"
#include "shill/wifi/wifi_phy.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kWiFi;
}  // namespace Logging

namespace {
void removeIface(std::vector<ConcurrencyCombination>* combinations,
                 nl80211_iftype iftype) {
  // Decrease/remove limit in each combination variant.
  // When removing elements on the fly, a simplified for loop cannot be used
  // because erase() increases iterator by itself.
  for (auto cit = combinations->begin(); cit != combinations->end();
       /* Empty on purpose. */) {
    // The same limitation applies for the limits loop.
    for (auto lit = cit->limits.begin(); lit != cit->limits.end();
         /* Empty on purpose. */) {
      if (base::Contains(lit->iftypes, iftype)) {
        lit->max--;
        cit->max_num--;
      }
      if (lit->max <= 0) {
        cit->limits.erase(lit);
      } else {
        ++lit;
      }
    }
    if (cit->max_num <= 0) {
      combinations->erase(cit);
    } else {
      ++cit;
    }
  }
}
}  // namespace

WiFiPhy::WiFiPhy(uint32_t phy_index)
    : phy_index_(phy_index), reg_self_managed_(false) {}

WiFiPhy::~WiFiPhy() = default;

void WiFiPhy::AddWiFiDevice(WiFiConstRefPtr device) {
  wifi_devices_.insert(device);
}

void WiFiPhy::DeleteWiFiDevice(WiFiConstRefPtr device) {
  wifi_devices_.erase(device);
}

void WiFiPhy::AddWiFiLocalDevice(LocalDeviceConstRefPtr device) {
  wifi_local_devices_.insert(device);
}

void WiFiPhy::DeleteWiFiLocalDevice(LocalDeviceConstRefPtr device) {
  wifi_local_devices_.erase(device);
}

// TODO(b/248103586): Move NL80211_CMD_NEW_WIPHY parsing out of WiFiPhy and into
// WiFiProvider.
void WiFiPhy::OnNewWiphy(const Nl80211Message& nl80211_message) {
  if (nl80211_message.const_attributes()->IsFlagAttributeTrue(
          NL80211_ATTR_WIPHY_SELF_MANAGED_REG)) {
    reg_self_managed_ = true;
  }
  ParseInterfaceTypes(nl80211_message);
  // TODO(b/244630773): Parse out the message and store phy information.
  ParseConcurrency(nl80211_message);
  ParseFrequencies(nl80211_message);
}

bool WiFiPhy::SupportsIftype(nl80211_iftype iftype) const {
  return base::Contains(supported_ifaces_, iftype);
}

void WiFiPhy::ParseInterfaceTypes(const Nl80211Message& nl80211_message) {
  net_base::AttributeListConstRefPtr ifaces;
  if (nl80211_message.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_SUPPORTED_IFTYPES, &ifaces)) {
    net_base::AttributeIdIterator ifaces_iter(*ifaces);
    for (; !ifaces_iter.AtEnd(); ifaces_iter.Advance()) {
      uint32_t iface;
      if (!ifaces->GetU32AttributeValue(ifaces_iter.GetId(), &iface)) {
        LOG(ERROR) << "Failed to get supported iface type "
                   << ifaces_iter.GetId();
        continue;
      }
      if (iface < 0 || iface > NL80211_IFTYPE_MAX) {
        LOG(ERROR) << "Invalid iface type: " << iface;
        continue;
      }
      supported_ifaces_.insert(nl80211_iftype(iface));
    }
  }
}

void WiFiPhy::ParseConcurrency(const Nl80211Message& nl80211_message) {
  // Check that the message contains concurrency combinations.
  net_base::AttributeListConstRefPtr interface_combinations_attr;
  if (!nl80211_message.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_INTERFACE_COMBINATIONS, &interface_combinations_attr)) {
    return;
  }
  // Iterate over the combinations in the message.
  concurrency_combs_.clear();
  net_base::AttributeIdIterator comb_iter(*interface_combinations_attr);
  for (; !comb_iter.AtEnd(); comb_iter.Advance()) {
    struct ConcurrencyCombination comb;
    net_base::AttributeListConstRefPtr iface_comb_attr;
    if (!interface_combinations_attr->ConstGetNestedAttributeList(
            comb_iter.GetId(), &iface_comb_attr)) {
      continue;  // Next combination.
    }

    // Check that the combination has limits.
    net_base::AttributeListConstRefPtr iface_limits_attr;
    if (!iface_comb_attr->ConstGetNestedAttributeList(NL80211_IFACE_COMB_LIMITS,
                                                      &iface_limits_attr)) {
      continue;  // Next combination.
    }

    iface_comb_attr->GetU32AttributeValue(NL80211_IFACE_COMB_MAXNUM,
                                          &comb.max_num);
    iface_comb_attr->GetU32AttributeValue(NL80211_IFACE_COMB_NUM_CHANNELS,
                                          &comb.num_channels);

    net_base::AttributeIdIterator limit_iter(*iface_limits_attr);
    for (; !limit_iter.AtEnd(); limit_iter.Advance()) {
      struct IfaceLimit limit;
      net_base::AttributeListConstRefPtr limit_attr;
      if (!iface_limits_attr->ConstGetNestedAttributeList(limit_iter.GetId(),
                                                          &limit_attr)) {
        LOG(WARNING) << "Interface combination limit " << limit_iter.GetId()
                     << " not found";
        // If we reach this line then the message is malformed and we should
        // stop parsing it.
        return;
      }
      limit_attr->GetU32AttributeValue(NL80211_IFACE_LIMIT_MAX, &limit.max);

      // Check that the limit contains interface types.
      net_base::AttributeListConstRefPtr iface_types_attr;
      if (!limit_attr->ConstGetNestedAttributeList(NL80211_IFACE_LIMIT_TYPES,
                                                   &iface_types_attr)) {
        continue;
      }
      for (uint32_t iftype = NL80211_IFTYPE_UNSPECIFIED;
           iftype < NUM_NL80211_IFTYPES; iftype++) {
        if (iface_types_attr->GetFlagAttributeValue(iftype, nullptr)) {
          limit.iftypes.push_back(nl80211_iftype(iftype));
        }
      }
      comb.limits.push_back(limit);
    }
    concurrency_combs_.push_back(comb);
  }
}

void WiFiPhy::PhyDumpComplete() {
  SLOG(3) << __func__;
  std::swap(frequencies_, temp_freqs_);
  temp_freqs_.clear();
  DumpFrequencies();
}

void WiFiPhy::ParseFrequencies(const Nl80211Message& nl80211_message) {
  // Code below depends on being able to pack all flags into bits.
  static_assert(
      sizeof(WiFiPhy::Frequency::flags) * CHAR_BIT > NL80211_FREQUENCY_ATTR_MAX,
      "Not enough bits to hold all possible flags");

  SLOG(3) << __func__;
  if (!(nl80211_message.flags() & NLM_F_MULTI)) {
    return;
  }

  net_base::AttributeListConstRefPtr bands_list;
  if (nl80211_message.const_attributes()->ConstGetNestedAttributeList(
          NL80211_ATTR_WIPHY_BANDS, &bands_list)) {
    net_base::AttributeIdIterator bands_iter(*bands_list);
    for (; !bands_iter.AtEnd(); bands_iter.Advance()) {
      // Each band has nested attributes and ...
      net_base::AttributeListConstRefPtr band_attrs;
      if (bands_list->ConstGetNestedAttributeList(bands_iter.GetId(),
                                                  &band_attrs)) {
        int current_band = bands_iter.GetId();
        // ... we are interested in freqs (which itself is a nested attribute).
        net_base::AttributeListConstRefPtr freqs_list;
        if (!band_attrs->ConstGetNestedAttributeList(NL80211_BAND_ATTR_FREQS,
                                                     &freqs_list)) {
          continue;
        }
        net_base::AttributeIdIterator freqs_iter(*freqs_list);
        for (; !freqs_iter.AtEnd(); freqs_iter.Advance()) {
          net_base::AttributeListConstRefPtr freq_attrs;
          if (freqs_list->ConstGetNestedAttributeList(freqs_iter.GetId(),
                                                      &freq_attrs)) {
            Frequency freq;
            for (auto attr = net_base::AttributeIdIterator(*freq_attrs);
                 !attr.AtEnd(); attr.Advance()) {
              if (attr.GetType() == net_base::NetlinkAttribute::kTypeFlag) {
                freq.flags |= 1 << attr.GetId();
              } else {
                if (attr.GetId() == NL80211_FREQUENCY_ATTR_FREQ) {
                  freq_attrs->GetU32AttributeValue(attr.GetId(), &freq.value);
                } else {
                  if (!freq_attrs->GetU32AttributeValue(
                          attr.GetId(), &freq.attributes[attr.GetId()])) {
                    LOG(WARNING) << "Failed to read frequency attribute: "
                                 << attr.GetId();
                  }
                }
              }
            }
            if (freq.value == 0) {
              continue;
            }
            SLOG(3) << "Found frequency: " << freq.value;
            auto& fvec = temp_freqs_[current_band];
            auto it =
                std::find_if(std::begin(fvec), std::end(fvec),
                             [&](auto& f) { return f.value == freq.value; });
            if (it == fvec.end()) {
              temp_freqs_[current_band].emplace_back(std::move(freq));
            } else {
              LOG(WARNING) << "Repeated frequency in WIPHY dump: "
                           << freq.value;
              *it = std::move(freq);
            }
          }
        }
      }
    }
  }
}

bool WiFiPhy::SupportAPMode() const {
  return SupportsIftype(NL80211_IFTYPE_AP);
}

bool WiFiPhy::SupportP2PMode() const {
  return SupportsIftype(NL80211_IFTYPE_P2P_GO) &&
         SupportsIftype(NL80211_IFTYPE_P2P_CLIENT) &&
         SupportsIftype(NL80211_IFTYPE_P2P_DEVICE);
}

uint32_t WiFiPhy::SupportConcurrency(nl80211_iftype iface_type1,
                                     nl80211_iftype iface_type2) const {
  uint32_t max_channels = 0;
  for (auto comb : concurrency_combs_) {
    if (comb.max_num < 2) {
      // Support less than 2 interfaces combination, skip this combination.
      continue;
    }

    bool support_type1 = false;
    bool support_type2 = false;

    for (auto limit : comb.limits) {
      std::set<nl80211_iftype> iftypes(limit.iftypes.begin(),
                                       limit.iftypes.end());

      if (limit.max == 1 && base::Contains(iftypes, iface_type1) &&
          base::Contains(iftypes, iface_type2)) {
        // Case #{ iface_type1, iface_type2 } <= 1 does not meet concurrency
        // requirement, skip and check next combination.
        break;
      }
      if (base::Contains(iftypes, iface_type1)) {
        support_type1 = true;
      } else if (base::Contains(iftypes, iface_type2)) {
        support_type2 = true;
      }
    }

    if (support_type1 && support_type2) {
      // This combination already satisfies concurrency, skip checking the rest
      // combinations.
      max_channels = std::max(max_channels, comb.num_channels);
    }
  }
  return max_channels;
}

bool WiFiPhy::SupportAPSTAConcurrency() const {
  return SupportConcurrency(NL80211_IFTYPE_AP, NL80211_IFTYPE_STATION) > 0;
}

WiFiPhy::ConcurrencySupportLevel WiFiPhy::P2PSTAConcurrency() const {
  // For now, we assume we need both P2P-GO and -Client modes need
  // to be concurrent with STA.
  auto go_conc =
      SupportConcurrency(NL80211_IFTYPE_P2P_GO, NL80211_IFTYPE_STATION);
  auto client_conc =
      SupportConcurrency(NL80211_IFTYPE_P2P_CLIENT, NL80211_IFTYPE_STATION);
  switch ((go_conc < client_conc) ? go_conc : client_conc) {
    case 0:
      return ConcurrencySupportLevel::SingleMode;
    case 1:
      return ConcurrencySupportLevel::SCC;
    default:  // (2+)
      return ConcurrencySupportLevel::MCC;
  }
}

bool WiFiPhy::ReserveIfaceType(nl80211_iftype iftype,
                               unsigned int min_channels) {
  if (CanUseIface(iftype, min_channels)) {
    concurrency_reservations_.push_back(iftype);
    return true;
  }
  return false;
}

bool WiFiPhy::FreeIfaceType(nl80211_iftype iftype) {
  // std::remove is not used, because only one instance of the
  // interface needs to be removed.
  auto it = std::find(concurrency_reservations_.begin(),
                      concurrency_reservations_.end(), iftype);
  if (it != concurrency_reservations_.end()) {
    concurrency_reservations_.erase(it);
    return true;
  }
  return false;
}

bool WiFiPhy::CanUseIface(nl80211_iftype iftype,
                          unsigned int min_channels) const {
  // Copy the current interface combinations;
  auto combinations_copy = concurrency_combs_;

  // Eliminate existing types, then only possible will remain.
  for (auto& rm_type : concurrency_reservations_) {
    removeIface(&combinations_copy, rm_type);
  }

  for (auto& combination : combinations_copy) {
    if (combination.num_channels < min_channels) {
      // No point checking limits, if channel number requirement is not met.
      continue;
    }
    for (auto& limit : combination.limits) {
      if (base::Contains(limit.iftypes, iftype)) {
        LOG(INFO) << iftype
                  << " allowed, valid combinations left: " << combinations_copy;
        return true;
      }
    }
  }
  LOG(WARNING) << iftype << " not allowed, no valid combinations left: "
               << combinations_copy;
  return false;
}

void WiFiPhy::DumpFrequencies() const {
  SLOG(3) << "Available frequencies:";
  for (auto band : frequencies_) {
    for (auto& freq : band.second) {
      SLOG(3) << "  Frequency " << freq.value << ", flag 0x" << std::hex
              << freq.flags;
    }
  }
}

std::optional<int> WiFiPhy::SelectFrequency(WiFiBand band) const {
  LOG(INFO) << "Select Frequency from band: " << band;
  DumpFrequencies();
  if (frequencies_.empty()) {
    LOG(ERROR) << "No valid band found";
    return std::nullopt;
  }
  size_t total_freqs = std::accumulate(
      frequencies_.begin(), frequencies_.end(), 0,
      [](auto acc, auto band) { return acc + band.second.size(); });
  if (total_freqs == 0) {
    LOG(ERROR) << "No valid frequency found";
    return std::nullopt;
  }

  std::vector<int> band_idxs;
  switch (band) {
    case WiFiBand::kLowBand:
      band_idxs = {NL80211_BAND_2GHZ};
      break;
    case WiFiBand::kHighBand:
      band_idxs = {NL80211_BAND_5GHZ};
      break;
    case WiFiBand::kAllBands:
    default:
      // Note that the order matters - preferred band comes first.
      band_idxs = {NL80211_BAND_5GHZ, NL80211_BAND_2GHZ};
  }

  int selected = -1;
  std::vector<uint32_t> freqs;

  for (auto bidx : band_idxs) {
    auto band = frequencies_.find(bidx);
    if (band == frequencies_.end()) {
      continue;
    }
    freqs.reserve(band->second.size());
    for (auto& freq : band->second) {
      if (freq.flags & (1 << NL80211_FREQUENCY_ATTR_DISABLED |
                        1 << NL80211_FREQUENCY_ATTR_NO_IR |
                        1 << NL80211_FREQUENCY_ATTR_RADAR) ||
          IsWiFiLimitedFreq(freq.value)) {
        SLOG(3) << "Skipping freq: " << freq.value;
        continue;
      }
      freqs.push_back(freq.value);
    }
    // We are moving now to a less preferred band, so if we have valid frequency
    // let's just keep it.
    if (!freqs.empty()) {
      selected = freqs[base::RandInt(0, freqs.size() - 1)];
      break;
    }
  }
  if (selected == -1) {
    LOG(ERROR) << "No usable frequency found";
    return std::nullopt;
  } else {
    LOG(INFO) << "Selected frequency: " << selected;
  }
  return selected;
}

// Operators to facilitate interface combination logging.
std::ostream& operator<<(std::ostream& out, const nl80211_iftype& it) {
  switch (it) {
    case NL80211_IFTYPE_ADHOC:
      return out << "IBSS";
    case NL80211_IFTYPE_STATION:
      return out << "STA";
    case NL80211_IFTYPE_AP:
      return out << "AP";
    case NL80211_IFTYPE_P2P_CLIENT:
      return out << "P2P_CLIENT";
    case NL80211_IFTYPE_P2P_GO:
      return out << "P2P_GO";
    case NL80211_IFTYPE_P2P_DEVICE:
      return out << "P2P_DEVICE";
    default:
      return out << "unknown(" << int(it) << ")";
  }
}

std::ostream& operator<<(std::ostream& out,
                         const std::vector<nl80211_iftype>& it) {
  out << "{ ";
  for (const auto& i : it)
    out << i << ", ";
  out << "\b\b }";
  return out;
}

std::ostream& operator<<(std::ostream& out, const IfaceLimit& il) {
  return out << "{ iftypes: " << il.iftypes << ", max:" << il.max << " }";
}

std::ostream& operator<<(std::ostream& out, const std::vector<IfaceLimit>& il) {
  out << "{ ";
  for (const auto& c : il)
    out << c << ", ";
  out << "\b\b }";
  return out;
}

std::ostream& operator<<(std::ostream& out, const ConcurrencyCombination& cc) {
  return out << "{ limits: " << cc.limits << ", max_num:" << cc.max_num
             << ", num_channels: " << cc.num_channels << " }";
}

std::ostream& operator<<(std::ostream& out,
                         const std::vector<ConcurrencyCombination>& cc) {
  out << "{ ";
  for (const auto& c : cc)
    out << c << ", ";
  out << "\b\b }";
  return out;
}

}  // namespace shill
