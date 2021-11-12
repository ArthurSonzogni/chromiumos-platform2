// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/containers/flat_map.h>
#include <base/files/file_util.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "ml/document_scanner_library.h"
#include "ml/grammar_library.h"
#include "ml/grammar_proto_mojom_conversion.h"
#include "ml/handwriting.h"
#include "ml/handwriting_proto_mojom_conversion.h"
#include "ml/machine_learning_service_impl.h"
#include "ml/mojom/document_scanner.mojom.h"
#include "ml/mojom/grammar_checker.mojom.h"
#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/handwriting_recognizer.mojom.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/mojom/shared_memory.mojom.h"
#include "ml/mojom/soda.mojom.h"
#include "ml/mojom/text_classifier.mojom.h"
#include "ml/mojom/text_suggester.mojom.h"
#include "ml/mojom/web_platform_handwriting.mojom.h"
#include "ml/process.h"
#include "ml/tensor_view.h"
#include "ml/test_utils.h"
#include "ml/text_suggester_proto_mojom_conversion.h"
#include "ml/text_suggestions.h"

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

constexpr double kSmartDim20210201TestInput[] = {
    0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0,
};

// Points that are used to generate a stroke for handwriting.
constexpr float kHandwritingTestPoints[23][2] = {
    {1.928, 0.827}, {1.828, 0.826}, {1.73, 0.858},  {1.667, 0.901},
    {1.617, 0.955}, {1.567, 1.043}, {1.548, 1.148}, {1.569, 1.26},
    {1.597, 1.338}, {1.641, 1.408}, {1.688, 1.463}, {1.783, 1.473},
    {1.853, 1.418}, {1.897, 1.362}, {1.938, 1.278}, {1.968, 1.204},
    {1.999, 1.112}, {2.003, 1.004}, {1.984, 0.905}, {1.988, 1.043},
    {1.98, 1.178},  {1.976, 1.303}, {1.984, 1.415},
};

// A fake 16x16 black jpg image.
constexpr uint8_t kFakeJpgData[] = {
    255, 216, 255, 224, 0,   16,  74,  70,  73,  70,  0,   1,   1,   0,   0,
    1,   0,   1,   0,   0,   255, 219, 0,   67,  0,   2,   1,   1,   1,   1,
    1,   2,   1,   1,   1,   2,   2,   2,   2,   2,   4,   3,   2,   2,   2,
    2,   5,   4,   4,   3,   4,   6,   5,   6,   6,   6,   5,   6,   6,   6,
    7,   9,   8,   6,   7,   9,   7,   6,   6,   8,   11,  8,   9,   10,  10,
    10,  10,  10,  6,   8,   11,  12,  11,  10,  12,  9,   10,  10,  10,  255,
    219, 0,   67,  1,   2,   2,   2,   2,   2,   2,   5,   3,   3,   5,   10,
    7,   6,   7,   10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
    10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
    10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,  10,
    10,  10,  10,  10,  10,  10,  10,  10,  255, 192, 0,   17,  8,   0,   16,
    0,   16,  3,   1,   34,  0,   2,   17,  1,   3,   17,  1,   255, 196, 0,
    31,  0,   0,   1,   5,   1,   1,   1,   1,   1,   1,   0,   0,   0,   0,
    0,   0,   0,   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,
    255, 196, 0,   181, 16,  0,   2,   1,   3,   3,   2,   4,   3,   5,   5,
    4,   4,   0,   0,   1,   125, 1,   2,   3,   0,   4,   17,  5,   18,  33,
    49,  65,  6,   19,  81,  97,  7,   34,  113, 20,  50,  129, 145, 161, 8,
    35,  66,  177, 193, 21,  82,  209, 240, 36,  51,  98,  114, 130, 9,   10,
    22,  23,  24,  25,  26,  37,  38,  39,  40,  41,  42,  52,  53,  54,  55,
    56,  57,  58,  67,  68,  69,  70,  71,  72,  73,  74,  83,  84,  85,  86,
    87,  88,  89,  90,  99,  100, 101, 102, 103, 104, 105, 106, 115, 116, 117,
    118, 119, 120, 121, 122, 131, 132, 133, 134, 135, 136, 137, 138, 146, 147,
    148, 149, 150, 151, 152, 153, 154, 162, 163, 164, 165, 166, 167, 168, 169,
    170, 178, 179, 180, 181, 182, 183, 184, 185, 186, 194, 195, 196, 197, 198,
    199, 200, 201, 202, 210, 211, 212, 213, 214, 215, 216, 217, 218, 225, 226,
    227, 228, 229, 230, 231, 232, 233, 234, 241, 242, 243, 244, 245, 246, 247,
    248, 249, 250, 255, 196, 0,   31,  1,   0,   3,   1,   1,   1,   1,   1,
    1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   1,   2,   3,   4,   5,
    6,   7,   8,   9,   10,  11,  255, 196, 0,   181, 17,  0,   2,   1,   2,
    4,   4,   3,   4,   7,   5,   4,   4,   0,   1,   2,   119, 0,   1,   2,
    3,   17,  4,   5,   33,  49,  6,   18,  65,  81,  7,   97,  113, 19,  34,
    50,  129, 8,   20,  66,  145, 161, 177, 193, 9,   35,  51,  82,  240, 21,
    98,  114, 209, 10,  22,  36,  52,  225, 37,  241, 23,  24,  25,  26,  38,
    39,  40,  41,  42,  53,  54,  55,  56,  57,  58,  67,  68,  69,  70,  71,
    72,  73,  74,  83,  84,  85,  86,  87,  88,  89,  90,  99,  100, 101, 102,
    103, 104, 105, 106, 115, 116, 117, 118, 119, 120, 121, 122, 130, 131, 132,
    133, 134, 135, 136, 137, 138, 146, 147, 148, 149, 150, 151, 152, 153, 154,
    162, 163, 164, 165, 166, 167, 168, 169, 170, 178, 179, 180, 181, 182, 183,
    184, 185, 186, 194, 195, 196, 197, 198, 199, 200, 201, 202, 210, 211, 212,
    213, 214, 215, 216, 217, 218, 226, 227, 228, 229, 230, 231, 232, 233, 234,
    242, 243, 244, 245, 246, 247, 248, 249, 250, 255, 218, 0,   12,  3,   1,
    0,   2,   17,  3,   17,  0,   63,  0,   254, 127, 232, 162, 138, 0,   255,
    217};

// The words "unknownword" and "a.bcd" should not be detected by the new
// vocabulary based dictionary annotator.
constexpr char kTextClassifierTestInput[] =
    "user.name@gmail.com. 123 George Street. unfathomable. 12pm. 350°F. "
    "unknownword. a.bcd";

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpec;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::CodepointSpan;
using ::chromeos::machine_learning::mojom::CodepointSpanPtr;
using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::DetectCornersResultPtr;
using ::chromeos::machine_learning::mojom::DocumentScanner;
using ::chromeos::machine_learning::mojom::DocumentScannerResultStatus;
using ::chromeos::machine_learning::mojom::DoPostProcessingResultPtr;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpec;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpecPtr;
using ::chromeos::machine_learning::mojom::GrammarChecker;
using ::chromeos::machine_learning::mojom::GrammarCheckerResult;
using ::chromeos::machine_learning::mojom::GrammarCheckerResultPtr;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::HandwritingRecognitionQuery;
using ::chromeos::machine_learning::mojom::HandwritingRecognitionQueryPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizer;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerResult;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerResultPtr;
using ::chromeos::machine_learning::mojom::HandwritingRecognizerSpec;
using ::chromeos::machine_learning::mojom::LoadHandwritingModelResult;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningService;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::MultiWordExperimentGroup;
using ::chromeos::machine_learning::mojom::SodaClient;
using ::chromeos::machine_learning::mojom::SodaConfig;
using ::chromeos::machine_learning::mojom::SodaRecognizer;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationPtr;
using ::chromeos::machine_learning::mojom::TextAnnotationRequest;
using ::chromeos::machine_learning::mojom::TextAnnotationRequestPtr;
using ::chromeos::machine_learning::mojom::TextClassifier;
using ::chromeos::machine_learning::mojom::TextLanguagePtr;
using ::chromeos::machine_learning::mojom::TextSuggester;
using ::chromeos::machine_learning::mojom::TextSuggesterQuery;
using ::chromeos::machine_learning::mojom::TextSuggesterQueryPtr;
using ::chromeos::machine_learning::mojom::TextSuggesterResult;
using ::chromeos::machine_learning::mojom::TextSuggesterResultPtr;
using ::chromeos::machine_learning::mojom::TextSuggesterSpec;
using ::chromeos::machine_learning::mojom::TextSuggestionMode;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequest;
using ::chromeos::machine_learning::mojom::TextSuggestSelectionRequestPtr;

using ::chromeos::machine_learning::mojom::NextWordCompletionCandidate;
using ::chromeos::machine_learning::mojom::NextWordCompletionCandidatePtr;

using ::gfx::mojom::PointF;
using ::gfx::mojom::PointFPtr;

using ::mojo_base::mojom::ReadOnlySharedMemoryRegion;
using ::mojo_base::mojom::ReadOnlySharedMemoryRegionPtr;

using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::StrictMock;

// A version of MachineLearningServiceImpl that loads from the testing model
// directory.
class MachineLearningServiceImplForTesting : public MachineLearningServiceImpl {
 public:
  // Pass an empty callback and use the testing model directory.
  explicit MachineLearningServiceImplForTesting(
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::MachineLearningService> receiver)
      : MachineLearningServiceImpl(
            std::move(receiver), base::OnceClosure(), GetTestModelDir()) {}
};

// A simple SODA client for testing.
class MockSodaClientImpl
    : public chromeos::machine_learning::mojom::SodaClient {
 public:
  MOCK_METHOD(void, OnStop, (), (override));
  MOCK_METHOD(void, OnStart, (), (override));
  MOCK_METHOD(
      void,
      OnSpeechRecognizerEvent,
      (chromeos::machine_learning::mojom::SpeechRecognizerEventPtr event),
      (override));
};

// Loads builtin model specified by `model_id`, binding the impl to `model`.
// Returns true on success.
bool LoadBuiltinModelForTesting(
    const mojo::Remote<MachineLearningService>& ml_service,
    BuiltinModelId model_id,
    mojo::Remote<Model>* model) {
  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = model_id;

  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), model->BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  return model_callback_done;
}

// Loads flatbuffer model specified by `spec`, binding the impl to `model`.
// Returns true on success.
bool LoadFlatBufferModelForTesting(
    const mojo::Remote<MachineLearningService>& ml_service,
    FlatBufferModelSpecPtr spec,
    mojo::Remote<Model>* model) {
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), model->BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  return model_callback_done;
}

// Creates graph executor of `model`, binding the impl to `graph_executor`.
// Returns true on success.
bool CreateGraphExecutorForTesting(
    const mojo::Remote<Model>& model,
    mojo::Remote<GraphExecutor>* graph_executor) {
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      graph_executor->BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* ge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *ge_callback_done = true;
          },
          &ge_callback_done));
  base::RunLoop().RunUntilIdle();
  return ge_callback_done;
}

// Checks that `result` is OK and that `outputs` contains a tensor matching
// `expected_shape` and `expected_value`. Sets `infer_callback_done` to true so
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

// Tests that Clone() connects to a working impl.
TEST(MachineLearningServiceImplTest, Clone) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Call Clone to bind another MachineLearningService.
  mojo::Remote<MachineLearningService> ml_service_2;
  ml_service->Clone(ml_service_2.BindNewPipeAndPassReceiver());

  // Verify that the new MachineLearningService works with a simple call:
  // Loading the TEST_MODEL.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TEST_MODEL;
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service_2->LoadBuiltinModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(model_callback_done);
  EXPECT_TRUE(model.is_bound());
}

TEST(MachineLearningServiceImplTest, TestBadModel) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Set up model spec to specify an invalid model.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::UNSUPPORTED_UNKNOWN;

  // Load model.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = "bad model string";
  spec->inputs["x"] = 1;
  spec->inputs["y"] = 2;
  spec->outputs["z"] = 0;
  spec->metrics_model_name = "TestModel";

  // Load model from an empty model string.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadFlatBufferModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Leave loading model and creating graph executor inline here to demonstrate
  // the usage details.
  // Set up model spec.
  BuiltinModelSpecPtr spec = BuiltinModelSpec::New();
  spec->id = BuiltinModelId::TEST_MODEL;

  // Load model.
  mojo::Remote<Model> model;
  bool model_callback_done = false;
  ml_service->LoadBuiltinModel(
      std::move(spec), model.BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  bool ge_callback_done = false;
  model->CreateGraphExecutor(
      graph_executor.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         0.75, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests loading TEST_MODEL through the downloaded model api.
TEST(MachineLearningServiceImplTest, TestModelString) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

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
  mojo::Remote<Model> model;
  ASSERT_TRUE(
      LoadFlatBufferModelForTesting(ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
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
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         0.75, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20181115) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20181115) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Load model.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20181115, &model));
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, std::size(kSmartDim20181115TestInput)},
                   std::vector<double>(std::begin(kSmartDim20181115TestInput),
                                       std::end(kSmartDim20181115TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         -3.36311, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190221) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20190221) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20190221, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, std::size(kSmartDim20190221TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190221TestInput),
                                       std::end(kSmartDim20190221TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         -0.900591, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20190521) model file loads correctly and produces
// the expected inference result.
TEST(BuiltinModelInferenceTest, SmartDim20190521) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SMART_DIM_20190521, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, std::size(kSmartDim20190521TestInput)},
                   std::vector<double>(std::begin(kSmartDim20190521TestInput),
                                       std::end(kSmartDim20190521TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         0.66962254, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Search Ranker (20190923) model file loads correctly and
// produces the expected inference result.
TEST(BuiltinModelInferenceTest, SearchRanker20190923) {
  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Load model and create graph executor.
  mojo::Remote<Model> model;
  ASSERT_TRUE(LoadBuiltinModelForTesting(
      ml_service, BuiltinModelId::SEARCH_RANKER_20190923, &model));
  ASSERT_TRUE(model.is_bound());

  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace("input", NewTensor<double>(
                              {1, std::size(kSearchRanker20190923TestInput)},
                              std::vector<double>(
                                  std::begin(kSearchRanker20190923TestInput),
                                  std::end(kSearchRanker20190923TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         0.658488, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20200206) model file loads correctly and
// produces the expected inference result.
TEST(DownloadableModelInferenceTest, SmartDim20200206) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

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
  mojo::Remote<Model> model;
  ASSERT_TRUE(
      LoadFlatBufferModelForTesting(ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, std::size(kSmartDim20200206TestInput)},
                   std::vector<double>(std::begin(kSmartDim20200206TestInput),
                                       std::end(kSmartDim20200206TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         -1.07195, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the Smart Dim (20210201) model file loads correctly and
// produces the expected inference result.
TEST(DownloadableModelInferenceTest, SmartDim20210201) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Load SmartDim model into string.
  std::string model_string;
  ASSERT_TRUE(base::ReadFileToString(
      base::FilePath(GetTestModelDir() +
                     "mlservice-model-smart_dim-20210201-downloadable.tflite"),
      &model_string));

  FlatBufferModelSpecPtr spec = FlatBufferModelSpec::New();
  spec->model_string = std::move(model_string);
  spec->inputs["input"] = 0;
  spec->outputs["output"] = 20;
  spec->metrics_model_name = "SmartDimModel_20210201";

  // Load model.
  mojo::Remote<Model> model;
  ASSERT_TRUE(
      LoadFlatBufferModelForTesting(ml_service, std::move(spec), &model));
  ASSERT_NE(model.get(), nullptr);
  ASSERT_TRUE(model.is_bound());

  // Get graph executor.
  mojo::Remote<GraphExecutor> graph_executor;
  ASSERT_TRUE(CreateGraphExecutorForTesting(model, &graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Construct input.
  base::flat_map<std::string, TensorPtr> inputs;
  inputs.emplace(
      "input", NewTensor<double>(
                   {1, std::size(kSmartDim20210201TestInput)},
                   std::vector<double>(std::begin(kSmartDim20210201TestInput),
                                       std::end(kSmartDim20210201TestInput))));
  std::vector<std::string> outputs({"output"});
  std::vector<int64_t> expected_shape{1L, 1L};

  // Perform inference.
  bool infer_callback_done = false;
  graph_executor->Execute(std::move(inputs), std::move(outputs),
                          base::BindOnce(&CheckOutputTensor, expected_shape,
                                         0.76872265, &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests loading text classifier only.
TEST(LoadTextClassifierTest, NoInference) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
  text_classifier->Annotate(std::move(request),
                            base::BindOnce(
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
      base::BindOnce(
          [](bool* infer_callback_done,
             std::vector<TextAnnotationPtr> annotations) {
            *infer_callback_done = true;
            EXPECT_EQ(annotations.size(), 5);
            EXPECT_EQ(annotations[0]->start_offset, 0);
            EXPECT_EQ(annotations[0]->end_offset, 19);
            ASSERT_GE(annotations[0]->entities.size(), 1);
            EXPECT_EQ(annotations[0]->entities[0]->name, "email");
            EXPECT_EQ(annotations[0]->entities[0]->data->get_string_value(),
                      "user.name@gmail.com");
            EXPECT_EQ(annotations[1]->start_offset, 21);
            EXPECT_EQ(annotations[1]->end_offset, 38);
            ASSERT_GE(annotations[1]->entities.size(), 1);
            EXPECT_EQ(annotations[1]->entities[0]->name, "address");
            EXPECT_EQ(annotations[1]->entities[0]->data->get_string_value(),
                      "123 George Street");
            EXPECT_EQ(annotations[2]->start_offset, 40);
            EXPECT_EQ(annotations[2]->end_offset, 52);
            ASSERT_GE(annotations[2]->entities.size(), 1);
            EXPECT_EQ(annotations[2]->entities[0]->name, "dictionary");
            EXPECT_EQ(annotations[2]->entities[0]->data->get_string_value(),
                      "unfathomable");
            EXPECT_EQ(annotations[3]->start_offset, 54);
            EXPECT_EQ(annotations[3]->end_offset, 59);
            ASSERT_GE(annotations[3]->entities.size(), 1);
            EXPECT_EQ(annotations[3]->entities[0]->name, "datetime");
            EXPECT_EQ(annotations[3]->entities[0]->data->get_string_value(),
                      "12pm.");
            EXPECT_EQ(annotations[4]->start_offset, 60);
            EXPECT_EQ(annotations[4]->end_offset, 65);
            ASSERT_GE(annotations[4]->entities.size(), 1);
            EXPECT_EQ(annotations[4]->entities[0]->name, "unit");
            EXPECT_EQ(annotations[4]->entities[0]->data->get_string_value(),
                      "350°F");
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier selection suggestion for an empty string.
// In this situation, text classifier will return the input span.
TEST(TextClassifierSelectionTest, EmptyString) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
      base::BindOnce(
          [](bool* infer_callback_done, CodepointSpanPtr suggested_span) {
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
      base::BindOnce(
          [](bool* infer_callback_done, CodepointSpanPtr suggested_span) {
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
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
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
      base::BindOnce(
          [](bool* infer_callback_done, CodepointSpanPtr suggested_span) {
            *infer_callback_done = true;
            EXPECT_EQ(suggested_span->start_offset, 30);
            EXPECT_EQ(suggested_span->end_offset, 26);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier language identification with some valid inputs.
TEST(TextClassifierLangIdTest, ValidInput) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  bool infer_callback_done = false;
  text_classifier->FindLanguages(
      "Bonjour",
      base::BindOnce(
          [](bool* infer_callback_done, std::vector<TextLanguagePtr> result) {
            *infer_callback_done = true;
            ASSERT_GT(result.size(), 0);
            EXPECT_EQ(result[0]->locale, "fr");
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests text classifier language identification with empty input.
// Empty input should produce empty result.
TEST(TextClassifierLangIdTest, EmptyInput) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  mojo::Remote<TextClassifier> text_classifier;
  bool model_callback_done = false;
  ml_service->LoadTextClassifier(
      text_classifier.BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            EXPECT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);

  bool infer_callback_done = false;
  text_classifier->FindLanguages(
      "",
      base::BindOnce(
          [](bool* infer_callback_done, std::vector<TextLanguagePtr> result) {
            *infer_callback_done = true;
            EXPECT_EQ(result.size(), 0);
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Test class for HandwritingRecognizerTest.
class HandwritingRecognizerTest : public testing::Test {
 protected:
  void SetUp() override {
    // Nothing to test on an unsupported platform.
    if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
      return;
    }
    // Set ml_service.
    ml_service_impl_ = std::make_unique<MachineLearningServiceImplForTesting>(
        ml_service_.BindNewPipeAndPassReceiver());

    // Set default request.
    request_.set_max_num_results(1);
    auto& stroke = *request_.mutable_ink()->add_strokes();
    for (int i = 0; i < 23; ++i) {
      auto& point = *stroke.add_points();
      point.set_x(kHandwritingTestPoints[i][0]);
      point.set_y(kHandwritingTestPoints[i][1]);
    }
  }

  // recognizer_ should be loaded successfully for this `language`.
  // Using new API (LoadHandwritingModelWithSpec) if use_load_handwriting_model
  // is true.
  void LoadRecognizerWithLanguage(
      const std::string& langauge,
      const bool use_load_handwriting_model = false) {
    bool model_callback_done = false;
    if (use_load_handwriting_model) {
      ml_service_->LoadHandwritingModel(
          HandwritingRecognizerSpec::New(langauge),
          recognizer_.BindNewPipeAndPassReceiver(),
          base::BindOnce(
              [](bool* model_callback_done,
                 const LoadHandwritingModelResult result) {
                ASSERT_EQ(result, LoadHandwritingModelResult::OK);
                *model_callback_done = true;
              },
              &model_callback_done));
    } else {
      ml_service_->LoadHandwritingModelWithSpec(
          HandwritingRecognizerSpec::New(langauge),
          recognizer_.BindNewPipeAndPassReceiver(),
          base::BindOnce(
              [](bool* model_callback_done, const LoadModelResult result) {
                ASSERT_EQ(result, LoadModelResult::OK);
                *model_callback_done = true;
              },
              &model_callback_done));
    }
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(model_callback_done);
    ASSERT_TRUE(recognizer_.is_bound());
  }

  // Recognizing on the request_ should produce expected text and score.
  void ExpectRecognizeResult(const std::string& text, const float score) {
    // Perform inference.
    bool infer_callback_done = false;
    recognizer_->Recognize(
        HandwritingRecognitionQueryFromProtoForTesting(request_),
        base::BindOnce(
            [](bool* infer_callback_done, const std::string& text,
               const float score, const HandwritingRecognizerResultPtr result) {
              // Check that the inference succeeded and gives
              // the expected number of outputs.
              EXPECT_EQ(result->status,
                        HandwritingRecognizerResult::Status::OK);
              ASSERT_EQ(result->candidates.size(), 1);
              EXPECT_EQ(result->candidates.at(0)->text, text);
              EXPECT_NEAR(result->candidates.at(0)->score, score, 1e-4);
              *infer_callback_done = true;
            },
            &infer_callback_done, text, score));
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(infer_callback_done);
  }

  std::unique_ptr<MachineLearningServiceImplForTesting> ml_service_impl_;
  mojo::Remote<MachineLearningService> ml_service_;
  mojo::Remote<HandwritingRecognizer> recognizer_;
  chrome_knowledge::HandwritingRecognizerRequest request_;
};

// Tests that the HandwritingRecognizer recognition returns expected scores.
TEST_F(HandwritingRecognizerTest, GetExpectedScores) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en");

  // Run Recognition on the default request_.
  ExpectRecognizeResult("a", 0.50640869f);

  // Modify the request_ by setting fake time.
  for (int i = 0; i < 23; ++i) {
    request_.mutable_ink()->mutable_strokes(0)->mutable_points(i)->set_t(i * i *
                                                                         100);
  }
  ExpectRecognizeResult("a", 0.5121f);
}

// Tests that the LoadHandwritingModel also perform as expected.
TEST_F(HandwritingRecognizerTest, LoadHandwritingModel) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en", true);

  // Clear the ink inside request.
  request_.clear_ink();

  // Perform inference should return an error.
  bool infer_callback_done = false;
  recognizer_->Recognize(
      HandwritingRecognitionQueryFromProtoForTesting(request_),
      base::BindOnce(
          [](bool* infer_callback_done,
             const HandwritingRecognizerResultPtr result) {
            // Check that the inference failed.
            EXPECT_EQ(result->status,
                      HandwritingRecognizerResult::Status::ERROR);
            EXPECT_EQ(result->candidates.size(), 0);
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests that the HandwritingRecognizer Recognition should fail on empty ink.
TEST_F(HandwritingRecognizerTest, FailOnEmptyInk) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en");

  // Clear the ink inside request.
  request_.clear_ink();

  // Perform inference should return an error.
  bool infer_callback_done = false;
  recognizer_->Recognize(
      HandwritingRecognitionQueryFromProtoForTesting(request_),
      base::BindOnce(
          [](bool* infer_callback_done,
             const HandwritingRecognizerResultPtr result) {
            // Check that the inference failed.
            EXPECT_EQ(result->status,
                      HandwritingRecognizerResult::Status::ERROR);
            EXPECT_EQ(result->candidates.size(), 0);
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

MATCHER_P(StructPtrEq, n, "") {
  return n.get().Equals(arg);
}

// Test class for WebPlatformHandwritingRecognizerTest.
class WebPlatformHandwritingRecognizerTest : public testing::Test {
 protected:
  void SetUp() override {
    // Nothing to test on an unsupported platform.
    if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
      return;
    }
    // Sets the mlservice to single process mode for testing here.
    Process::GetInstance()->SetTypeForTesting(
        Process::Type::kSingleProcessForTest);

    // Set ml_service.
    ml_service_impl_ = std::make_unique<MachineLearningServiceImplForTesting>(
        ml_service_.BindNewPipeAndPassReceiver());

    // Set default inputs.
    hints_ = chromeos::machine_learning::web_platform::mojom::HandwritingHints::
        New();
    hints_->alternatives = 1u;
    auto stroke = chromeos::machine_learning::web_platform::mojom::
        HandwritingStroke::New();
    for (int i = 0; i < 23; ++i) {
      auto point = chromeos::machine_learning::web_platform::mojom::
          HandwritingPoint::New();
      auto location = gfx::mojom::PointF::New();
      location->x = kHandwritingTestPoints[i][0];
      location->y = kHandwritingTestPoints[i][1];
      point->location = std::move(location);
      stroke->points.push_back(std::move(point));
    }
    strokes_.push_back(std::move(stroke));
  }

  // recognizer_ should be loaded successfully for this `language`.
  void LoadRecognizerWithLanguage(const std::string& language) {
    bool model_callback_done = false;
    auto constraint = chromeos::machine_learning::web_platform::mojom::
        HandwritingModelConstraint::New();
    constraint->languages.push_back(language);
    ml_service_->LoadWebPlatformHandwritingModel(
        std::move(constraint), recognizer_.BindNewPipeAndPassReceiver(),
        base::BindOnce(
            [](bool* model_callback_done,
               const LoadHandwritingModelResult result) {
              ASSERT_EQ(result, LoadHandwritingModelResult::OK);
              *model_callback_done = true;
            },
            &model_callback_done));
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(model_callback_done);
    ASSERT_TRUE(recognizer_.is_bound());
  }

  // Recognizing on the strokes_ and hints_, should produce expected text and
  // score.
  void ExpectRecognizeResult(const std::string& text) {
    // Perform inference.
    bool infer_callback_done = false;
    // Make a copy of strokes and hints to avoid them being cleared after
    recognizer_->GetPrediction(
        GetDefaultStrokes(), hints_.Clone(),
        base::BindOnce(
            [](bool* infer_callback_done, const std::string& text,
               base::Optional<
                   std::vector<chromeos::machine_learning::web_platform::mojom::
                                   HandwritingPredictionPtr>> predictions) {
              // Check that the inference succeeded and gives
              // the expected number of outputs.
              ASSERT_TRUE(predictions.has_value());
              ASSERT_EQ(predictions->size(), 1u);
              EXPECT_EQ(predictions->at(0)->text, text);
              *infer_callback_done = true;
            },
            &infer_callback_done, text));
    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(infer_callback_done);
  }

  //  Make a copy of strokes_ to avoid them being cleared after
  // `GetPrediction()`.
  std::vector<
      chromeos::machine_learning::web_platform::mojom::HandwritingStrokePtr>
  GetDefaultStrokes() {
    std::vector<
        chromeos::machine_learning::web_platform::mojom::HandwritingStrokePtr>
        strokes_clone;
    for (const auto& stroke : strokes_) {
      strokes_clone.push_back(stroke.Clone());
    }
    return strokes_clone;
  }

  std::unique_ptr<MachineLearningServiceImplForTesting> ml_service_impl_;
  mojo::Remote<MachineLearningService> ml_service_;
  mojo::Remote<
      chromeos::machine_learning::web_platform::mojom::HandwritingRecognizer>
      recognizer_;
  std::vector<
      chromeos::machine_learning::web_platform::mojom::HandwritingStrokePtr>
      strokes_;
  chromeos::machine_learning::web_platform::mojom::HandwritingHintsPtr hints_;
};

// Tests that the web_platform::mojom::HandwritingRecognizer::GetPrediction
// returns expected scores.
TEST_F(WebPlatformHandwritingRecognizerTest, GetExpectedRecognizedText) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en");

  // Run Recognition on the default strokes_.
  ExpectRecognizeResult("a");

  // Modify the strokes_ by setting fake time.
  ASSERT_EQ(strokes_.size(), 1u);
  ASSERT_EQ(strokes_[0]->points.size(), 23u);
  for (int i = 0; i < 23; ++i) {
    strokes_[0]->points[i]->t = base::TimeDelta::FromMilliseconds(i * i * 100);
  }
  ExpectRecognizeResult("a");
}

TEST_F(WebPlatformHandwritingRecognizerTest, FailOnEmptyStrokes) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Load Recognizer successfully.
  LoadRecognizerWithLanguage("en");

  // Perform inference should return an error.
  bool infer_callback_done = false;
  recognizer_->GetPrediction(
      {}, hints_.Clone(),
      base::BindOnce(
          [](bool* infer_callback_done,
             base::Optional<
                 std::vector<chromeos::machine_learning::web_platform::mojom::
                                 HandwritingPredictionPtr>> predictions) {
            // Check that the inference failed.
            EXPECT_FALSE(predictions.has_value());
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// Tests the SODA CrOS mojo callback for the fake implementation can return
// expected error string.
TEST(SODARecognizerTest, FakeImplMojoCallback) {
#ifdef USE_ONDEVICE_SPEECH
  return;
#else
  StrictMock<MockSodaClientImpl> soda_client_impl;
  mojo::Receiver<SodaClient> soda_client(&soda_client_impl);
  auto soda_config = SodaConfig::New();
  mojo::Remote<SodaRecognizer> soda_recognizer;

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  ml_service->LoadSpeechRecognizer(std::move(soda_config),
                                   soda_client.BindNewPipeAndPassRemote(),
                                   soda_recognizer.BindNewPipeAndPassReceiver(),
                                   base::BindOnce([](LoadModelResult) {}));
  chromeos::machine_learning::mojom::SpeechRecognizerEventPtr event =
      chromeos::machine_learning::mojom::SpeechRecognizerEvent::New();
  chromeos::machine_learning::mojom::FinalResultPtr final_result =
      chromeos::machine_learning::mojom::FinalResult::New();
  final_result->final_hypotheses.push_back(
      "On-device speech is not supported.");
  final_result->endpoint_reason =
      chromeos::machine_learning::mojom::EndpointReason::ENDPOINT_UNKNOWN;
  event->set_final_result(std::move(final_result));

  // TODO(robsc): Update this unittest to use regular Eq() once
  // https://chromium-review.googlesource.com/c/chromium/src/+/2456184 is
  // submitted.
  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->Start();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->AddAudio({});
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->MarkDone();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(soda_client_impl,
              OnSpeechRecognizerEvent(StructPtrEq(std::ref(event))))
      .Times(1);
  soda_recognizer->Stop();
  base::RunLoop().RunUntilIdle();
#endif
}

TEST(GrammarCheckerTest, LoadModelAndInference) {
  if (ml::GrammarLibrary::GetInstance()->GetStatus() ==
      ml::GrammarLibrary::Status::kNotSupported) {
    return;
  }

  mojo::Remote<MachineLearningService> ml_service;
  const MachineLearningServiceImplForTesting ml_service_impl(
      ml_service.BindNewPipeAndPassReceiver());

  // Load GrammarChecker.
  mojo::Remote<GrammarChecker> checker;
  bool model_callback_done = false;
  ml_service->LoadGrammarChecker(
      checker.BindNewPipeAndPassReceiver(),
      base::BindOnce(
          [](bool* model_callback_done, const LoadModelResult result) {
            ASSERT_EQ(result, LoadModelResult::OK);
            *model_callback_done = true;
          },
          &model_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(model_callback_done);
  ASSERT_TRUE(checker.is_bound());

  chrome_knowledge::GrammarCheckerRequest request;
  request.set_text("They is student.");
  request.set_language("en-US");

  bool infer_callback_done = false;
  checker->Check(
      GrammarCheckerQueryFromProtoForTesting(request),
      base::BindOnce(
          [](bool* infer_callback_done, const GrammarCheckerResultPtr result) {
            EXPECT_EQ(result->status, GrammarCheckerResult::Status::OK);
            ASSERT_GE(result->candidates.size(), 1);
            EXPECT_EQ(result->candidates.at(0)->text, "They are students.");

            ASSERT_EQ(result->candidates.at(0)->fragments.size(), 1);
            EXPECT_EQ(result->candidates.at(0)->fragments.at(0)->offset, 5);
            EXPECT_EQ(result->candidates.at(0)->fragments.at(0)->length, 10);
            EXPECT_EQ(result->candidates.at(0)->fragments.at(0)->replacement,
                      "are students");

            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

bool TextSuggesterNotSupportedOnDevice() {
  return ml::TextSuggestions::GetInstance()->GetStatus() ==
         ml::TextSuggestions::Status::kNotSupported;
}

class TextSuggesterTest : public ::testing::Test {
 public:
  void ConnectTextSuggester(MultiWordExperimentGroup experiment_group) {
    if (TextSuggesterNotSupportedOnDevice()) {
      return;
    }

    mojo::Remote<MachineLearningService> ml_service;
    const MachineLearningServiceImplForTesting ml_service_impl(
        ml_service.BindNewPipeAndPassReceiver());

    // Load TextSuggester.
    bool model_callback_done = false;
    ml_service->LoadTextSuggester(
        suggester_.BindNewPipeAndPassReceiver(),
        TextSuggesterSpec::New(experiment_group),
        base::BindOnce(
            [](bool* model_callback_done, const LoadModelResult result) {
              ASSERT_EQ(result, LoadModelResult::OK);
              *model_callback_done = true;
            },
            &model_callback_done));

    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(model_callback_done);
    ASSERT_TRUE(suggester_.is_bound());
  }

 protected:
  mojo::Remote<TextSuggester> suggester_;

  const float scoring_equality_delta_ = 0.0015f;
};

TEST_F(TextSuggesterTest, LoadModelAndGenerateCompletionCandidate) {
  if (TextSuggesterNotSupportedOnDevice()) {
    return;
  }

  ConnectTextSuggester(MultiWordExperimentGroup::kDefault);

  TextSuggesterQueryPtr query = TextSuggesterQuery::New();
  query->text = "how are y";
  query->suggestion_mode = TextSuggestionMode::kCompletion;

  NextWordCompletionCandidatePtr candidate_one =
      NextWordCompletionCandidate::New();
  candidate_one->text = "you";
  candidate_one->normalized_score = -1.0f;
  query->next_word_candidates.push_back(std::move(candidate_one));

  bool infer_callback_done = false;
  suggester_->Suggest(
      std::move(query),
      base::BindOnce(
          [](bool* infer_callback_done, float equality_delta,
             const TextSuggesterResultPtr result) {
            EXPECT_EQ(result->status, TextSuggesterResult::Status::OK);
            ASSERT_EQ(result->candidates.size(), 1);
            ASSERT_TRUE(result->candidates.at(0)->is_multi_word());
            EXPECT_EQ(result->candidates.at(0)->get_multi_word()->text,
                      "you doing");
            EXPECT_NEAR(
                result->candidates.at(0)->get_multi_word()->normalized_score,
                -0.680989f, equality_delta);
            *infer_callback_done = true;
          },
          &infer_callback_done, scoring_equality_delta_));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

TEST_F(TextSuggesterTest, LoadModelAndGeneratePredictionCandidate) {
  if (TextSuggesterNotSupportedOnDevice()) {
    return;
  }

  ConnectTextSuggester(MultiWordExperimentGroup::kDefault);

  TextSuggesterQueryPtr query = TextSuggesterQuery::New();
  query->text = "how are ";
  query->suggestion_mode = TextSuggestionMode::kPrediction;

  bool infer_callback_done = false;
  suggester_->Suggest(
      std::move(query),
      base::BindOnce(
          [](bool* infer_callback_done, float equality_delta,
             const TextSuggesterResultPtr result) {
            EXPECT_EQ(result->status, TextSuggesterResult::Status::OK);
            ASSERT_EQ(result->candidates.size(), 1);
            ASSERT_TRUE(result->candidates.at(0)->is_multi_word());
            EXPECT_EQ(result->candidates.at(0)->get_multi_word()->text,
                      "you doing");
            EXPECT_NEAR(
                result->candidates.at(0)->get_multi_word()->normalized_score,
                -0.8141749f, equality_delta);
            *infer_callback_done = true;
          },
          &infer_callback_done, scoring_equality_delta_));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

// The default experiment group should show a suggestion with the preceding
// text "how are" -> "how are you". The Gboard experiment group does not
// show such a suggestion. Let's make sure that the experiment group given
// to a TextSuggester instance is honoured, and does not show unexpected
// suggestions.
TEST_F(TextSuggesterTest,
       GboardExperimentGroupDoesNotTriggerDefaultSuggestions) {
  if (TextSuggesterNotSupportedOnDevice()) {
    return;
  }

  ConnectTextSuggester(MultiWordExperimentGroup::kGboard);

  TextSuggesterQueryPtr query = TextSuggesterQuery::New();
  query->text = "how are y";
  query->suggestion_mode = TextSuggestionMode::kCompletion;

  NextWordCompletionCandidatePtr candidate_one =
      NextWordCompletionCandidate::New();
  candidate_one->text = "you";
  candidate_one->normalized_score = -1.0f;
  query->next_word_candidates.push_back(std::move(candidate_one));

  bool infer_callback_done = false;
  suggester_->Suggest(
      std::move(query),
      base::BindOnce(
          [](bool* infer_callback_done, const TextSuggesterResultPtr result) {
            EXPECT_EQ(result->status, TextSuggesterResult::Status::OK);
            ASSERT_EQ(result->candidates.size(), 0);
            *infer_callback_done = true;
          },
          &infer_callback_done));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

TEST_F(TextSuggesterTest, GboardExperimentGroupTriggersExpectedSuggestions) {
  if (TextSuggesterNotSupportedOnDevice()) {
    return;
  }

  ConnectTextSuggester(MultiWordExperimentGroup::kGboard);

  TextSuggesterQueryPtr query = TextSuggesterQuery::New();
  query->text = "why ar";
  query->suggestion_mode = TextSuggestionMode::kCompletion;

  NextWordCompletionCandidatePtr candidate_one =
      NextWordCompletionCandidate::New();
  candidate_one->text = "aren\'t";
  candidate_one->normalized_score = -1.0f;
  query->next_word_candidates.push_back(std::move(candidate_one));

  bool infer_callback_done = false;
  suggester_->Suggest(
      std::move(query),
      base::BindOnce(
          [](bool* infer_callback_done, float equality_delta,
             const TextSuggesterResultPtr result) {
            EXPECT_EQ(result->status, TextSuggesterResult::Status::OK);
            ASSERT_EQ(result->candidates.size(), 1);
            ASSERT_TRUE(result->candidates.at(0)->is_multi_word());
            EXPECT_EQ(result->candidates.at(0)->get_multi_word()->text,
                      "aren\'t you");
            EXPECT_NEAR(
                result->candidates.at(0)->get_multi_word()->normalized_score,
                -0.13418171f, equality_delta);
            *infer_callback_done = true;
          },
          &infer_callback_done, scoring_equality_delta_));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

ReadOnlySharedMemoryRegionPtr ToSharedMemory(const std::vector<uint8_t>& data) {
  base::MappedReadOnlyRegion mapped_region =
      base::ReadOnlySharedMemoryRegion::Create(data.size());
  memcpy(mapped_region.mapping.memory(), data.data(), data.size());
  auto image = mojo_base::mojom::ReadOnlySharedMemoryRegion::New();
  image->buffer =
      mojo::WrapReadOnlySharedMemoryRegion(std::move(mapped_region.region));
  return image;
}

class DocumentScannerTest : public ::testing::Test {
 public:
  bool IsDocumentScannerSupported() {
    return ml::DocumentScannerLibrary::GetInstance()->IsSupported();
  }

  void ConnectDocumentScanner() {
    mojo::Remote<MachineLearningService> ml_service;
    const MachineLearningServiceImplForTesting ml_service_impl(
        ml_service.BindNewPipeAndPassReceiver());

    bool model_callback_done = false;
    ml_service->LoadDocumentScanner(
        scanner_.BindNewPipeAndPassReceiver(),
        base::BindOnce(
            [](bool* model_callback_done, const LoadModelResult result) {
              ASSERT_EQ(result, LoadModelResult::OK);
              *model_callback_done = true;
            },
            &model_callback_done));

    base::RunLoop().RunUntilIdle();
    ASSERT_TRUE(model_callback_done);
    ASSERT_TRUE(scanner_.is_bound());
  }

 protected:
  mojo::Remote<DocumentScanner> scanner_;
};

TEST_F(DocumentScannerTest, DetectFromNV12Image) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  if (!IsDocumentScannerSupported()) {
    return;
  }
  ConnectDocumentScanner();

  constexpr int kNv12ImageSize = 256 * 256 * 3 / 2;
  std::vector<uint8_t> fake_nv12_data(kNv12ImageSize, 0);

  bool infer_callback_done = false;
  scanner_->DetectCornersFromNV12Image(
      ToSharedMemory(std::move(fake_nv12_data)),
      base::BindOnce(
          [](bool* infer_callback_done, DetectCornersResultPtr result) {
            EXPECT_EQ(result->status, DocumentScannerResultStatus::OK);
            EXPECT_TRUE(result->corners.size() == 0 ||
                        result->corners.size() == 4);
            *infer_callback_done = true;
          },
          &infer_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

TEST_F(DocumentScannerTest, DetectFromJPEGImage) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  if (!IsDocumentScannerSupported()) {
    return;
  }
  ConnectDocumentScanner();

  size_t jpeg_size = sizeof(kFakeJpgData) / sizeof(kFakeJpgData[0]);
  std::vector<uint8_t> fake_jpeg_image(kFakeJpgData, kFakeJpgData + jpeg_size);

  bool infer_callback_done = false;
  scanner_->DetectCornersFromJPEGImage(
      ToSharedMemory(std::move(fake_jpeg_image)),
      base::BindOnce(
          [](bool* infer_callback_done, DetectCornersResultPtr result) {
            EXPECT_EQ(result->status, DocumentScannerResultStatus::OK);
            EXPECT_TRUE(result->corners.size() == 0 ||
                        result->corners.size() == 4);
            *infer_callback_done = true;
          },
          &infer_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

TEST_F(DocumentScannerTest, PostProcessing) {
  // Set the mlservice to single process mode for testing here.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  if (!IsDocumentScannerSupported()) {
    return;
  }
  ConnectDocumentScanner();

  size_t jpeg_size = sizeof(kFakeJpgData) / sizeof(kFakeJpgData[0]);
  std::vector<uint8_t> fake_jpeg_image(kFakeJpgData, kFakeJpgData + jpeg_size);

  auto to_pointf_ptr = [](float x, float y) -> PointFPtr {
    auto pt = PointF::New();
    pt->x = x;
    pt->y = y;
    return pt;
  };

  std::vector<PointFPtr> fake_corners;
  fake_corners.push_back(to_pointf_ptr(0.0, 0.0));
  fake_corners.push_back(to_pointf_ptr(0.0, 1.0));
  fake_corners.push_back(to_pointf_ptr(1.0, 1.0));
  fake_corners.push_back(to_pointf_ptr(1.0, 0.0));

  bool infer_callback_done = false;
  scanner_->DoPostProcessing(
      ToSharedMemory(std::move(fake_jpeg_image)), std::move(fake_corners),
      chromeos::machine_learning::mojom::Rotation::ROTATION_0,
      base::BindOnce(
          [](bool* infer_callback_done, DoPostProcessingResultPtr result) {
            EXPECT_EQ(result->status, DocumentScannerResultStatus::OK);
            EXPECT_GT(result->processed_jpeg_image.size(), 0);
            *infer_callback_done = true;
          },
          &infer_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(infer_callback_done);
}

}  // namespace
}  // namespace ml
