#ifndef HSS_PORTS_H
#define HSS_PORTS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PORTS 256
#define CONFIG_PATH "~/.config/.hssrc"

static const int hss_modern_ports[] = {
    8080, 8443, 3000, 5000, 8000, 8888, 9000, 9090, 1337, 27017, 0
};

struct hss_port_entry {
    int port;
    char service_name[64];
};

static int hss_is_common_port(int port) {
    if (port >= 1 && port <= 1023) return 1;
    for (int i = 0; hss_modern_ports[i]; i++)
        if (hss_modern_ports[i] == port) return 1;
    return 0;
}

/* Append port to list in sorted order, skipping duplicates; name NULL -> use port number */
static void hss_add_entry(struct hss_port_entry *e, int *n, int port, const char *name) {
    for (int i = 0; i < *n; i++)
        if (e[i].port == port) return;
    if (*n >= MAX_PORTS) return;
    int i = (*n)++;
    while (i > 0 && e[i - 1].port > port) { e[i] = e[i - 1]; i--; }
    e[i].port = port;
    if (name) snprintf(e[i].service_name, sizeof(e[i].service_name), "%.20s", name);
    else snprintf(e[i].service_name, sizeof(e[i].service_name), "%d", port);
}

static void hss_parse_services(struct hss_port_entry *e, int *n) {
    FILE *f = fopen("/etc/services", "r");
    if (!f) return;
    char line[256], name[64], proto[16];
    int start, end;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        end = -1;
        if (sscanf(line, "%63s %d-%d/%15s", name, &start, &end, proto) < 4 &&
            sscanf(line, "%63s %d/%15s", name, &start, proto) != 3) continue;
        if (end < 0) end = start;
        if (strstr(proto, "6")) continue;
        if (end - start + 1 > 20) end = start + 19;
        for (int p = start; p <= end; p++)
            if (p > 0 && p < 65536 && hss_is_common_port(p)) hss_add_entry(e, n, p, name);
    }
    fclose(f);
}

static void hss_parse_config(const char *path, struct hss_port_entry *e, int *n) {
    char expanded[256], line[256];
    if (path[0] == '~') {
        if (!getenv("HOME")) return;
        snprintf(expanded, sizeof(expanded), "%s%s", getenv("HOME"), path + 1);
        path = expanded;
    }
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        int port = atoi(line);
        if (port > 0 && port < 65536) hss_add_entry(e, n, port, NULL);
    }
    fclose(f);
}

#endif /* HSS_PORTS_H */
