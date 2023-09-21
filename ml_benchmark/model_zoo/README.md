# ChromeOS ML Model Zoo

This is a collection of TFLite models that can be used to benchmark devices
for typical ML use cases within ChromeOS. Where applicable, baseline figures
are provided to indicate the minimum performance requirements for these models
to meet the user experience goals of those use cases.

These models can be easily deployed to `/usr/local/share/ml-test-assets` on a
DUT via the `chromeos-base/ml-test-assets` package:

`emerge-${BOARD} ml-test-assets && cros deploy <DUT> ml-test-assets`

The models can be downloaded directly [here](https://commondatastorage.googleapis.com/chromeos-localmirror/distfiles/ml-test-assets-0.0.5.tar.xz)

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

* **Note 1 : These models are CNN based.**
* **Note 2 : selfie_* have an F16 and F32 variant, indicated by the filename.**

The `convolution_benchmark_*` and `segmentation_benchmark_512x512*` models are production model graphs with
randomized weights, so we don't measure the accuracy of these models.

| Model                                     | Target Latency (ms)  | Accuracy                                    | Power Usage | Max Memory |
|-------------------------------------------|---------------------:|--------------------------------------------:|-------------|------------|
| selfie_segmentation_landscape_256x256     |                 <= 6 | avg_err <=0.0000003<br/> std_dev<=5e-06     |         TBD |    <=100MB |
| segmentation_benchmark_512x512            |                 <= 10|                                             |         TBD |    <=100MB |
| convolution_benchmark_144_256_1           |                 <= 4 | -                                           |         TBD |    <=100MB |
| convolution_benchmark_144_256_2           |                 <= 4 | -                                           |         TBD |    <=100MB |
| convolution_benchmark_288_512_1           |                 <= 6 | -                                           |         TBD |    <=100MB |
| convolution_benchmark_288_512_2           |                 <= 6 | -                                           |         TBD |    <=100MB |

### Image Search

**Note: These models are CNN based.**

| Model                      | Latency (ms)  | Accuracy                               | Power Usage | Max Memory |
|----------------------------|--------------:|---------------------------------------:|-------------|------------|
| mobilenet_v2_1.0_224       |          <= 5 | avg_err <=0.00005<br/>std_dev <=6e-06  |         TBD |    <=150MB |
| mobilenet_v2_1.0_224_quant |          <= 5 | avg_err <=1.5<br/>std_dev <=0.2        |         TBD |    <=150MB |

### Audio Models

**Note: These models are running on CPU in production**

**Note2: While running `benchmark_model` with following models,
add `--run_delay=<secs>` to simulate audio server behavior.**

| Model       | Latency on CPU (ms) | Extra arguments    | sha256                                                           |
|-------------|--------------------:|--------------------| ---------------------------------------------------------------- |
| lstm        |                <= 1 | `--run_delay=0.01` | a1f1329501c0a87dff6a20d3b330cb73e85ccc23a5c36880c81476e2fb338fd2 |
| seanet_wave |                <= 2 | `--run_delay=0.02` | 7bb40d8e72471a13324491777e03207646f1641942d373d40478d237e87d032d |
| seanet_stft |                <= 2 | `--run_delay=0.02` | a3ea8c3eae3373cb9ef4ac46d22ad5a254aa2e40d764a8f6dbee218be27f9b31 |
