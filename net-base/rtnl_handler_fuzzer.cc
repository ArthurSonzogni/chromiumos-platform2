// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "net-base/rtnl_handler.h"
#include "net-base/rtnl_listener.h"
#include "net-base/rtnl_message.h"

namespace net_base {

class RTNLHandlerFuzz {
 public:
  static void Run(const uint8_t* data, size_t size) {
    base::AtExitManager exit_manager;
    base::span<const uint8_t> input(data, size);

    // Listen for all messages.
    RTNLListener listener(~0u, base::BindRepeating(&RTNLHandlerFuzz::Listener));
    RTNLHandler::GetInstance()->ParseRTNL(input);
  }

 private:
  static void Listener(const RTNLMessage& msg) {
    CHECK_NE(msg.ToString(), "");

    const auto& bytes = msg.Encode();
    switch (msg.type()) {
      case RTNLMessage::kTypeRdnss: {
        const net_base::RTNLMessage::RdnssOption& rdnss_option =
            msg.rdnss_option();
        CHECK(base::IsStringUTF8(rdnss_option.ToString()));
        CHECK(bytes.empty());
        break;
      }
      case RTNLMessage::kTypeDnssl: {
        const net_base::RTNLMessage::DnsslOption& dnssl_option =
            msg.dnssl_option();
        for (const auto& domain : dnssl_option.domains) {
          CHECK(base::IsStringUTF8(domain));
        }
        CHECK(base::IsStringUTF8(dnssl_option.ToString()));
        CHECK(bytes.empty());
        break;
      }
      case RTNLMessage::kTypeCaptivePortal: {
        const HttpUrl& uri = msg.captive_portal_uri();
        CHECK(base::IsStringUTF8(uri.ToString()));
        CHECK(bytes.empty());
        break;
      }
      case RTNLMessage::kTypePref64:
        CHECK(bytes.empty());
        break;
      case RTNLMessage::kTypeNdUserOption: {
        const net_base::RTNLMessage::NdUserOption& nd_user_option =
            msg.nd_user_option();
        CHECK(base::IsStringUTF8(nd_user_option.ToString()));
        CHECK(bytes.empty());
        break;
      }
      case RTNLMessage::kTypePrefix:
        // RTM_NEWNDUSEROPT and RTM_NEWPREFIX don't have "query" modes, so we
        // don't support re-constructing them in user space.
        CHECK(bytes.empty());
        break;
      default:
        CHECK(!bytes.empty());
    }
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);

  RTNLHandlerFuzz::Run(data, size);
  return 0;
}

}  // namespace net_base
