// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBPMT_PMT_DECODER_H_
#define LIBPMT_PMT_DECODER_H_

#include <memory>
#include <unordered_map>
#include <vector>

#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/brillo_export.h>
#include <libpmt/bits/pmt_data.pb.h>
#include <libpmt/bits/pmt_data_interface.h>
#include <libpmt/bits/pmt_metadata.h>

namespace pmt {

// C++ class for processing Intel PMT data.
class BRILLO_EXPORT PmtDecoder {
 public:
  // Default implementation using the real filesystem to read metadata.
  PmtDecoder();

  // Create a PMT decoder with a specified implementation of the PMT data
  // interface.
  //
  // Used for testing.
  // @param intf Implementation of the PMT data interface.
  explicit PmtDecoder(std::unique_ptr<PmtDataInterface> intf);

  // Default destructor will take care of releasing any resources allocated
  // while setting up the decoding.
  ~PmtDecoder();

  // Detect the Guids with the decoding metadata present in the system.
  //
  // @return The vector of GUIDs of devices that can be decoded.
  std::vector<Guid> DetectMetadata();

  // Initialize decoding for the given list of devices.
  //
  // Caller should provide a list of guids which the library will initialize
  // and allocate internal data structures for. Only after this function
  // returns, the caller may utilize Decode().
  // NOTE: Do not run this function in your fast-path. It parses through
  // multiple XML files and performs O(n^2) type of searching. Since the PMT
  // devices to not change in runtime, this can be run in control-path of your
  // application and fast-path should only utilize Decode().
  //
  // @param guids List of GUIDs to setup the decoding for.
  // @return 0 on success or a negative error code.
  // @retval -EINVAL One of the GUIDs is missing metadata.
  // @retval -EBUSY Decoding was initialized already.
  // @retval -EBADF There was an issue parsing metadata.
  int SetUpDecoding(const std::vector<Guid> guids);

  // Terminate any device that was set up and clean up associated data.
  //
  // @return 0 on success or a negative error code.
  // @retval -ENOENT Decoding was not set up.
  int CleanUpDecoding();

  // Decode the data from snapshot.
  //
  // Given data will be decoded into a vectors of samples and their
  // metadata as set up by a previous call to SetUpDecoding().
  //
  // @param data The snapshot to decode.
  // @return Pointer to the decoded data or nullptr on error.
  const DecodingResult* Decode(const Snapshot* const data);

 private:
  struct MetadataFilePaths {
    base::FilePath aggregator_;
    base::FilePath aggregator_interface_;
  };
  // Prepares a list of metadata files present in the system along with their
  // corresponding GUID.
  std::unordered_map<Guid, struct MetadataFilePaths> FindMetadata();

  // Decoding context that is passed throughout the decoding process.
  struct DecodingContext ctx_;
  // Interface for getting PMT data information from the system.
  std::unique_ptr<PmtDataInterface> intf_;
};

}  // namespace pmt

#endif  // LIBPMT_PMT_DECODER_H_
