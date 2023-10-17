// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/carrier_entitlement.h"

#include <memory>
#include <utility>
#include <vector>
#include <base/files/scoped_temp_dir.h>
#include "base/functional/bind.h"
#include <base/strings/string_number_conversions.h>
#include <base/test/mock_callback.h>
#include <base/test/task_environment.h>
#include <brillo/http/http_request.h>
#include <brillo/http/mock_connection.h>
#include <brillo/http/mock_transport.h>
#include <brillo/streams/mock_stream.h>
#include <curl/curl.h>
#include <gmock/gmock-cardinalities.h>
#include <gmock/gmock-more-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>

#include "shill/cellular/mobile_operator_mapper.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/logging.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/network/mock_network.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using ::testing::AtLeast;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Test;
using ::testing::Unused;
using ::testing::WithArg;
using ::testing::WithArgs;

using brillo::http::request_type::kGet;
using brillo::http::request_type::kPost;

namespace shill {

namespace {
constexpr net_base::IPAddress kDNS1(net_base::IPv4Address(8, 8, 8, 8));
constexpr net_base::IPAddress kDNS2(net_base::IPv4Address(1, 1, 1, 1));
constexpr net_base::IPAddress kDNS3(net_base::IPv6Address(
    0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0, 0, 0, 0, 0, 0, 0, 0x88, 0x88));
constexpr net_base::IPAddress kSrcIp(net_base::IPv4Address(192, 168, 88, 5));
constexpr char kImsi[] = "001010000000004";
constexpr char kInterfaceName[] = "wwan8";
constexpr char kUrl[] = "testurl.com";
}  // namespace

base::ScopedTempDir MakeTempDir() {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
  return temp_dir;
}

class CarrierEntitlementTest : public testing::Test {
 public:
  explicit CarrierEntitlementTest(const std::string& url,
                                  const std::string& method,
                                  const Stringmap& params)
      : temp_dir_(MakeTempDir()),
        path_(temp_dir_.GetPath().value()),
        manager_(
            &control_interface_, &dispatcher_, &metrics_, path_, path_, path_),
        modem_info_(&control_interface_, &manager_),
        transport_(std::make_shared<brillo::http::MockTransport>()),
        brillo_connection_(
            std::make_shared<brillo::http::MockConnection>(transport_)),
        url_(url),
        interface_name_(kInterfaceName) {
    config_.url = url;
    config_.method = method;
    config_.params = params;

    EXPECT_CALL(manager_, modem_info())
        .Times(AtLeast(0))
        .WillRepeatedly(Return(&modem_info_));

    cellular_ = scoped_refptr<MockCellular>(
        new MockCellular(&manager_, kInterfaceName, kSrcIp.ToString(), 1, "",
                         RpcIdentifier("")));
    auto mock_network = std::make_unique<MockNetwork>(
        cellular_->interface_index(), cellular_->link_name(),
        cellular_->technology());
    EXPECT_CALL(*mock_network, GetDNSServers())
        .Times(AtLeast(0))
        .WillRepeatedly(
            Return(std::vector<net_base::IPAddress>({kDNS1, kDNS2, kDNS3})));

    EXPECT_CALL(*cellular_, GetPrimaryNetwork())
        .Times(AtLeast(0))
        .WillRepeatedly(Return(mock_network.get()));
    network_ = mock_network.get();
    cellular_->set_network_for_testing(std::move(mock_network));
    EXPECT_CALL(*network_, HasInternetConnectivity())
        .Times(AtLeast(0))
        .WillRepeatedly(Return(true));
    carrier_entitlement_ = std::make_unique<CarrierEntitlement>(
        cellular_.get(), &metrics_, check_cb_.Get());
  }

  void SetUp() override {
    shill::ScopeLogger::GetInstance()->set_verbose_level(3);
    shill::ScopeLogger::GetInstance()->EnableScopesByName("cellular");
    carrier_entitlement_->transport_ = transport_;
  }

  void TearDown() override {
    VerifyAllExpectations();
    brillo_connection_.reset();
    EXPECT_CALL(*transport_, CancelRequest(_));
    transport_.reset();
  }

 protected:
  using HttpSuccessCallback =
      void(base::OnceCallback<void(CarrierEntitlement::Result result)>,
           bool,
           brillo::http::RequestID,
           std::unique_ptr<brillo::http::Response>);
  using HttpErrorCallback =
      void(base::OnceCallback<void(CarrierEntitlement::Result result)>,
           bool,
           brillo::http::RequestID,
           const brillo::Error*);

  void FinishRequestAsyncSuccess(
      brillo::http::SuccessCallback success_callback) {
    auto read_data = [this](void* buffer, Unused, size_t* read,
                            Unused) -> bool {
      memcpy(buffer, resp_data_.data(), resp_data_.size());
      *read = resp_data_.size();
      return true;
    };

    auto mock_stream = std::make_unique<brillo::MockStream>();
    if (resp_data_.size() == 0) {
      EXPECT_CALL(*mock_stream, ReadBlocking(_, _, _, _))
          .WillOnce(
              DoAll(Invoke(read_data), SetArgPointee<2>(0), Return(true)));
    } else {
      EXPECT_CALL(*mock_stream, ReadBlocking(_, _, _, _))
          .WillOnce(Invoke(read_data))
          .WillOnce(DoAll(SetArgPointee<2>(0), Return(true)));
    }

    EXPECT_CALL(*brillo_connection_, MockExtractDataStream(_))
        .WillOnce(Return(mock_stream.release()));
    auto resp = std::make_unique<brillo::http::Response>(brillo_connection_);

    std::move(success_callback)
        .Run(carrier_entitlement_->request_id_, std::move(resp));
  }

  void FinishRequestAsyncFail(brillo::http::ErrorCallback error_callback) {
    brillo::ErrorPtr error;
    brillo::Error::AddTo(&error, FROM_HERE, "curl_easy_error",
                         base::NumberToString(CURLE_COULDNT_CONNECT), "");
    std::move(error_callback)
        .Run(carrier_entitlement_->request_id_, error.get());
  }

  void ExpectFinishRequestAsyncSuccess(const std::string& resp_data,
                                       const int status_code) {
    resp_data_ = resp_data;
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(WithArg<0>([this](auto callback) {
          FinishRequestAsyncSuccess(std::move(callback));
          return 0;
        }));
    EXPECT_CALL(*brillo_connection_, GetResponseStatusCode())
        .WillOnce(Return(status_code));
  }

  void ExpectFinishRequestAsyncFail() {
    EXPECT_CALL(*brillo_connection_, FinishRequestAsync(_, _))
        .WillOnce(WithArg<1>([this](auto callback) {
          FinishRequestAsyncFail(std::move(callback));
          return 0;
        }));
  }

  void VerifyRequestData(brillo::Stream* stream,
                         brillo::ErrorPtr* err,
                         std::string expected) {
    req_data_.clear();
    char buf[100];
    size_t read = 0;
    while (stream->ReadBlocking(buf, sizeof(buf), &read, nullptr) && read > 0) {
      req_data_.append(buf, read);
    }
    EXPECT_EQ(req_data_, expected);
  }

  void ExpectCreateConnection(const char* http_method,
                              const std::string& content) {
    EXPECT_CALL(*network_, IsConnected()).WillOnce(Return(true));
    ExpectSetupBeforeHttpCall();
    EXPECT_CALL(*transport_, CreateConnection(url_, http_method, _, "", "", _))
        .WillOnce(Return(brillo_connection_));
    if (http_method != kGet) {
      EXPECT_CALL(*brillo_connection_, MockSetRequestData(_, _))
          .WillOnce(
              DoAll(WithArgs<0, 1>(Invoke([=, this](brillo::Stream* stream,
                                                    brillo::ErrorPtr* err) {
                      this->VerifyRequestData(stream, err, content);
                    })),
                    Return(true)));
    }
  }

  void ExpectSetupBeforeHttpCall() {
    EXPECT_CALL(*transport_,
                SetDnsServers(std::vector<std::string>{
                    kDNS1.ToString(), kDNS2.ToString(), kDNS3.ToString()}));
    EXPECT_CALL(*transport_, SetDnsInterface(interface_name_));
    EXPECT_CALL(*transport_, SetInterface(interface_name_));
    EXPECT_CALL(*transport_, UseCustomCertificate(_));
    EXPECT_CALL(*transport_,
                SetDefaultTimeout(CarrierEntitlement::kHttpRequestTimeout));
  }

  void VerifyAllExpectations() {
    dispatcher_.DispatchPendingEvents();
    Mock::VerifyAndClearExpectations(&transport_);
    Mock::VerifyAndClearExpectations(&brillo_connection_);
    Mock::VerifyAndClearExpectations(&check_cb_);
    Mock::VerifyAndClearExpectations(&cellular_);
    Mock::VerifyAndClearExpectations(&metrics_);
    Mock::VerifyAndClearExpectations(&network_);
  }

  base::ScopedTempDir temp_dir_;
  std::string path_;
  NiceMock<MockControl> control_interface_;
  MockManager manager_;
  MockModemInfo modem_info_;
  MockNetwork* network_;  // owned by |cellular_|
  EventDispatcherForTest dispatcher_;
  scoped_refptr<MockCellular> cellular_;
  testing::StrictMock<MockMetrics> metrics_;
  std::shared_ptr<brillo::http::MockTransport> transport_;
  std::shared_ptr<brillo::http::MockConnection> brillo_connection_;
  base::MockRepeatingCallback<void(CarrierEntitlement::Result)> check_cb_;
  std::unique_ptr<CarrierEntitlement> carrier_entitlement_;
  MobileOperatorMapper::EntitlementConfig config_;
  std::string resp_data_;
  std::string req_data_;
  std::string url_;
  std::string interface_name_;
};

class CarrierEntitlementTestNoParams : public CarrierEntitlementTest {
 public:
  CarrierEntitlementTestNoParams()
      : CarrierEntitlementTest(
            kUrl, brillo::http::request_type::kPost, {} /* params */) {}
};

TEST_F(CarrierEntitlementTestNoParams, Constructor) {}

TEST_F(CarrierEntitlementTestNoParams, CheckAllowed) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckUserNotAllowedToTether) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(
      CarrierEntitlement::kServerCodeUserNotAllowedToTether,
      brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_,
              Run(CarrierEntitlement::Result::kUserNotAllowedToTether));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckUserNotAllowedToTether));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckUnrecognizedUser) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(
      CarrierEntitlement::kServerCodeUnrecognizedUser,
      brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kUnrecognizedUser));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckUnrecognizedUser));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckHttpSyntaxError) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(
      CarrierEntitlement::kServerCodeHttpSyntaxError,
      brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kGenericError));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckHttpSyntaxErrorOnServer));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckServerErrorUseCachedPassValue) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();

  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(CarrierEntitlement::kServerCodeInternalError,
                                  brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckInternalErrorOnServer));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckServerErrorUseCachedFailValue) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(
      CarrierEntitlement::kServerCodeUnrecognizedUser,
      brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kUnrecognizedUser));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckUnrecognizedUser));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();

  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(CarrierEntitlement::kServerCodeInternalError,
                                  brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kUnrecognizedUser));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckInternalErrorOnServer));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckUnrecognizedHttpStatusCode) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Redirect);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kGenericError));
  EXPECT_CALL(
      metrics_,
      NotifyCellularEntitlementCheckResult(
          Metrics::kCellularEntitlementCheckUnrecognizedHttpStatusCode));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, CheckErrorCallback) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncFail();
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kGenericError));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckHttpRequestError));
  carrier_entitlement_->Check(config_);
}

TEST_F(CarrierEntitlementTestNoParams, BackgroundCheckAllowed) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();

  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  dispatcher_.task_environment().FastForwardBy(
      CarrierEntitlement::kBackgroundCheckPeriod + base::Seconds(1));
  VerifyAllExpectations();

  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  dispatcher_.task_environment().FastForwardBy(
      CarrierEntitlement::kBackgroundCheckPeriod + base::Seconds(1));
}

TEST_F(CarrierEntitlementTestNoParams, BackgroundCheckReturnsNotAllowed) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();

  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess(
      CarrierEntitlement::kServerCodeUnrecognizedUser,
      brillo::http::status_code::Forbidden);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kUnrecognizedUser));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckUnrecognizedUser));
  dispatcher_.task_environment().FastForwardBy(
      CarrierEntitlement::kBackgroundCheckPeriod + base::Seconds(1));
  VerifyAllExpectations();

  // When the check fails, no more background checks are scheduled.
  EXPECT_CALL(check_cb_, Run(_)).Times(0);
  dispatcher_.task_environment().FastForwardBy(
      CarrierEntitlement::kBackgroundCheckPeriod + base::Seconds(1));
}

TEST_F(CarrierEntitlementTestNoParams, BackgroundCheckErrorCallback) {
  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();

  ExpectCreateConnection(kPost, "{}");
  ExpectFinishRequestAsyncFail();
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kGenericError));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckHttpRequestError));
  dispatcher_.task_environment().FastForwardBy(
      CarrierEntitlement::kBackgroundCheckPeriod + base::Seconds(1));
  VerifyAllExpectations();

  // When the check fails, no more background checks are scheduled.
  EXPECT_CALL(check_cb_, Run(_)).Times(0);
  dispatcher_.task_environment().FastForwardBy(
      CarrierEntitlement::kBackgroundCheckPeriod + base::Seconds(1));
}

TEST_F(CarrierEntitlementTestNoParams, CheckNoEntitlementNeeded) {
  config_.url = "";
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();
}

TEST_F(CarrierEntitlementTestNoParams, CheckNoNetwork) {
  EXPECT_CALL(*cellular_, GetPrimaryNetwork()).WillOnce(Return(nullptr));
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kNetworkNotReady));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckNoNetwork));
  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();
}

TEST_F(CarrierEntitlementTestNoParams, CheckNetworkNotConnected) {
  EXPECT_CALL(*network_, IsConnected()).WillOnce(Return(false));
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kNetworkNotReady));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckNetworkNotConnected));

  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();
}
TEST_F(CarrierEntitlementTestNoParams, CheckNetworkNotOnline) {
  EXPECT_CALL(*network_, IsConnected())
      .Times(AtLeast(0))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*network_, HasInternetConnectivity()).WillOnce(Return(false));
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kNetworkNotReady));
  EXPECT_CALL(metrics_,
              NotifyCellularEntitlementCheckResult(
                  Metrics::kCellularEntitlementCheckNetworkNotOnline));

  carrier_entitlement_->Check(config_);
  VerifyAllExpectations();
}

class CarrierEntitlementTestGet : public CarrierEntitlementTest {
 public:
  CarrierEntitlementTestGet()
      : CarrierEntitlementTest(
            kUrl, brillo::http::request_type::kGet, {} /* params */) {}
};

TEST_F(CarrierEntitlementTestGet, CheckAllowed) {
  ExpectCreateConnection(kGet, "");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
}

class CarrierEntitlementTestWithImsi : public CarrierEntitlementTest {
 public:
  CarrierEntitlementTestWithImsi()
      : CarrierEntitlementTest(kUrl,
                               brillo::http::request_type::kPost,
                               {{CarrierEntitlement::kImsiProperty, kImsi}}) {}
};

TEST_F(CarrierEntitlementTestWithImsi, CheckAllowed) {
  ExpectCreateConnection(kPost, "{\"imsi\":\"001010000000004\"}");
  ExpectFinishRequestAsyncSuccess("", brillo::http::status_code::Ok);
  EXPECT_CALL(check_cb_, Run(CarrierEntitlement::Result::kAllowed));
  EXPECT_CALL(metrics_, NotifyCellularEntitlementCheckResult(
                            Metrics::kCellularEntitlementCheckAllowed));
  carrier_entitlement_->Check(config_);
}

}  // namespace shill
