#pragma once

#include <nbio/net/address.hpp>
#include <nbio/net/payload.hpp>

// RDMA, in two layers. The services are the interface to build against: every wait
// they hand back is a Task, and every blocking step is a park on the multiplexer.
#include <nbio/net/rdma_connect_service.hpp>
#include <nbio/net/rdma_deliver_service.hpp>
#include <nbio/net/rdma_header.hpp>
#include <nbio/net/rdma_listen_service.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_session_service.hpp>

// The data plane: a prepared communication, and the channel adapters that attach
// it to the multiplexer.
#include <nbio/net/rdma_communication.hpp>
#include <nbio/net/rdma_connect_channel.hpp>
#include <nbio/net/rdma_establish_channel.hpp>
#include <nbio/net/rdma_listen_channel.hpp>
#include <nbio/net/rdma_receive_channel.hpp>
#include <nbio/net/rdma_send_channel.hpp>

// The control plane -- the primitives, which are infrastructure: they wait with a
// local poll(2) and are exposed only for callers that want direct control. See
// docs/rdma.md.
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_listener.hpp>
#include <nbio/net/rdma_resource.hpp>

#include <nbio/net/result.hpp>
#include <nbio/net/tcp_listen_channel.hpp>
#include <nbio/net/tcp_listen_service.hpp>
#include <nbio/net/tcp_listener.hpp>
#include <nbio/net/tcp_connect_channel.hpp>
#include <nbio/net/tcp_connect_service.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/net/tcp_receive_channel.hpp>
#include <nbio/net/tcp_send_channel.hpp>
#include <nbio/net/tcp_session_service.hpp>
#include <nbio/net/socket.hpp>
