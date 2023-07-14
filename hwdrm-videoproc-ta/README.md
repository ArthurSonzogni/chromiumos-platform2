# HWDRM Video processing TA

This package will produce an OP-TEE trusted application.

This is used for auxilliary video processing operations. Currently the only one
is parsing a decrypted H264 stream and returning a limited structure
representing the needed header contents for V4L2 video decoding.
