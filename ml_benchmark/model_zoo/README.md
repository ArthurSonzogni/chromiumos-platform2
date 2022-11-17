# ChromeOS ML Model Zoo

This is a collection of TFLite models that can be used to benchmark devices
for typical ML use cases within ChromeOS. Where applicable, baseline figures
are provided to indicate the minimum performance requirements for these models
to meet the user experience goals of those use cases.

## Tools

### Latency, Max Memory

Latency and maximum memory usage is measured by the
[TFLite Benchmark Model Tool](https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/tools/benchmark).

This is installed by default on all ChromeOS test images.

Example usage:

`benchmark_model --graph=${tflite_file} --min_secs=20 <delegate options>`

### Accuracy

Accuracy is measured by the
[TFLite Inference Diff Tool](https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/tools/evaluation/tasks/inference_diff).

This is installed by default on all ChromeOS test images.

Example usage:

`inference_diff_eval  --graph=${tflite_file} <delegate options>`

## Use Cases

### Video Conferencing

**Note: These models are CNN based.**

| Model                                     | Latency (ms)  | Accuracy                             | Power Usage | Max Memory |
|-------------------------------------------|--------------:|-------------------------------------:|-------------|------------|
| selfie_segmentation_landscape_256x256     |          <=10 |                                  TBD |         TBD |    <=100MB |
| convolution_benchmark_1_144x256           |          <= 4 | avg_err <=0.0003<br/>std_dev <=5e-06 |         TBD |    <=100MB |
| convolution_benchmark_2_144x256           |          <= 4 | avg_err <=0.0003<br/>std_dev <=5e-06 |         TBD |    <=100MB |
