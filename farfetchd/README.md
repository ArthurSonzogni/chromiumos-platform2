# Farfetchd: generalizing readahead-as-a-service

Farfetchd is a file prefetching D-Bus service. Prefetching is available using the following interfaces:

* Method PreloadFile: Reads the file at the given path into the cache using pread(). This is performed synchronously (within the service).
* Method PreloadFileAsync: Reads the file at the given path into the cache using readahead(). readahead() has a max read limit of 32 pages per read so reading a file larger than that may result in multiple syscalls being scheduled. This is performed asynchronously.
* Method PreloadFileMmap: Reads the file at the given path and saves it into the cache using the MAP_POPULATE and MAP_SHARED flags. Immediately after, mummap() is then run to free memory (this will not affect the cache). This is performed asynchronously.

more info at go/cros-farfetchd.
