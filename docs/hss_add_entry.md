# hss_add_entry

**Source:** `hss-ports.h:28`

Inserts a port into a sorted array, skipping duplicates.

## Signature

```c
static void hss_add_entry(struct hss_port_entry *e, int *n, int port, const char *name);
```

| Parameter | Description |
|-----------|-------------|
| `e` | Array of `struct hss_port_entry` |
| `n` | Pointer to current count (updated in-place) |
| `port` | Port number to insert |
| `name` | Service name, or `NULL` to use the port number as the name |

## Deduplication

```c
for (int i = 0; i < *n; i++)
    if (e[i].port == port) return;
```

Before inserting, scan the existing array. If the port already exists,
return immediately — no duplicate entries.

## Capacity guard

```c
if (*n >= MAX_PORTS) return;
```

`MAX_PORTS` is 256 (`hss-ports.h:8`). If the array is full, silently
skip. This prevents buffer overflow when `/etc/services` plus the
modern-port list plus user config produce more than 256 candidates.

## Sorted insertion

```c
int i = (*n)++;
while (i > 0 && e[i - 1].port > port) { e[i] = e[i - 1]; i--; }
e[i].port = port;
```

Rather than appending and sorting later, the port is inserted in sorted
order in a single pass. The `while` loop shifts larger entries rightward
to make room, then writes the new entry at the correct position.

This means the array is **always sorted** — no separate sort pass
needed. The sorted order is visible in `hss.c` where the probe loop
iterates `ports[]` in ascending port-number order, so output appears
sorted by port.

## Service name handling

```c
if (name)
    snprintf(e[i].service_name, sizeof(e[i].service_name), "%.20s", name);
else
    snprintf(e[i].service_name, sizeof(e[i].service_name), "%d", port);
```

- If `name` is provided (from `/etc/services`), store it, truncated to
  20 characters.
- If `name` is `NULL` (from the built-in modern-port list or user
  config), use the decimal port number as the name.

## Why sorted + deduplicated

The final port list is probed in order (`hss.c:80`), so sorted insertion
means:
- Output appears in ascending port order (matches `ss` behavior).
- No port is probed twice (dedup keeps the scan fast).
- The array is ready to use immediately — no post-processing.
