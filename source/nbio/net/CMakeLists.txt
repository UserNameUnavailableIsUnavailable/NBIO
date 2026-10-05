cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    address.cpp
    tcp_socket.cpp
    tcp_acceptor.cpp
    tcp_connector.cpp
    tcp_accept_channel.cpp
    tcp_accept_service.cpp
    tcp_connect_channel.cpp
    tcp_connect_service.cpp
    tcp_receive_channel.cpp
    tcp_send_channel.cpp
    tcp_session_service.cpp
)

if(NBIO_ENABLE_RDMA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RDMACM_PC REQUIRED IMPORTED_TARGET librdmacm)
    pkg_check_modules(IBVERBS_PC REQUIRED IMPORTED_TARGET libibverbs)
    target_sources(
        nbio
        PRIVATE
        rdma_resource_manager.cpp
        rdma_connector.cpp
        rdma_acceptor.cpp
        rdma_accept_channel.cpp
        rdma_accept_service.cpp
        rdma_connect_channel.cpp
        rdma_connect_service.cpp
        rdma_deliver_service.cpp
        rdma_receive_channel.cpp
        rdma_send_channel.cpp
        rdma_session_service.cpp
    )
    target_link_libraries(nbio PUBLIC PkgConfig::RDMACM_PC PkgConfig::IBVERBS_PC)
endif()