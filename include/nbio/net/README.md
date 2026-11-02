ControlPlane:

- RdmaConnector(RdmaResource& rdma_resource)
  - Bind(Address source) -> void | error_code # binds to a local address:port
  - Resolve(Address target, int timeout_ms) -> void | error_code # resolves address and route for the RDMA connection
  - Connect(int timeout_ms) -> RdmaCommunication | error_code # resolves address and route for the RDMA connection, then allocates required resources, but does not finish the connection by rdma_connect
- RdmaListener(RdmaResource& rdma_resource)
  - Bind(Address address) -> void | error_code # binds to a local address:port
  - Listen(int backlog) -> void | error_code # starts listening for incoming connections with the specified backlog
  - Accept() -> RdmaCommunication | error_code # waits for an incoming connection and returns a communication object
  
Data Plane:

- RdmaCommunication
  - Establish() -> void | error_code # calls rdma_connect | rdma_accept to finish the connection
  - SubmitSend(span<const char> data) -> RdmaSeq | error_code
  - SubmitReceive(span<char> buffer) -> RdmaSeq | error_code
  - WaitSend(RdmaSeq seq, int timeout_ms) -> RdmaSeq | error_code
  - WaitReceive(RdmaSeq seq, int timeout_ms) -> RdmaSeq | error_code
  - PeekSend(RdmaSeq seq) -> RdmaSendResult | error_code
  - PeekReceive(RdmaSeq seq) -> RdmaReceiveResult | error_code
  - CompleteSend(RdmaSeq seq) -> void
  - CompleteReceive(RdmaSeq seq) -> void
  - Reset() -> void

The above classes do not support asynchronous operations directly. They are merely a thin wrapper of RDMA APIs.

Channel classes provides asynchronous interfaces for RDMA operations.

Channels:

- RdmaListenChannel(listener, multiplexer, scheduler)
  - Accept() -> Task<RdmaCommunication | error_code> # NOTE: it does not call RdmaListener::Accept()
- RdmaConnectChannel(connector, multiplexer, scheduler)
  - Connect() -> Task<RdmaCommunication | error_code> # NOTE: it does not call RdmaConnector::Connect()
- RdmaEstablishChannel(communication, multiplexer, scheduler) # built from a communication object
  - Establish() -> Task<void | error_code> # NOTE: it does not call RdmaCommunication::Establish()
- RdmaSendChannel(communication, multiplexer, scheduler) # built from a communication object
  - SubmitSend(span<const char> data) -> RdmaSeq | error_code
  - WaitSend(RdmaSeq seq) -> Task<RdmaSeq | error_code>
  - PeekSend(RdmaSeq seq) -> RdmaSendResult | error_code
  - CompleteSend(RdmaSeq seq) -> void
- RdmaReceiveChannel(communication, multiplexer, scheduler) # built from a communication object
  - SubmitReceive(span<char> buffer) -> RdmaSeq | error_code
  - WaitReceive(RdmaSeq seq) -> Task<RdmaSeq | error_code>
  - PeekReceive(RdmaSeq seq) -> RdmaReceiveResult | error_code
  - CompleteReceive(RdmaSeq seq) -> void

The connection establishment has three stages:

- Prepare stage: the receive & send work queues are created, and settles everything needed for rdma_connect() or rdma_accept().
- Submit stage: one can submit operations. This avoids packets arrived without corresponding work requests being posted.
- Complete stage: the connection is fully established after rdma_connect() or rdma_accept() completes successfully.

On top of channels, we build services:

- RdmaListenService(RdmaResource& res): it holds listener and RdmaListenChannel.
  - Bind: mirror of RdmaListener::Bind
  - Listen: mirror of RdmaListener::Listen
  - Accept: mirror of RdmaListenChannel::Accept
- RdmaConnectService(RdmaResource& res, Address address): it holds RdmaConnectChannel.
  - Bind: mirror of RdmaConnector::Bind
  - Connect: mirror of RdmaConnectChannel::Connect
- RdmaSessionService(RdmaCommunication communication): it holds RdmaSendChannel, RdmaReceiveChannel, and RdmaEstablishChannel.
  - Establish: mirror of RdmaEstablishChannel::Establish
  - SubmitSend: mirror of RdmaSendChannel::SubmitSend
  - WaitSend: mirror of RdmaSendChannel::WaitSend
  - PeekSend: mirror of RdmaSendChannel::PeekSend
  - CompleteSend: mirror of RdmaSendChannel::CompleteSend
  - SubmitReceive: mirror of RdmaReceiveChannel::SubmitReceive
  - WaitReceive: mirror of RdmaReceiveChannel::WaitReceive
  - PeekReceive: mirror of RdmaReceiveChannel::PeekReceive
  - CompleteReceive: mirror of RdmaReceiveChannel::CompleteReceive