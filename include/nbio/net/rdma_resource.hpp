#pragma once

#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>

// Only ever taken by reference: what an id carries is the device, and the device
// is what is compared.
struct rdma_cm_id;

namespace nbio::net {
/**
 * @brief The device, its protection domain, and the two memory regions every
 *        connection built from it uses.
 *
 * @details This class is a registrant, not an owner. The send and receive spans
 * are the caller's memory; they are registered once, and neither this object nor
 * anything built from it ever allocates, carves, or frees a byte of them. The
 * caller must keep both spans alive and unregistered-free for as long as this
 * object lives, and must keep this object alive for as long as any connection
 * built from it lives.
 *
 * A connection may post any subspan of either region. The region is an arena and
 * the transport never learns how the caller carves it up.
 *
 * This class is designed to be used in a single thread.
 */
class RdmaResource {
   public:
    /**
     * @brief Opens a device and registers the two caller-provided regions.
     *
     * @param device_name The RDMA device to open, for example @c siw0.
     * @param send_memory_region The memory the device reads when sending. Must be
     *        non-empty and its start must be page aligned; its length may be any
     *        size, because a registration covers the pages the range touches.
     * @param receive_memory_region The memory the device writes when receiving.
     *        The same: page-aligned start, any length.
     *
     * @throws std::runtime_error if the device is missing, the domain cannot be
     *         allocated, or either region cannot be registered.
     * @throws std::invalid_argument if a region is empty or does not start on a
     *         page boundary.
     *
     * @note The two regions are registered with the access a two-sided transfer
     *       needs: local read for the send region (implicit), local write for the
     *       receive region. A one-sided operation needs a region registered with
     *       @c kRemoteRead or @c kRemoteWrite instead, which is why this
     *       constructor is not the only way to obtain a registration.
     */
    RdmaResource(std::string_view device_name, std::span<const char> send_memory_region,
                 std::span<char> receive_memory_region);

    /**
     * @brief Deregisters both regions, then deallocates the protection domain.
     *
     * @note The device context is borrowed from the communication manager and is
     *       never closed here.
     */
    ~RdmaResource() noexcept;

    RdmaResource(const RdmaResource&) = delete;
    RdmaResource& operator=(const RdmaResource&) = delete;

    /** @brief The context every connection on this resource is built on. */
    ::ibv_context* context() const noexcept { return context_; }

    /** @brief The domain every region here is registered on. */
    ::ibv_pd* protection_domain() const noexcept { return protection_domain_.get(); }

    /** @brief The device name this resource was opened with. */
    std::string_view name() const noexcept { return name_; }

    /** @brief The registered send region, as registered (not as requested). */
    std::span<const char> send_memory_region() const {
        return {static_cast<const char*>(send_memory_region_->addr), send_memory_region_->length};
    }

    /** @brief The registered receive region, as registered (not as requested). */
    std::span<char> receive_memory_region() const {
        return {static_cast<char*>(receive_memory_region_->addr), receive_memory_region_->length};
    }

    /**
     * @brief The send region as writable memory.
     *
     * @details The device only reads the send region, which is why the span above is
     * const; the CPU still has to fill it before a post, so this is the same
     * registration seen mutably.
     */
    std::span<char> send_memory() const {
        return {static_cast<char*>(send_memory_region_->addr), send_memory_region_->length};
    }

    /** @brief The local key a send descriptor must carry. */
    std::uint32_t send_lkey() const noexcept { return send_memory_region_->lkey; }

    /** @brief The local key a receive descriptor must carry. */
    std::uint32_t receive_lkey() const noexcept { return receive_memory_region_->lkey; }

    /** @brief The remote key a peer needs to read the send region one-sidedly. */
    std::uint32_t send_rkey() const noexcept { return send_memory_region_->rkey; }

    /** @brief The remote key a peer needs to write the receive region one-sidedly. */
    std::uint32_t receive_rkey() const noexcept { return receive_memory_region_->rkey; }

    /**
     * @brief Whether a connection rdma_cm resolved for @p id would run on the
     *        device this resource holds.
     *
     * @details Every id on one device carries the same context, so this is what
     * says the id's queue pair would be built on the context this resource's
     * domain and regions were made on. A connection built with another device's
     * regions and keys fails every completion.
     */
    bool Serves(const ::rdma_cm_id& id) const noexcept;

    /**
     * @brief Whether a registration belongs to this resource's domain.
     *
     * @details An @c lkey from another domain fails every completion, and no type
     * can say so, so a caller-supplied pool can be checked here once rather than
     * paying for it on every post.
     */
    bool Admits(const ::ibv_mr& region) const noexcept;

    /**
     * @brief The granularity a registration is pinned at.
     *
     * @details 4096 is the x86 answer and a page can be larger, so anything that
     * allocates memory meant to be registered asks this rather than assuming.
     */
    static std::size_t page_size() noexcept;

   private:
    struct ProtectionDomainDeleter {
        void operator()(::ibv_pd* pd) const noexcept {
            if (pd) {
                ::ibv_dealloc_pd(pd);
            }
        }
    };
    struct MemoryRegionDeleter {
        void operator()(::ibv_mr* mr) const noexcept {
            if (mr) {
                ::ibv_dereg_mr(mr);
            }
        }
    };

    // Both are used in the member-initialiser list, which is why they are static
    // members rather than free functions: the deleter type is private.
    static ::ibv_context* FindDevice(std::string_view device_name);
    static std::unique_ptr<::ibv_pd, ProtectionDomainDeleter> AllocateProtectionDomain(::ibv_context* context,
                                                                                      std::string_view device_name);

    // Borrowed from the communication manager, never closed here: it is the
    // context every id on this device carries, so it is the one the queue pairs
    // are built on, and the manager that owns it outlives every connection.
    ::ibv_context* context_{nullptr};
    std::unique_ptr<::ibv_pd, ProtectionDomainDeleter> protection_domain_;
    std::string name_;
    // Declared last, so that they are deregistered before the domain they were
    // registered on.
    std::unique_ptr<::ibv_mr, MemoryRegionDeleter> send_memory_region_;
    std::unique_ptr<::ibv_mr, MemoryRegionDeleter> receive_memory_region_;
};
}  // namespace nbio::net