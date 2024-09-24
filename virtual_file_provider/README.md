# Virtual File Provider

Virtual File Provider is a service which provides file descriptors which forward
access requests to chrome.
From the accessing process's perspective, the file descriptor behaves like a
regular file descriptor (unlike pipe, it's seekable), while actually there is no
real file associated with it.
Currently, this service is only used by ARC++ container.

## Private FUSE file system
To forward access requests on file descriptors, this service implements a FUSE
file system which is only accessible to this service itself.

## D-Bus interface
This service provides two D-Bus methods, GenerateVirtualFileId() and
OpenFileById().

GenerateVirtualFileId() generates and returns a new unique ID. When
OpenFileById() is later called with the generated unique ID, it creates and
returns a seekable file descriptor (FD) backed by the private FUSE file system.

When the file descriptor created above is being accessed, Virtual File Provider
will send signal to forward the access request to chrome.
