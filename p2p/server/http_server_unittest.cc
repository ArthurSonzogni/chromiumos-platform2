// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "common/server_message.h"
#include "common/testutil.h"
#include "common/util.h"
#include "server/http_server.h"

#include <glib-object.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <base/bind.h>
#include <base/threading/simple_thread.h>
#include <base/strings/stringprintf.h>
#include <metrics/metrics_library_mock.h>

using testing::_;
using testing::StrictMock;

using p2p::testutil::ExpectFileSize;
using p2p::testutil::kDefaultMainLoopTimeoutMs;
using p2p::testutil::RunGMainLoopMaxIterations;
using p2p::testutil::RunGMainLoopUntil;
using p2p::testutil::SetExpectedFileSize;
using p2p::testutil::SetupTestDir;
using p2p::testutil::TeardownTestDir;

using std::string;
using std::vector;

using base::Bind;
using base::FilePath;
using base::Unretained;

namespace p2p {

namespace server {

bool PortNonZero(HttpServer* server) {
  return server->Port() != 0;
}

// ------------------------------------------------------------------------

class HttpServerListener {
 public:
  explicit HttpServerListener(HttpServer* server) {
    server->SetNumConnectionsCallback(base::Bind(
        &HttpServerListener::NumConnectionsCallback, base::Unretained(this)));
  }

  virtual void NumConnectionsCallback(int num_connections) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(HttpServerListener);
};

class MockHttpServerListener : public HttpServerListener {
 public:
  explicit MockHttpServerListener(HttpServer* server)
      : HttpServerListener(server),
      num_calls_(0) {
    ON_CALL(*this, NumConnectionsCallback(_))
      .WillByDefault(testing::InvokeWithoutArgs(
          this, &MockHttpServerListener::OnCall));
  }

  MOCK_METHOD1(NumConnectionsCallback, void(int));

  // NumCallsReached() returns true when the number of calls to |this|
  // is at least |num_calls|. This is used to terminate the GLib main loop
  // excecution and verify the expectations.
  bool NumCallsReached(int num_calls) const { return num_calls_ >= num_calls; }

 private:
  void OnCall() { num_calls_++; }

  int num_calls_;

  DISALLOW_COPY_AND_ASSIGN(MockHttpServerListener);
};

// ------------------------------------------------------------------------

static const int kMultipleTestNumFiles = 5;

class ClientThread : public base::SimpleThread {
 public:
  ClientThread(FilePath testdir_path, uint16_t port, int num)
      : base::SimpleThread("client", base::SimpleThread::Options()),
        testdir_path_(testdir_path),
        port_(port),
        num_(num) {}

 private:
  virtual void Run() {
    const char* dir = testdir_path_.value().c_str();
    EXPECT_COMMAND(0,
                   "curl -s -o %s/dl_%d http://127.0.0.1:%d/file",
                   dir,
                   num_,
                   port_);
    EXPECT_COMMAND(0, "cmp -l -b %s/file.p2p %s/dl_%d", dir, dir, num_);

    string name = base::StringPrintf("dl_%d", num_);
    ExpectFileSize(testdir_path_, name.c_str(), 2000);
  }

  FilePath testdir_path_;
  uint16_t port_;
  int num_;

  DISALLOW_COPY_AND_ASSIGN(ClientThread);
};

TEST(HttpServer, Basic) {
  if (!util::IsXAttrSupported(FilePath("/tmp"))) {
    LOG(WARNING) << "Skipping test because /tmp does not support xattr. "
                 << "Please update your system to support this feature.";
    return;
  }

  FilePath testdir = SetupTestDir("http-server");
  StrictMock<MetricsLibraryMock> metrics_lib;

  // Forces HttpServer to run p2p-http-server from the build
  // directory
  setenv("RUN_UNINSTALLED", "1", 1);

  HttpServer* server = HttpServer::Construct(&metrics_lib, testdir, 0);
  server->Start();

  // Wait until the HTTP server is running and accepting connections.
  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs, Bind(&PortNonZero, server));
  EXPECT_NE(server->Port(), 0);

  StrictMock<MockHttpServerListener> listener(server);

  // Set the metric expectations.
  EXPECT_CALL(metrics_lib, SendEnumToUMA(
      "P2P.Server.RequestResult",
      p2p::util::kP2PRequestResultResponseSent,
      p2p::util::kNumP2PServerRequestResults))
      .Times(kMultipleTestNumFiles);

  // The server file has 2000 bytes, so is reported as 0 MB.
  EXPECT_CALL(metrics_lib, SendToUMA(
      "P2P.Server.ContentServedSuccessfullyMB", 0, _, _, _))
      .Times(kMultipleTestNumFiles);

  EXPECT_CALL(metrics_lib, SendToUMA(
      "P2P.Server.RangeBeginPercentage", 0, _, _, _))
      .Times(kMultipleTestNumFiles);

  // We cant ensure that the reported download speed here is correct, but
  // at least a download speed has to be reported.
  EXPECT_CALL(metrics_lib, SendToUMA(
      "P2P.Server.DownloadSpeedKBps", _, _, _, _))
      .Times(kMultipleTestNumFiles);

  // Now set the expectations for the number of connections. We'll
  // climb all the way up to N and then go back to 0. So we'll
  // get to each integer in the open interval twice and each
  // of the boundary points just once, e.g. for N=5
  //
  // 0 -> 1  (twice)
  // 1 -> 2  (twice)
  // 2 -> 3  (twice)
  // 3 -> 4  (twice)
  // 4 -> 5  (once)
  // 5 -> 4  (twice)
  // 4 -> 3  (twice)
  // 3 -> 2  (twice)
  // 2 -> 1  (twice)
  // 1 -> 0  (once)
  //
  for (int n = 0; n <= kMultipleTestNumFiles; n++) {
    int times = 2;
    if (n == 0 || n == kMultipleTestNumFiles)
      times = 1;
    EXPECT_CALL(listener, NumConnectionsCallback(n)).Times(times);
    if (n > 0)
      EXPECT_CALL(metrics_lib, SendToUMA("P2P.Server.ClientCount", n, _, _, _));
  }

  // Create a 1000 byte file (with random content) with an EAs
  // indicating that it's 2000 bytes. This will make clients hang
  // and thus enable us to reliably get the NumConnections count
  // to N.
  EXPECT_COMMAND(0,
                 "dd if=/dev/urandom of=%s/file.p2p bs=1000 count=1",
                 testdir.value().c_str());
  ASSERT_TRUE(SetExpectedFileSize(testdir.Append("file.p2p"), 2000));

  // Start N threads for downloading, one for each file.
  vector<ClientThread*> threads;
  for (int n = 0; n < kMultipleTestNumFiles; n++) {
    ClientThread* thread = new ClientThread(testdir, server->Port(), n);
    thread->Start();
    threads.push_back(thread);
  }

  // Allow clients to start - this ensures that the server reaches
  // the number of connections kMultipleTestNumFiles.
  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockHttpServerListener::NumCallsReached,
                         base::Unretained(&listener),
                         kMultipleTestNumFiles /* num_calls */));

  // Now, complete the file. This causes each client to finish up.
  EXPECT_COMMAND(0,
                 "dd if=/dev/zero of=%s/file.p2p conv=notrunc "
                 "oflag=append bs=1000 count=1",
                 testdir.value().c_str());

  // Catch again all the disconnection events.
  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs,
                    Bind(&MockHttpServerListener::NumCallsReached,
                         base::Unretained(&listener),
                         2 * kMultipleTestNumFiles /* num_calls */));

  // Wait for all downloads to finish.
  for (auto& t : threads) {
    t->Join();
    delete t;
  }

  // Dispatch messages that could remain in the main loop after the last
  // "{NumConnections: 0}" is received. This could happen if the metrics are
  // sent after the NumConnections message.
  RunGMainLoopMaxIterations(100);

  server->Stop();
  delete server;
  TeardownTestDir(testdir);
}

TEST(HttpServer, PortNumberTest) {
  FilePath testdir = SetupTestDir("http-server-port");
  StrictMock<MetricsLibraryMock> metrics_lib;

  // Forces HttpServer to run p2p-http-server from the build directory.
  setenv("RUN_UNINSTALLED", "1", 1);

  HttpServer* server = HttpServer::Construct(&metrics_lib, testdir, 0);
  EXPECT_EQ(server->Port(), 0);
  server->Start();

  // Run for 60s (failure) or until the Port number is not 0.
  RunGMainLoopUntil(kDefaultMainLoopTimeoutMs, Bind(&PortNonZero, server));
  EXPECT_NE(server->Port(), 0);

  server->Stop();
  EXPECT_EQ(server->Port(), 0);

  delete server;
  TeardownTestDir(testdir);
}

}  // namespace server

}  // namespace p2p
