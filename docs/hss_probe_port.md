# hss_probe_port

**Source:** `hss-probe.h:43`

Probes a single port and, if a listener is found, formats the local
address string — including multi-address display for localhost.

## Signature

```c
static int hss_probe_port(const char *host, int port, char *out, size_t len,
                          char (*ips)[INET_ADDRSTRLEN], int nips);
```

| Parameter | Description |
|-----------|-------------|
| `host` | Target host (e.g., `"localhost"`, `"192.168.1.100"`) |
| `port` | Port number to probe |
| `out` | Output buffer for the formatted `"address:port"` string |
| `len` | Size of `out` |
| `ips` | Array of the machine's ethernet IPs (from `hss_get_eth_ips`) |
| `nips` | Number of entries in `ips` |

Returns `1` if a listener is detected, `0` otherwise.

## Initial probe

```c
if (!hss_try_connect(host, port)) return 0;
```

First, confirm something is listening at `host:port`. If not, return
immediately — no address formatting needed.

## Localhost multi-address display

```c
int local = !strcmp(host, "localhost") || !strcmp(host, "127.0.0.1");
if (local && nips > 0) {
    size_t off = 0;
    int shown = 0;
    for (int i = 0; i < nips && shown < 3; i++)
        if (hss_try_connect(ips[i], port))
            off += snprintf(out + off, len - off, "%s%s:%d",
                            shown++ ? ", " : "", ips[i], port);
    if (shown) {
        if (nips > 3) snprintf(out + off, len - off, ", +more");
        return 1;
    }
}
```

When the target is `localhost` or `127.0.0.1`, a listener may be bound
to `0.0.0.0` (all interfaces) or to specific IPs. To replicate `ss`'s
per-interface display, `hss` cross-checks the listener against each of
the machine's ethernet IPs (obtained earlier via `hss_get_eth_ips`).

- Probes each ethernet IP at the same port.
- Lists up to 3 matching addresses, comma-separated.
- If more than 3 match, appends `", +more"`.

This produces output like:

```
192.168.86.42:22, 10.0.0.5:22, +more
```

## Remote / fallback

```c
snprintf(out, len, "%s:%d", host, port);
return 1;
```

For non-localhost targets, or localhost listeners that don't match any
specific ethernet IP (e.g., bound only to `127.0.0.1`), the output is
simply `host:port`.

## Why ethernet IPs matter

On Linux, `ss` reads bound interface addresses from the kernel. On Hurd,
there is no centralized table to query. `hss` enumerates the machine's
active ethernet interfaces via `getifaddrs` and tests each one, building
the same multi-address view from userspace.
