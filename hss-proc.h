#ifndef HSS_PROC_H
#define HSS_PROC_H

#include <hurd.h>
#include <hurd/process.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_PROC_LIMIT 3

static int hss_match_port_token(const char *args, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = args; (p = strstr(p, needle)); p++)
        if ((p == args || p[-1] < '0' || p[-1] > '9') && (p[n] < '0' || p[n] > '9')) return 1;
    return 0;
}

static int hss_match_process(int port, const char *args, const char *needle) {
    if (port == 22 && strstr(args, "sshd")) return 1;
    if ((port == 80 || port == 443) && (strstr(args, "httpd") || strstr(args, "apache") || strstr(args, "nginx") || strstr(args, "lighttpd"))) return 1;
    if ((port == 21 && strstr(args, "ftp")) || (port == 5432 && strstr(args, "postgres")) || (port == 6379 && strstr(args, "redis"))) return 1;
    if (port == 3306 && (strstr(args, "mysql") || strstr(args, "mariadb"))) return 1;
    return (strstr(args, "python") || strstr(args, "node") || strstr(args, "java") || strstr(args, "ruby")) && hss_match_port_token(args, needle);
}

static void hss_find_pids(process_t proc, int port, char *out, size_t len, int max_procs) {
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
        if (hss_match_process(port, args, needle)) {
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

#endif /* HSS_PROC_H */
