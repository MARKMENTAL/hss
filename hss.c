#!/usr/bin/tcc -run

/* hss: Hurd Socket Stat, GPLv3 licensed, originally written by MARKMENTAL
 * Made possible thanks to Debian, TCC and GNU
 * Code shortened with the assistance of LongCat-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <hurd.h>
#include <hurd/paths.h>
#include <hurd/lookup.h>
#include <hurd/process.h>
#include <hurd/ifsock.h>
#include <mach/mach.h>
#include <mach/mach_interface.h>

#define TIMEOUT_SEC 2
#define DEFAULT_PROC_LIMIT 3
#define MAX_ETH_IPS 10
#define MAX_DISPLAY_IPS 3

static int initial_ports[] = {22, 80, 443, 21, 3306, 5432, 6379, 8080, 1337};
#define INITIAL_PORTS_LEN (sizeof(initial_ports) / sizeof(initial_ports[0]))

static int get_all_ethernet_ips(char ips[][INET_ADDRSTRLEN], int max_ips) {
    struct ifaddrs *ifaddr, *ifa;
    int count = 0;
    if (getifaddrs(&ifaddr) == -1) return 0;
    for (ifa = ifaddr; ifa != NULL && count < max_ips; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || (ifa->ifa_flags & IFF_LOOPBACK)) continue;
        struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, ips[count], INET_ADDRSTRLEN);
        count++;
    }
    freeifaddrs(ifaddr);
    return count;
}
static int try_connect(const char *host, int port) {
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    int sock = -1, status = 0;
    snprintf(port_str, sizeof(port_str), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hints, &res) != 0) return 0;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) continue;
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) < 0 && errno == EINPROGRESS) {
            fd_set fdset;
            struct timeval tv;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);
            tv.tv_sec = TIMEOUT_SEC;
            tv.tv_usec = 0;
            if (select(sock + 1, NULL, &fdset, NULL, &tv) > 0) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) status = 1;
            }
        } else if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            status = 1;
        }
        close(sock);
        if (status) break;
    }
    freeaddrinfo(res);
    return status;
}

static int probe_port(const char *host, int port, char *local_addr, size_t addr_len,
                      char (*eth_ips)[INET_ADDRSTRLEN], int num_eth_ips) {
    if (!try_connect(host, port)) return 0;
    int is_local = (strcmp(host, "localhost") == 0 || strcmp(host, "127.0.0.1") == 0);
    if (is_local && num_eth_ips > 0) {
        char verified_ips[MAX_DISPLAY_IPS][INET_ADDRSTRLEN];
        int verified_count = 0;
        for (int i = 0; i < num_eth_ips && verified_count < MAX_DISPLAY_IPS; i++) {
            if (try_connect(eth_ips[i], port)) {
                strncpy(verified_ips[verified_count], eth_ips[i], INET_ADDRSTRLEN);
                verified_count++;
            }
        }
        if (verified_count > 0) {
            char addr_buf[256] = "";
            for (int i = 0; i < verified_count; i++) {
                if (i > 0) strncat(addr_buf, ", ", sizeof(addr_buf) - strlen(addr_buf) - 1);
                strncat(addr_buf, verified_ips[i], sizeof(addr_buf) - strlen(addr_buf) - 1);
                strncat(addr_buf, ":", sizeof(addr_buf) - strlen(addr_buf) - 1);
                char port_buf[16];
                snprintf(port_buf, sizeof(port_buf), "%d", port);
                strncat(addr_buf, port_buf, sizeof(addr_buf) - strlen(addr_buf) - 1);
            }
            if (num_eth_ips > MAX_DISPLAY_IPS) strncat(addr_buf, ", +more", sizeof(addr_buf) - strlen(addr_buf) - 1);
            snprintf(local_addr, addr_len, "%s", addr_buf);
            return 1;
        }
    }
    snprintf(local_addr, addr_len, "%s:%d", host, port);
    return 1;
}

static int is_process_match(int port, const char *args, const char *port_needle) {
    if (port == 22 && strstr(args, "sshd")) return 1;
    if ((port == 80 || port == 443) && (strstr(args, "httpd") || strstr(args, "apache") || strstr(args, "nginx") || strstr(args, "lighttpd"))) return 1;
    if (port == 21 && (strstr(args, "ftp") || strstr(args, "vsftpd") || strstr(args, "proftpd"))) return 1;
    if (port == 3306 && (strstr(args, "mysql") || strstr(args, "mariadb"))) return 1;
    if (port == 5432 && strstr(args, "postgres")) return 1;
    if (port == 6379 && strstr(args, "redis")) return 1;
    if (strstr(args, "python") || strstr(args, "node") || strstr(args, "java") || strstr(args, "ruby")) {
        if (strstr(args, port_needle)) return 1;
    }
    return 0;
}

static void find_pids_for_port_hurd(process_t proc_server, int port, char *out_status, size_t buf_len, int max_processes) {
    pid_t *pids = NULL;
    mach_msg_type_number_t num_pids = 0;
    char process_buf[256] = "";
    int match_count = 0;
    char port_needle[16];
    snprintf(port_needle, sizeof(port_needle), "%d", port);
    if (proc_getallpids(proc_server, &pids, &num_pids) != 0) {
        snprintf(out_status, buf_len, "users:((\"unknown\",pid=0))");
        return;
    }
    for (size_t i = 0; i < num_pids; i++) {
        pid_t current_pid = pids[i];
        char *args = NULL;
        mach_msg_type_number_t args_len = 0;
        if (proc_getprocargs(proc_server, current_pid, &args, &args_len) == 0 && args != NULL && args_len > 0) {
            for (size_t j = 0; j < args_len; j++) {
                if (args[j] == '\0') args[j] = ' ';
            }
            if (is_process_match(port, args, port_needle)) {
                if (max_processes > 0 && match_count >= max_processes) {
                    strncat(process_buf, ",+more", sizeof(process_buf) - strlen(process_buf) - 1);
                    match_count++;
                    vm_deallocate(mach_task_self(), (vm_address_t)args, args_len);
                    break;
                }
                if (match_count > 0) strncat(process_buf, ",", sizeof(process_buf) - strlen(process_buf) - 1);
                char *basename = strrchr(args, '/');
                basename = basename ? basename + 1 : args;
                char *space = strchr(basename, ' ');
                if (space) *space = '\0';
                char proc_entry[64];
                snprintf(proc_entry, sizeof(proc_entry), "(\"%s\",pid=%d)", basename, current_pid);
                strncat(process_buf, proc_entry, sizeof(process_buf) - strlen(process_buf) - 1);
                match_count++;
            }
            vm_deallocate(mach_task_self(), (vm_address_t)args, args_len);
        }
    }
    if (pids && num_pids > 0) vm_deallocate(mach_task_self(), (vm_address_t)pids, num_pids * sizeof(pid_t));
    if (match_count > 0) snprintf(out_status, buf_len, "users:(%s)", process_buf);
    else snprintf(out_status, buf_len, "users:((\"unknown\",pid=0))");
}

int main(int argc, char *argv[]) {
    const char *target = (argc > 1) ? argv[1] : "localhost";
    int proc_limit = DEFAULT_PROC_LIMIT;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--all-processes") == 0) proc_limit = 0;
        else if (strncmp(argv[i], "--process-limit=", 16) == 0) proc_limit = atoi(argv[i] + 16);
    }
    char status[256];
    char local_addr[256];
    char eth_ips[MAX_ETH_IPS][INET_ADDRSTRLEN];
    int num_eth_ips = get_all_ethernet_ips(eth_ips, MAX_ETH_IPS);
    process_t proc_server = getproc();
    if (proc_server == MACH_PORT_NULL) {
        fprintf(stderr, "hss: Error fetching Hurd proc server port via getproc()\n");
        return 1;
    }
    file_t sock_port = file_name_lookup(_SERVERS_SOCKET "/2", 0, 0);
    if (sock_port == MACH_PORT_NULL) fprintf(stderr, "hss: Warning - IPv4 socket translator not responding\n");
    else mach_port_deallocate(mach_task_self(), sock_port);
    printf("%-8s %-12s %-30s %s\n", "Netid", "State", "Local Address:Port", "Process");
    for (size_t i = 0; i < INITIAL_PORTS_LEN; i++) {
        int port = initial_ports[i];
        if (!probe_port(target, port, local_addr, sizeof(local_addr), eth_ips, num_eth_ips)) continue;
        find_pids_for_port_hurd(proc_server, port, status, sizeof(status), proc_limit);
        printf("%-8s %-12s %-30s %s\n", "tcp", "LISTEN", local_addr, status);
    }
    mach_port_deallocate(mach_task_self(), proc_server);
    return 0;
}
