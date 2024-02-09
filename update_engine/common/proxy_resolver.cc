// Copyright 2010 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/proxy_resolver.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/location.h>

using brillo::MessageLoop;
using std::deque;
using std::string;

namespace chromeos_update_engine {

const char kNoProxy[] = "direct://";
const ProxyRequestId kProxyRequestIdNull = brillo::MessageLoop::kTaskIdNull;

DirectProxyResolver::~DirectProxyResolver() {
  if (idle_callback_id_ != MessageLoop::kTaskIdNull) {
    // The DirectProxyResolver is instantiated as part of the UpdateAttempter
    // which is also instantiated by default by the FakeSystemState, even when
    // it is not used. We check the manage_shares_id_ before calling the
    // MessageLoop::current() since the unit test using a FakeSystemState may
    // have not define a MessageLoop for the current thread.
    MessageLoop::current()->CancelTask(idle_callback_id_);
    idle_callback_id_ = MessageLoop::kTaskIdNull;
  }
}

ProxyRequestId DirectProxyResolver::GetProxiesForUrl(
    const string& url, ProxiesResolvedFn callback) {
  idle_callback_id_ = MessageLoop::current()->PostTask(
      FROM_HERE, base::BindOnce(&DirectProxyResolver::ReturnCallback,
                                base::Unretained(this), std::move(callback)));
  return idle_callback_id_;
}

bool DirectProxyResolver::CancelProxyRequest(ProxyRequestId request) {
  return MessageLoop::current()->CancelTask(request);
}

void DirectProxyResolver::ReturnCallback(ProxiesResolvedFn callback) {
  idle_callback_id_ = MessageLoop::kTaskIdNull;

  // Initialize proxy pool with as many proxies as indicated (all identical).
  deque<string> proxies(num_proxies_, kNoProxy);

  std::move(callback).Run(proxies);
}

}  // namespace chromeos_update_engine
