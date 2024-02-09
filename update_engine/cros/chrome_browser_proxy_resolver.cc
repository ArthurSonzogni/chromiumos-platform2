// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/chrome_browser_proxy_resolver.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_util.h>
#include <brillo/http/http_proxy.h>

#include "update_engine/cros/dbus_connection.h"

namespace chromeos_update_engine {

ChromeBrowserProxyResolver::ChromeBrowserProxyResolver()
    : next_request_id_(kProxyRequestIdNull + 1), weak_ptr_factory_(this) {}

ChromeBrowserProxyResolver::~ChromeBrowserProxyResolver() = default;

ProxyRequestId ChromeBrowserProxyResolver::GetProxiesForUrl(
    const std::string& url, ProxiesResolvedFn callback) {
  const ProxyRequestId id = next_request_id_++;
  brillo::http::GetChromeProxyServersAsync(
      DBusConnection::Get()->GetDBus(), url,
      base::BindRepeating(&ChromeBrowserProxyResolver::OnGetChromeProxyServers,
                          weak_ptr_factory_.GetWeakPtr(), id));
  pending_callbacks_[id] = std::move(callback);
  return id;
}

bool ChromeBrowserProxyResolver::CancelProxyRequest(ProxyRequestId request) {
  return pending_callbacks_.erase(request) != 0;
}

void ChromeBrowserProxyResolver::OnGetChromeProxyServers(
    ProxyRequestId request_id,
    bool success,
    const std::vector<std::string>& proxies) {
  // If |success| is false, |proxies| will still hold the direct proxy option
  // which is what we do in our error case.
  auto it = pending_callbacks_.find(request_id);
  if (it == pending_callbacks_.end())
    return;

  ProxiesResolvedFn callback = std::move(it->second);
  pending_callbacks_.erase(it);
  std::move(callback).Run(
      std::deque<std::string>(proxies.begin(), proxies.end()));
}

}  // namespace chromeos_update_engine
