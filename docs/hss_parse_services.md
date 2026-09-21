# hss_parse_services / hss_parse_config

**Source:** `hss-ports.h:39` and `hss-ports.h:58`

Builds the candidate port list from `/etc/services` and the user's
`~/.config/.hssrc` config file.

## hss_parse_services

```c
static void hss_parse_services(struct hss_port_entry *e, int *n);
```

Reads `/etc/services`, the system-wide service database, and adds
matching ports to the list via `hss_add_entry`.

### Parsing

```c
FILE *f = fopen("/etc/services", "r");
while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    end = -1;
    if (sscanf(line, "%63s %d-%d/%15s", name, &start, &end, proto) < 4 &&
        scanf(line, "%63s %d/%15s", name, &start, proto) != 3) continue;
```

- Skip comments and blank lines.
- Accept two formats: `name start-end/proto` (range) and
  `name start/proto` (single port).
- If the first `sscanf` matches fewer than 4 fields, try the single-port
  form. If neither matches, skip the line.

### Range handling

```c
if (end < 0) end = start;
if (end - start + 1 > 20) end = start + 19;
for (int p = start; p <= end; p++)
    if (p > 0 && p < 65536 && hss_is_common_port(p))
        hss_add_entry(e, n, p, name);
```

- If the entry was a single port, `end == start`.
- Ranges longer than 20 ports are capped (`start + 19`) to avoid adding
  huge blocks of ephemeral ports.
- Only ports passing `hss_is_common_port()` are added — that function
  (`hss-ports.h:20`) accepts ports 1–1023 and the curated modern-port
  list (8080, 8443, 3000, 5000, etc.).

### IPv6 skip

```c
if (strstr(proto, "6")) continue;
```

Entries whose protocol contains `"6"` (e.g., `tcp6`) are skipped. The
tool only probes IPv4 (`AF_INET`) because `hss_get_eth_ips` enumerates
IPv4 addresses only.

## hss_parse_config

```c
static void hss_parse_config(const char *path, struct hss_port_entry *e, int *n);
```

Reads a user config file (default: `~/.config/.hssrc`) and adds ports.

### Path expansion

```c
if (path[0] == '~') {
    if (!getenv("HOME")) return;
    snprintf(expanded, sizeof(expanded), "%s%s", getenv("HOME"), path + 1);
    path = expanded;
}
```

If the path starts with `~`, expand it using `$HOME`. If `HOME` is
unset, skip silently.

### Parsing

```c
while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    int port = atoi(line);
    if (port > 0 && port < 65536) hss_add_entry(e, n, port, NULL);
}
```

- Skip comments and blank lines.
- Convert each line to an integer with `atoi`.
- Valid ports (1–65535) are added with `NULL` name (displayed as the
  port number).

The config file is line-based — one port per line:

```
# extra ports to scan
9090
5432
3306
```

## Call order

In `hss.c`, the sources are loaded in priority order:

1. `hss_parse_services` — system-wide common ports.
2. Built-in modern-port list (`hss_modern_ports`) — hardcoded in
   `hss-ports.h:11`.
3. `hss_parse_config` — user overrides.

Because `hss_add_entry` deduplicates, the same port appearing in
multiple sources is only probed once.
