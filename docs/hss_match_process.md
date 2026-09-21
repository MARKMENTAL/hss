# hss_match_process / hss_match_port_token

**Source:** `hss-proc.h:12` and `hss-proc.h:19`

Heuristically determines whether a process's command line suggests it
owns a given port.

## hss_match_port_token

```c
static int hss_match_port_token(const char *args, const char *needle);
```

Checks whether `needle` (the port number as a string) appears in
`args` as a **whole token**, not as a substring of a larger number.

```c
size_t n = strlen(needle);
for (const char *p = args; (p = strstr(p, needle)); p++)
    if ((p == args || p[-1] < '0' || p[-1] > '9') &&
        (p[n] < '0' || p[n] > '9')) return 1;
return 0;
```

The boundary check: the character before and after the match must both
be non-digit (or the match is at the start/end of the string). This
prevents `8000` from matching inside `18000` or `80001`.

## hss_match_process

```c
static int hss_match_process(int port, const char *args, const char *needle);
```

Two-tier matching strategy:

### Tier 1: Known services

Hard-coded associations between well-known services and their ports:

| Service | Port(s) |
|---------|---------|
| `sshd` | 22 |
| `httpd`, `apache`, `nginx`, `lighttpd` | 80, 443 |
| `ftp` | 21 |
| `postgres` | 5432 |
| `redis` | 6379 |
| `mysql`, `mariadb` | 3306 |

If the command line contains the service name **and** the port matches,
it's a direct hit. No token-boundary check needed — `sshd` on port 22
is unambiguous.

### Tier 2: Interpreters

```c
return (strstr(args, "python") || strstr(args, "node") ||
        strstr(args, "java") || strstr(args, "ruby")) &&
       hss_match_port_token(args, needle);
```

For interpreted languages, the port number typically appears in the
command line (e.g., `python3 -m http.server 8000`, `node app.js 3000`).
The match requires **both**:

1. An interpreter name in the command line.
2. The port number as a whole token (via `hss_match_port_token`).

This avoids false positives — a `java` process whose command line
happens to contain `8000` as part of a heap size (`-Xmx8000m`) won't
match, because `8000` is adjacent to other digits.

## Limitations

- Only matches processes whose command line is visible via
  `proc_getprocargs` (all processes, but the args may be truncated or
  unavailable for kernel-internal tasks).
- Cannot detect a process that listens on a port without mentioning it
  on the command line (e.g., a config-file-only port binding).
- The known-service table is static; new services require a code change.

This is the "heuristic" tradeoff: fast and dependency-free, but not
exhaustive.
