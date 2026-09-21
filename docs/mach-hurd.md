# Mach / Hurd Concepts in hss

`hss` talks to the GNU/Hurd process server using Mach IPC. This document
explains the Hurd-specific primitives that appear throughout the code.

## The proc server is a userspace translator

On Linux, process enumeration reads from the kernel's `/proc` filesystem.
On Hurd, there is no monolithic kernel — the **process server** is a
userspace program (a *translator*) that manages process state. To ask it
questions, you send it **Mach IPC messages**, not syscalls.

```c
process_t proc_server = getproc();  // returns a Mach port handle
```

`getproc()` is a libc helper that returns a **Mach port** — a
kernel-managed communication channel — to the proc server. Think of it as
a file descriptor, but for IPC instead of I/O.

## Ports and port rights

A **Mach port** is a unidirectional communication endpoint. The proc
server owns one end; your task owns the other. Your task holds **receive
rights** (to receive messages) and/or **send rights** (to send messages).
`getproc()` gives you send rights to the proc server.

When the function signature says `process_t`, that's a typedef for a Mach
port name (an integer the kernel uses to look up the port in your task's
port namespace).

## IPC and out-of-line memory

When you call `proc_getallpids(proc, &pids, &num_pids)`, the proc server
sends back an array of PIDs. Mach delivers this as **out-of-line data**:
the server maps fresh pages into your task's address space and tells the
kernel to send the port-name and a pointer to those pages.

This means **your task now owns those pages** and must free them:

```c
vm_deallocate(mach_task_self(), (vm_address_t)pids, num_pids * sizeof(pid_t));
```

`vm_deallocate` is the Mach equivalent of `free()`, but for entire virtual
memory regions. The "zero memory leaks" claim in the README comes from
every out-of-line buffer being explicitly deallocated — there is no
garbage collection.

`mach_task_self()` returns your task's own port (needed as the target for
the deallocate operation).

## The RPC calls

| Call | What it does | Why it's an RPC |
|------|--------------|-----------------|
| `getproc()` | Get a port handle to the proc server | Looks up the proc server in the port namespace |
| `proc_getallpids(proc, &pids, &num_pids)` | Get all running PIDs | Sends a message to the proc server asking for its PID list |
| `proc_getprocargs(proc, pid, &args, &args_len)` | Get a process's argv | Sends a message asking for that process's command-line arguments |

Each of these returns `KERN_SUCCESS` (0) on success. The `hss_find_pids`
function checks for failure and falls back to `"users:(("unknown",pid=0))"`.

## Why this matters

The Mach IPC model is why `hss` can exist at all: on Linux, `ss` asks the
kernel for a centralized socket table via netlink. On Hurd, no such table
exists by design — sockets live in per-process pfinet translators. The
proc server is the closest thing Hurd has to a global process view, and
it's accessible to any task with the right port rights. `hss` leverages
this to build a heuristic socket table from userspace.
