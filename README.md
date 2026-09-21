# hss - Hurd Socket Stat

> A practical, heuristics-based socket table emulator for GNU/Hurd that mimics modern Linux `ss` and `netstat` behavior.

## Architectural Motivation

On monolithic kernels like Linux, `ss` queries a centralized kernel-space socket array via Netlink (`sock_diag`). GNU/Hurd has no monolithic kernel — the network stack and tasks are isolated user-space translators. A single global "socket-to-PID" lookup table does not exist by design. `hss` emulates this missing table from user space.

## Features

- **Non-blocking heuristics**: Lightning-fast connection scans using `O_NONBLOCK` and `select()` across interfaces
- **Low-level Mach/Hurd inspection**: Crawls the Mach process server using `proc_getallpids` and `proc_getprocargs`
- **Zero memory leaks**: Explicit `vm_deallocate` for all Mach page memory
- **Fast runtime**: Resolves the entire user-space distributed map in ~0.320 seconds
- **Compact design**: Full functionality in ~210 lines of C
- **Dual compiler support**: Compiles with both `tcc` (rapid development) and `gcc` (production)

## Design

The entire implementation fits in ~210 lines of C, built around a simple pipeline:

1. **Gather** — Candidate ports are collected from three sources in priority order:
   common services from `/etc/services` (ports 1–1023 plus curated modern ports),
   a built-in modern-port list, and optional user ports from `~/.config/.hssrc`
   (one port per line).
2. **Deduplicate & sort** — A single `add_entry()` helper inserts each candidate in
   sorted order while skipping duplicates — no separate merge or sort passes.
3. **Probe** — Each candidate is tested with a non-blocking `connect()` and a
   2-second `select()` timeout. Localhost listeners are cross-checked against the
   machine's ethernet IPs for an `ss`-style multi-address display.
4. **Resolve** — Owning processes are identified by crawling the Mach proc server
   (`proc_getallpids` / `proc_getprocargs`) and heuristically matching command
   lines to ports.

## Documentation

Detailed function breakdowns, Mach/Hurd concepts, and code explanations are
in [`docs/INDEX.md`](docs/INDEX.md).

## Sample Output

```
Netid    State        Local Address:Port             Process
tcp      LISTEN       192.168.86.42:22               users:(("sshd-session",pid=13635),...)
tcp      LISTEN       localhost:8000                 users:(("python3",pid=14434))
```

## Usage

```bash
hss [target] [options]
```

### Options

| Option | Description |
|--------|-------------|
| `target` | Host to scan (default: localhost) |
| `--all-processes` | Show all processes for each port |
| `--process-limit=N` | Limit processes shown per port (default: 3) |

Add extra ports to scan by listing them in `~/.config/.hssrc`, one per line.

### Examples

```bash
# Scan localhost with default settings
hss

# Scan remote host
hss 192.168.1.100

# Show all processes
hss localhost --all-processes

# Limit to 5 processes per port
hss localhost --process-limit=5
```

## Compilation & Build

### Dependencies

- `libc6-dev` (standard C library)
- `hurd-dev` (Hurd system headers)
- `gcc` or `tcc` (compiler)

### GCC (Production)
```bash
gcc -Wall -Wextra -O2 -o hss hss.c
```

### TCC (Development)
```bash
tcc -run hss.c
```

## Debian Package

A standard `debian/` directory is included, targeting `Architecture: hurd-any`.

### Build Dependencies (on Debian GNU/Hurd)

```bash
sudo apt install build-essential hurd-dev dpkg-dev debhelper
```

### Building

```bash
./build.sh
```

This builds both source and binary packages via `dpkg-buildpackage -us -uc`
and collects the resulting artifacts (`.deb`, `.buildinfo`, `.changes`) into
`dist/`.

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

## Author

Mark Robillard Jr (MARKMENTAL)
