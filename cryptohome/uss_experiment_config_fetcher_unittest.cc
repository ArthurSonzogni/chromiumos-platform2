// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/uss_experiment_config_fetcher.h>

#include <memory>
#include <string>
#include <utility>

#include <base/test/task_environment.h>
#include <brillo/http/http_transport_fake.h>
#include <brillo/mime_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-proxy-mocks.h>
#include <shill/dbus-constants.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;

using org::chromium::flimflam::ManagerProxyMock;

namespace cryptohome {

namespace {

constexpr char kGstaticUrlPrefix[] =
    "https://www.gstatic.com/uss-experiment/v1.json";

constexpr char kDefaultConfig[] = R"(
  {
    "default": {
      "last_invalid": 3,
      "population": 0.3
    },
    "stable-channel": {
      "last_invalid": 4,
      "population": 0.01
    },
    "testimage-channel": {
      "population": 1
    }
  }
)";

constexpr char kInvalidConfig[] = "not a json file";

constexpr char kFakeErrMessage[] = "error";

// This is what the kConnectionState property will get set to for mocked
// calls into shill flimflam manager.
std::string* g_connection_state;

}  // namespace

// Handles calls for getting the network state.
bool GetShillProperties(
    brillo::VariantDictionary* dict,
    brillo::ErrorPtr* error,
    int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
  dict->emplace(shill::kConnectionStateProperty, *g_connection_state);
  return true;
}

class UssExperimentConfigFetcherTest : public ::testing::Test {
 protected:
  using FetchSuccessCallback =
      base::RepeatingCallback<void(int last_invalid, double population)>;

  void SetUp() override {
    g_connection_state = &initial_connection_state_;
    fake_transport_ = std::make_shared<brillo::http::fake::Transport>();
    auto mock_proxy = std::make_unique<ManagerProxyMock>();
    mock_proxy_ = mock_proxy.get();
    fetcher_ = std::make_unique<UssExperimentConfigFetcher>();

    fetcher_->SetTransportForTesting(fake_transport_);
    fetcher_->SetProxyForTesting(std::move(mock_proxy));
  }

  void TearDown() override {
    EXPECT_EQ(expected_success_count_, actual_success_count_);
  }

  void AddSimpleReplyHandler(int status_code, const std::string& reply_text) {
    fake_transport_->AddSimpleReplyHandler(
        kGstaticUrlPrefix, brillo::http::request_type::kGet, status_code,
        reply_text, brillo::mime::application::kJson);
  }

  void SetCreateConnectionError() {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, FROM_HERE, "", "", kFakeErrMessage);
    fake_transport_->SetCreateConnectionError(std::move(error));
  }

  void ClearCreateConnectionError() {
    fake_transport_->SetCreateConnectionError(brillo::ErrorPtr());
  }

  void OnManagerPropertyChangeRegistration() {
    fetcher_->OnManagerPropertyChangeRegistration(/*interface=*/"",
                                                  /*signal_name=*/"",
                                                  /*success=*/true);
  }

  void OnConnectionStateChange(const std::string& state) {
    fetcher_->OnManagerPropertyChange(shill::kConnectionStateProperty, state);
  }

  void SetConnectionState(std::string state) {
    initial_connection_state_ = state;
  }

  void SetReleaseTrack(std::string track) {
    fetcher_->SetReleaseTrackForTesting(track);
  }

  void FetchAndExpectSuccessWith(int expected_last_invalid,
                                 double expected_population) {
    expected_success_count_++;
    fetcher_->Fetch(
        base::BindRepeating(&UssExperimentConfigFetcherTest::OnFetchSuccess,
                            weak_ptr_factory_.GetWeakPtr(),
                            expected_last_invalid, expected_population));
  }

  void FetchAndExpectError() {
    fetcher_->Fetch(
        base::BindRepeating(&UssExperimentConfigFetcherTest::OnFetchSuccess,
                            weak_ptr_factory_.GetWeakPtr(),
                            /*expected_last_invalid=*/std::nullopt,
                            /*expected_population=*/std::nullopt));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::shared_ptr<brillo::http::fake::Transport> fake_transport_;
  ManagerProxyMock* mock_proxy_;

 private:
  // If expected_* is nullopt, we don't check the actual value of the fetched
  // fields.
  void OnFetchSuccess(std::optional<int> expected_last_invalid,
                      std::optional<double> expected_population,
                      int last_invalid,
                      double population) {
    actual_success_count_++;
    if (expected_last_invalid.has_value()) {
      EXPECT_EQ(expected_last_invalid.value(), last_invalid);
    }
    if (expected_population.has_value()) {
      EXPECT_DOUBLE_EQ(expected_population.value(), population);
    }
  }

  int expected_success_count_ = 0;
  int actual_success_count_ = 0;
  std::string initial_connection_state_;
  std::unique_ptr<UssExperimentConfigFetcher> fetcher_;
  base::WeakPtrFactory<UssExperimentConfigFetcherTest> weak_ptr_factory_{this};
};

TEST_F(UssExperimentConfigFetcherTest, OnlineWhenFirstConnected) {
  SetConnectionState("online");
  EXPECT_CALL(*mock_proxy_, GetProperties(_, _, _))
      .WillOnce(DoAll(Invoke(&GetShillProperties), Return(true)));

  // We will test fetching logic in other test cases.
  AddSimpleReplyHandler(brillo::http::status_code::NotFound, "");

  // The fetcher should find out that the connection state is already "online"
  // when registered. It will then fetch config on the server (but won't
  // succeed).
  OnManagerPropertyChangeRegistration();
  EXPECT_EQ(fake_transport_->GetRequestCount(), 1);
}

TEST_F(UssExperimentConfigFetcherTest, OnlineAfterFirstConnected) {
  SetConnectionState("idle");
  EXPECT_CALL(*mock_proxy_, GetProperties(_, _, _))
      .WillOnce(DoAll(Invoke(&GetShillProperties), Return(true)));

  // The fetcher should find out that the connection state not "online" yet
  // when registered, and wait for property change signals.
  OnManagerPropertyChangeRegistration();

  // Connection state changed to "connected", but not yet "online".
  OnConnectionStateChange("connected");

  // We will test fetching logic in other test cases.
  AddSimpleReplyHandler(brillo::http::status_code::NotFound, "");

  // After connection state changed to "online", the fetcher will fetch config
  // on the server (but won't succeed).
  OnConnectionStateChange("online");
  EXPECT_EQ(fake_transport_->GetRequestCount(), 1);
}

TEST_F(UssExperimentConfigFetcherTest, FetchAndParseConfigSuccess) {
  AddSimpleReplyHandler(brillo::http::status_code::Ok, kDefaultConfig);

  SetReleaseTrack("stable-channel");
  FetchAndExpectSuccessWith(4, 0.01);

  SetReleaseTrack("testimage-channel");
  FetchAndExpectSuccessWith(3, 1);

  SetReleaseTrack("beta-channel");
  FetchAndExpectSuccessWith(3, 0.3);

  EXPECT_EQ(fake_transport_->GetRequestCount(), 3);
}

TEST_F(UssExperimentConfigFetcherTest, FetchAndParseConfigError) {
  AddSimpleReplyHandler(brillo::http::status_code::Ok, kInvalidConfig);

  SetReleaseTrack("stable-channel");
  FetchAndExpectError();

  EXPECT_EQ(fake_transport_->GetRequestCount(), 1);
}

TEST_F(UssExperimentConfigFetcherTest, FetchErrorReachRetryLimit) {
  AddSimpleReplyHandler(brillo::http::status_code::NotFound, "");

  SetReleaseTrack("stable-channel");
  FetchAndExpectError();
  task_environment_.FastForwardUntilNoTasksRemain();

  EXPECT_EQ(fake_transport_->GetRequestCount(), 10);
}

TEST_F(UssExperimentConfigFetcherTest, FetchErrorRetrySuccess) {
  SetReleaseTrack("stable-channel");
  // First simulate a connection error. The first fetch attempt should fail.
  SetCreateConnectionError();
  FetchAndExpectSuccessWith(4, 0.01);

  // Clear the connection error, but simulate a ServiceUnavailable. This should
  // fail the first retry.
  ClearCreateConnectionError();
  AddSimpleReplyHandler(brillo::http::status_code::ServiceUnavailable, "");
  task_environment_.FastForwardBy(base::Seconds(1));

  // Now set the server to return a valid response. This should make the second
  // retry succeed.
  AddSimpleReplyHandler(brillo::http::status_code::Ok, kDefaultConfig);
  task_environment_.FastForwardBy(base::Seconds(1));

  // Connection error will not count as a request.
  EXPECT_EQ(fake_transport_->GetRequestCount(), 2);
}

}  // namespace cryptohome
