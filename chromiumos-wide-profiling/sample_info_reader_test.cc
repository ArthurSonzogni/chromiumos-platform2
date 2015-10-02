// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromiumos-wide-profiling/sample_info_reader.h"

#include <byteswap.h>

#include "chromiumos-wide-profiling/compat/test.h"
#include "chromiumos-wide-profiling/kernel/perf_event.h"
#include "chromiumos-wide-profiling/kernel/perf_internals.h"
#include "chromiumos-wide-profiling/test_perf_data.h"
#include "chromiumos-wide-profiling/test_utils.h"

namespace quipper {

using testing::PunU32U64;

TEST(SampleInfoReaderTest, ReadSampleEvent) {
  uint64_t sample_type =       // * == in sample_id_all
      PERF_SAMPLE_IP |
      PERF_SAMPLE_TID |        // *
      PERF_SAMPLE_TIME |       // *
      PERF_SAMPLE_ADDR |
      PERF_SAMPLE_ID |         // *
      PERF_SAMPLE_STREAM_ID |  // *
      PERF_SAMPLE_CPU |        // *
      PERF_SAMPLE_PERIOD |
      PERF_SAMPLE_WEIGHT |
      PERF_SAMPLE_DATA_SRC |
      PERF_SAMPLE_TRANSACTION;

  struct perf_event_attr attr = {0};
  attr.sample_type = sample_type;

  SampleInfoReader reader(attr, false /* read_cross_endian */);

  const u64 sample_event_array[] = {
    0xffffffff01234567,                    // IP
    PunU32U64{.v32 = {0x68d, 0x68e}}.v64,  // TID (u32 pid, tid)
    1415837014*1000000000ULL,              // TIME
    0x00007f999c38d15a,                    // ADDR
    2,                                     // ID
    1,                                     // STREAM_ID
    8,                                     // CPU
    10001,                                 // PERIOD
    12345,                                 // WEIGHT
    0x68100142,                            // DATA_SRC
    67890,                                 // TRANSACTIONS
  };
  const sample_event sample_event_struct = {
    .header = {
      .type = PERF_RECORD_SAMPLE,
      .misc = 0,
      .size = sizeof(sample_event) + sizeof(sample_event_array),
    }
  };

  stringstream input;
  input.write(reinterpret_cast<const char*>(&sample_event_struct),
              sizeof(sample_event_struct));
  input.write(reinterpret_cast<const char*>(sample_event_array),
              sizeof(sample_event_array));
  string input_string = input.str();
  const event_t& event = *reinterpret_cast<const event_t*>(input_string.data());

  perf_sample sample;
  ASSERT_TRUE(reader.ReadPerfSampleInfo(event, &sample));

  EXPECT_EQ(0xffffffff01234567, sample.ip);
  EXPECT_EQ(0x68d, sample.pid);
  EXPECT_EQ(0x68e, sample.tid);
  EXPECT_EQ(1415837014*1000000000ULL, sample.time);
  EXPECT_EQ(0x00007f999c38d15a, sample.addr);
  EXPECT_EQ(2, sample.id);
  EXPECT_EQ(1, sample.stream_id);
  EXPECT_EQ(8, sample.cpu);
  EXPECT_EQ(10001, sample.period);
  EXPECT_EQ(12345, sample.weight);
  EXPECT_EQ(0x68100142, sample.data_src);
  EXPECT_EQ(67890, sample.transaction);
}

TEST(SampleInfoReaderTest, ReadSampleEventCrossEndian) {
  uint64_t sample_type =      // * == in sample_id_all
      PERF_SAMPLE_IP |
      PERF_SAMPLE_TID |        // *
      PERF_SAMPLE_TIME |       // *
      PERF_SAMPLE_ADDR |
      PERF_SAMPLE_ID |         // *
      PERF_SAMPLE_STREAM_ID |  // *
      PERF_SAMPLE_CPU |        // *
      PERF_SAMPLE_PERIOD;

  struct perf_event_attr attr = {0};
  attr.sample_type = sample_type;

  SampleInfoReader reader(attr, true /* read_cross_endian */);

  const u64 sample_event_array[] = {
    0xffffffff01234567,                    // IP
    PunU32U64{.v32 = {0x68d, 0x68e}}.v64,  // TID (u32 pid, tid)
    1415837014*1000000000ULL,              // TIME
    0x00007f999c38d15a,                    // ADDR
    2,                                     // ID
    1,                                     // STREAM_ID
    8,                                     // CPU
    10001,                                 // PERIOD
  };

  const sample_event sample_event_struct = {
    .header = {
      .type = PERF_RECORD_SAMPLE,
      .misc = 0,
      .size = sizeof(sample_event) + sizeof(sample_event_array),
    }
  };

  stringstream input;
  input.write(reinterpret_cast<const char*>(&sample_event_struct),
              sizeof(sample_event_struct));
  input.write(reinterpret_cast<const char*>(sample_event_array),
              sizeof(sample_event_array));
  string input_string = input.str();
  const event_t& event = *reinterpret_cast<const event_t*>(input_string.data());

  perf_sample sample;
  ASSERT_TRUE(reader.ReadPerfSampleInfo(event, &sample));

  EXPECT_EQ(bswap_64(0xffffffff01234567), sample.ip);
  EXPECT_EQ(bswap_32(0x68d), sample.pid);   // 32-bit
  EXPECT_EQ(bswap_32(0x68e), sample.tid);   // 32-bit
  EXPECT_EQ(bswap_64(1415837014*1000000000ULL), sample.time);
  EXPECT_EQ(bswap_64(0x00007f999c38d15a), sample.addr);
  EXPECT_EQ(bswap_64(2), sample.id);
  EXPECT_EQ(bswap_64(1), sample.stream_id);
  EXPECT_EQ(bswap_32(8), sample.cpu);       // 32-bit
  EXPECT_EQ(bswap_64(10001), sample.period);
}

TEST(SampleInfoReaderTest, ReadMmapEvent) {
  uint64_t sample_type =      // * == in sample_id_all
      PERF_SAMPLE_IP |
      PERF_SAMPLE_TID |        // *
      PERF_SAMPLE_TIME |       // *
      PERF_SAMPLE_ADDR |
      PERF_SAMPLE_ID |         // *
      PERF_SAMPLE_STREAM_ID |  // *
      PERF_SAMPLE_CPU |        // *
      PERF_SAMPLE_PERIOD;

  struct perf_event_attr attr = {0};
  attr.sample_type = sample_type;

  SampleInfoReader reader(attr, false /* read_cross_endian */);

  // PERF_RECORD_MMAP
  ASSERT_EQ(40, offsetof(struct mmap_event, filename));
  const u64 mmap_sample_id[] = {
    PunU32U64{.v32 = {0x68d, 0x68e}}.v64,  // TID (u32 pid, tid)
    1415911367*1000000000ULL,              // TIME
    3,                                     // ID
    2,                                     // STREAM_ID
    9,                                     // CPU
  };
  const size_t mmap_event_size =
      offsetof(struct mmap_event, filename) +
      10+6 /* ==16, nearest 64-bit boundary for filename */ +
      sizeof(mmap_sample_id);

  const char mmap_filename[10+6] = "/dev/zero";
  struct mmap_event written_mmap_event = {
    .header = {
      .type = PERF_RECORD_MMAP,
      .misc = 0,
      .size = mmap_event_size,
    },
    .pid = 0x68d, .tid = 0x68d,
    .start = 0x1d000,
    .len = 0x1000,
    .pgoff = 0,
    // .filename = ..., // written separately
  };

  stringstream input;
  input.write(reinterpret_cast<const char*>(&written_mmap_event),
              offsetof(struct mmap_event, filename));
  input.write(mmap_filename, 10+6);
  input.write(reinterpret_cast<const char*>(mmap_sample_id),
              sizeof(mmap_sample_id));

  string input_string = input.str();
  const event_t& event = *reinterpret_cast<const event_t*>(input_string.data());

  perf_sample sample;
  ASSERT_TRUE(reader.ReadPerfSampleInfo(event, &sample));

  EXPECT_EQ(0x68d, sample.pid);
  EXPECT_EQ(0x68e, sample.tid);
  EXPECT_EQ(1415911367*1000000000ULL, sample.time);
  EXPECT_EQ(3, sample.id);
  EXPECT_EQ(2, sample.stream_id);
  EXPECT_EQ(9, sample.cpu);
}

}  // namespace quipper

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
