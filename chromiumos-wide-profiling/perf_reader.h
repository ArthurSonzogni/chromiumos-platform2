// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUMOS_WIDE_PROFILING_PERF_READER_H_
#define CHROMIUMOS_WIDE_PROFILING_PERF_READER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/macros.h"

#include "chromiumos-wide-profiling/compat/proto.h"
#include "chromiumos-wide-profiling/compat/string.h"
#include "chromiumos-wide-profiling/kernel/perf_event.h"
#include "chromiumos-wide-profiling/perf_serializer.h"
#include "chromiumos-wide-profiling/sample_info_reader.h"
#include "chromiumos-wide-profiling/utils.h"

namespace quipper {

// This is becoming more like a partial struct perf_evsel
struct PerfFileAttr {
  struct perf_event_attr attr;
  string name;
  std::vector<u64> ids;
};

// Based on code in tools/perf/util/header.c, the metadata are of the following
// formats:

// Based on kernel/perf_internals.h
const size_t kBuildIDArraySize = 20;
const size_t kBuildIDStringLength = kBuildIDArraySize * 2;

struct PerfUint32Metadata {
  u32 type;
  std::vector<uint32_t> data;
};

struct PerfUint64Metadata {
  u32 type;
  std::vector<uint64_t> data;
};

typedef u32 num_siblings_type;

struct PerfCPUTopologyMetadata {
  std::vector<string> core_siblings;
  std::vector<string> thread_siblings;
};

struct PerfNodeTopologyMetadata {
  u32 id;
  u64 total_memory;
  u64 free_memory;
  string cpu_list;
};

class DataReader;
class DataWriter;

class PerfReader {
 public:
  PerfReader();
  ~PerfReader();

  // Dump contents to a protobuf.
  bool Serialize(PerfDataProto* perf_data_proto) const;
  // Read in contents from a protobuf.
  bool Deserialize(const PerfDataProto& perf_data_proto);

  // Makes |build_id| fit the perf format, by either truncating it or adding
  // zeros to the end so that it has length kBuildIDStringLength.
  static void PerfizeBuildIDString(string* build_id);

  // Changes |build_id| to the best guess of what the build id was before going
  // through perf.  Specifically, it keeps removing trailing sequences of four
  // zero bytes (or eight '0' characters) until there are no more such
  // sequences, or the build id would be empty if the process were repeated.
  static void TrimZeroesFromBuildIDString(string* build_id);

  bool ReadFile(const string& filename);
  bool ReadFromVector(const std::vector<char>& data);
  bool ReadFromString(const string& str);
  bool ReadFromPointer(const char* data, size_t size);
  bool ReadFromData(DataReader* data);

  bool WriteFile(const string& filename);
  bool WriteToVector(std::vector<char>* data);
  bool WriteToString(string* str);
  bool WriteToPointer(char* buffer, size_t size);

  // Stores the mapping from filenames to build ids in build_id_events_.
  // Returns true on success.
  // Note: If |filenames_to_build_ids| contains a mapping for a filename for
  // which there is already a build_id_event in build_id_events_, a duplicate
  // build_id_event will be created, and the old build_id_event will NOT be
  // deleted.
  bool InjectBuildIDs(const std::map<string, string>& filenames_to_build_ids);

  // Replaces existing filenames with filenames from |build_ids_to_filenames|
  // by joining on build ids.  If a build id in |build_ids_to_filenames| is not
  // present in this parser, it is ignored.
  bool Localize(const std::map<string, string>& build_ids_to_filenames);

  // Same as Localize, but joins on filenames instead of build ids.
  bool LocalizeUsingFilenames(const std::map<string, string>& filename_map);

  // Stores a list of unique filenames found in MMAP/MMAP2 events into
  // |filenames|.  Any existing data in |filenames| will be lost.
  void GetFilenames(std::vector<string>* filenames) const;
  void GetFilenamesAsSet(std::set<string>* filenames) const;

  // Uses build id events to populate |filenames_to_build_ids|.
  // Any existing data in |filenames_to_build_ids| will be lost.
  // Note:  A filename returned by GetFilenames need not be present in this map,
  // since there may be no build id event corresponding to the MMAP/MMAP2.
  void GetFilenamesToBuildIDs(
      std::map<string, string>* filenames_to_build_ids) const;

  // Accessors and mutators.

  const RepeatedPtrField<PerfDataProto_PerfFileAttr>& attrs() const {
    return proto_.file_attrs();
  }
  const RepeatedPtrField<PerfDataProto_PerfEventType>& event_types() const {
    return proto_.event_types();
  }

  const RepeatedPtrField<PerfDataProto_PerfEvent>& events() const {
    return proto_.events();
  }
  // WARNING: Modifications to the protobuf events may change the amount of
  // space required to store the corresponding raw event. If that happens, the
  // caller is responsible for correctly updating the size in the event header.
  // TODO(sque): Remove this restriction.
  RepeatedPtrField<PerfDataProto_PerfEvent>* mutable_events() {
    return proto_.mutable_events();
  }

  const RepeatedPtrField<PerfDataProto_PerfBuildID>& build_ids() const {
    return proto_.build_ids();
  }
  RepeatedPtrField<PerfDataProto_PerfBuildID>* mutable_build_ids() {
    return proto_.mutable_build_ids();
  }

  const string& tracing_data() const {
    return proto_.tracing_data().tracing_data();
  }

  const PerfDataProto_StringMetadata& string_metadata() const {
    return proto_.string_metadata();
  }

  uint64_t metadata_mask() const {
    return proto_.metadata_mask().Get(0);
  }

  // This is a plain accessor for the internal protobuf storage. It is meant for
  // exposing the internals. This is not initialized until Read*() or
  // Deserialize() has been called.
  //
  // Call Serialize() instead of this function to acquire a protobuf with checks
  // that it has properly initialized. Serialize() also adds a timestamp.
  const PerfDataProto& proto() const {
    return proto_;
  }

 private:
  bool ReadHeader(DataReader* data);
  bool ReadAttrsSection(DataReader* data);
  bool ReadAttr(DataReader* data);
  bool ReadEventAttr(DataReader* data, perf_event_attr* attr);
  bool ReadUniqueIDs(DataReader* data, size_t num_ids, std::vector<u64>* ids);

  bool ReadEventTypesSection(DataReader* data);
  // if event_size == 0, then not in an event.
  bool ReadEventType(DataReader* data, int attr_idx, size_t event_size);

  bool ReadDataSection(DataReader* data);

  // Reads metadata in normal mode.
  bool ReadMetadata(DataReader* data);

  // The following functions read various types of metadata.
  bool ReadTracingMetadata(DataReader* data, size_t size);
  bool ReadBuildIDMetadata(DataReader* data, size_t size);
  // Reads contents of a build ID event or block beyond the header. Useful for
  // reading build IDs in piped mode, where the header must be read first in
  // order to determine that it is a build ID event.
  bool ReadBuildIDMetadataWithoutHeader(DataReader* data,
                                        const perf_event_header& header);

  // Reads a singular string metadata field (with preceding size field) from
  // |data| and writes the string and its Md5sum prefix into |dest|.
  bool ReadSingleStringMetadata(
      DataReader* data,
      size_t max_readable_size,
      PerfDataProto_StringMetadata_StringAndMd5sumPrefix* dest) const;
  // Reads a string metadata with multiple string fields (each with preceding
  // size field) from |data|. Writes each string field and its Md5sum prefix
  // into |dest_array|. Writes the combined string fields (joined into one
  // string into |dest_single|.
  bool ReadRepeatedStringMetadata(
      DataReader* data,
      size_t max_readable_size,
      RepeatedPtrField<PerfDataProto_StringMetadata_StringAndMd5sumPrefix>*
          dest_array,
      PerfDataProto_StringMetadata_StringAndMd5sumPrefix* dest_single) const;

  bool ReadUint32Metadata(DataReader* data, u32 type, size_t size);
  bool ReadUint64Metadata(DataReader* data, u32 type, size_t size);
  bool ReadCPUTopologyMetadata(DataReader* data, u32 type, size_t size);
  bool ReadNUMATopologyMetadata(DataReader* data, u32 type, size_t size);
  bool ReadEventDescMetadata(DataReader* data, u32 type, size_t size);

  // Read perf data from piped perf output data.
  bool ReadPipedData(DataReader* data);

  // Returns the size in bytes that would be written by any of the methods that
  // write the entire perf data file (WriteFile, WriteToPointer, etc).
  size_t GetSize() const;

  // Populates |*header| with the proper contents based on the perf data that
  // has been read.
  void GenerateHeader(struct perf_file_header* header) const;

  // Like WriteToPointer, but does not check if the buffer is large enough.
  bool WriteToPointerWithoutCheckingSize(char* buffer, size_t size);

  bool WriteHeader(const struct perf_file_header& header,
                   DataWriter* data) const;
  bool WriteAttrs(const struct perf_file_header& header,
                  DataWriter* data) const;
  bool WriteData(const struct perf_file_header& header, DataWriter* data) const;
  bool WriteMetadata(const struct perf_file_header& header,
                     DataWriter* data) const;

  // For writing the various types of metadata.
  bool WriteBuildIDMetadata(u32 type, DataWriter* data) const;
  bool WriteSingleStringMetadata(
      const PerfDataProto_StringMetadata_StringAndMd5sumPrefix& src,
      DataWriter* data) const;
  bool WriteRepeatedStringMetadata(
      const RepeatedPtrField<
          PerfDataProto_StringMetadata_StringAndMd5sumPrefix>& src_array,
      DataWriter* data) const;
  bool WriteUint32Metadata(u32 type, DataWriter* data) const;
  bool WriteUint64Metadata(u32 type, DataWriter* data) const;
  bool WriteEventDescMetadata(u32 type, DataWriter* data) const;
  bool WriteCPUTopologyMetadata(u32 type, DataWriter* data) const;
  bool WriteNUMATopologyMetadata(u32 type, DataWriter* data) const;

  // For reading event blocks within piped perf data.
  bool ReadAttrEventBlock(DataReader* data, size_t size);

  // Swaps byte order for non-header fields of the data structure pointed to by
  // |event|, if |is_cross_endian| is true. Otherwise leaves the data the same.
  void MaybeSwapEventFields(event_t* event, bool is_cross_endian);

  // Returns the number of types of metadata stored and written to output data.
  size_t GetNumSupportedMetadata() const;

  // For computing the sizes of the various types of metadata.
  size_t GetBuildIDMetadataSize() const;
  size_t GetStringMetadataSize() const;
  size_t GetUint32MetadataSize() const;
  size_t GetUint64MetadataSize() const;
  size_t GetEventDescMetadataSize() const;
  size_t GetCPUTopologyMetadataSize() const;
  size_t GetNUMATopologyMetadataSize() const;

  // Returns true if we should write the number of strings for the string
  // metadata of type |type|.
  bool NeedsNumberOfStringData(u32 type) const;

  // Replaces existing filenames in MMAP/MMAP2 events based on |filename_map|.
  // This method does not change |build_id_events_|.
  bool LocalizeMMapFilenames(const std::map<string, string>& filename_map);

  // Stores a PerfFileAttr in |proto_| and updates |serializer_|.
  void AddPerfFileAttr(const PerfFileAttr& attr);

  bool get_metadata_mask_bit(uint32_t bit) const {
    return metadata_mask() & (1 << bit);
  }
  void set_metadata_mask_bit(uint32_t bit) {
    proto_.set_metadata_mask(0, metadata_mask() | (1 << bit));
  }

  // The file header is either a normal header or a piped header.
  union {
    struct perf_file_header header_;
    struct perf_pipe_file_header piped_header_;
  };

  // Store the perf data as a protobuf.
  // TODO(sque): Store all fields in here, not just events.
  PerfDataProto proto_;

  // Whether the incoming data is from a machine with a different endianness. We
  // got rid of this flag in the past but now we need to store this so it can be
  // passed to |serializer_|.
  bool is_cross_endian_;

  // For serializing individual events.
  PerfSerializer serializer_;

  // When writing to a new perf data file, this is used to hold the generated
  // file header, which may differ from the input file header, if any.
  struct perf_file_header out_header_;

  DISALLOW_COPY_AND_ASSIGN(PerfReader);
};

}  // namespace quipper

#endif  // CHROMIUMOS_WIDE_PROFILING_PERF_READER_H_
