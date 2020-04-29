// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/containers/flat_map.h>
#include <base/files/file_util.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <base/stl_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/binding.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "ml/machine_learning_service_impl.h"
#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/mojom/text_classifier.mojom.h"
#include "ml/tensor_view.h"
#include "ml/test_utils.h"

namespace ml {
namespace {

constexpr double kSearchRanker20190923TestInput[] = {
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
};

constexpr double kSmartDim20181115TestInput[] = {
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr double kSmartDim20190221TestInput[] = {
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr double kSmartDim20190521TestInput[] = {
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 1,
};

constexpr double kSmartDim20200206TestInput[] = {
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

constexpr double kTopCat20190722TestInput[] = {
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0
};

constexpr char kTextClassifierTestInput[] =
    "user.name@gmail.com. 123 George Street. unknownword. 12pm";

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpec;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::CodepointSpan;
using ::chromeos::machine_learning::mojom::CodepointSpanPtr;
using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpec;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpecPtr;
using ::chromeos::machine_learning::mojom::GraphExecutorPtr;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningServicePtr;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::ModelPtr;
using ::chromeos::machine_learning::mojom::ModelRequest;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationRequest;
using ::chromeos::machine_learning::mojom::TextAnnotationRequestPtr;
using ::chromeos::machine_learning::mojom::TextClassifierPtr;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequest;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequestPtr;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::ElementsAre;

// A version of MachineLearningServiceImpl that loads from the testing model
// directory.
class MachineLearningServiceImplForTesting : public MachineLearningServiceImpl {
 public:
  // Pass a dummy callback and use the testing model directory.
  explicit MachineLearningServiceImplForTesting(
      mojo::ScopedMessagePipeHandle pipe)
      : MachineLearningServiceImpl(
            std::move(pipe), base::Closure(), GetTestModelDir()) {}
};

// Loads builtin model specified by |model_id|, binding the impl to |model|.
// Returns true on success.
bool LoadBuiltinModelForTesting(const MachineLearningServicePtr& ml_service,
                                BuiltinModelId model_id,
                                ModelPtr* model) {
  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = model_id;

  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  return model_callback_done;
}

// Loads flatbuffer model specified by |spec|, binding the impl to |model|.
// Returns true on success.
bool LoadFlatBufferModelForTesting(const MachineLearningServicePtr& ml_service,
                                   FlatBufferModelSpecPtr spec,
                                   ModelPtr* model) {
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), mojo::MakeRequest(model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  return model_callback_done;
}

// Creates graph executor of |model|, binding the impl to |graph_executor|.
// Returns true on success.
bool CreateGraphExecutorForTesting(const ModelPtr& model,
                                   GraphExecutorPtr* graph_executor) {
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  return ge_callback_done;
}

// Checks that |result| is OK and that |outputs| contains a tensor matching
// |expected_shape| and |expected_value|. Sets |infer_callback_done| to true so
// that this function can be used to verify that a Mojo callback has been run.
// TODO(alanlxl): currently the output size of all models are 1, and value types
// are all double. Parameterization may be necessary for future models.
void CheckOutputTensor(const std::vector<int64_t> expected_shape,
                       const double expected_value,
                       bool* infer_callback_done,
                       ExecuteResult result,
                       base::Optional<std::vector<TensorPtr>> outputs) {
  // Check that the inference succeeded and gives the expected number
  // of outputs.
  EXPECT_EQ(result, ExecuteResult::OK);
  ASSERT_TRUE(outputs.has_value());
  // currently all the models here has the same output size 1.
  ASSERT_EQ(outputs->size(), 1);

  // Check that the output tensor has the right type and format.
  const TensorView<double> out_tensor((*outputs)[0]);
  EXPECT_TRUE(out_tensor.IsValidType());
  EXPECT_TRUE(out_tensor.IsValidFormat());

  // Check the output tensor has the expected shape and values.
  EXPECT_EQ(out_tensor.GetShape(), expected_shape);
  EXPECT_THAT(out_tensor.GetValues(),
              ElementsAre(DoubleNear(expected_value, 1e-5)));
  *infer_callback_done = true;
}

TEST(MachineLearningServiceImplTest, TestBadModel) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Set up model spec to specify an invalid model.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::UNSUPPORTED_UNKNOWN;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::MODEL_SPEC_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading an empty model through the downloaded model api.
TEST(MachineLearningServiceImplTest, EmptyModelString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading a bad model string through the downloaded model api.
TEST(MachineLearningServiceImplTest, BadModelString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "bad model string";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading TEST_MODEL through the builtin model api.
TEST(MachineLearningServiceImplTest, TestModel) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Leave loading model and creating graph executor inline here to demonstrate
  // the usage details.
  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TEST_MODEL;

  // Load model.
  ModelPtr model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), mojo::MakeRequest(&model),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor),
      base::Bind(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(ge_callback_done);
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("x", NewTensor<double>({1}, {0.5}));
  inputs.emplace("y", NewTensor<double>({1}, {0.25}));
  std::vector<std::string> outputs({"z"});
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, 0.75, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests loading TEST_MODEL through the downloaded model api.
TEST(MachineLearningServiceImplTest, TestModelString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load the TEST_MODEL model file into string.
  std::string model_string;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath(GetTestModelDir() +
                     "mlservice-model-test_add-20180914.tflite"),
      &model_string));

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = std::move(model_string);
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model.
  ModelPtr model;
  ASSERT_TRUE(LoadFlatBufferModelForTesting(
      ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("x", NewTensor<double>({1}, {0.5}));
  inputs.emplace("y", NewTensor<double>({1}, {0.25}));
  std::vector<std::string> outputs({"z"});
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, 0.75, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20181115) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20181115) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load model.
  ModelPtr model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20181115, &model));
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20181115TestInput)},
                   std::vector<double>(std::begin(kSmartDim20181115TestInput),
                                       std::end(kSmartDim20181115TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, -3.36311, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190221) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20190221) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load model and create graph executor.
  ModelPtr model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20190221, &model));
  ASSERT_TRUE(model.is_bound());

  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20190221TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190221TestInput),
                                       std::end(kSmartDim20190221TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, -0.900591, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190521) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20190521) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load model and create graph executor.
  ModelPtr model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20190521, &model));
  ASSERT_TRUE(model.is_bound());

  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20190521TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190521TestInput),
                                       std::end(kSmartDim20190521TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(&CheckOutputTensor, expected_shape,
                 0.66962254, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Top Cat (20190722) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, TopCat20190722) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load model and create graph executor.
  ModelPtr model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::TOP_CAT_20190722, &model));
  ASSERT_TRUE(model.is_bound());

  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input",
                 NewTensor<double>(
                     {1, base::size(kTopCat20190722TestInput)},
                     std::vector<double>(std::begin(kTopCat20190722TestInput),
                                         std::end(kTopCat20190722TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, -3.02972, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Search Ranker (20190923) model file loads correctly and
// produces the expected inference result.
TEST(BuiltinModelInferenceTest, SearchRanker20190923) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load model and create graph executor.
  ModelPtr model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SEARCH_RANKER_20190923, &model));
  ASSERT_TRUE(model.is_bound());

  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input", NewTensor<double>(
                              {1, base::size(kSearchRanker20190923TestInput)},
                              std::vector<double>(
                                  std::begin(kSearchRanker20190923TestInput),
                                  std::end(kSearchRanker20190923TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, 0.658488, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20200206) model file loads correctly and
// produces the expected inference result.
TEST(DownloadableModelInferenceTest, SmartDim20200206) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  // Load SmartDim model into string.
  std::string model_string;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath(GetTestModelDir() +
                     "mlservice-model-smart_dim-20200206-downloadable.tflite"),
      &model_string));

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = std::move(model_string);
  spec->inputs["input"] = 0;
  spec->outputs["output"] = 6;
  spec->metrics_model_name = "SmartDimModel_20200206";

  // Load model.
  ModelPtr model;
  ASSERT_TRUE(LoadFlatBufferModelForTesting(
      ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  GraphExecutorPtr graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, base::size(kSmartDim20200206TestInput)},
                   std::vector<double>(std::begin(kSmartDim20200206TestInput),
                                       std::end(kSmartDim20200206TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          &CheckOutputTensor, expected_shape, -1.07195, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Test when text classifier can not find the model file.
TEST(LoadTextClassifierTest, BadModelFilename) {
  MachineLearningServicePtr ml_service;
  MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  ml_service_impl.SetTextClassifierModelFilenameForTesting(
      "bad_model_filename");

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::LOAD_MODEL_ERROR);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests loading text classifier only.
TEST(LoadTextClassifierTest, NoInference) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
}

// Tests text classifier annotator for empty string.
TEST(TextClassifierAnnotateTest, EmptyString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextAnnotationRequestPtr request = TextAnnotationRequest::New();
  request->text = "";
  bool infer_callback_done = false;
  text_classifier->Annotate(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done,
             std::vector<TextAnnotationPtr> annotations) {
            *infer_callback_done = true;
            EXPECT_EQ(annotations.size(), 0);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier annotator for a complex string.
TEST(TextClassifierAnnotateTest, ComplexString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextAnnotationRequestPtr request = TextAnnotationRequest::New();
  request->text = kTextClassifierTestInput;
  bool infer_callback_done = false;
  text_classifier->Annotate(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done,
             std::vector<TextAnnotationPtr> annotations) {
            *infer_callback_done = true;
            EXPECT_EQ(annotations.size(), 4);
            EXPECT_EQ(annotations[0]->start_offset, 0);
            EXPECT_EQ(annotations[0]->end_offset, 19);
            ASSERT_GE(annotations[0]->entities.size(), 1);
            EXPECT_EQ(annotations[0]->entities[0]->name, "email");
            EXPECT_EQ(annotations[1]->start_offset, 21);
            EXPECT_EQ(annotations[1]->end_offset, 38);
            ASSERT_GE(annotations[1]->entities.size(), 1);
            EXPECT_EQ(annotations[1]->entities[0]->name, "address");
            EXPECT_EQ(annotations[2]->start_offset, 40);
            EXPECT_EQ(annotations[2]->end_offset, 51);
            ASSERT_GE(annotations[2]->entities.size(), 1);
            EXPECT_EQ(annotations[2]->entities[0]->name, "dictionary");
            EXPECT_EQ(annotations[3]->start_offset, 53);
            EXPECT_EQ(annotations[3]->end_offset, 57);
            ASSERT_GE(annotations[3]->entities.size(), 1);
            EXPECT_EQ(annotations[3]->entities[0]->name, "datetime");
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion for an empty string.
// In this situation, text classifier will return the input span.
TEST(TextClassifierSelectionTest, EmptyString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextSuggestSelectionRequestPtr request = TextSuggestSelectionRequest::New();
  request->text = "";
  request->user_selection = CodepointSpan::New();
  request->user_selection->start_offset = 1;
  request->user_selection->end_offset = 2;
  bool infer_callback_done = false;
  text_classifier->SuggestSelection(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done,
             CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 1);
            EXPECT_EQ(suggested_span->end_offset, 2);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion for a complex string.
TEST(TextClassifierSelectionTest, ComplexString) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextSuggestSelectionRequestPtr request = TextSuggestSelectionRequest::New();
  request->text = kTextClassifierTestInput;
  request->user_selection = CodepointSpan::New();
  request->user_selection->start_offset = 25;
  request->user_selection->end_offset = 26;
  bool infer_callback_done = false;
  text_classifier->SuggestSelection(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done,
             CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 21);
            EXPECT_EQ(suggested_span->end_offset, 38);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion with wrong inputs.
// In this situation, text classifier will return the input span.
TEST(TextClassifierSelectionTest, WrongInput) {
  MachineLearningServicePtr ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      mojo::MakeRequest(&ml_service).PassMessagePipe());

  TextClassifierPtr text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      mojo::MakeRequest(&text_classifier),
      base::Bind(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  TextSuggestSelectionRequestPtr request = TextSuggestSelectionRequest::New();
  request->text = kTextClassifierTestInput;
  request->user_selection = CodepointSpan::New();
  request->user_selection->start_offset = 30;
  request->user_selection->end_offset = 26;
  bool infer_callback_done = false;
  text_classifier->SuggestSelection(
      std::move(request),
      base::Bind(
          [](bool* infer_callback_done,
             CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 30);
            EXPECT_EQ(suggested_span->end_offset, 26);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

}  // namespace
}  // namespace ml
