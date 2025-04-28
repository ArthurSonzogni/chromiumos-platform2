// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/on_device_model_service.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <base/files/scoped_temp_file.h>
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
#include "odml/on_device_model/fake/fake_chrome_ml_api.h"
#include "odml/on_device_model/fake/on_device_model_fake.h"
#include "odml/on_device_model/features.h"
#include "odml/on_device_model/ml/chrome_ml_types.h"
#include "odml/on_device_model/public/cpp/test_support/test_response_holder.h"
#include "odml/on_device_model/public/cpp/text_safety_assets.h"
#include "odml/periodic_metrics.h"
#include "odml/utils/odml_shim_loader_mock.h"

namespace on_device_model {
namespace {

constexpr char kFakeModelName1[] = "5f30b8ca-2447-445e-9716-a6da073fae51";
constexpr char kFakeModelName2[] = "90eeb7f8-9491-452d-9ec9-5b6edd6c93fa";
constexpr char kFakeApuModelName[] = "e0b11b2d-cd05-43e2-ac9e-4cf608727128";

using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::NiceMock;
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
    CHECK(temp_file_.Create());
    base::File file(temp_file_.path(), base::File::FLAG_OPEN |
                                           base::File::FLAG_WRITE |
                                           base::File::FLAG_READ);
    CHECK(file.IsValid());
    file.WriteAtCurrentPos(base::as_byte_span(content));
  }
  ~FakeFile() = default;

  base::File Open() {
    base::ScopedAllowBlockingForTesting allow_blocking;
    return base::File(temp_file_.path(), base::File::FLAG_OPEN |
                                             base::File::FLAG_WRITE |
                                             base::File::FLAG_READ);
  }

  base::FilePath Path() { return temp_file_.path(); }

 private:
  base::ScopedTempFile temp_file_;
};

class OnDeviceModelServiceTest : public testing::Test {
 public:
  OnDeviceModelServiceTest()
      : service_impl_(raw_ref(metrics_),
                      raw_ref(periodic_metrics_),
                      raw_ref(shim_loader_)) {
    fake_ml::SetupFakeChromeML(raw_ref(metrics_), raw_ref(shim_loader_));
    mojo::core::Init();
    service_impl_.AddReceiver(service_.BindNewPipeAndPassReceiver());
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

  mojo::Remote<mojom::OnDeviceModel> LoadAdaptationWithParams(
      mojom::OnDeviceModel& model,
      mojom::LoadAdaptationParamsPtr adaptation_params) {
    base::RunLoop run_loop;
    mojo::Remote<mojom::OnDeviceModel> remote;
    model.LoadAdaptation(
        std::move(adaptation_params), remote.BindNewPipeAndPassReceiver(),
        base::BindLambdaForTesting([&](mojom::LoadModelResult result) {
          EXPECT_EQ(mojom::LoadModelResult::kSuccess, result);
          run_loop.Quit();
        }));
    run_loop.Run();
    return remote;
  }

  mojo::Remote<mojom::OnDeviceModel> LoadAdaptation(
      mojom::OnDeviceModel& model, base::File adaptation_data) {
    auto params = mojom::LoadAdaptationParams::New();
    params->assets.weights = std::move(adaptation_data);
    return LoadAdaptationWithParams(model, std::move(params));
  }

  mojo::Remote<mojom::OnDeviceModel> LoadAdaptation(
      mojom::OnDeviceModel& model, base::FilePath adaptation_path) {
    auto params = mojom::LoadAdaptationParams::New();
    params->assets.weights_path = std::move(adaptation_path);
    return LoadAdaptationWithParams(model, std::move(params));
  }

  mojom::AppendOptionsPtr MakeInput(const std::string& input) {
    return MakeInput({ml::InputPiece(input)});
  }

  mojom::AppendOptionsPtr MakeInput(std::vector<ml::InputPiece> input) {
    auto options = mojom::AppendOptions::New();
    options->input = mojom::Input::New(std::move(input));
    return options;
  }

  std::vector<std::string> GetResponses(mojom::OnDeviceModel& model,
                                        const std::string& input) {
    TestResponseHolder response;
    mojo::Remote<mojom::Session> session;
    model.StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
    auto options = mojom::AppendOptions::New();
    options->input =
        mojom::Input::New(std::vector<ml::InputPiece>{ml::InputPiece(input)});
    session->Append(std::move(options), {});
    session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
    response.WaitForCompletion();
    return response.responses();
  }

  size_t GetNumModels() { return service_impl_.NumModelsForTesting(); }

  void FlushService() { service_.FlushForTesting(); }

 protected:
  base::test::TaskEnvironment task_environment_;
  NiceMock<MetricsLibraryMock> metrics_;
  odml::PeriodicMetrics periodic_metrics_{raw_ref(metrics_)};
  odml::OdmlShimLoaderMock shim_loader_;
  mojo::Remote<mojom::OnDeviceModelPlatformService> service_;
  OnDeviceModelService service_impl_;
};

TEST_F(OnDeviceModelServiceTest, Responds) {
  auto model = LoadModel();
  EXPECT_THAT(GetResponses(*model, "bar"), ElementsAre("Context: bar\n"));
  // Try another input on  the same model.
  EXPECT_THAT(GetResponses(*model, "cat"), ElementsAre("Context: cat\n"));
}

TEST_F(OnDeviceModelServiceTest, Append) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
  session->Append(MakeInput("cheese"), {});
  session->Append(MakeInput("more"), {});
  session->Append(MakeInput("cheddar"), {});
  session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
  response.WaitForCompletion();

  EXPECT_THAT(response.responses(),
              ElementsAre("Context: cheese\n", "Context: more\n",
                          "Context: cheddar\n"));
}

TEST_F(OnDeviceModelServiceTest, CloneContextAndContinue) {
  auto model = LoadModel();

  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
  session->Append(MakeInput("cheese"), {});
  session->Append(MakeInput("more"), {});

  mojo::Remote<mojom::Session> cloned;
  session->Clone(cloned.BindNewPipeAndPassReceiver());

  {
    TestResponseHolder response;
    cloned->Generate(mojom::GenerateOptions::New(), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(response.responses(),
                ElementsAre("Context: cheese\n", "Context: more\n"));
  }
  {
    TestResponseHolder response;
    session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(response.responses(),
                ElementsAre("Context: cheese\n", "Context: more\n"));
  }

  session->Append(MakeInput("foo"), {});
  cloned->Append(MakeInput("bar"), {});
  {
    TestResponseHolder response;
    session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(
        response.responses(),
        ElementsAre("Context: cheese\n", "Context: more\n", "Context: foo\n"));
  }
  {
    TestResponseHolder response;
    cloned->Generate(mojom::GenerateOptions::New(), response.BindRemote());
    response.WaitForCompletion();
    EXPECT_THAT(
        response.responses(),
        ElementsAre("Context: cheese\n", "Context: more\n", "Context: bar\n"));
  }
}

TEST_F(OnDeviceModelServiceTest, MultipleSessionsAppend) {
  auto model = LoadModel();

  TestResponseHolder response1, response2, response3, response4, response5;
  mojo::Remote<mojom::Session> session1, session2, session3, session4, session5;

  model->StartSession(session1.BindNewPipeAndPassReceiver(), nullptr);
  model->StartSession(session2.BindNewPipeAndPassReceiver(), nullptr);

  session1->Append(MakeInput("cheese"), {});
  session1->Append(MakeInput("more"), {});
  session2->Append(MakeInput("apple"), {});

  session1->Clone(session3.BindNewPipeAndPassReceiver());
  session1->Append(MakeInput("cheddar"), {});
  session1->Generate(mojom::GenerateOptions::New(), response1.BindRemote());

  session2->Append(MakeInput("banana"), {});

  session2->Clone(session4.BindNewPipeAndPassReceiver());
  session2->Append(MakeInput("candy"), {});
  session2->Generate(mojom::GenerateOptions::New(), response2.BindRemote());

  session4->Clone(session5.BindNewPipeAndPassReceiver());
  session4->Append(MakeInput("chip"), {});
  session4->Generate(mojom::GenerateOptions::New(), response3.BindRemote());

  session3->Append(MakeInput("choco"), {});
  session3->Generate(mojom::GenerateOptions::New(), response4.BindRemote());

  session5->Append(MakeInput("orange"), {});
  session5->Generate(mojom::GenerateOptions::New(), response5.BindRemote());

  response1.WaitForCompletion();
  response2.WaitForCompletion();
  response3.WaitForCompletion();
  response4.WaitForCompletion();
  response5.WaitForCompletion();

  EXPECT_THAT(response1.responses(),
              ElementsAre("Context: cheese\n", "Context: more\n",
                          "Context: cheddar\n"));
  EXPECT_THAT(
      response2.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Context: candy\n"));
  EXPECT_THAT(
      response3.responses(),
      ElementsAre("Context: apple\n", "Context: banana\n", "Context: chip\n"));
  EXPECT_THAT(
      response4.responses(),
      ElementsAre("Context: cheese\n", "Context: more\n", "Context: choco\n"));
  EXPECT_THAT(response5.responses(),
              ElementsAre("Context: apple\n", "Context: banana\n",
                          "Context: orange\n"));
}

TEST_F(OnDeviceModelServiceTest, CountTokens) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
  session->Append(MakeInput("cheese"), {});
  session->Append(MakeInput("more"), {});

  std::string input = "cheddar";
  session->Append(MakeInput(input), {});
  session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
  response.WaitForCompletion();

  // 3 context.
  EXPECT_THAT(response.output_token_count(), 3);
}

TEST_F(OnDeviceModelServiceTest, AppendWithTokenLimits) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);

  std::string input = "big cheese";
  ContextClientWaiter client1;
  auto max_input = MakeInput("big cheese");
  max_input->max_tokens = 4;
  session->Append(std::move(max_input), client1.BindRemote());
  EXPECT_EQ(client1.WaitForCompletion(), 4);

  ContextClientWaiter client2;
  auto offset_input = MakeInput("big cheese");
  offset_input->token_offset = 4;
  session->Append(std::move(offset_input), client2.BindRemote());
  EXPECT_EQ(client2.WaitForCompletion(), 6);

  session->Append(MakeInput("cheddar"), {});
  session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
  response.WaitForCompletion();

  EXPECT_THAT(response.responses(),
              ElementsAre("Context: big \n", "Context: cheese\n",
                          "Context: cheddar\n"));
}

TEST_F(OnDeviceModelServiceTest, MultipleSessionsWaitPreviousSession) {
  auto model = LoadModel();

  TestResponseHolder response1;
  mojo::Remote<mojom::Session> session1;
  model->StartSession(session1.BindNewPipeAndPassReceiver(), nullptr);
  session1->Append(MakeInput("1"), {});
  session1->Generate(mojom::GenerateOptions::New(), response1.BindRemote());

  mojo::Remote<mojom::Session> session2;
  model->StartSession(session2.BindNewPipeAndPassReceiver(), nullptr);

  // First session should not get canceled.
  session1.reset_on_disconnect();
  FlushService();
  EXPECT_TRUE(session1);

  // Response from first session should still work.
  response1.WaitForCompletion();
  EXPECT_THAT(response1.responses(), ElementsAre("Context: 1\n"));

  // Second session still works.
  TestResponseHolder response2;
  session2->Append(MakeInput("2"), {});
  session2->Generate(mojom::GenerateOptions::New(), response2.BindRemote());
  response2.WaitForCompletion();
  EXPECT_THAT(response2.responses(), ElementsAre("Context: 2\n"));
}

TEST_F(OnDeviceModelServiceTest, LoadsAdaptation) {
  FakeFile weights1("Adapt1");
  FakeFile weights2("Adapt2");
  auto model = LoadModel();
  auto adaptation1 = LoadAdaptation(*model, weights1.Open());
  EXPECT_THAT(GetResponses(*model, "foo"), ElementsAre("Context: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation1, "foo"),
              ElementsAre("Adaptation: Adapt1 (0)\n", "Context: foo\n"));

  auto adaptation2 = LoadAdaptation(*model, weights2.Open());
  EXPECT_THAT(GetResponses(*model, "foo"), ElementsAre("Context: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation1, "foo"),
              ElementsAre("Adaptation: Adapt1 (0)\n", "Context: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation2, "foo"),
              ElementsAre("Adaptation: Adapt2 (1)\n", "Context: foo\n"));
}

TEST_F(OnDeviceModelServiceTest, LoadsAdaptationWithPath) {
  FakeFile weights1("Adapt1");
  FakeFile weights2("Adapt2");
  auto model = LoadModel(kFakeApuModelName);
  auto adaptation1 = LoadAdaptation(*model, weights1.Path());
  EXPECT_THAT(GetResponses(*model, "foo"), ElementsAre("Context: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation1, "foo"),
              ElementsAre("Adaptation: Adapt1 (0)\n", "Context: foo\n"));

  auto adaptation2 = LoadAdaptation(*model, weights2.Path());
  EXPECT_THAT(GetResponses(*model, "foo"), ElementsAre("Context: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation1, "foo"),
              ElementsAre("Adaptation: Adapt1 (0)\n", "Context: foo\n"));
  EXPECT_THAT(GetResponses(*adaptation2, "foo"),
              ElementsAre("Adaptation: Adapt2 (1)\n", "Context: foo\n"));
}

TEST_F(OnDeviceModelServiceTest, LoadingAdaptationDoesNotCancelSession) {
  FakeFile weights1("Adapt1");
  auto model = LoadModel();

  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
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
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
  session->Append(MakeInput("hi"), {});

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

TEST_F(OnDeviceModelServiceTest, ValidateSafetyResult) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("ValidateSafetyResult"))
      .WillOnce(Return(reinterpret_cast<void*>(ValidateSafetyResultSignature(
          [](SafetyFeature feature, const std::string& input,
             const std::vector<float>& score) -> bool {
            EXPECT_EQ(feature, static_cast<SafetyFeature>(
                                   mojom::SafetyFeature::kAudioSummaryRequest));
            EXPECT_EQ(input, "My input");
            EXPECT_THAT(score, ElementsAre(1.0, 0.0, 0.5));
            return true;
          }))));

  auto safety_info = on_device_model::mojom::SafetyInfo::New();
  safety_info->class_scores = std::vector<float>({1.0, 0.0, 0.5});

  base::RunLoop run_loop;
  service()->ValidateSafetyResult(mojom::SafetyFeature::kAudioSummaryRequest,
                                  "My input", std::move(safety_info),
                                  base::BindLambdaForTesting([&](bool result) {
                                    EXPECT_TRUE(result);
                                    run_loop.Quit();
                                  }));
  run_loop.Run();
}

TEST_F(OnDeviceModelServiceTest, ValidateSafetyResultNoFunction) {
  EXPECT_CALL(shim_loader_, IsShimReady()).WillOnce(Return(true));
  EXPECT_CALL(shim_loader_, GetFunctionPointer("ValidateSafetyResult"))
      .WillOnce(Return(nullptr));

  auto safety_info = on_device_model::mojom::SafetyInfo::New();
  safety_info->class_scores = std::vector<float>({1.0, 0.0, 0.5});

  base::RunLoop run_loop;
  service()->ValidateSafetyResult(mojom::SafetyFeature::kAudioSummaryRequest,
                                  "My input", std::move(safety_info),
                                  base::BindLambdaForTesting([&](bool result) {
                                    EXPECT_FALSE(result);
                                    run_loop.Quit();
                                  }));
  run_loop.Run();
}

TEST_F(OnDeviceModelServiceTest, AppendWithTokens) {
  auto model = LoadModel();

  TestResponseHolder response;
  mojo::Remote<mojom::Session> session;
  model->StartSession(session.BindNewPipeAndPassReceiver(), nullptr);
  {
    std::vector<ml::InputPiece> pieces;
    pieces.push_back(ml::Token::kSystem);
    pieces.push_back("hi");
    pieces.push_back(ml::Token::kEnd);
    session->Append(MakeInput(std::move(pieces)), {});
  }
  {
    std::vector<ml::InputPiece> pieces;
    pieces.push_back(ml::Token::kModel);
    pieces.push_back("hello");
    pieces.push_back(ml::Token::kEnd);
    session->Append(MakeInput(std::move(pieces)), {});
  }
  {
    std::vector<ml::InputPiece> pieces;
    pieces.push_back(ml::Token::kUser);
    pieces.push_back("bye");
    session->Append(MakeInput(std::move(pieces)), {});
    session->Generate(mojom::GenerateOptions::New(), response.BindRemote());
  }
  response.WaitForCompletion();

  EXPECT_THAT(response.responses(), ElementsAre("Context: System: hi End.\n",
                                                "Context: Model: hello End.\n",
                                                "Context: User: bye\n"));
}

TEST_F(OnDeviceModelServiceTest, ClassifyTextSafety) {
  FakeFile ts_data("fake_ts_data");
  FakeFile ts_sp_model("fake_ts_sp_model");
  TextSafetyLoaderParams params;
  params.ts_paths.emplace();
  params.ts_paths->data = ts_data.Path();
  params.ts_paths->sp_model = ts_sp_model.Path();
  mojo::Remote<mojom::TextSafetyModel> model;
  service_impl_.LoadTextSafetyModel(LoadTextSafetyParams(params),
                                    model.BindNewPipeAndPassReceiver());
  base::test::TestFuture<mojom::SafetyInfoPtr> future1;
  base::test::TestFuture<mojom::SafetyInfoPtr> future2;
  mojo::Remote<mojom::TextSafetySession> session;
  model->StartSession(session.BindNewPipeAndPassReceiver());
  session->ClassifyTextSafety("unsafe text", future1.GetCallback());
  session->ClassifyTextSafety("reasonable text", future2.GetCallback());
  auto resp1 = future1.Take();
  auto resp2 = future2.Take();

  ASSERT_TRUE(resp1);
  EXPECT_THAT(resp1->class_scores, ElementsAre(0.8, 0.8));
  ASSERT_TRUE(resp2);
  EXPECT_THAT(resp2->class_scores, ElementsAre(0.2, 0.2));
}

}  // namespace
}  // namespace on_device_model
