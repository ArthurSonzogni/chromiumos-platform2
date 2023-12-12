// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file tests some public interface methods of AttributeList.
#include "net-base/attribute_list.h"

#include <linux/netlink.h>

#include <string>
#include <vector>

#include <base/containers/span.h>
#include <base/functional/bind.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "net-base/byte_utils.h"

using testing::_;
using testing::InSequence;
using testing::Mock;
using testing::Return;
using testing::Test;

namespace net_base {

class AttributeListTest : public Test {
 public:
  MOCK_METHOD(bool, AttributeMethod, (int, base::span<const uint8_t>));

 protected:
  static const uint16_t kHeaderLength = 4;
  static const uint16_t kType1 = 1;
  static const uint16_t kType2 = 2;
  static const uint16_t kType3 = 3;

  static std::vector<uint8_t> MakeNetlinkAttribute(
      uint16_t len, uint16_t type, const std::string& payload_string) {
    const nlattr attribute{len, type};
    const std::vector<uint8_t> payload =
        net_base::byte_utils::ByteStringToBytes(payload_string);

    std::vector<uint8_t> data = net_base::byte_utils::ToBytes(attribute);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
  }

  static std::vector<uint8_t> MakePaddedNetlinkAttribute(
      uint16_t len, uint16_t type, const std::string& payload) {
    std::vector<uint8_t> data(MakeNetlinkAttribute(len, type, payload));
    data.resize(NLA_ALIGN(data.size()), 0);
    return data;
  }
};

MATCHER_P(PayloadIs, payload, "") {
  return std::vector<uint8_t>(arg.begin(), arg.end()) ==
         net_base::byte_utils::ByteStringToBytes(payload);
}

TEST_F(AttributeListTest, IterateEmptyPayload) {
  EXPECT_CALL(*this, AttributeMethod(_, _)).Times(0);
  AttributeListRefPtr list(new AttributeList());
  EXPECT_TRUE(list->IterateAttributes(
      {}, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
}

TEST_F(AttributeListTest, IteratePayload) {
  std::vector<uint8_t> payload =
      MakePaddedNetlinkAttribute(kHeaderLength + 10, kType1, "0123456789");
  const uint16_t kLength1 = kHeaderLength + 10 + 2;  // 2 bytes padding.
  ASSERT_EQ(kLength1, payload.size());

  const auto attr2 =
      MakePaddedNetlinkAttribute(kHeaderLength + 3, kType2, "123");
  payload.insert(payload.end(), attr2.begin(), attr2.end());
  const uint16_t kLength2 = kLength1 + kHeaderLength + 3 + 1;  // 1 byte pad.
  ASSERT_EQ(kLength2, payload.size());

  const auto attr3 = MakeNetlinkAttribute(kHeaderLength + 5, kType3, "12345");
  payload.insert(payload.end(), attr3.begin(), attr3.end());
  const uint16_t kLength3 = kLength2 + kHeaderLength + 5;
  ASSERT_EQ(kLength3, payload.size());

  InSequence seq;
  EXPECT_CALL(*this, AttributeMethod(kType1, PayloadIs("0123456789")))
      .WillOnce(Return(true));
  EXPECT_CALL(*this, AttributeMethod(kType2, PayloadIs("123")))
      .WillOnce(Return(true));
  EXPECT_CALL(*this, AttributeMethod(kType3, PayloadIs("12345")))
      .WillOnce(Return(true));
  AttributeListRefPtr list(new AttributeList());
  EXPECT_TRUE(list->IterateAttributes(
      payload, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // If a valid offset is provided only the attributes that follow should
  // be enumerated.
  EXPECT_CALL(*this, AttributeMethod(kType1, _)).Times(0);
  EXPECT_CALL(*this, AttributeMethod(kType2, PayloadIs("123")))
      .WillOnce(Return(true));
  EXPECT_CALL(*this, AttributeMethod(kType3, PayloadIs("12345")))
      .WillOnce(Return(true));
  EXPECT_TRUE(list->IterateAttributes(
      payload, kLength1,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // If one of the attribute methods returns false, the iteration should abort.
  EXPECT_CALL(*this, AttributeMethod(kType1, PayloadIs("0123456789")))
      .WillOnce(Return(true));
  EXPECT_CALL(*this, AttributeMethod(kType2, PayloadIs("123")))
      .WillOnce(Return(false));
  EXPECT_CALL(*this, AttributeMethod(kType3, PayloadIs("12345"))).Times(0);
  EXPECT_FALSE(list->IterateAttributes(
      payload, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);
}

TEST_F(AttributeListTest, SmallPayloads) {
  // A payload must be at least 4 bytes long to incorporate the nlattr header.
  EXPECT_CALL(*this, AttributeMethod(_, _)).Times(0);
  AttributeListRefPtr list(new AttributeList());
  const auto payload1 = MakeNetlinkAttribute(kHeaderLength - 1, kType1, "0123");
  EXPECT_FALSE(list->IterateAttributes(
      payload1, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // This is a minimal valid payload.
  const auto payload2 = MakeNetlinkAttribute(kHeaderLength, kType2, "");
  EXPECT_CALL(*this, AttributeMethod(kType2, PayloadIs("")))
      .WillOnce(Return(true));
  EXPECT_TRUE(list->IterateAttributes(
      payload2, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // This is a minmal payload except there are not enough bytes to retrieve
  // the attribute value.
  const uint16_t kType3 = 1;
  const auto payload3 = MakeNetlinkAttribute(kHeaderLength + 1, kType3, "");
  EXPECT_CALL(*this, AttributeMethod(_, _)).Times(0);
  EXPECT_FALSE(list->IterateAttributes(
      payload3, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
}

TEST_F(AttributeListTest, TrailingGarbage) {
  // +---------+
  // | Attr #1 |
  // +-+-+-+-+-+
  // |LEN|TYP|0|
  // +-+-+-+-+-+
  // Well formed frame.
  std::vector<uint8_t> payload =
      MakeNetlinkAttribute(kHeaderLength + 1, kType1, "0");
  EXPECT_CALL(*this, AttributeMethod(kType1, PayloadIs("0")))
      .WillOnce(Return(true));
  AttributeListRefPtr list(new AttributeList());
  EXPECT_TRUE(list->IterateAttributes(
      payload, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // +---------------+
  // | Attr #1 + pad |
  // +-+-+-+-+-+-+-+-+
  // |LEN|TYP|0|1|2|3|
  // +-+-+-+-+-+-+-+-+
  // "123" assumed to be padding for attr1.
  const auto attr1 = net_base::byte_utils::ByteStringToBytes("123");
  payload.insert(payload.end(), attr1.begin(), attr1.end());
  EXPECT_CALL(*this, AttributeMethod(kType1, PayloadIs("0")))
      .WillOnce(Return(true));
  EXPECT_TRUE(list->IterateAttributes(
      payload, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // +---------------+-----+
  // | Attr #1 + pad |RUNT |
  // +-+-+-+-+-+-+-+-+-+-+-+
  // |LEN|TYP|0|1|2|3|4|5|6|
  // +-+-+-+-+-+-+-+-+-+-+-+
  // "456" is acceptable since it is not long enough to complete an netlink
  // attribute header.
  const auto attr2 = net_base::byte_utils::ByteStringToBytes("456");
  payload.insert(payload.end(), attr2.begin(), attr2.end());
  EXPECT_CALL(*this, AttributeMethod(kType1, PayloadIs("0")))
      .WillOnce(Return(true));
  EXPECT_TRUE(list->IterateAttributes(
      payload, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);

  // +---------------+-------+
  // | Attr #1 + pad |Broken |
  // +-+-+-+-+-+-+-+-+-+-+-+-+
  // |LEN|TYP|0|1|2|3|4|5|6|7|
  // +-+-+-+-+-+-+-+-+-+-+-+-+
  // This is a broken frame, since '4567' can be interpreted as a complete
  // nlatter header, but is malformed since there is not enough payload to
  // satisfy the "length" parameter.
  const auto attr3 = net_base::byte_utils::ByteStringToBytes("7");
  payload.insert(payload.end(), attr3.begin(), attr3.end());
  EXPECT_CALL(*this, AttributeMethod(kType1, PayloadIs("0")))
      .WillOnce(Return(true));
  EXPECT_FALSE(list->IterateAttributes(
      payload, 0,
      base::BindRepeating(&AttributeListTest::AttributeMethod,
                          base::Unretained(this))));
  Mock::VerifyAndClearExpectations(this);
}

}  // namespace net_base
