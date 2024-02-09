// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/http_fetcher.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>

using brillo::MessageLoop;
using std::deque;
using std::string;

namespace chromeos_update_engine {

HttpFetcher::~HttpFetcher() {
  CancelProxyResolution();
}

void HttpFetcher::SetPostData(const void* data,
                              size_t size,
                              HttpContentType type) {
  post_data_set_ = true;
  post_data_.clear();
  const char* char_data = reinterpret_cast<const char*>(data);
  post_data_.insert(post_data_.end(), char_data, char_data + size);
  post_content_type_ = type;
}

void HttpFetcher::SetPostData(const void* data, size_t size) {
  SetPostData(data, size, kHttpContentTypeUnspecified);
}

// Proxy methods to set the proxies, then to pop them off.
void HttpFetcher::ResolveProxiesForUrl(const string& url,
                                       base::OnceClosure callback) {
  CHECK_EQ(static_cast<base::OnceClosure*>(nullptr), callback_.get());
  callback_.reset(new base::OnceClosure(std::move(callback)));

  if (!proxy_resolver_) {
    LOG(INFO) << "Not resolving proxies (no proxy resolver).";
    no_resolver_idle_id_ = MessageLoop::current()->PostTask(
        FROM_HERE, base::BindOnce(&HttpFetcher::NoProxyResolverCallback,
                                  base::Unretained(this)));
    return;
  }
  proxy_request_ = proxy_resolver_->GetProxiesForUrl(
      url,
      base::BindOnce(&HttpFetcher::ProxiesResolved, base::Unretained(this)));
}

void HttpFetcher::NoProxyResolverCallback() {
  no_resolver_idle_id_ = MessageLoop::kTaskIdNull;
  ProxiesResolved(deque<string>());
}

void HttpFetcher::ProxiesResolved(const deque<string>& proxies) {
  proxy_request_ = kProxyRequestIdNull;
  if (!proxies.empty())
    SetProxies(proxies);
  CHECK(callback_.get()) << "ProxiesResolved but none pending.";
  base::OnceClosure* callback = callback_.release();
  // This may indirectly call back into ResolveProxiesForUrl():
  std::move(*callback).Run();
  delete callback;
}

bool HttpFetcher::CancelProxyResolution() {
  bool ret = false;
  if (no_resolver_idle_id_ != MessageLoop::kTaskIdNull) {
    ret = MessageLoop::current()->CancelTask(no_resolver_idle_id_);
    no_resolver_idle_id_ = MessageLoop::kTaskIdNull;
  }
  if (proxy_request_ && proxy_resolver_) {
    ret = proxy_resolver_->CancelProxyRequest(proxy_request_) || ret;
    proxy_request_ = kProxyRequestIdNull;
  }
  return ret;
}

}  // namespace chromeos_update_engine
