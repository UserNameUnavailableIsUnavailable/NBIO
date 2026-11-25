# NBIO

NBIO is a cross-platform C++ 20 Asynchronous I/O library.

- Linux: Three multiplexers are supported.
  - `poll`
  - `epoll`
  - `io_uring` (kernel verson > 5.1)

Under construction:
- Windows: initial `IOCP` backend (see below; not yet a complete Windows port)
- UNIX variants: `kqueue`

## Configure and build

### Windows core increment

The IOCP backend supports timer channels, vectorized socket receives/sends, and
sequential overlapped file reads/writes. `nbio::core::Scatter` describes a
buffer pointer (`buffers`) and byte length (`count`). Both file and socket
payloads expose these through `header()`; Linux backends use `native_header()`
to obtain persistent `iovec`/`msghdr` storage instead.

Files submit one queued buffer at a time with an explicit 64-bit offset; partial
writes continue before the next buffer. Sockets use `WSARecv`/`WSASend` batches.
Windows data handles must support overlapped I/O, retain normal completion-port
notifications, and belong to only one multiplexer for their lifetime. Association
is retained across channel disarming and shared by receive/send channels.
Destroy channels before their native objects and the multiplexer. Unregistering
an in-flight channel cancels and waits for kernel access to its buffers to end;
its completion storage stays alive until the completion packet is drained.
Call registration and dispatch methods on the owning scheduler thread, and keep
submission buffers alive until completion or channel removal.

Windows `Timer` uses a waitable timer, bridged to IOCP by a one-shot registered
wait. `Wait()` waits for one completion; `WaitFor()` waits up to the requested
duration (zero or negative means poll), dispatches at most one completion, then
submits any remaining work.

File opening and the remaining Windows services, notifications, signals, and
asynchronous accept/connect are deferred. Unsupported channel types throw
instead of silently doing nothing. The complete library/examples are not yet
Windows-buildable. The focused target builds without those deferred objects:

```powershell
cmake --build build --config Debug --target nbio_windows_core_testing
ctest --test-dir build -C Debug -R "^windows_core\." --output-on-failure
```

Configure with `BUILD_TESTING=ON` and the existing vcpkg toolchain first.
These tests exercise timers, socket completions/cancellation, and shared payloads;
end-to-end Windows file tests await the file-object implementation.

### Linux

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

## Asynchronous file I/O

`FileStreamService::Read(buffer, offset)` and `Write(buffer, offset)` use explicit
byte offsets and return awaitable `expected<size_t, error_code>` results.
`FileStream` exposes the same operations. A stream does not maintain file
pointers and cannot be opened with `FileMode::kAppend`.

Reads and writes share a FIFO queue. Consecutive operations in the same direction
with adjacent ranges are vectorized; nonadjacent or overlapping ranges are
submitted separately. Reads can return fewer bytes at EOF. Writes are retried
after short transfers. An I/O failure fails that operation and all later queued
and future reads and writes with the same error. A failed write may have modified
part of the file, so recovery requires inspecting the file and opening a new
stream.

`GetActualSize()` queries the current filesystem length, while `GetExpectedSize()`
reports the projected length after accepted writes succeed. Both return
`expected<uint64_t, error_code>`; the latter returns the latched I/O error
after a failure. The projected size includes a write once its lazy task starts
and enters the queue, not when the task is created. For append-style use,
query the expected size and immediately await the write without suspending in
between. The projection assumes no external writes or truncation. Zero-length
writes do not extend the file.

Use the stream on its owning scheduler thread and keep it and externally owned
buffers alive until queued I/O completes, including after cancellation.
Offsets and ranges exceeding `INT64_MAX` are rejected with `invalid_argument`.
Neither size query guarantees durability.

```cpp
auto offset = file.GetExpectedSize();
if (!offset) throw std::system_error(offset.error(), "querying file size");
auto written = co_await file.Write(bytes, *offset);
if (!written) throw std::system_error(written.error(), "writing file");
```

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

RDMA is built in two layers: coroutine services, which are the interface to
program against, and the primitives underneath them, which are infrastructure
exposed for callers that want direct control over a queue pair. See
[docs/rdma.md](docs/rdma.md) for the layering, the memory contract and the
completion model.

## FAQ

- Building `liburing` fails in Ubuntu 26.04.
- Ubuntu 26.04 replaces system GNU coreutils with uutils, and it has been reported that uutils has compatibility issues with `liburing`'s build scripts. Try `sudo apt remove coreutils-from-uutils --allow-remove-essential` to restore the original GNU coreutils.