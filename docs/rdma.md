# NBIO RDMA: two systems, one connection

This document describes the design as built. It replaces the earlier proposal
that lived in `rdma-memory-design.md`.

## 1. The split

NBIO's Channel system and the RDMA primitives are **different systems** that share
the same objects at the bottom. Know which one you are programming against.

| | Primitives | Channel system |
| --- | --- | --- |
| Objects | `RdmaResource`, `RdmaListener`, `RdmaConnector` | `RdmaSendChannel`, `RdmaReceiveChannel`, `RdmaListenChannel`, `RdmaConnectChannel`, and the services built on them |
| Waits by | a local `poll(2)` on its own event or completion fd | parking the coroutine and registering the fd with the multiplexer |
| Blocking | by default; `NonBlocking()` makes the call return empty-handed | never; `Complete()` is called by the backend |
| Thread model | whatever thread calls it | the scheduler thread |
| Audience | infrastructure, and callers who want direct control | applications |

The two cooperate rather than nest: a channel is registered on the *primitive's*
fd and only ever peeks (`poll(fd, 0)`) once the multiplexer has woken it. No fd
mode is ever changed, which is why the same descriptor can be used by a blocking
caller and by epoll at the same time.

## 2. What to program against

**Recommended: the services.**

```cpp
nbio::net::RdmaListenService     // accept prepared sessions
nbio::net::RdmaConnectService    // target a peer, get a prepared session
nbio::net::RdmaSessionService    // establish, then post and wait
nbio::net::RdmaDeliverService    // a windowed, acknowledged byte stream over a session
```

They are coroutine-native: every wait is a `Task`, and every blocking step is a
park on the multiplexer.

**Direct control: the primitives.** `RdmaListener` and `RdmaConnector` are
reserved as basic infrastructure and are not the recommended interface. They are
still public, because some callers genuinely want them:

- an application with its own thread and its own event loop;
- a probe or a benchmark that drives one queue pair synchronously;
- a new service being written on top of them.

They wait with a local `poll(2)`, so they must never be driven from the scheduler
thread while the loop is expected to keep running.

## 3. Memory

- `RdmaResource` holds **views** of two caller-owned regions and registers them.
  It allocates nothing, frees nothing, and carves nothing.
- Only the **start** of a region must be page aligned; the length may be any size,
  because a registration covers the pages the range touches. This is what lets an
  `mmap`ed file be registered and sent without a copy.
- Every posted buffer must lie inside its region. That range check is the only
  validation a post needs: a descriptor carries one `lkey`, so a buffer inside the
  region needs no further proof.
- A buffer must not be touched by the CPU until its completion has been reaped,
  and must outlive the request.

## 4. Completions: sequence numbers, not cursors

Each direction numbers its work requests from one.

| Call | Answers |
| --- | --- |
| `PostSend` / `PostReceive` | the sequence number assigned |
| `WaitSend(seq, timeout)` / `WaitReceive(seq, timeout)` | the highest sequence number reaped; a timeout returns the high-water mark, not an error |
| `FetchSend(seq)` / `FetchReceive(seq)` | the buffer, and for a receive the bytes written; `kNotReady` until reaped; may be asked repeatedly |
| `DropSend(seq)` / `DropReceive(seq)` | releases the slot |

Slots are indexed by `seq % depth`, so a completion is one array index. The head
advances only over slots that are dropped **and** no longer in flight — advancing
over an in-flight slot would let a late completion land on a new occupant.
Releasing out of order therefore does not free capacity until the oldest live
sequence number is released; a producer that never drops sees `kQueueFull` with
the hardware queue empty.

`Fetch` is a read and `Drop` is a release, which is why the connector is
deliberately forgiving and the channels are not: a channel's `Wait` drops, so a
second wait for the same sequence number fails with `kInvalidSequence`.

## 5. Connection lifecycle

```
             BindSource(local)            BindTarget(peer)            Establish()
active:      rdma_bind_addr      ->      resolve addr + route  ->    rdma_connect
                                         build QP                   + ESTABLISHED

passive:     listener Accept()   ->      QP already built      ->    rdma_accept
             (request in hand)           (post receives here)       + ESTABLISHED
```

The split exists because **`rdma_accept` / `rdma_connect` is what lets the peer
send**. Everything needed to post a receive exists one step earlier, so a receiver
posts between the two and no packet can arrive unannounced. The same gap is where
a server decides to refuse: letting a prepared connector go rejects the request.

An active connector has no destination until `BindTarget` has been called;
`Establish` answers `kNoTarget` rather than waiting for a connection that can
never be made.

## 6. Where policy lives

The transport has none. Pools, windows, slicing, acknowledgement — all of it is
above:

- `RdmaDeliverService` carves the session's regions into packet-sized slots, keeps
  its own window, and refuses a layout the connection cannot honour. An
  acknowledgement means "the slot is posted again", not "the bytes went past".
- A caller that wants no window at all posts to the session directly and sends
  from its own mapping.

## 7. Waiting, precisely

- A primitive's blocking call is `poll(fd, -1)` then `rdma_get_cm_event`, so it is
  the caller's thread that sleeps.
- A primitive's non-blocking call is `poll(fd, 0)`: it takes what has arrived and
  answers `kNotReady` otherwise.
- A channel never polls to wait. It parks its coroutine, arms the channel, and the
  multiplexer calls `Complete()` when the fd is readable; `Complete()` takes at
  most one step and re-arms if the step was not the last.
