// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WIFI_PHY_H_
#define SHILL_WIFI_WIFI_PHY_H_

#include <map>
#include <set>
#include <string_view>
#include <vector>

#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/mockable.h"
#include "shill/refptr_types.h"
#include "shill/wifi/nl80211_message.h"
#include "shill/wifi/wifi_rf.h"

namespace shill {
class LocalDevice;
class WiFi;
class WiFiProvider;

struct IfaceLimit {
  std::vector<nl80211_iftype> iftypes;
  uint32_t max;
};

struct ConcurrencyCombination {
  std::vector<IfaceLimit> limits;
  uint32_t max_num;
  uint32_t num_channels;
};

// Compare two ConcurrencyCombinations by number of channels.
struct ConcurrencyCombinationComparator {
  bool operator()(ConcurrencyCombination lhs,
                  ConcurrencyCombination rhs) const {
    return lhs.num_channels > rhs.num_channels;
  }
};

// A set of ConcurrencyCombination structs, sorted by number of channels, with
// higher channel counts coming first.
typedef std::multiset<ConcurrencyCombination, ConcurrencyCombinationComparator>
    ConcurrencyCombinationSet;

// A WiFiPhy object represents a wireless physical layer device. Objects of this
// class map 1:1 with an NL80211 "wiphy". WiFiPhy objects are created and owned
// by the WiFiProvider singleton. The lifecycle of a WiFiPhy object begins with
// the netlink command NL80211_CMD_NEW_WIPHY and ends with
// NL80211_CMD_DEL_WIPHY.

// TODO(b/244630773): Update WiFiPhy to store phy capabilities, and update the
// documentation accordingly.

class WiFiPhy {
 public:
  // A Priority object represents the priority of a WiFi interface, to be used
  // in concurrency conflict resolution.
  class Priority {
   public:
    static constexpr int32_t kMinimumPriority = 0;
    static constexpr int32_t kMaximumPriority =
        static_cast<int32_t>(WiFiInterfacePriority::NUM_PRIORITIES) - 1;
    explicit constexpr Priority(int32_t priority) : priority_(priority) {}
    ~Priority() = default;
    operator int32_t() const { return priority_; }
    bool IsValid() {
      return kMinimumPriority <= priority_ && priority_ <= kMaximumPriority;
    }

   private:
    int32_t priority_;
  };

  explicit WiFiPhy(uint32_t phy_index);

  virtual ~WiFiPhy();

  // Return the phy index.
  uint32_t GetPhyIndex() const { return phy_index_; }

  // Return the wifi devices.
  const std::set<WiFiConstRefPtr>& GetWiFiDevices() const {
    return wifi_devices_;
  }

  // Remove a WiFi device instance from wifi_devices_.
  void DeleteWiFiDevice(std::string_view link_name);

  // Add a WiFi device instance to wifi_devices_.
  void AddWiFiDevice(WiFiConstRefPtr device);

  // Indicates that a WiFi device's state has changed.
  void WiFiDeviceStateChanged(WiFiConstRefPtr device);

  // Remove a WiFi local device instance from wifi_local_devices_.
  void DeleteWiFiLocalDevice(LocalDeviceConstRefPtr device);

  // Add a WiFi local device instance to wifi_local_devices_.
  void AddWiFiLocalDevice(LocalDeviceConstRefPtr device);

  // Signals the end of the sequence of the PHY dump messages - all the
  // frequencies cached during parsing of NewWiphy messages are accepted as
  // a new value.
  mockable void PhyDumpComplete();

  // Parse an NL80211_CMD_NEW_WIPHY netlink message.
  // TODO(b/248103586): Move NL80211_CMD_NEW_WIPHY parsing out of WiFiPhy and
  // into WiFiProvider.
  mockable void OnNewWiphy(const Nl80211Message& nl80211_message);

  // Return true if the phy supports iftype, false otherwise.
  bool SupportsIftype(nl80211_iftype iftype) const;

  // Returns true if the PHY handles 802.11d country notifications (for
  // automatic changes of regulatory domains).
  mockable bool reg_self_managed() const { return reg_self_managed_; }

  ConcurrencyCombinationSet ConcurrencyCombinations() {
    return concurrency_combs_;
  }

  // Helper functions to retrieve WiFiPhy capabilities.
  // Return true if the phy supports AP interface type, false otherwise.
  mockable bool SupportAPMode() const;

  // Helper functions to retrieve WiFiPhy capabilities.
  // Return true if the phy supports P2P interface type, false otherwise.
  mockable bool SupportP2PMode() const;

  // Return the number of channels on which all ifaces in |iface_types| can be
  // operated concurrently. A return value of 0 indicates that the
  // concurrency isn't supported at all.
  mockable uint32_t
  SupportsConcurrency(const std::multiset<nl80211_iftype>& iface_types) const;

  // Return true if the phy supports AP/STA concurrency, false otherwise.
  mockable bool SupportAPSTAConcurrency() const;

  // Returns the set of interfaces which must be destroyed before enabling an
  // interface with |priority| and |desired_type|. An empty return set
  // indicates the interface can be created without destroying any existing
  // interfaces. An std::nullopt indicates that the interface cannot be started
  // at all.
  mockable std::optional<std::multiset<nl80211_iftype>> RequestNewIface(
      nl80211_iftype desired_type, Priority priority) const;

  // This structure keeps information about frequency reported in PHY dump.
  // |flags| is a bitmap with bits corresponding to NL80211_FREQUENCY_ATTR_*
  // flags reported, |value| is the actual frequency in MHz and |attributes|
  // keeps map of reported attributes that has value (e.g.
  // NL80211_FREQUENCY_ATTR_MAX_TX_POWER)
  struct Frequency {
    uint64_t flags = 0;
    uint32_t value = 0;
    std::map<int, uint32_t> attributes;
  };

  // Frequencies available are returned as a map:
  //   "band" -> "list of frequencies".
  // The key (band) is the NL band attribute (NL80211_BAND_2GHZ etc.) and the
  // value is just vector of Frequency structs (see above).
  using Frequencies = std::map<int, std::vector<Frequency>>;
  // Utility function to choose frequency from the available frequencies with
  // |band| preference. Returns frequency or std::nullopt on error.
  std::optional<int> SelectFrequency(WiFiBand band) const;
  // Utility function to get the frequencies supported.
  mockable std::vector<int> GetFrequencies() const;

 private:
  friend class WiFiPhyTest;
  friend class MockWiFiPhy;

  FRIEND_TEST(WiFiPhyTest, IfaceSorted);
  FRIEND_TEST(WiFiPhyTest, RemovalCandidateSet);
  FRIEND_TEST(WiFiPhyTest, SupportsConcurrency);
  FRIEND_TEST(WiFiPhyTest, RemovalCandidateSet2);
  FRIEND_TEST(WiFiPhyTest, GetAllCandidates);
  FRIEND_TEST(WiFiPhyTest, GetAllCandidates_empty);

  // Represents an interface under consideration for concurrent operation.
  // Contains the relevant bits of information about a WiFi interface which are
  // required for making concurrency decisions. Used to allow common comparison
  // of interfaces which may have different object types.
  struct ConcurrentIface {
    nl80211_iftype iftype;
    Priority priority;
    bool operator==(const ConcurrentIface&) const = default;
  };

  // Compares ConcurrentIface structs by their priority values, with higher
  // priorities coming first.
  struct CompareConcurrentIface {
    bool operator()(ConcurrentIface lhs, ConcurrentIface rhs) const {
      return lhs.priority > rhs.priority;
    }
  };

  // A set of interfaces which are candidates to be removed in concurrency
  // conflict resolution. Interfaces are sorted by their priority.
  typedef std::multiset<ConcurrentIface, CompareConcurrentIface>
      RemovalCandidate;

  // Compares removal candidates by their preferability. A candidate is
  // preferable if it includes fewer interfaces at a given priority level than
  // another candidate, with higher priorities taking precedence.
  // TODO(b/328075705): Add a reference to the documentation which fully details
  // this comparison.
  struct RemovalCandidateComparator {
    bool operator()(const RemovalCandidate& lhs,
                    const RemovalCandidate& rhs) const {
      auto lhs_iter = lhs.begin();
      auto rhs_iter = rhs.begin();
      // RemovalCandidates are always sorted by priority, so we can step through
      // them and compare elements one-by-one.
      while (true) {
        // If we've reached the end of either candidate, that must be the
        // preferable candidate. (If we've reached the end of both, that's a tie
        // and it's fine to prefer the lhs).
        if (lhs_iter == lhs.end()) {
          return true;
        }
        if (rhs_iter == rhs.end()) {
          return false;
        }
        if (lhs_iter->priority == rhs_iter->priority) {
          lhs_iter++;
          rhs_iter++;
          continue;
        }
        return lhs_iter->priority < rhs_iter->priority;
      }
    }
  };

  // A set of RemovalCandidates sorted by their preferability.
  typedef std::multiset<RemovalCandidate, RemovalCandidateComparator>
      RemovalCandidateSet;

  // Helper functions used to parse NL80211_CMD_NEW_WIPHY message.  They take
  // relevant portion (attribute), parse it and store the information in member
  // variables.  Respectively these are:
  // - NL80211_ATTR_SUPPORTED_IFTYPES -> supported_ifaces_
  // - NL80211_ATTR_INTERFACE_COMBINATIONS -> concurrency_combs_
  // - NL80211_ATTR_WIPHY_BANDS/NL80211_BAND_ATTR_FREQS -> frequencies_
  void ParseInterfaceTypes(const Nl80211Message& nl80211_message);
  void ParseConcurrency(const Nl80211Message& nl80211_message);
  void ParseFrequencies(const Nl80211Message& nl80211_message);

  void DumpFrequencies() const;

  // Helper for interface concurrency checking.
  static bool CombSupportsConcurrency(
      ConcurrencyCombination comb,
      std::multiset<nl80211_iftype> desired_iftypes);

  static RemovalCandidateSet GetAllCandidates(
      std::vector<ConcurrentIface> ifaces);

  uint32_t phy_index_;
  bool reg_self_managed_;
  std::set<WiFiConstRefPtr> wifi_devices_;
  std::set<LocalDeviceConstRefPtr> wifi_local_devices_;
  std::set<nl80211_iftype> supported_ifaces_;
  ConcurrencyCombinationSet concurrency_combs_;
  Frequencies frequencies_;
  // This is temporarily used during parsing of WiFi PHY dumps.  At the end of
  // PHY dump this is transferred into |frequencies_| - see also
  // PhyDumpComplete().
  Frequencies temp_freqs_;
};

inline bool operator==(const WiFiPhy::Frequency& f1,
                       const WiFiPhy::Frequency& f2) {
  return f1.value == f2.value && f1.flags == f2.flags &&
         f1.attributes == f2.attributes;
}

// Operators to facilitate interface combination logging.
std::ostream& operator<<(std::ostream& out, const nl80211_iftype& it);

std::ostream& operator<<(std::ostream& out,
                         const std::vector<nl80211_iftype>& it);

std::ostream& operator<<(std::ostream& out, const IfaceLimit& il);

std::ostream& operator<<(std::ostream& out, const std::vector<IfaceLimit>& il);

std::ostream& operator<<(std::ostream& out, const ConcurrencyCombination& cc);

std::ostream& operator<<(std::ostream& out,
                         const std::vector<ConcurrencyCombination>& cc);

}  // namespace shill

#endif  // SHILL_WIFI_WIFI_PHY_H_
