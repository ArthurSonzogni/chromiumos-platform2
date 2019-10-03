// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/model_metadata.h"

namespace ml {

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::ModelId;

// TODO(crbug.com/990619): Remove this once clients migrate to
// |LoadBuiltinModel|.
std::map<ModelId, ModelMetadata> GetModelMetadata() {
  return {
    {
      ModelId::TEST_MODEL, {
        ModelId::TEST_MODEL,
        "mlservice-model-test_add-20180914.tflite",
        {{"x", 1}, {"y", 2}},
        {{"z", 0}},
        "TestModel",
      },
    },
    {
      ModelId::SMART_DIM_20181115, {
        ModelId::SMART_DIM_20181115,
        "mlservice-model-smart_dim-20181115.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "SmartDimModel",
      },
    },
    {
      ModelId::SMART_DIM_20190221, {
        ModelId::SMART_DIM_20190221,
        "mlservice-model-smart_dim-20190221.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "SmartDimModel",
      },
    },
    {
      ModelId::SMART_DIM_20190521, {
        ModelId::SMART_DIM_20190521,
        "mlservice-model-smart_dim-20190521-v3.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "SmartDimModel",
      },
    },
    {
      ModelId::TOP_CAT_20190722, {
        ModelId::TOP_CAT_20190722,
        "mlservice-model-top_cat-20190722.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "TopCatModel",
      },
    },
    {
      ModelId::SEARCH_RANKER_20190923, {
        ModelId::SEARCH_RANKER_20190923,
        "mlservice-model-search_ranker-20190923.tflite",
        {{"input", 7}},
        {{"output", 8}},
        "SearchRankerModel",
      },
    },
  };
}

std::map<BuiltinModelId, BuiltinModelMetadata> GetBuiltinModelMetadata() {
  return {
    {
      BuiltinModelId::TEST_MODEL, {
        BuiltinModelId::TEST_MODEL,
        "mlservice-model-test_add-20180914.tflite",
        {{"x", 1}, {"y", 2}},
        {{"z", 0}},
        "TestModel",
      },
    },
    {
      BuiltinModelId::SMART_DIM_20181115, {
        BuiltinModelId::SMART_DIM_20181115,
        "mlservice-model-smart_dim-20181115.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "SmartDimModel",
      },
    },
    {
      BuiltinModelId::SMART_DIM_20190221, {
        BuiltinModelId::SMART_DIM_20190221,
        "mlservice-model-smart_dim-20190221.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "SmartDimModel",
      },
    },
    {
      BuiltinModelId::SMART_DIM_20190521, {
        BuiltinModelId::SMART_DIM_20190521,
        "mlservice-model-smart_dim-20190521-v3.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "SmartDimModel",
      },
    },
    {
      BuiltinModelId::TOP_CAT_20190722, {
        BuiltinModelId::TOP_CAT_20190722,
        "mlservice-model-top_cat-20190722.tflite",
        {{"input", 3}},
        {{"output", 4}},
        "TopCatModel",
      },
    },
    {
      BuiltinModelId::SEARCH_RANKER_20190923, {
        BuiltinModelId::SEARCH_RANKER_20190923,
        "mlservice-model-search_ranker-20190923.tflite",
        {{"input", 7}},
        {{"output", 8}},
        "SearchRankerModel",
      },
    },
  };
}

}  // namespace ml
