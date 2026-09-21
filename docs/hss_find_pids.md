# hss_find_pids

**Source:** `hss-proc.h:27`

> **Note:** This function only runs when the target is local (`localhost`,
> `127.0.0.1`, or one of the machine's own IPs). For remote targets, `hss`
> skips process lookup entirely and reports `users:(("n/a",pid=0))`.

Finds the process(es) that own a given listening port, using heuristic
command-line matching against the Mach proc server.

## Signature

```c
static void hss_find_pids(process_t proc, int port, char *out, size_t len, int max_procs);
```

| Parameter | Description |
|-----------|-------------|
| `proc` | Mach port handle to the process server (from `getproc()`) |
| `port` | The port number to match against |
| `out` | Output buffer for the formatted result string |
| `len` | Size of `out` |
| `max_procs` | Max processes to list; `0` means unlimited |

## Phase 1: Setup

```c
char buf[256] = "", needle[16];
snprintf(needle, sizeof(needle), "%d", port);
```

Builds the search token — `port` formatted as a string (e.g., `"80"`).
Passed to the matcher to check whether a process's command line mentions
this port.

## Phase 2: Get all PIDs from the proc server

```c
if (proc_getallpids(proc, &pids, &num_pids) != 0) {
    snprintf(out, len, "users:((\"unknown\",pid=0))");
    return;
}
```

A Mach RPC to the proc server. `pids` comes back as an **out-of-line**
array — fresh pages mapped into this task by the IPC. Failure falls back
to `ss`'s `"users:(("unknown",pid=0))"` placeholder format.

## Phase 3: Loop over PIDs, fetch and flatten argv

```c
for (size_t i = 0; i < num_pids; i++) {
    char *args = NULL;
    mach_msg_type_number_t args_len = 0;
    if (proc_getprocargs(proc, pids[i], &args, &args_len) != 0 || !args || !args_len) continue;
    for (size_t j = 0; j < args_len; j++) if (!args[j]) args[j] = ' ';
```

For each PID, an RPC fetches its argv. The returned buffer contains
NUL-separated argument strings (`python3\0-m\0http.server\08000\0`). The
inner loop **flattens** it — replaces every NUL with a space — so the
entire command line becomes one searchable string:
`python3 -m http.server 8000`. Now `strstr` can scan it naively.

Each iteration also `vm_deallocate`s its own `args` buffer (Phase 5).

## Phase 4: Heuristic match

```c
if (hss_match_process(port, args, needle)) {
```

Delegated to `hss_match_process` (`hss-proc.h:19`), which encodes two
strategies:

- **Known services** — `sshd`→22, `httpd`/`apache`/`nginx`/`lighttpd`→80/443,
  `postgres`→5432, `mysql`/`mariadb`→3306, `redis`→6379, `ftp`→21
- **Interpreters** — `python`/`node`/`java`/`ruby` **and** the port
  appears as a *whole token* in the command line (the
  `hss_match_port_token` boundary check prevents `8000` from matching
  inside `18000`)

This is the "heuristic" in *Heuristic Socket Stat* — best-effort
attribution, not a socket table lookup.

## Phase 5: Limit, extract basename, accumulate output

```c
if (max_procs > 0 && matches >= max_procs) {
    snprintf(buf + off, sizeof(buf) - off, ",+more");
    vm_deallocate(mach_task_self(), (vm_address_t)args, args_len);
    break;
}
```

Respects `--process-limit` / `--all-processes`. Note the `vm_deallocate`
before `break` — leaking Mach pages would accumulate fast in a tool that
RPCs once per PID. This is the "zero memory leaks" guarantee.

```c
char *base = strrchr(args, '/'), *space;
base = base ? base + 1 : args;
if ((space = strchr(base, ' '))) *space = '\0';
off += snprintf(buf + off, sizeof(buf) - off,
                "%s(\"%.40s\",pid=%d)", matches ? "," : "", base, pids[i]);
```

Extracts the **basename** of the executable (`/usr/sbin/sshd` → `sshd`),
truncates at the first space to drop arguments, and appends
`("name",pid=N)` to the accumulating buffer — exactly `ss`'s
`users:(("name",pid=N))` format.

## Phase 6: Cleanup and final output

```c
if (pids && num_pids)
    vm_deallocate(mach_task_self(), (vm_address_t)pids, num_pids * sizeof(pid_t));
if (matches)
    snprintf(out, len, "users:(%s)", buf);
else
    snprintf(out, len, "users:((\"unknown\",pid=0))");
```

Frees the PID array (Mach out-of-line pages again), then emits either
the collected matches or the unknown placeholder.

## Output format

```
users:(("sshd-session",pid=13635),("python3",pid=14434))
```

Mirrors `ss -p` output so Hurd admins can use the same mental model they
know from Linux.
