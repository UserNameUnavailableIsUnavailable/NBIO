# NBIO

NBIO is a cross-platform C++ 20 asynchronous I/O library.

## Configure and build

The Linux `epoll` backend is the default. io_uring and RDMA are opt-in so a
normal build does not require their development packages. Examples are also
opt-in:

```sh
cmake -S . -B build \
	-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
	-DNBIO_BUILD_EXAMPLES=ON
cmake --build build
```

The vcpkg manifest supplies `tl-expected`; the `nbio` target links its exported
`tl::expected` target so consumers inherit the required include path.

Enable io_uring with `-DNBIO_ENABLE_IO_URING=ON`; this requires liburing and
pkg-config. When using vcpkg, selecting the `io-uring` manifest feature enables
the matching CMake option by default.

Enable RDMA with `-DNBIO_ENABLE_RDMA=ON`; this requires the Linux
`librdmacm` and `libibverbs` development packages discoverable through
pkg-config. Selecting the `rdma` vcpkg manifest feature enables the
matching CMake option by default. Likewise, the `testing` and `benchmark`
manifest features enable their CMake targets. Explicit CMake cache options can
override these defaults. The two RDMA examples are added only when RDMA is
enabled. io_uring's `Condition` example is added only when io_uring is enabled.

Both options can be enabled together. Building the RDMA examples does not
require an RDMA device, but running them does.
