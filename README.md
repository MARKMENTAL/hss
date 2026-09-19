# hss - Hurd Socket Stat

> A practical, heuristics-based socket table emulator for GNU/Hurd that mimics modern Linux `ss` and `netstat` behavior.

## Architectural Motivation

On monolithic kernels like Linux, `ss` queries a centralized kernel-space socket array via Netlink (`sock_diag`). GNU/Hurd has no monolithic kernel — the network stack and tasks are isolated user-space translators. A single global "socket-to-PID" lookup table does not exist by design. `hss` emulates this missing table from user space.

## Features

- **Non-blocking heuristics**: Lightning-fast connection scans using `O_NONBLOCK` and `select()` across interfaces
- **Low-level Mach/Hurd inspection**: Crawls the Mach process server using `proc_getallpids` and `proc_getprocargs`
- **Zero memory leaks**: Explicit `vm_deallocate` for all Mach page memory
- **Fast runtime**: Resolves the entire user-space distributed map in ~0.320 seconds
- **Dual compiler support**: Compiles with both `tcc` (rapid development) and `gcc` (production)

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

## Debian Package Target

Planned for official inclusion with a standard `debian/` directory targeting `Architecture: hurd-any`.

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

## Author

Mark Robillard Jr (MARKMENTAL)
