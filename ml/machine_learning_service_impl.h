// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_MACHINE_LEARNING_SERVICE_IMPL_H_
#define ML_MACHINE_LEARNING_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include <base/callback_forward.h>
#include <base/macros.h>
#include <dbus/bus.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "ml/dlcservice_client.h"
#include "ml/model_metadata.h"
#include "ml/mojom/machine_learning_service.mojom.h"

namespace ml {

class MachineLearningServiceImpl
    : public chromeos::machine_learning::mojom::MachineLearningService {
 public:
  // Creates an instance bound to `pipe`. The specified `disconnect_handler`
  // will be invoked if the binding encounters a connection error or is closed.
  // The `bus` is used to construct `dlcservice_client_` if it is not nullptr.
  MachineLearningServiceImpl(
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::MachineLearningService> receiver,
      base::Closure disconnect_handler,
      dbus::Bus* bus = nullptr);

 protected:
  // Testing constructor that allows overriding of the model dir. Should not be
  // used outside of tests.
  MachineLearningServiceImpl(
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::MachineLearningService> receiver,
      base::Closure disconnect_handler,
      const std::string& model_dir);
  MachineLearningServiceImpl(const MachineLearningServiceImpl&) = delete;
  MachineLearningServiceImpl& operator=(const MachineLearningServiceImpl&) =
      delete;

 private:
  // chromeos::machine_learning::mojom::MachineLearningService:
  void Clone(mojo::PendingReceiver<
             chromeos::machine_learning::mojom::MachineLearningService>
                 receiver) override;
  void LoadBuiltinModel(
      chromeos::machine_learning::mojom::BuiltinModelSpecPtr spec,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::Model> receiver,
      LoadBuiltinModelCallback callback) override;
  void LoadFlatBufferModel(
      chromeos::machine_learning::mojom::FlatBufferModelSpecPtr spec,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::Model> receiver,
      LoadFlatBufferModelCallback callback) override;
  void LoadTextClassifier(
      mojo::PendingReceiver<chromeos::machine_learning::mojom::TextClassifier>
          receiver,
      LoadTextClassifierCallback callback) override;
  void LoadHandwritingModel(
      chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec,
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::HandwritingRecognizer> receiver,
      LoadHandwritingModelCallback callback) override;
  void LoadHandwritingModelWithSpec(
      chromeos::machine_learning::mojom::HandwritingRecognizerSpecPtr spec,
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::HandwritingRecognizer> receiver,
      LoadHandwritingModelWithSpecCallback callback) override;
  void LoadSpeechRecognizer(
      chromeos::machine_learning::mojom::SodaConfigPtr spec,
      mojo::PendingRemote<chromeos::machine_learning::mojom::SodaClient>
          soda_client,
      mojo::PendingReceiver<chromeos::machine_learning::mojom::SodaRecognizer>
          soda_recognizer,
      LoadSpeechRecognizerCallback callback) override;
  void LoadGrammarChecker(
      mojo::PendingReceiver<chromeos::machine_learning::mojom::GrammarChecker>
          receiver,
      LoadGrammarCheckerCallback callback) override;
  void LoadTextSuggester(
      mojo::PendingReceiver<chromeos::machine_learning::mojom::TextSuggester>
          receiver,
      LoadTextSuggesterCallback callback) override;
  void LoadWebPlatformHandwritingModel(
      chromeos::machine_learning::web_platform::mojom::
          HandwritingModelConstraintPtr constraint,
      mojo::PendingReceiver<chromeos::machine_learning::web_platform::mojom::
                                HandwritingRecognizer> receiver,
      LoadWebPlatformHandwritingModelCallback callback) override;

  // Metadata required to load builtin models. Initialized at construction.
  const std::map<chromeos::machine_learning::mojom::BuiltinModelId,
                 BuiltinModelMetadata>
      builtin_model_metadata_;

  const std::string model_dir_;

  // DlcserviceClient used to communicate with DlcService.
  std::unique_ptr<DlcserviceClient> dlcservice_client_;

  // Primordial receiver bootstrapped over D-Bus. Once opened, is never closed.
  mojo::Receiver<chromeos::machine_learning::mojom::MachineLearningService>
      receiver_;

  // Additional receivers bound via `Clone`.
  mojo::ReceiverSet<chromeos::machine_learning::mojom::MachineLearningService>
      clone_receivers_;
};

}  // namespace ml

#endif  // ML_MACHINE_LEARNING_SERVICE_IMPL_H_
