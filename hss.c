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
#include <hurd.h>
#include <hurd/paths.h>
#include <hurd/lookup.h>
#include <hurd/process.h>
#include <hurd/ifsock.h>
#include <mach/mach.h>
#include <mach/mach_interface.h>

#define TIMEOUT_SEC 2

static int initial_ports[] = {22, 80, 443, 21, 3306, 5432, 6379, 8080, 1337};
#define INITIAL_PORTS_LEN (sizeof(initial_ports) / sizeof(initial_ports[0]))

static const char *service_names[] = {"ssh", "http", "https", "ftp", "mysql", "postgres", "redis", "http-alt", "dev"};

static int probe_port(const char *host, int port) {
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

static void find_pids_for_port_hurd(process_t proc_server, int port, char *out_status, size_t buf_len) {
    pid_t *pids = NULL;
    mach_msg_type_number_t num_pids = 0;
    char pids_str[128] = "";
    char first_pname[128] = "";
    int match_count = 0;
    char port_needle[16];
    snprintf(port_needle, sizeof(port_needle), "%d", port);    
    if (proc_getallpids(proc_server, &pids, &num_pids) != 0) {
        snprintf(out_status, buf_len, "OPEN (Active System Service)");
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
            int is_match = 0;
            if (port == 22 && strstr(args, "sshd")) is_match = 1;
            else if ((port == 80 || port == 443) && (strstr(args, "httpd") || strstr(args, "apache") || strstr(args, "nginx") || strstr(args, "lighttpd"))) is_match = 1;
            else if (port == 21 && (strstr(args, "ftp") || strstr(args, "vsftpd") || strstr(args, "proftpd"))) is_match = 1;
            else if (port == 3306 && (strstr(args, "mysql") || strstr(args, "mariadb"))) is_match = 1;
            else if (port == 5432 && strstr(args, "postgres")) is_match = 1;
            else if (port == 6379 && strstr(args, "redis")) is_match = 1;
            else if (strstr(args, "python") || strstr(args, "node") || strstr(args, "java") || strstr(args, "ruby")) {
                if (strstr(args, port_needle)) is_match = 1;
            }
            if (is_match) {
                char pid_buf[16];
                snprintf(pid_buf, sizeof(pid_buf), "%d", current_pid);
                if (match_count > 0) strncat(pids_str, ",", sizeof(pids_str) - strlen(pids_str) - 1);
                strncat(pids_str, pid_buf, sizeof(pids_str) - strlen(pids_str) - 1);
                if (match_count == 0) {
                    strncpy(first_pname, args, sizeof(first_pname) - 1);
                    first_pname[sizeof(first_pname) - 1] = '\0';
                    if (strlen(first_pname) > 40) first_pname[40] = '\0';
                }
                match_count++;
            }
            vm_deallocate(mach_task_self(), (vm_address_t)args, args_len);
        }
    }
    if (pids && num_pids > 0) {
        vm_deallocate(mach_task_self(), (vm_address_t)pids, num_pids * sizeof(pid_t));
    }
    if (match_count > 0) {
        snprintf(out_status, buf_len, "OPEN (PIDs: %s -> %s)", pids_str, first_pname);
    } else {
        snprintf(out_status, buf_len, "OPEN (Active System Service / Translator)");
    }
}

int main(int argc, char *argv[]) {
    const char *target = (argc > 1) ? argv[1] : "localhost";
    char status[256];
    process_t proc_server = getproc();
    if (proc_server == MACH_PORT_NULL) {
        fprintf(stderr, "hss: Error fetching Hurd proc server port via getproc()\n");
        return 1;
    }
    file_t sock_port = file_name_lookup(_SERVERS_SOCKET "/2", 0, 0);
    if (sock_port == MACH_PORT_NULL) fprintf(stderr, "hss: Warning - IPv4 socket translator not responding\n");
    else mach_port_deallocate(mach_task_self(), sock_port);
    printf("hss: Querying active network sockets on [%s] (Hurd RPC Mode)...\n", target);
    printf("------------------------------------------------------------------------\n");
    printf("%-10s %-15s %s\n", "PORT", "SERVICE", "STATUS");
    printf("------------------------------------------------------------------------\n");
    for (size_t i = 0; i < INITIAL_PORTS_LEN; i++) {
        int port = initial_ports[i];
        if (!probe_port(target, port)) continue;
        find_pids_for_port_hurd(proc_server, port, status, sizeof(status));
        printf("%-10d %-15s %s\n", port, service_names[i], status);
    }
    mach_port_deallocate(mach_task_self(), proc_server);
    return 0;
}
