// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <gtest/gtest.h>
#include <mojo/core/core.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <net-base/http_url.h>

#include "shill/mojom/mojo_portal_service.h"
#include "shill/mojom/portal.mojom.h"
#include "shill/network/mock_network.h"
#include "shill/network/mock_network_manager.h"

namespace shill {
namespace {

constexpr int kNetworkId = 3;

using MojomPortalService = chromeos::connectivity::mojom::PortalService;
using MojomPortalUIInteractionHandler =
    chromeos::connectivity::mojom::PortalUIInteractionHandler;

using testing::Return;

class FakePortalClient {
 public:
  explicit FakePortalClient(mojo::PendingRemote<MojomPortalService> service) {
    service_.Bind(std::move(service));
  }

  mojo::Remote<MojomPortalUIInteractionHandler>
  GetPortalUIInteractionHandler() {
    mojo::Remote<MojomPortalUIInteractionHandler> handler;
    service_->ConnectPortalUIInteractionHandler(
        handler.BindNewPipeAndPassReceiver());
    return handler;
  }

 private:
  mojo::Remote<chromeos::connectivity::mojom::PortalService> service_;
};

class MojoPortalServiceTest : public testing::Test {
 public:
  void SetUp() override {
    mojo::core::Init();

    ON_CALL(mock_network_manager_, GetNetwork(kNetworkId))
        .WillByDefault(Return(&mock_network_));
    service_ = std::make_unique<MojoPortalService>(&mock_network_manager_);

    service_receiver_ = std::make_unique<
        mojo::Receiver<chromeos::connectivity::mojom::PortalService>>(
        service_.get());

    client_ = std::make_unique<FakePortalClient>(
        service_receiver_->BindNewPipeAndPassRemote());
  }

  void TearDown() override {
    client_.reset();
    service_receiver_.reset();
    service_.reset();

    auto core = mojo::core::Core::Get();
    std::vector<MojoHandle> leaks;
    core->GetActiveHandlesForTest(&leaks);
    EXPECT_TRUE(leaks.empty());

    mojo::core::ShutDown();
  }

 protected:
  // It's required by Mojo environment.
  base::test::TaskEnvironment task_environment_;

  MockNetworkManager mock_network_manager_;
  MockNetwork mock_network_{/*interface_index=*/3,
                            /*interface_name=*/"wlan0",
                            /*technology=*/Technology::kWiFi};

  std::unique_ptr<MojoPortalService> service_;
  // In production, we rely on SimpleMojoServiceProvider to keep the receivers
  // of service. Here we manually keep the receiver and connect the service.
  std::unique_ptr<mojo::Receiver<chromeos::connectivity::mojom::PortalService>>
      service_receiver_;
  std::unique_ptr<FakePortalClient> client_;
};

TEST_F(MojoPortalServiceTest, ConnectPortalUIInteractionHandler) {
  mojo::Remote<MojomPortalUIInteractionHandler> remote_handler =
      client_->GetPortalUIInteractionHandler();

  const NotificationEvent kEvent = NotificationEvent::kClicked;
  EXPECT_CALL(mock_network_, OnNotificationEvent(kEvent)).Times(1);
  remote_handler->OnNotificationEvent(kNetworkId, kEvent);
  remote_handler.FlushForTesting();
  testing::Mock::VerifyAndClearExpectations(&mock_network_);

  const net_base::HttpUrl kUrl =
      *net_base::HttpUrl::CreateFromString("https://example.org");
  EXPECT_CALL(mock_network_, OnSigninPageShown(kUrl)).Times(1);
  remote_handler->OnSigninPageShown(kNetworkId, kUrl);
  remote_handler.FlushForTesting();
  testing::Mock::VerifyAndClearExpectations(&mock_network_);

  const int kError = 3;
  EXPECT_CALL(mock_network_, OnSigninPageLoaded(kError)).Times(1);
  remote_handler->OnSigninPageLoaded(kNetworkId, kError);
  remote_handler.FlushForTesting();
  testing::Mock::VerifyAndClearExpectations(&mock_network_);

  EXPECT_CALL(mock_network_, OnSigninPageClosed()).Times(1);
  remote_handler->OnSigninPageClosed(kNetworkId);
  remote_handler.FlushForTesting();
  testing::Mock::VerifyAndClearExpectations(&mock_network_);
}

TEST_F(MojoPortalServiceTest, ConnectMultiplePortalUIInteractionHandler) {
  mojo::Remote<MojomPortalUIInteractionHandler> remote_handler1 =
      client_->GetPortalUIInteractionHandler();
  mojo::Remote<MojomPortalUIInteractionHandler> remote_handler2 =
      client_->GetPortalUIInteractionHandler();

  const NotificationEvent kEvent = NotificationEvent::kClicked;
  EXPECT_CALL(mock_network_, OnNotificationEvent(kEvent)).Times(1);
  remote_handler1->OnNotificationEvent(kNetworkId, kEvent);
  remote_handler1.FlushForTesting();
  testing::Mock::VerifyAndClearExpectations(&mock_network_);

  const int kError = 3;
  EXPECT_CALL(mock_network_, OnSigninPageLoaded(kError)).Times(1);
  remote_handler2->OnSigninPageLoaded(kNetworkId, kError);
  remote_handler2.FlushForTesting();
  testing::Mock::VerifyAndClearExpectations(&mock_network_);
}

}  // namespace
}  // namespace shill
