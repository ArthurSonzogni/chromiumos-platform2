// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_CHROME_BROWSER_PROXY_RESOLVER_H_
#define UPDATE_ENGINE_CROS_CHROME_BROWSER_PROXY_RESOLVER_H_

#include <deque>
#include <map>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "update_engine/common/proxy_resolver.h"

namespace chromeos_update_engine {

class ChromeBrowserProxyResolver : public ProxyResolver {
 public:
  ChromeBrowserProxyResolver();
  ChromeBrowserProxyResolver(const ChromeBrowserProxyResolver&) = delete;
  ChromeBrowserProxyResolver& operator=(const ChromeBrowserProxyResolver&) =
      delete;

  ~ChromeBrowserProxyResolver() override;

  // ProxyResolver:
  ProxyRequestId GetProxiesForUrl(const std::string& url,
                                  ProxiesResolvedFn callback) override;
  bool CancelProxyRequest(ProxyRequestId request) override;

 private:
  // Callback for calls made by GetProxiesForUrl().
  void OnGetChromeProxyServers(ProxyRequestId request_id,
                               bool success,
                               const std::vector<std::string>& proxies);

  // Finds the callback identified by |request_id| in |pending_callbacks_|,
  // passes |proxies| to it, and deletes it. Does nothing if the request has
  // been cancelled.
  void RunCallback(ProxyRequestId request_id,
                   const std::deque<std::string>& proxies);

  // Next ID to return from GetProxiesForUrl().
  ProxyRequestId next_request_id_;

  // Callbacks that were passed to GetProxiesForUrl() but haven't yet been run.
  std::map<ProxyRequestId, ProxiesResolvedFn> pending_callbacks_;

  base::WeakPtrFactory<ChromeBrowserProxyResolver> weak_ptr_factory_;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_CHROME_BROWSER_PROXY_RESOLVER_H_
