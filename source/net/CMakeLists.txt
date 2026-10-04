cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    INTERFACE
    Address.cpp
    TcpSocket.cpp
    TcpAcceptor.cpp
    TcpConnector.cpp
    TcpAcceptChannel.cpp
    TcpAcceptService.cpp
    TcpConnectChannel.cpp
    TcpConnectService.cpp
    TcpReceiveChannel.cpp
    TcpSendChannel.cpp
    TcpSessionService.cpp
)

if(NBIO_ENABLE_RDMA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RDMACM_PC REQUIRED IMPORTED_TARGET librdmacm)
    pkg_check_modules(IBVERBS_PC REQUIRED IMPORTED_TARGET libibverbs)
    target_sources(
        nbio
        INTERFACE
        RdmaResourceManager.cpp
        RdmaConnector.cpp
        RdmaAcceptor.cpp
        RdmaAcceptChannel.cpp
        RdmaConnectChannel.cpp
        RdmaDeliverService.cpp
        RdmaReceiveChannel.cpp
        RdmaSendChannel.cpp
        RdmaSessionService.cpp
    )
    target_link_libraries(nbio INTERFACE PkgConfig::RDMACM_PC PkgConfig::IBVERBS_PC)
endif()