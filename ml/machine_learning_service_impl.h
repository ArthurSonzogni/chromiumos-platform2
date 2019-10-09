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
#include <mojo/public/cpp/bindings/binding.h>

#include "ml/model_metadata.h"
#include "ml/mojom/machine_learning_service.mojom.h"

namespace ml {

class MachineLearningServiceImpl
    : public chromeos::machine_learning::mojom::MachineLearningService {
 public:
  // Creates an instance bound to |pipe|. The specified
  // |connection_error_handler| will be invoked if the binding encounters a
  // connection error.
  MachineLearningServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                             base::Closure connection_error_handler);

 protected:
  // Testing constructor that allows overriding of the model dir. Should not be
  // used outside of tests.
  MachineLearningServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                             base::Closure connection_error_handler,
                             const std::string& model_dir);

 private:
  // chromeos::machine_learning::mojom::MachineLearningService:
  void LoadBuiltinModel(
      chromeos::machine_learning::mojom::BuiltinModelSpecPtr spec,
      chromeos::machine_learning::mojom::ModelRequest request,
      const LoadBuiltinModelCallback& callback) override;

  // chromeos::machine_learning::mojom::MachineLearningService:
  void LoadFlatBufferModel(
      chromeos::machine_learning::mojom::FlatBufferModelSpecPtr spec,
      chromeos::machine_learning::mojom::ModelRequest request,
      const LoadFlatBufferModelCallback& callback) override;

  // Metadata required to load builtin models. Initialized at construction.
  const std::map<chromeos::machine_learning::mojom::BuiltinModelId,
                 BuiltinModelMetadata>
      builtin_model_metadata_;

  const std::string model_dir_;

  mojo::Binding<chromeos::machine_learning::mojom::MachineLearningService>
      binding_;

  DISALLOW_COPY_AND_ASSIGN(MachineLearningServiceImpl);
};

}  // namespace ml

#endif  // ML_MACHINE_LEARNING_SERVICE_IMPL_H_
