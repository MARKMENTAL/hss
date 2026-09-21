/* hss: Hurd Socket Stat, GPLv3 licensed, originally written by MARKMENTAL */

#include "hss.h"

static void print_help(void) {

    printf("Usage: %s [target] [options]\n"
           "\n"
           "Heuristic socket-table emulator for GNU/Hurd, mimicking ss/netstat.\n"
           "\n"
           "  target            host to scan (default: localhost)\n"
           "  --all-processes   show all processes for each listening port\n"
           "  --process-limit=N limit processes shown per port (default: 3)\n"
           "  --version         print version and exit\n"
           "  --help            print this help and exit\n"
           "\n"
           "Scans curated common ports (from /etc/services and a built-in list),\n"
           "probes each with a non-blocking TCP connect, and identifies owning\n"
           "processes by inspecting the Mach proc server.\n", APP_NAME);
}

int main(int argc, char *argv[]) {

    const char *target = (argc > 1) ? argv[1] : "localhost";
    int proc_limit = DEFAULT_PROC_LIMIT;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--version")) {
            printf("%s %s\n", APP_NAME, APP_VERSION);
            return 0;
        }
        else if (!strcmp(argv[i], "--help")) {
            print_help();
            return 0;
        }
        else if (!strcmp(argv[i], "--all-processes")) {
            proc_limit = 0;
        }
        else if (!strncmp(argv[i], "--process-limit=", 16)) {
            proc_limit = atoi(argv[i] + 16);
        }
    }

    /* Unlike Linux `ss` (which reads a kernel socket table via netlink), Hurd
     * has no centralized socket table, so we build a candidate port list
     * heuristically and probe each with a TCP connect. */
    static struct hss_port_entry ports[MAX_PORTS];
    int num_ports = 0;

    hss_parse_services(ports, &num_ports);

    for (int i = 0; hss_modern_ports[i]; i++) {
        hss_add_entry(ports, &num_ports, hss_modern_ports[i], NULL);
    }

    hss_parse_config(CONFIG_PATH, ports, &num_ports);

    /* On Linux, `ss` reads bound interface addresses from the kernel. We
     * enumerate the machine's ethernet IPs to replicate that per-interface
     * "Local Address:Port" column ourselves. */
    char eth_ips[10][INET_ADDRSTRLEN];
    int num_eth_ips = hss_get_eth_ips(eth_ips, 10);

    /* getproc() returns a Mach port handle to the Hurd process server — a
     * userspace translator, not a kernel syscall. Used below to enumerate
     * every running process. */
    process_t proc_server = getproc();

    if (proc_server == MACH_PORT_NULL) {
        fprintf(stderr, "%s: Error fetching Hurd proc server port via getproc()\n", APP_NAME);
        return 1;
    }

    printf("%-8s %-12s %-30s %s\n", "Netid", "State", "Local Address:Port", "Process");

    /* For each candidate: attempt a non-blocking TCP connect. If it succeeds,
     * something is listening. Then crawl the proc server to match owning PIDs
     * by inspecting each process's command line against known service names
     * (sshd, nginx, ...) or the port number itself. */
    for (int i = 0; i < num_ports; i++) {
        char local_addr[256], proc_status[256];

        if (!hss_probe_port(target, ports[i].port, local_addr, sizeof(local_addr),
                            eth_ips, num_eth_ips)) {
            continue;
        }

        hss_find_pids(proc_server, ports[i].port, proc_status, sizeof(proc_status), proc_limit);
        printf("%-8s %-12s %-30s %s\n", "tcp", "LISTEN", local_addr, proc_status);
    }

    return 0;
}
