#!/usr/bin/tcc -run
/* hss: Hurd Socket Stat, GPLv3 licensed, originally written by MARKMENTAL
 * Made possible thanks to Debian, TCC and GNU. Condensed with the assistance of Kimi */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <hurd.h>
#include <hurd/paths.h>
#include <hurd/lookup.h>
#include <hurd/process.h>
#include <mach/mach.h>

#define TIMEOUT_SEC 2
#define DEFAULT_PROC_LIMIT 3
#define MAX_PORTS 256
#define CONFIG_PATH "~/.config/.hssrc"

static const int modern[] = {8080, 8443, 3000, 5000, 8000, 8888, 9000, 9090, 1337, 27017, 0};
struct port_entry { int port; char service_name[64]; };
static int is_common_port(int port) {
    if (port >= 1 && port <= 1023) return 1;
    for (int i = 0; modern[i]; i++)
        if (modern[i] == port) return 1;
    return 0;
}

/* Append port to list in sorted order, skipping duplicates; name NULL -> use port number */
static void add_entry(struct port_entry *e, int *n, int port, const char *name) {
    for (int i = 0; i < *n; i++)
        if (e[i].port == port) return;
    if (*n >= MAX_PORTS) return;
    int i = (*n)++;
    while (i > 0 && e[i - 1].port > port) { e[i] = e[i - 1]; i--; }
    e[i].port = port;
    if (name) snprintf(e[i].service_name, sizeof(e[i].service_name), "%.20s", name);
    else snprintf(e[i].service_name, sizeof(e[i].service_name), "%d", port);
}

static void parse_etc_services(struct port_entry *e, int *n) {
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
            if (p > 0 && p < 65536 && is_common_port(p)) add_entry(e, n, p, name);
    }
    fclose(f);
}

static void parse_config_file(const char *path, struct port_entry *e, int *n) {
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
        if (port > 0 && port < 65536) add_entry(e, n, port, NULL);
    }
    fclose(f);
}

static int get_all_ethernet_ips(char ips[][INET_ADDRSTRLEN], int max) {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;
    if (getifaddrs(&ifaddr) == -1) return 0;
    for (ifa = ifaddr; ifa && count < max; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET ||
            !(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
        inet_ntop(AF_INET, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, ips[count++], INET_ADDRSTRLEN);
    }
    freeifaddrs(ifaddr);
    return count;
}

static int try_connect(const char *host, int port) {
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res;
    char port_str[16];
    int status = 0;
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return 0;
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock >= 0) {
        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
        if (connect(sock, res->ai_addr, res->ai_addrlen) == 0) status = 1;
        else if (errno == EINPROGRESS) {
            fd_set fdset; FD_ZERO(&fdset); FD_SET(sock, &fdset);
            struct timeval tv = {TIMEOUT_SEC, 0};
            if (select(sock + 1, NULL, &fdset, NULL, &tv) > 0) {
                int err = 0; socklen_t l = sizeof(err);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &l);
                status = !err;
            }
        }
        close(sock);
    }
    freeaddrinfo(res);
    return status;
}

static int probe_port(const char *host, int port, char *out, size_t len, char (*ips)[INET_ADDRSTRLEN], int nips) {
    if (!try_connect(host, port)) return 0;
    int local = !strcmp(host, "localhost") || !strcmp(host, "127.0.0.1");
    if (local && nips > 0) {
        size_t off = 0;
        int shown = 0;
        for (int i = 0; i < nips && shown < 3; i++)
            if (try_connect(ips[i], port))
                off += snprintf(out + off, len - off, "%s%s:%d", shown++ ? ", " : "", ips[i], port);
        if (shown) {
            if (nips > 3) snprintf(out + off, len - off, ", +more");
            return 1;
        }
    }
    snprintf(out, len, "%s:%d", host, port);
    return 1;
}

static int has_port_token(const char *args, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = args; (p = strstr(p, needle)); p++)
        if ((p == args || p[-1] < '0' || p[-1] > '9') && (p[n] < '0' || p[n] > '9')) return 1;
    return 0;
}

static int is_process_match(int port, const char *args, const char *needle) {
    if (port == 22 && strstr(args, "sshd")) return 1;
    if ((port == 80 || port == 443) && (strstr(args, "httpd") || strstr(args, "apache") || strstr(args, "nginx") || strstr(args, "lighttpd"))) return 1;
    if ((port == 21 && strstr(args, "ftp")) || (port == 5432 && strstr(args, "postgres")) || (port == 6379 && strstr(args, "redis"))) return 1;
    if (port == 3306 && (strstr(args, "mysql") || strstr(args, "mariadb"))) return 1;
    return (strstr(args, "python") || strstr(args, "node") || strstr(args, "java") || strstr(args, "ruby")) && has_port_token(args, needle);
}

static void find_pids_for_port_hurd(process_t proc, int port, char *out, size_t len, int max_procs) {
    pid_t *pids = NULL;
    mach_msg_type_number_t num_pids = 0;
    char buf[256] = "", needle[16];
    size_t off = 0;
    int matches = 0;
    snprintf(needle, sizeof(needle), "%d", port);
    if (proc_getallpids(proc, &pids, &num_pids) != 0) { snprintf(out, len, "users:((\"unknown\",pid=0))"); return; }
    for (size_t i = 0; i < num_pids; i++) {
        char *args = NULL;
        mach_msg_type_number_t args_len = 0;
        if (proc_getprocargs(proc, pids[i], &args, &args_len) != 0 || !args || !args_len) continue;
        for (size_t j = 0; j < args_len; j++) if (!args[j]) args[j] = ' ';
        if (is_process_match(port, args, needle)) {
            if (max_procs > 0 && matches >= max_procs) {
                snprintf(buf + off, sizeof(buf) - off, ",+more");
                vm_deallocate(mach_task_self(), (vm_address_t)args, args_len);
                break;
            }
            char *base = strrchr(args, '/'), *space;
            base = base ? base + 1 : args;
            if ((space = strchr(base, ' '))) *space = '\0';
            off += snprintf(buf + off, sizeof(buf) - off, "%s(\"%.40s\",pid=%d)", matches ? "," : "", base, pids[i]);
            if (off > sizeof(buf) - 1) off = sizeof(buf) - 1;
            matches++;
        }
        vm_deallocate(mach_task_self(), (vm_address_t)args, args_len);
    }
    if (pids && num_pids) vm_deallocate(mach_task_self(), (vm_address_t)pids, num_pids * sizeof(pid_t));
    if (matches) snprintf(out, len, "users:(%s)", buf);
    else snprintf(out, len, "users:((\"unknown\",pid=0))");
}

int main(int argc, char *argv[]) {
    const char *target = (argc > 1) ? argv[1] : "localhost";
    int proc_limit = DEFAULT_PROC_LIMIT;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--all-processes")) proc_limit = 0;
        else if (!strncmp(argv[i], "--process-limit=", 16)) proc_limit = atoi(argv[i] + 16);
    }
    static struct port_entry ports[MAX_PORTS];
    int num_ports = 0;
    parse_etc_services(ports, &num_ports);
    for (int i = 0; modern[i]; i++) add_entry(ports, &num_ports, modern[i], NULL);
    parse_config_file(CONFIG_PATH, ports, &num_ports);
    char eth_ips[10][INET_ADDRSTRLEN];
    int num_eth_ips = get_all_ethernet_ips(eth_ips, 10);
    process_t proc_server = getproc();
    if (proc_server == MACH_PORT_NULL) { fprintf(stderr, "hss: Error fetching Hurd proc server port via getproc()\n"); return 1; }
    file_t sock_port = file_name_lookup(_SERVERS_SOCKET "/2", 0, 0);
    if (sock_port == MACH_PORT_NULL) fprintf(stderr, "hss: Warning - IPv4 socket translator not responding\n");
    else mach_port_deallocate(mach_task_self(), sock_port);
    printf("%-8s %-12s %-30s %s\n", "Netid", "State", "Local Address:Port", "Process");
    for (int i = 0; i < num_ports; i++) {
        char local_addr[256], status[256];
        if (!probe_port(target, ports[i].port, local_addr, sizeof(local_addr), eth_ips, num_eth_ips)) continue;
        find_pids_for_port_hurd(proc_server, ports[i].port, status, sizeof(status), proc_limit);
        printf("%-8s %-12s %-30s %s\n", "tcp", "LISTEN", local_addr, status);
    }
    mach_port_deallocate(mach_task_self(), proc_server);
    return 0;
}
