#ifndef HSS_PROBE_H
#define HSS_PROBE_H

#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

#define TIMEOUT_SEC 2

static int hss_try_connect(const char *host, int port) {
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

static int hss_probe_port(const char *host, int port, char *out, size_t len,
                          char (*ips)[INET_ADDRSTRLEN], int nips) {
    if (!hss_try_connect(host, port)) return 0;
    int local = !strcmp(host, "localhost") || !strcmp(host, "127.0.0.1");
    if (local && nips > 0) {
        size_t off = 0;
        int shown = 0;
        for (int i = 0; i < nips && shown < 3; i++)
            if (hss_try_connect(ips[i], port))
                off += snprintf(out + off, len - off, "%s%s:%d", shown++ ? ", " : "", ips[i], port);
        if (shown) {
            if (nips > 3) snprintf(out + off, len - off, ", +more");
            return 1;
        }
    }
    snprintf(out, len, "%s:%d", host, port);
    return 1;
}

static int hss_get_eth_ips(char ips[][INET_ADDRSTRLEN], int max) {
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

#endif /* HSS_PROBE_H */
