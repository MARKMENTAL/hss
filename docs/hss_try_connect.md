# hss_try_connect

**Source:** `hss-probe.h:18`

Attempts a non-blocking TCP connect to a host:port and reports whether
the connection succeeded within a timeout.

## Signature

```c
static int hss_try_connect(const char *host, int port);
```

Returns `1` if a listener is detected, `0` otherwise.

## Why non-blocking?

A blocking `connect()` to an unresponsive host can hang for minutes
(TCP retransmission timeout). Since `hss` probes many candidate ports,
a single hung connect would stall the entire scan. Non-blocking mode
returns immediately with `EINPROGRESS`, and `select()` lets us wait with
a bounded timeout.

## The pattern

```c
int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
```

1. Create a TCP socket.
2. Set `O_NONBLOCK` via `fcntl`. From this point, `connect()` returns
   immediately instead of blocking.

```c
if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
    status = 1;  // connected immediately (localhost common case)
}
else if (errno == EINPROGRESS) {
    fd_set fdset; FD_ZERO(&fdset); FD_SET(sock, &fdset);
    struct timeval tv = {TIMEOUT_SEC, 0};
    if (select(sock + 1, NULL, &fdset, NULL, &tv) > 0) {
        int err = 0; socklen_t l = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &l);
        status = !err;
    }
}
```

Two outcomes:

- **`connect()` returns 0** — connection succeeded immediately. Common
  for localhost listeners where the handshake completes before the
  kernel returns to userspace.

- **`connect()` returns -1 with `EINPROGRESS`** — the handshake is
  in-flight. We `select()` on the socket for writeability with a
  2-second timeout. When the socket becomes writable, we check
  `SO_ERROR` via `getsockopt`: `0` means the handshake succeeded;
  anything else means the connection was refused or timed out.

```c
close(sock);
freeaddrinfo(res);
return status;
```

The socket is always closed and the `addrinfo` always freed, regardless
of outcome.

## POSIX portability

`O_NONBLOCK`, `fcntl`, `select`, `getsockopt`, and `connect` are all
POSIX-standard interfaces. They are **not** Linux-specific. This is the
reason `hss` compiles and runs on GNU/Hurd — the entire network probing
layer uses only portable APIs. The Hurd-specific code is isolated to
`hss-proc.h` (Mach IPC) and `hss.c` (the `getproc()` call).

## Timeout

`TIMEOUT_SEC` is defined as `2` (`hss-probe.h:16`). Two seconds is long
enough for remote hosts on a LAN and short enough that scanning 200+
ports completes in reasonable time. The timeout is per-port, not global.
