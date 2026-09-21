# hss_get_eth_ips

**Source:** `hss-probe.h:62`

Enumerates the machine's active, non-loopback IPv4 addresses.

## Signature

```c
static int hss_get_eth_ips(char ips[][INET_ADDRSTRLEN], int max);
```

| Parameter | Description |
|-----------|-------------|
| `ips` | Output array of IP address strings (each `INET_ADDRSTRLEN` bytes) |
| `max` | Maximum number of IPs to return |

Returns the number of IPs found.

## Implementation

```c
struct ifaddrs *ifaddr, *ifa;
int count = 0;
if (getifaddrs(&ifaddr) == -1) return 0;
for (ifa = ifaddr; ifa && count < max; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET ||
        !(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
    inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
              ips[count++], INET_ADDRSTRLEN);
}
freeifaddrs(ifaddr);
return count;
```

1. `getifaddrs()` builds a linked list of all interfaces.
2. For each interface, keep only those that:
   - Have an address (`ifa_addr` is non-NULL).
   - Are IPv4 (`AF_INET`).
   - Are up (`IFF_UP`).
   - Are **not** the loopback interface (`IFF_LOOPBACK`).
3. Convert the binary address to a string with `inet_ntop`.
4. `freeifaddrs()` frees the list.

## Why this is needed

When `hss` probes `localhost`, a listener may be bound to `0.0.0.0`
(all interfaces) or to specific IPs. To replicate `ss`'s per-interface
"Local Address:Port" display, `hss` needs to know the machine's IPs so
it can test each one.

In `hss.c`, the IPs are fetched once and passed to every `hss_probe_port`
call:

```c
char eth_ips[10][INET_ADDRSTRLEN];
int num_eth_ips = hss_get_eth_ips(eth_ips, 10);
```

A limit of 10 is arbitrary but sufficient for typical machines.

## Why not Linux-specific

`getifaddrs`/`freeifaddrs` are POSIX-standard (from `ifaddrs.h`) and
work on any system with a libc that implements them, including GNU/Hurd.
The tool does not use netlink, `/proc/net/dev`, or any Linux-only
interface enumeration mechanism.
