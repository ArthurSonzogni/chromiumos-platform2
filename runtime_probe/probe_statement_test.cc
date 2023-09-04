// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>

#include <base/functional/callback.h>
#include <base/json/json_reader.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_function.h"
#include "runtime_probe/probe_statement.h"

namespace runtime_probe {
namespace {

using ::testing::ByMove;
using ::testing::Return;

class MockProbeFunction : public ProbeFunction {
  using ProbeFunction::ProbeFunction;

 public:
  NAME_PROBE_FUNCTION("mock_function");
  MOCK_METHOD(DataType, EvalImpl, (), (const, override));
};

class ProbeStatementTest : public testing::Test {
 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
};

TEST_F(ProbeStatementTest, FromValue) {
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "memory": {}
    }
  })");

  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);
  EXPECT_NE(probe_statement, nullptr);
}

TEST_F(ProbeStatementTest, FromValueWithNonDictValue) {
  auto non_dict_value = base::JSONReader::Read("[]");

  auto probe_statement =
      ProbeStatement::FromValue("component", *non_dict_value);
  EXPECT_EQ(probe_statement, nullptr);
}

TEST_F(ProbeStatementTest, FromValueWithoutEval) {
  // No required field "eval".
  auto dict_value = base::JSONReader::Read("{}");

  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);
  EXPECT_EQ(probe_statement, nullptr);
}

TEST_F(ProbeStatementTest, FromValueWithInvalidFunction) {
  // The probe function is not defined.
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "invalid_function": {}
    }
  })");

  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);
  EXPECT_EQ(probe_statement, nullptr);
}

TEST_F(ProbeStatementTest, Eval) {
  // Set a valid probe function and mock it later.
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "memory": {}
    }
  })");
  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);

  auto ans = std::move(base::JSONReader::Read(R"([
    {
      "field": "value"
    }
  ])")
                           ->GetList());

  auto probe_function = std::make_unique<MockProbeFunction>();
  EXPECT_CALL(*probe_function, EvalImpl())
      .WillOnce(Return(ByMove(ans.Clone())));

  probe_statement->SetProbeFunctionForTesting(std::move(probe_function));

  base::test::TestFuture<ProbeFunction::DataType> future;
  probe_statement->Eval(future.GetCallback());
  EXPECT_EQ(future.Get(), ans);
}

TEST_F(ProbeStatementTest, EvalWithFilteredKeys) {
  // Set a valid probe function and mock it later.
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "memory": {}
    },
    "keys": ["field_1", "field_2"]
  })");
  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);

  auto eval_result = std::move(base::JSONReader::Read(R"([
    {
      "field_1": "value_1",
      "field_2": "value_2",
      "field_3": "value_3"
    }
  ])")
                                   ->GetList());

  auto probe_function = std::make_unique<MockProbeFunction>();
  EXPECT_CALL(*probe_function, EvalImpl())
      .WillOnce(Return(ByMove(std::move(eval_result))));

  probe_statement->SetProbeFunctionForTesting(std::move(probe_function));

  // Should only get fields defined in "keys".
  auto ans = std::move(base::JSONReader::Read(R"([
    {
      "field_1": "value_1",
      "field_2": "value_2"
    }
  ])")
                           ->GetList());
  base::test::TestFuture<ProbeFunction::DataType> future;
  probe_statement->Eval(future.GetCallback());
  EXPECT_EQ(future.Get(), ans);
}

TEST_F(ProbeStatementTest, EvalWithExpectValue) {
  // Set a valid probe function and mock it later.
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "memory": {}
    }
  })");
  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);

  auto eval_result = std::move(base::JSONReader::Read(R"([
    {
      "field_1": "value_1"
    },
    {
      "field_2": "value_2"
    }
  ])")
                                   ->GetList());

  auto probe_function = std::make_unique<MockProbeFunction>();
  EXPECT_CALL(*probe_function, EvalImpl())
      .WillOnce(Return(ByMove(std::move(eval_result))));

  auto expect_value = base::JSONReader::Read(R"([
    {
      "field_2": [true, "str"]
    }
  ])");

  probe_statement->SetProbeFunctionForTesting(std::move(probe_function));
  probe_statement->SetExpectForTesting(std::move(*expect_value));

  // Should only get results that pass the check.
  auto ans = std::move(base::JSONReader::Read(R"([
    {
      "field_2": "value_2"
    }
  ])")
                           ->GetList());
  base::test::TestFuture<ProbeFunction::DataType> future;
  probe_statement->Eval(future.GetCallback());
  EXPECT_EQ(future.Get(), ans);
}

TEST_F(ProbeStatementTest, GetInformation) {
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "memory": {}
    },
    "information": {
      "field": "value"
    }
  })");

  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);

  auto ans = base::JSONReader::Read(R"({
    "field": "value"
  })");
  auto res = probe_statement->GetInformation();
  EXPECT_EQ(res, ans);
}

TEST_F(ProbeStatementTest, GetInformationWithoutInformation) {
  auto dict_value = base::JSONReader::Read(R"({
    "eval": {
      "memory": {}
    }
  })");

  auto probe_statement = ProbeStatement::FromValue("component", *dict_value);

  auto res = probe_statement->GetInformation();
  EXPECT_EQ(res, std::nullopt);
}

}  // namespace
}  // namespace runtime_probe
