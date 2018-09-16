// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_MODEL_IMPL_H_
#define ML_MODEL_IMPL_H_

#include <list>
#include <map>
#include <memory>
#include <string>

#include <base/macros.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <tensorflow/contrib/lite/model.h>

#include "ml/graph_executor_impl.h"
#include "ml/mojom/model.mojom.h"

namespace ml {

// Holds a TensorFlow lite graph and produces GraphExecutors that may run the
// graph.
//
// All GraphExecutors created by a ModelImpl reference its model definition (and
// hence may not outlive the ModelImpl). Multiple such GraphExecutors may be
// used concurrently from different sequences.
class ModelImpl : public chromeos::machine_learning::mojom::Model {
 public:
  // Creates an instance bound to |request|.
  //
  // The |required_inputs| and |required_outputs| arguments specify a mapping
  // from required input / output tensor names to their indices in the TF lite
  // graph, and must outlive this object.
  ModelImpl(const std::map<std::string, int>& required_inputs,
            const std::map<std::string, int>& required_outputs,
            std::unique_ptr<tflite::FlatBufferModel> model,
            chromeos::machine_learning::mojom::ModelRequest request);

  void set_connection_error_handler(base::Closure connection_error_handler);

  int num_graph_executors_for_testing() const;

 private:
  // chromeos::machine_learning::mojom::Model:
  void CreateGraphExecutor(
      chromeos::machine_learning::mojom::GraphExecutorRequest request,
      const CreateGraphExecutorCallback& callback) override;

  // Remove a graph executor from our hosted set.
  void EraseGraphExecutor(std::list<GraphExecutorImpl>::const_iterator it);

  const std::map<std::string, int>& required_inputs_;
  const std::map<std::string, int>& required_outputs_;

  const std::unique_ptr<tflite::FlatBufferModel> model_;

  mojo::Binding<chromeos::machine_learning::mojom::Model> binding_;

  // Emulate a strong binding set: hold a set of GraphExecutors, specific
  // elements of which are erased on connection error.
  //
  // That is, when a pipe to a GraphExecutorImpl closes, that object is removed
  // from this set (by its binding connection error handler). Further, when a
  // ModelImpl is destoyed, its entire collection of GraphExecutorImpls is also
  // destroyed.
  std::list<GraphExecutorImpl> graph_executors_;

  DISALLOW_COPY_AND_ASSIGN(ModelImpl);
};

}  // namespace ml

#endif  // ML_MODEL_IMPL_H_
