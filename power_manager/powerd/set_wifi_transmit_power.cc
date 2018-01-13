// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Helper program for setting WiFi transmission power.

#include <string>
#include <vector>

#include <linux/nl80211.h>
#include <net/if.h>

#include <base/at_exit.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/string_util.h>
#include <base/sys_info.h>
#include <brillo/flag_helper.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>

// Vendor command definition for marvell mwifiex driver
// Defined in Linux kernel:
// drivers/net/wireless/marvell/mwifiex/main.h
#define MWIFIEX_VENDOR_ID 0x005043

// Vendor sub command
#define MWIFIEX_VENDOR_CMD_SET_TX_POWER_LIMIT 0

#define MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24 1
#define MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52 2

// Vendor command definition for intel iwl7000 driver
// Defined in Linux kernel:
// drivers/net/wireless/iwl7000/iwlwifi/mvm/vendor-cmd.h
#define INTEL_OUI 0x001735

// Vendor sub command
#define IWL_MVM_VENDOR_CMD_SET_SAR_PROFILE 28

#define IWL_MVM_VENDOR_ATTR_SAR_CHAIN_A_PROFILE 58
#define IWL_MVM_VENDOR_ATTR_SAR_CHAIN_B_PROFILE 59

#define IWL_TABLET_PROFILE_INDEX 1
#define IWL_CLAMSHELL_PROFILE_INDEX 2

// Legacy vendor subcommand used for devices without limits in VPD.
#define IWL_MVM_VENDOR_CMD_SET_NIC_TXPOWER_LIMIT 13

#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_24 13
#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52L 14
#define IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52H 15

namespace {

// Kernel module path for Marvell wireless driver.
const char kMwifiexModulePath[] = "/sys/module/mwifiex";

int ErrorHandler(struct sockaddr_nl* nla, struct nlmsgerr* err, void* arg) {
  *static_cast<int*>(arg) = err->error;
  return NL_STOP;
}

int FinishHandler(struct nl_msg* msg, void* arg) {
  *static_cast<int*>(arg) = 0;
  return NL_SKIP;
}

int AckHandler(struct nl_msg* msg, void* arg) {
  *static_cast<int*>(arg) = 0;
  return NL_STOP;
}

int ValidHandler(struct nl_msg* msg, void* arg) {
  return NL_OK;
}

enum class WirelessDriver { NONE, MWIFIEX, IWL };

// Returns the type of wireless driver that's present on the system.
WirelessDriver GetWirelessDriverType() {
  // TODO(crbug.com/782924): Improve wireless driver detection.
  return base::PathExists(base::FilePath(kMwifiexModulePath))
             ? WirelessDriver::MWIFIEX
             : WirelessDriver::IWL;
}

// Returns the wireless device index (either wlan0 or mlan0).
// 0 is returned if the device isn't found.
uint32_t GetWirelessDeviceIndex() {
  // TODO(crbug.com/782924): Don't hardcode device names.
  uint32_t index = if_nametoindex("mlan0");
  if (!index)
    index = if_nametoindex("wlan0");

  return index;
}

// Fill in nl80211 message for the mwifiex driver.
void FillMessageMwifiex(struct nl_msg* msg, bool tablet) {
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, MWIFIEX_VENDOR_ID))
      << "Failed to put NL80211_ATTR_VENDOR_ID";
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                     MWIFIEX_VENDOR_CMD_SET_TX_POWER_LIMIT))
      << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* limits = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
  CHECK(limits) << "Failed in nla_nest_start";

  CHECK(!nla_put_u8(msg, MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24, tablet))
      << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24";
  CHECK(!nla_put_u8(msg, MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52, tablet))
      << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52";
  CHECK(!nla_nest_end(msg, limits)) << "Failed in nla_nest_end";
}

// Returns a vector of three IWL transmit power limits for mode |tablet| if the
// board doesn't contain limits in VPD, or an empty vector if VPD should be
// used. VPD limits are expected; this is just a hack for devices (currently
// only cave) that lack limits in VPD. See b:70549692 for details.
std::vector<uint32_t> GetNonVpdIwlPowerTable(bool tablet) {
  // Get the board name minus an e.g. "-signed-mpkeys" suffix.
  std::string board = base::SysInfo::GetLsbReleaseBoard();
  const size_t index = board.find("-signed-");
  if (index != std::string::npos)
    board.resize(index);

  if (board == "cave") {
    return tablet ? std::vector<uint32_t>{13, 9, 9}
                  : std::vector<uint32_t>{13, 9, 9};
  }
  return {};
}

// Fill in nl80211 message for the iwl driver.
void FillMessageIwl(struct nl_msg* msg, bool tablet) {
  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, INTEL_OUI))
      << "Failed to put NL80211_ATTR_VENDOR_ID";

  const std::vector<uint32_t> table = GetNonVpdIwlPowerTable(tablet);
  const bool use_vpd = table.empty();

  CHECK(!nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
                     use_vpd ? IWL_MVM_VENDOR_CMD_SET_SAR_PROFILE
                             : IWL_MVM_VENDOR_CMD_SET_NIC_TXPOWER_LIMIT))
      << "Failed to put NL80211_ATTR_VENDOR_SUBCMD";

  struct nlattr* limits = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
  CHECK(limits) << "Failed in nla_nest_start";

  if (use_vpd) {
    int index = tablet ? IWL_TABLET_PROFILE_INDEX : IWL_CLAMSHELL_PROFILE_INDEX;
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_SAR_CHAIN_A_PROFILE, index))
        << "Failed to put IWL_MVM_VENDOR_ATTR_SAR_CHAIN_A_PROFILE";
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_SAR_CHAIN_B_PROFILE, index))
        << "Failed to put IWL_MVM_VENDOR_ATTR_SAR_CHAIN_B_PROFILE";
  } else {
    DCHECK_EQ(table.size(), 3);
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_24, table[0] * 8))
        << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_24";
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52L, table[1] * 8))
        << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52L";
    CHECK(!nla_put_u32(msg, IWL_MVM_VENDOR_ATTR_TXP_LIMIT_52H, table[2] * 8))
        << "Failed to put MWIFIEX_VENDOR_CMD_ATTR_TXP_LIMIT_52H";
  }

  CHECK(!nla_nest_end(msg, limits)) << "Failed in nla_nest_end";
}

class PowerSetter {
 public:
  PowerSetter() : nl_sock_(nl_socket_alloc()), cb_(nl_cb_alloc(NL_CB_DEFAULT)) {
    CHECK(nl_sock_);
    CHECK(cb_);

    // Register libnl callbacks.
    nl_cb_err(cb_, NL_CB_CUSTOM, ErrorHandler, &err_);
    nl_cb_set(cb_, NL_CB_FINISH, NL_CB_CUSTOM, FinishHandler, &err_);
    nl_cb_set(cb_, NL_CB_ACK, NL_CB_CUSTOM, AckHandler, &err_);
    nl_cb_set(cb_, NL_CB_VALID, NL_CB_CUSTOM, ValidHandler, nullptr);
  }
  ~PowerSetter() {
    nl_socket_free(nl_sock_);
    nl_cb_put(cb_);
  }

  // Sets power mode according to tablet mode state. Returns true on success and
  // false on failure.
  bool SetPowerMode(bool tablet) {
    CHECK(!genl_connect(nl_sock_)) << "Failed to connect to netlink";

    int nl_family_id = genl_ctrl_resolve(nl_sock_, "nl80211");
    CHECK_GE(nl_family_id, 0) << "family nl80211 not found";

    struct nl_msg* msg = nlmsg_alloc();
    CHECK(msg);

    // Set header.
    genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, nl_family_id, 0, 0,
                NL80211_CMD_VENDOR, 0);

    // Set actual message.
    const uint32_t index = GetWirelessDeviceIndex();
    if (!index) {
      LOG(ERROR) << "Failed to find wireless device index";
      return false;
    }
    CHECK(!nla_put_u32(msg, NL80211_ATTR_IFINDEX, index))
        << "Failed to put NL80211_ATTR_IFINDEX";

    switch (GetWirelessDriverType()) {
      case WirelessDriver::MWIFIEX:
        FillMessageMwifiex(msg, tablet);
        break;
      case WirelessDriver::IWL:
        FillMessageIwl(msg, tablet);
        break;
      case WirelessDriver::NONE:
        LOG(ERROR) << "No valid wireless driver found";
        return false;
    }

    CHECK_GE(nl_send_auto(nl_sock_, msg), 0)
        << "nl_send_auto failed: " << nl_geterror(err_);
    while (err_ != 0)
      nl_recvmsgs(nl_sock_, cb_);

    nlmsg_free(msg);
    return true;
  }

 private:
  struct nl_sock* nl_sock_;
  struct nl_cb* cb_;
  int err_ = 0;  // Used by |cb_| to store errors.

  DISALLOW_COPY_AND_ASSIGN(PowerSetter);
};

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(tablet, false, "Set wifi transmit power mode to tablet mode");
  brillo::FlagHelper::Init(argc, argv, "Set wifi transmit power mode");

  base::AtExitManager at_exit_manager;
  return PowerSetter().SetPowerMode(FLAGS_tablet) ? 0 : 1;
}
