// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef METRICS_SERIALIZATION_SERIALIZATION_UTILS_H_
#define METRICS_SERIALIZATION_SERIALIZATION_UTILS_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <base/time/time.h>

namespace metrics {

class MetricSample;

// Metrics helpers to serialize and deserialize metrics collected by
// ChromeOS.
namespace SerializationUtils {

// Deserializes a sample passed as a string and return a sample.
// The return value will either be a std::unique_ptr to a Metric sample (if the
// deserialization was successful) or a NULL std::unique_ptr.
MetricSample ParseSample(const std::string& sample);

// Reads samples from a file, and modifies the file to reflect the samples
// processed.  If all samples are read, truncates the file to zero size and
// returns true.  If sample_batch_max_length is exceeded when reading a batch of
// samples, changes the file to logically contain only the remaining samples.
// Returns false if samples are left for further processing, true in all other
// cases (including errors).
bool ReadAndTruncateMetricsFromFile(const std::string& filename,
                                    std::vector<MetricSample>* metrics,
                                    size_t sample_batch_max_length);

// Serializes a vector of samples and writes them to |filename|.
// The format for each sample is:
//  message_size, serialized_message
// where
//  * message_size is the total length of the message (message_size +
//    serialized_message) on 4 bytes
//  * serialized_message is the serialized version of sample (using ToString)
//
//  Will block by default if unable to grab the lock to |filename|. If
//  |use_nonblocking_lock| is set, the function tries to grab the lock with a
//  maximum of two attempts, sleeping between each attempt to give time for the
//  lock to be freed by the holding process. If the function is not able to grab
//  the lock after two attempts, it will not write the sample to |filename|.
//
//  NB: the file will never leave the device so message_size will be written
//  with the architecture's endianness.
bool WriteMetricsToFile(const std::vector<MetricSample>& samples,
                        const std::string& filename,
                        bool use_nonblocking_lock = false);

// Same as above, but user must explicitly specify:
//  * if the function should block if unable to grab the lock to |filename|
//  * a callback to run if the function is unable to grab the lock on the first
//    attempt
bool WriteMetricsToFile(
    const std::vector<MetricSample>& samples,
    const std::string& filename,
    bool use_nonblocking_lock,
    base::OnceCallback<void(base::TimeDelta)> sleep_function);

// Maximum length of a serialized message.
static const size_t kMessageMaxLength = 1024;

// Maximum size of serialized messages that we will read and upload in one
// pass.  If a device does not have connectivity for a long time, a large
// number of messages can accumulate.  Without this limit, the large number of
// messages can put strain on resources (such as RAM for the metrics daemon's
// heap) when connectivity is restored and the upload is attempted.
static const size_t kSampleBatchMaxLength = 10 * 1024 * 1024;

}  // namespace SerializationUtils
}  // namespace metrics

#endif  // METRICS_SERIALIZATION_SERIALIZATION_UTILS_H_
