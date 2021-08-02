// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/model_impl.h"

#include <algorithm>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>

#include "ml/graph_executor_delegate.h"
#include "ml/machine_learning_service_impl.h"

namespace {

// Callback for self-owned ModelImpl's to delete themselves upon disconnection.
void DeleteModelImpl(const ml::ModelImpl* const model_impl) {
  delete model_impl;
}

}  // namespace

namespace ml {

using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::GraphExecutorOptions;
using ::chromeos::machine_learning::mojom::GraphExecutorOptionsPtr;
using ::chromeos::machine_learning::mojom::Model;

ModelImpl* ModelImpl::Create(std::unique_ptr<ModelDelegate> model_delegate,
                             mojo::PendingReceiver<Model> receiver) {
  auto model_impl =
      new ModelImpl(std::move(model_delegate), std::move(receiver));
  // Use a disconnection handler to strongly bind `model_impl` to `receiver`.
  model_impl->set_disconnect_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));

  return model_impl;
}

ModelImpl::ModelImpl(std::unique_ptr<ModelDelegate> model_delegate,
                     mojo::PendingReceiver<Model> receiver)
    : model_delegate_(std::move(model_delegate)),
      receiver_(this, std::move(receiver)) {}

void ModelImpl::set_disconnect_handler(base::Closure disconnect_handler) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

int ModelImpl::num_graph_executors_for_testing() const {
  return graph_executors_.size();
}

void ModelImpl::CreateGraphExecutor(
    mojo::PendingReceiver<GraphExecutor> receiver,
    CreateGraphExecutorCallback callback) {
  auto options = GraphExecutorOptions::New(
      /*use_nnapi=*/false, /*use_gpu=*/false);
  CreateGraphExecutorWithOptions(std::move(options), std::move(receiver),
                                 std::move(callback));
}

void ModelImpl::CreateGraphExecutorWithOptions(
    GraphExecutorOptionsPtr options,
    mojo::PendingReceiver<GraphExecutor> receiver,
    CreateGraphExecutorCallback callback) {
  GraphExecutorDelegate* graph_executor_delegate;
  auto result = model_delegate_->CreateGraphExecutorDelegate(
      options->use_nnapi, options->use_gpu, &graph_executor_delegate);
  if (result != CreateGraphExecutorResult::OK) {
    std::move(callback).Run(result);
    return;
  }

  // Add graph executor and schedule its deletion on pipe closure.
  graph_executors_.emplace_front(
      std::unique_ptr<GraphExecutorDelegate>(graph_executor_delegate),
      std::move(receiver));
  graph_executors_.front().set_disconnect_handler(
      base::Bind(&ModelImpl::EraseGraphExecutor, base::Unretained(this),
                 graph_executors_.begin()));

  std::move(callback).Run(CreateGraphExecutorResult::OK);
}

void ModelImpl::EraseGraphExecutor(
    const std::list<GraphExecutorImpl>::const_iterator it) {
  graph_executors_.erase(it);
}

}  // namespace ml
