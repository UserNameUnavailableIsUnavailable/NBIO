#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <rdma/rdma_cma.h>
#include <unistd.h>

#include <cerrno>
#include <format>
#include <nbio/net/rdma_resource.hpp>
#include <nbio/utility/defer.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace nbio::net {
namespace {
std::runtime_error RegistrationFailed(std::span<const char> memory, int access) {
    const int error = errno;
    return std::runtime_error(std::format("failed to register {} bytes at {} (access {:#x}): {} (errno {})",
                                          memory.size(), static_cast<const void*>(memory.data()), access,
                                          std::generic_category().message(error), error));
}
}  // namespace

std::size_t RdmaResource::page_size() noexcept {
    const long size = ::sysconf(_SC_PAGESIZE);
    return size > 0 ? static_cast<std::size_t>(size) : std::size_t{4096};
}

::ibv_context* RdmaResource::FindDevice(std::string_view device_name) {
    // The contexts come from the communication manager rather than from
    // ibv_open_device: every id rdma_cm creates carries the context rdma_cm opened
    // for the device that id resolves to, and every id on one device carries the
    // same one. A protection domain, and the regions registered on it, are only
    // usable by a queue pair built on that same context, so a context opened here
    // would give a domain no queue pair could be built with.
    int num_devices{0};
    ::ibv_context** device_list = ::rdma_get_devices(&num_devices);
    if (!device_list) [[unlikely]] {
        throw std::runtime_error("failed to list rdma devices");
    }
    // Only the list is this resource's to free; the contexts in it are the
    // communication manager's, and it keeps them open for the life of the process.
    auto d1 = utility::MakeDefer([&device_list] { ::rdma_free_devices(device_list); });
    if (num_devices == 0) {
        throw std::runtime_error("no rdma devices available");
    }
    for (int i = 0; i < num_devices; i++) {
        if (::ibv_get_device_name(device_list[i]->device) == device_name) {
            return device_list[i];
        }
    }
    throw std::runtime_error(std::format("device {} not found", device_name));
}

std::unique_ptr<::ibv_pd, RdmaResource::ProtectionDomainDeleter> RdmaResource::AllocateProtectionDomain(
    ::ibv_context* context, std::string_view device_name) {
    std::unique_ptr<::ibv_pd, ProtectionDomainDeleter> domain{::ibv_alloc_pd(context)};
    if (!domain) [[unlikely]] {
        throw std::runtime_error(std::format("failed to allocate protection domain for {}", device_name));
    }
    return domain;
}

RdmaResource::RdmaResource(std::string_view device_name, std::span<const char> send_memory_region,
                           std::span<char> receive_memory_region)
    : context_(FindDevice(device_name)),
      protection_domain_(AllocateProtectionDomain(context_, device_name)),
      name_(device_name) {
    if (send_memory_region.empty() || receive_memory_region.empty()) [[unlikely]] {
        throw std::invalid_argument("an rdma resource needs both a send and a receive region");
    }
    // Alignment is a property of the starting address alone: the provider pins the
    // pages the range touches, so a length that ends in the middle of a page is
    // registered as far as the caller asked and no further. An address that is not
    // page aligned cannot be described at all, because the first partial page would
    // have to be pinned without knowing where it begins.
    const std::size_t page = page_size();
    for (const auto memory : {std::span<const char>(send_memory_region), std::span<const char>(receive_memory_region)}) {
        if (reinterpret_cast<std::uintptr_t>(memory.data()) % page != 0) [[unlikely]] {
            throw std::invalid_argument(
                std::format("a memory region has to start on a page boundary: {} is not a multiple of {}",
                            static_cast<const void*>(memory.data()), page));
        }
    }

    // Local read is implicit for the owner, so a send source needs no flag at all;
    // a receive target needs local write, which is the one thing the device may
    // not do without being told.
    send_memory_region_.reset(
        ::ibv_reg_mr(protection_domain_.get(), const_cast<char*>(send_memory_region.data()), send_memory_region.size(), 0));
    if (!send_memory_region_) [[unlikely]] {
        throw RegistrationFailed(send_memory_region, 0);
    }
    receive_memory_region_.reset(::ibv_reg_mr(protection_domain_.get(), receive_memory_region.data(),
                                              receive_memory_region.size(), IBV_ACCESS_LOCAL_WRITE));
    if (!receive_memory_region_) [[unlikely]] {
        throw RegistrationFailed(receive_memory_region, IBV_ACCESS_LOCAL_WRITE);
    }
}

// The members do the work, and each of them knows what it holds: the regions are
// deregistered, then the protection domain is deallocated. That is the reverse of
// the order they were taken in, which is what the member order -- regions last,
// the context first -- gets right.
RdmaResource::~RdmaResource() noexcept = default;

bool RdmaResource::Serves(const ::rdma_cm_id& id) const noexcept {
    return context_ != nullptr && id.verbs == context_;
}

bool RdmaResource::Admits(const ::ibv_mr& region) const noexcept {
    return protection_domain_ != nullptr && region.pd == protection_domain_.get();
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
