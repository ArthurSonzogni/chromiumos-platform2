// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/on_device_model_service.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <base/memory/raw_ref.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/uuid.h>
#include <metrics/metrics_library_mock.h>
#include <ml_core/dlc/dlc_client.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/features.h"
#include "odml/on_device_model/on_device_model_fake.h"
#include "odml/on_device_model/public/cpp/test_support/test_response_holder.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace on_device_model {
namespace {

constexpr char kFakeModelName1[] = "5f30b8ca-2447-445e-9716-a6da073fae51";
constexpr char kFakeModelName2[] = "90eeb7f8-9491-452d-9ec9-5b6edd6c93fa";

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Pair;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::UnorderedElementsAre;

class ContextClientWaiter : public mojom::ContextClient {
 public:
  mojo::PendingRemote<mojom::ContextClient> BindRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void OnComplete(uint32_t tokens_processed) override {
    tokens_processed_ = tokens_processed;
    run_loop_.Quit();
  }

  int WaitForCompletion() {
    run_loop_.Run();
    return tokens_processed_;
  }

 private:
  base::RunLoop run_loop_;
  mojo::Receiver<mojom::ContextClient> receiver_{this};
  int tokens_processed_ = 0;
};

class FakeFile {
 public:
  explicit FakeFile(const std::string& content) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    CHECK(temp_dir_.CreateUniqueTempDir());
    base::File file(temp_dir_.GetPath().Append("file"),
                    base::File::FLAG_OPEN | base::File::FLAG_CREATE |
                        base::File::FLAG_WRITE | base::File::FLAG_READ);
    CHECK(file.IsValid());
    file.WriteAtCurrentPos(base::as_byte_span(content));
  }
  ~FakeFile() = default;

  base::File Open() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return base::File(
        temp_dir_.GetPath().Append("file"),
        base::File::FLAG_OPEN | base::File::FLAG_WRITE | base::File::FLAG_READ);
  }

 private:
  base::ScopedTempDir temp_dir_;
};

class OnDeviceModelServiceTest : public testing::Test {
 public:
  OnDeviceModelServiceTest()
      : service_impl_(raw_ref(metrics_),
                      raw_ref(shim_loader_),
                      GetOnDeviceModelFakeImpl(raw_ref(metrics_),
                                               raw_ref(shim_loader_))) {
    mojo::core::Init();
    service_impl_.AddReceiver(service_.BindNewPipeAndPassReceiver());
  }

  void SetUp() override {
    EXPECT_CALL(
        metrics_,
        SendTimeToUMA("OnDeviceModel.LoadAdaptationModelDuration", _, _, _, _))
        .WillRepeatedly(Return(true));
  }

  mojo::Remote<mojom::OnDeviceModelPlatformService>& service() {
    return service_;
  }

  mojo::Remote<mojom::OnDeviceModel> LoadModel(
      const std::string& model_name = kFakeModelName1) {
    // Set DlcClient to return paths from /build.
    auto dlc_path = base::FilePath("testdata").Append(model_name);
    cros::DlcClient::SetDlcPathForTest(&dlc_path);

    EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
    EXPECT_CALL(metrics_,
                SendEnumToUMA("OnDeviceModel.LoadPlatformModelStatus", 0, _))
        .WillOnce(Return(true));
    EXPECT_CALL(metrics_,
                SendTimeToUMA("OnDeviceModel.LoadModelDuration", _, _, _, _))
        .WillOnce(Return(true));

    base::RunLoop run_loop;
    mojo::Remote<mojom::OnDeviceModel> remote;
    service()->LoadPlatformModel(
        base::Uuid::ParseLowercase(model_name),
        remote.BindNewPipeAndPassReceiver(), mojo::NullRemote(),
        base::BindLambdaForTesting([&](mojom::LoadModelResult result) {
          EXPECT_EQ(mojom::LoadModelResult::kSuccess, result);
          run_loop.Quit();
        }));
    run_loop.Run();
    return remote;
  }

  mojo::Remote<mojom::OnDeviceModel> LoadAdaptation(
      mojom::OnDeviceModel& model, base::File adaptation_data) {
    base::RunLoop run_loop;
    mojo::Remote<mojom::OnDeviceModel> remote;
    auto params = mojom::LoadAdaptationParams::New();
    params->assets.weights = std::move(adaptation_data);
    model.LoadAdaptation(
        std::move(params), remote.BindNewPipeAndPassReceiver(),
        base::BindLambdaForTesting([&](mojom::LoadModelResult result) {
          EXPECT_EQ(mojom::LoadModelResult::kSuccess, result);
          run_loop.Quit();
        }));
    run_loop.Run();
    return remote;
  }

  mojom::InputOptionsPtr MakeInput(const std::string& input) {
    return mojom::InputOptions::New(input, std::nullopt, std::nullopt, false,
                                    std::nullopt, std::nullopt, std::nullopt,
                                    std::nullopt);
  }

  std::vector<std::string> GetResponses(mojom::OnDeviceModel& model,
                                        const std::string& input) {
    TestResponseHolder response;
    mojo::Remote<mojom::Session> session;
    model.StartSession(session.BindNewPipeAndPassReceiver());
    session->Execute(MakeInput(input), response.BindRemote());
    response.WaitForCompletion();
    return response.responses();
  }

  size_t GetNumModels() { return service_impl_.NumModelsForTesting(); }

  void FlushService() { service_.FlushForTesting(); }

 protected:
  base::test::TaskEnvironment task_environment_;
  MetricsLibraryMock metrics_;
  odml::OdmlShimLoaderMock shim_loader_;
  mojo::Remote<mojom::OnDeviceModelPlatformService> service_;
  OnDeviceModelService service_impl_;
};

TEST_F(OnDeviceModelServiceTest, Responds) {
  auto model = LoadModel();
  EXPECT_THAT(GetResponses(*model, "bar"), ElementsAre("Input: bar\n"));
  // Try another input on  the same model.
  EXPECT_THAT(GetResponses(*model, "cat"), ElementsAre("Input: cat\n"));
}

TEST_F(OnDeviceModelServiceTest, AddContext) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());
  session->AddContext(MakeInput("cheese"), {});
  session->AddContext(MakeInput("more"), {});
  session->Execute(MakeInput("cheddar"), response.BindRemote());
  response.WaitForCompletion();

  EXPECT_THAT(
      response.responses(),
      ElementsAre("Context: cheese\n", "Context: more\n", "Input: cheddar\n"));
}

TEST_F(OnDeviceModelServiceTest, CloneContext) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());
  session->AddContext(MakeInput("cheese"), {});
  session->AddContext(MakeInput("more"), {});

  mojo::Remote<mojom::Session> cloned;
  session->Clone(cloned.BindNewPipeAndPassReceiver());
  cloned->Execute(MakeInput("cheddar"), response.BindRemote());
  response.WaitForCompletion();

  EXPECT_THAT(
      response.responses(),
      ElementsAre("Context: cheese\n", "Context: more\n", "Input: cheddar\n"));
}

TEST_F(OnDeviceModelServiceTest, MultipleSessionsCloneContextAndContinue) {
  auto model = LoadModel();

  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());
  session->AddContext(MakeInput("cheese"), {});
  session->AddContext(MakeInput("more"), {});

  mojo::Remote<mojom::Session> cloned;
  session->Clone(cloned.BindNewPipeAndPassReceiver());

  {
    TestResponseHolder response;
    cloned->Execute(MakeInput("cheddar"), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(response.responses(),
                ElementsAre("Context: cheese\n", "Context: more\n",
                            "Input: cheddar\n"));
  }
  {
    TestResponseHolder response;
    session->Execute(MakeInput("swiss"), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(
        response.responses(),
        ElementsAre("Context: cheese\n", "Context: more\n", "Input: swiss\n"));
  }

  session->AddContext(MakeInput("foo"), {});
  cloned->AddContext(MakeInput("bar"), {});
  {
    TestResponseHolder response;
    session->Execute(MakeInput("swiss"), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(response.responses(),
                ElementsAre("Context: cheese\n", "Context: more\n",
                            "Context: foo\n", "Input: swiss\n"));
  }
  {
    TestResponseHolder response;
    cloned->Execute(MakeInput("cheddar"), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(response.responses(),
                ElementsAre("Context: cheese\n", "Context: more\n",
                            "Context: bar\n", "Input: cheddar\n"));
  }
}

TEST_F(OnDeviceModelServiceTest, MultipleSessionsAddContext) {
  auto model = LoadModel();

  TestResponseHolder response1, response2, response3, response4, response5;
  mojo::Remote<mojom::Session> session1, session2;

  model->StartSession(session1.BindNewPipeAndPassReceiver());
  model->StartSession(session2.BindNewPipeAndPassReceiver());

  session1->AddContext(MakeInput("cheese"), {});
  session1->AddContext(MakeInput("more"), {});
  session2->AddContext(MakeInput("apple"), {});

  session1->Execute(MakeInput("cheddar"), response1.BindRemote());

  session2->AddContext(MakeInput("banana"), {});

  session2->Execute(MakeInput("candy"), response2.BindRemote());
  session2->Execute(MakeInput("chip"), response3.BindRemote());
  session1->Execute(MakeInput("choco"), response4.BindRemote());
  session2->Execute(MakeInput("orange"), response5.BindRemote());

  response1.WaitForCompletion();
  response2.WaitForCompletion();
  response3.WaitForCompletion();
  response4.WaitForCompletion();
  response5.WaitForCompletion();

  EXPECT_THAT(
      response1.responses(),
      ElementsAre("Context: cheese\n", "Context: more\n", "Input: cheddar\n"));
  EXPECT_THAT(
      response2.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Input: candy\n"));
  EXPECT_THAT(
      response3.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Input: chip\n"));
  EXPECT_THAT(
      response4.responses(),
      ElementsAre("Context: cheese\n", "Context: more\n", "Input: choco\n"));
  EXPECT_THAT(
      response5.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Input: orange\n"));
}

TEST_F(OnDeviceModelServiceTest, MultipleSessionsIgnoreContext) {
  auto model = LoadModel();

  TestResponseHolder response1, response2, response3, response4, response5;
  mojo::Remote<mojom::Session> session1, session2;

  model->StartSession(session1.BindNewPipeAndPassReceiver());
  model->StartSession(session2.BindNewPipeAndPassReceiver());

  session1->AddContext(MakeInput("cheese"), {});

  session1->Execute(MakeInput("cheddar"), response1.BindRemote());

  session1->AddContext(MakeInput("more"), {});
  session2->AddContext(MakeInput("apple"), {});
  session2->AddContext(MakeInput("banana"), {});

  session2->Execute(MakeInput("candy"), response2.BindRemote());
  session2->Execute(
      mojom::InputOptions::New("chip", std::nullopt, std::nullopt,
                               /*ignore_context=*/true, std::nullopt,
                               std::nullopt, std::nullopt, std::nullopt),
      response3.BindRemote());
  session1->Execute(
      mojom::InputOptions::New("choco", std::nullopt, std::nullopt,
                               /*ignore_context=*/true, std::nullopt,
                               std::nullopt, std::nullopt, std::nullopt),
      response4.BindRemote());
  session2->Execute(MakeInput("orange"), response5.BindRemote());

  response1.WaitForCompletion();
  response2.WaitForCompletion();
  response3.WaitForCompletion();
  response4.WaitForCompletion();
  response5.WaitForCompletion();

  EXPECT_THAT(response1.responses(),
              ElementsAre("Context: cheese\n", "Input: cheddar\n"));
  EXPECT_THAT(
      response2.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Input: candy\n"));
  EXPECT_THAT(response3.responses(), ElementsAre("Input: chip\n"));
  EXPECT_THAT(response4.responses(), ElementsAre("Input: choco\n"));
  EXPECT_THAT(
      response5.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Input: orange\n"));
}

TEST_F(OnDeviceModelServiceTest, AddContextWithTokenLimits) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());

  std::string input = "big cheese";
  ContextClientWaiter client1;
  session->AddContext(
      mojom::InputOptions::New(input, /*max_tokens=*/4, std::nullopt, false,
                               std::nullopt, std::nullopt, std::nullopt,
                               std::nullopt),
      client1.BindRemote());
  EXPECT_EQ(client1.WaitForCompletion(), 4);

  ContextClientWaiter client2;
  session->AddContext(
      mojom::InputOptions::New(input, std::nullopt, /*token_offset=*/4, false,
                               std::nullopt, std::nullopt, std::nullopt,
                               std::nullopt),
      client2.BindRemote());
  EXPECT_EQ(client2.WaitForCompletion(), 6);

  session->Execute(MakeInput("cheddar"), response.BindRemote());
  response.WaitForCompletion();

  EXPECT_THAT(
      response.responses(),
      ElementsAre("Context: big \n", "Context: cheese\n", "Input: cheddar\n"));
}

TEST_F(OnDeviceModelServiceTest, MultipleSessionsWaitPreviousSession) {
  auto model = LoadModel();

  TestResponseHolder response1;
  mojo::Remote<mojom::Session> session1;
  model->StartSession(session1.BindNewPipeAndPassReceiver());
  session1->Execute(MakeInput("1"), response1.BindRemote());

  mojo::Remote<mojom::Session> session2;
  model->StartSession(session2.BindNewPipeAndPassReceiver());

  // First session should not get canceled.
  session1.reset_on_disconnect();
  FlushService();
  EXPECT_TRUE(session1);

  // Response from first session should still work.
  response1.WaitForCompletion();
  EXPECT_THAT(response1.responses(), ElementsAre("Input: 1\n"));

  // Second session still works.
  TestResponseHolder response2;
  session2->Execute(MakeInput("2"), response2.BindRemote());
  response2.WaitForCompletion();
  EXPECT_THAT(response2.responses(), ElementsAre("Input: 2\n"));
}

TEST_F(OnDeviceModelServiceTest, LoadsAdaptation) {
  FakeFile weights1("Adapt1");
  FakeFile weights2("Adapt2");
  auto model = LoadModel();
  auto adaptation1 = LoadAdaptation(*model, weights1.Open());
  EXPECT_THAT(GetResponses(*model, "foo"), ElementsAre("Input: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation1, "foo"),
              ElementsAre("Adaptation: Adapt1\n", "Input: foo\n"));

  auto adaptation2 = LoadAdaptation(*model, weights2.Open());
  EXPECT_THAT(GetResponses(*model, "foo"), ElementsAre("Input: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation1, "foo"),
              ElementsAre("Adaptation: Adapt1\n", "Input: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation2, "foo"),
              ElementsAre("Adaptation: Adapt2\n", "Input: foo\n"));
}

TEST_F(OnDeviceModelServiceTest,
       MultipleSessionsLoadingAdaptationNotCancelsSession) {
  FakeFile weights1("Adapt1");
  auto model = LoadModel();

  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());
  session.reset_on_disconnect();

  LoadAdaptation(*model, weights1.Open());
  FlushService();
  EXPECT_TRUE(session);
}

TEST_F(OnDeviceModelServiceTest, DeletesModel) {
  FakeFile weights1("Adapt1");
  FakeFile weights2("Adapt2");
  FakeFile weights3("Adapt3");
  auto model1 = LoadModel();
  auto adaptation1 = LoadAdaptation(*model1, weights1.Open());
  auto adaptation2 = LoadAdaptation(*model1, weights2.Open());
  EXPECT_EQ(GetNumModels(), 1u);

  auto model2 = LoadModel(kFakeModelName2);
  auto adaptation3 = LoadAdaptation(*model2, weights3.Open());
  EXPECT_EQ(GetNumModels(), 2u);

  adaptation1.reset();
  adaptation2.reset();
  FlushService();
  EXPECT_EQ(GetNumModels(), 2u);

  model1.reset();
  FlushService();
  EXPECT_EQ(GetNumModels(), 1u);

  model2.reset();
  FlushService();
  EXPECT_EQ(GetNumModels(), 1u);

  adaptation3.reset();
  FlushService();
  EXPECT_EQ(GetNumModels(), 0u);
}

TEST_F(OnDeviceModelServiceTest, Score) {
  auto model = LoadModel();

  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());
  session->AddContext(MakeInput("hi"), {});

  {
    base::test::TestFuture<float> future;
    session->Score("x", future.GetCallback());
    EXPECT_EQ(future.Get(), static_cast<float>('x'));
  }
  {
    base::test::TestFuture<float> future;
    session->Score("y", future.GetCallback());
    EXPECT_EQ(future.Get(), static_cast<float>('y'));
  }
}

TEST_F(OnDeviceModelServiceTest, FormatInput) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("FormatInput"))
      .WillOnce(Return(reinterpret_cast<void*>(FormatInputSignature(
          [](const std::string& uuid, Feature feature,
             const std::unordered_map<std::string, std::string>& fields)
              -> std::optional<std::string> {
            EXPECT_EQ(uuid, kFakeModelName1);
            EXPECT_EQ(feature, static_cast<Feature>(
                                   mojom::FormatFeature::kAudioSummary));
            EXPECT_THAT(fields, UnorderedElementsAre(Pair("name", "joe"),
                                                     Pair("action", "eat")));
            return "My final input.";
          }))));

  base::RunLoop run_loop;
  service()->FormatInput(
      base::Uuid::ParseLowercase(kFakeModelName1),
      mojom::FormatFeature::kAudioSummary, {{"name", "joe"}, {"action", "eat"}},
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        EXPECT_EQ(result, "My final input.");
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(OnDeviceModelServiceTest, FormatInputNoResult) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("FormatInput"))
      .WillOnce(Return(reinterpret_cast<void*>(FormatInputSignature(
          [](const std::string& uuid, Feature feature,
             const std::unordered_map<std::string, std::string>& fields)
              -> std::optional<std::string> { return std::nullopt; }))));

  base::RunLoop run_loop;
  service()->FormatInput(
      base::Uuid::ParseLowercase(kFakeModelName1),
      mojom::FormatFeature::kAudioSummary, {{"name", "joe"}, {"action", "eat"}},
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        EXPECT_EQ(result, std::nullopt);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(OnDeviceModelServiceTest, FormatInputNoFunction) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("FormatInput"))
      .WillOnce(Return(nullptr));

  base::RunLoop run_loop;
  service()->FormatInput(
      base::Uuid::ParseLowercase(kFakeModelName1),
      mojom::FormatFeature::kAudioSummary, {},
      base::BindLambdaForTesting([&](const std::optional<std::string>& result) {
        EXPECT_EQ(result, std::nullopt);
        run_loop.Quit();
      }));
  run_loop.Run();
}

}  // namespace
}  // namespace on_device_model
