// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_RTNL_LISTENER_H_
#define NET_BASE_RTNL_LISTENER_H_

#include <base/functional/callback.h>
#include <base/observer_list_types.h>

#include "net-base/export.h"
#include "net-base/rtnl_message.h"

namespace net_base {

class RTNLHandler;

class NET_BASE_EXPORT RTNLListener : public base::CheckedObserver {
 public:
  RTNLListener(
      uint32_t listen_flags,
      const base::RepeatingCallback<void(const net_base::RTNLMessage&)>&
          callback);
  RTNLListener(
      uint32_t listen_flags,
      const base::RepeatingCallback<void(const net_base::RTNLMessage&)>&
          callback,
      RTNLHandler* rtnl_handler);
  RTNLListener(const RTNLListener&) = delete;
  RTNLListener& operator=(const RTNLListener&) = delete;

  ~RTNLListener() override;

  void NotifyEvent(uint32_t type, const net_base::RTNLMessage& msg) const;

 private:
  const uint32_t listen_flags_;
  const base::RepeatingCallback<void(const net_base::RTNLMessage&)> callback_;
  RTNLHandler* const rtnl_handler_;
};

}  // namespace net_base

#endif  // NET_BASE_RTNL_LISTENER_H_
