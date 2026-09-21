# hss Documentation

Detailed function breakdowns, Mach/Hurd concepts, and code explanations for the
`hss` (Hurd Socket Stat) utility.

## Architecture Overview

`hss` is built as a **two-tier design** that exploits the boundary between
POSIX portability and Hurd-specific APIs:

1. **Network probing layer** — uses only POSIX APIs (`socket`, `connect`,
   `select`, `getifaddrs`). This layer is portable to any POSIX system and
   is the reason `hss` compiles and runs on GNU/Hurd without Linux-specific
   dependencies.

2. **Process inspection layer** — uses Mach/Hurd APIs (`proc_getallpids`,
   `proc_getprocargs`, `vm_deallocate`, `getproc`). This layer is
   Hurd-specific because the Hurd process server is a userspace translator,
   not a kernel, so process enumeration happens via IPC, not syscalls.

The pipeline: gather candidate ports → deduplicate and sort → probe each
with non-blocking connect → for hits, crawl the proc server to attribute
ownership.

## Quick Reference

| File | Topic |
|------|-------|
| [`mach-hurd.md`](mach-hurd.md) | Mach IPC, port rights, the proc server, out-of-line memory, `vm_deallocate` |
| [`hss_try_connect.md`](hss_try_connect.md) | Non-blocking `connect()` + `select()` pattern, `EINPROGRESS` handling |
| [`hss_probe_port.md`](hss_probe_port.md) | Port probing with localhost multi-address display |
| [`hss_find_pids.md`](hss_find_pids.md) | PID attribution via Mach proc server, heuristic matching |
| [`hss_match_process.md`](hss_match_process.md) | Service recognition heuristics and port-token boundary matching |
| [`hss_add_entry.md`](hss_add_entry.md) | Sorted port-list insertion with deduplication |
| [`hss_parse_services.md`](hss_parse_services.md) | `/etc/services` and `~/.config/.hssrc` parsing |
| [`hss_get_eth_ips.md`](hss_get_eth_ips.md) | Ethernet interface IP enumeration |

## Source Layout

```
hss.c          — main(), argument parsing, output formatting
hss.h          — includes the three implementation headers
hss-probe.h    — network probing (try_connect, probe_port, get_eth_ips)
hss-proc.h     — Mach process inspection (find_pids, match_process)
hss-ports.h    — port list management (add_entry, parse_services, parse_config)
```
