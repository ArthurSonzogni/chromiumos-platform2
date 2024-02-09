// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/proxy_resolver.h"

#include <deque>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <gtest/gtest.h>

using std::deque;
using std::string;

namespace chromeos_update_engine {

class ProxyResolverTest : public ::testing::Test {
 protected:
  virtual ~ProxyResolverTest() = default;

  void SetUp() override { loop_.SetAsCurrent(); }

  void TearDown() override { EXPECT_FALSE(loop_.PendingTasks()); }

  brillo::FakeMessageLoop loop_{nullptr};
  DirectProxyResolver resolver_;
};

TEST_F(ProxyResolverTest, DirectProxyResolverCallbackTest) {
  bool called = false;
  deque<string> callback_proxies;
  auto callback = base::BindOnce(
      [](bool* called, deque<string>* callback_proxies,
         const deque<string>& proxies) {
        *called = true;
        *callback_proxies = proxies;
      },
      &called, &callback_proxies);

  EXPECT_NE(kProxyRequestIdNull,
            resolver_.GetProxiesForUrl("http://foo", std::move(callback)));
  // Check the callback is not called until the message loop runs.
  EXPECT_FALSE(called);
  loop_.Run();
  EXPECT_TRUE(called);
  EXPECT_EQ(kNoProxy, callback_proxies.front());
}

TEST_F(ProxyResolverTest, DirectProxyResolverCancelCallbackTest) {
  bool called = false;
  auto callback = base::BindOnce(
      [](bool* called, const deque<string>& proxies) { *called = true; },
      &called);

  ProxyRequestId request =
      resolver_.GetProxiesForUrl("http://foo", std::move(callback));
  EXPECT_FALSE(called);
  EXPECT_TRUE(resolver_.CancelProxyRequest(request));
  loop_.Run();
  EXPECT_FALSE(called);
}

TEST_F(ProxyResolverTest, DirectProxyResolverSimultaneousCallbacksTest) {
  int called = 0;
  auto callback = base::BindRepeating(
      [](int* called, const deque<string>& proxies) { (*called)++; }, &called);

  resolver_.GetProxiesForUrl("http://foo", callback);
  resolver_.GetProxiesForUrl("http://bar", callback);
  EXPECT_EQ(0, called);
  loop_.Run();
  EXPECT_EQ(2, called);
}

}  // namespace chromeos_update_engine
