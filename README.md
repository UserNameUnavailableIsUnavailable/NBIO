# NBIO

NBIO is a cross-platform C++ 20 Asynchronous I/O library.

- Linux: Three multiplexers are supported.
  - `poll`
  - `epoll`
  - `io_uring` (kernel verson > 5.1)

Under construction:
- Windows: `IOCP`
- UNIX variants: `kqueue`

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

The vcpkg manifest supplies `tl-expected`; the `NBIO` target links its exported
`tl::expected` target so consumers inherit the required include path.

## Using NBIO from vcpkg

The port in `ports/NBIO` builds from this local checkout and installs the
`NBIO::NBIO` CMake target. This is intended for unstable development through an
overlay port. Before publishing NBIO as a custom registry, pin the source commit
in the port, then keep the port and vcpkg version metadata
(`versions/baseline.json` and `versions/n-/NBIO.json`) in this repository. Use
`vcpkg x-add-version NBIO` after updating the port to refresh the version
metadata.

In a consumer's `vcpkg-configuration.json`, map the package to this registry:

```json
{
	"registries": [
		{
			"kind": "git",
			"repository": "https://github.com/UserNameUnavailableIsUnavailable/NBIO.git",
			"baseline": "<commit containing the registry version files>",
			"packages": ["NBIO"]
		}
	]
}
```

Keep the consumer's default Microsoft registry entry as well. Once the registry
metadata is pushed, set `baseline` to that commit. The current license file is
an MIT template; replace its copyright placeholders before release.

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

## IP address parsing

`Address::FromIPv4(ip, port)` and `Address::FromIPv6(ip, port)` parse numeric IP
addresses and return `utility::expected<Address, std::error_code>`. Use
`FromUnknown` to detect the family. Invalid text, mismatched families, and embedded null characters
return `invalid_argument`; native parsing failures preserve the system error.
Ports are supplied in host byte order. The deprecated `FromV4` and `FromV6`
factories retain their throwing interface.

## TCP connection setup

`TcpConnector` inherits `Socket::Connect`: blocking sockets connect
synchronously, while non-blocking sockets return the native in-progress error
(`operation_in_progress` on Linux, `WSAEWOULDBLOCK` on Windows). After waiting
for writability, use `TakeError` to check the connection result. Other errors,
including repeated connection attempts, are returned unchanged.

`TcpConnectChannel` handles the non-blocking wait and completion check for
coroutine callers. There are no separate `StartConnect` or `FinishConnect`
methods.
