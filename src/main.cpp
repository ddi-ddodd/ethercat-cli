/* soem-pdo-dump: EtherCAT PDO mapping and live data dumper
 *
 * Uses SOEM v2.0.0 API.
 * Requires root or CAP_NET_RAW to open a raw AF_PACKET socket (Linux),
 * or Administrator with Npcap installed (Windows).
 *
 * Usage: soem-pdo-dump --list
 *        sudo ./soem-pdo-dump <interface>          (Linux)
 *        soem-pdo-dump.exe <\Device\NPF_{GUID}>    (Windows)
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ethercat.h"

/* ------------------------------------------------------------------ */
/* Interface listing                                                   */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
/* On Windows SOEM uses Npcap/pcap_open(); enumerate via pcap_findalldevs. */
extern "C" {
#include <pcap.h>
}

static void list_interfaces()
{
    pcap_if_t *alldevs, *d;
    char errbuf[PCAP_ERRBUF_SIZE];

    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        std::fprintf(stderr, "pcap_findalldevs failed: %s\n", errbuf);
        return;
    }

    std::printf("Available network interfaces (pass the device name to this tool):\n\n");
    for (d = alldevs; d != nullptr; d = d->next) {
        std::printf("  Device : %s\n", d->name);
        if (d->description)
            std::printf("  Desc   : %s\n", d->description);
        std::printf("\n");
    }

    pcap_freealldevs(alldevs);
}

#else
/* On Linux SOEM uses raw AF_PACKET sockets; list interfaces from the kernel. */
#include <ifaddrs.h>
#include <net/if.h>

static void list_interfaces()
{
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) == -1) {
        std::perror("getifaddrs");
        return;
    }

    std::printf("Available network interfaces (pass the interface name to this tool):\n\n");

    /* Track which names we have already printed to avoid duplicates
     * (getifaddrs returns one entry per address family per interface). */
    char seen[64][IF_NAMESIZE];
    int  nseen = 0;

    for (ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name)
            continue;

        /* skip loopback */
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        int already = 0;
        for (int i = 0; i < nseen; i++) {
            if (std::strncmp(seen[i], ifa->ifa_name, IF_NAMESIZE) == 0) {
                already = 1;
                break;
            }
        }
        if (already)
            continue;

        if (nseen < 64)
            std::strncpy(seen[nseen++], ifa->ifa_name, IF_NAMESIZE - 1);

        const char *state = (ifa->ifa_flags & IFF_UP) ? "UP" : "DOWN";
        std::printf("  Interface : %s  [%s]\n", ifa->ifa_name, state);
    }
    std::printf("\n");

    freeifaddrs(ifap);
}
#endif

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 2) {
#ifdef _WIN32
        std::fprintf(stderr, "Usage: %s --list\n", argv[0]);
        std::fprintf(stderr, "       %s <\\Device\\NPF_{GUID}>\n\n", argv[0]);
        std::fprintf(stderr, "Run '%s --list' to see available device names.\n", argv[0]);
        std::fprintf(stderr, "Requires Administrator and Npcap installed.\n");
#else
        std::fprintf(stderr, "Usage: %s --list\n", argv[0]);
        std::fprintf(stderr, "       sudo %s <interface>\n\n", argv[0]);
        std::fprintf(stderr, "Run '%s --list' to see available interfaces.\n", argv[0]);
        std::fprintf(stderr, "Requires root or CAP_NET_RAW (raw socket access).\n");
#endif
        return EXIT_FAILURE;
    }

    if (std::strcmp(argv[1], "--list") == 0) {
        list_interfaces();
        return EXIT_SUCCESS;
    }

    return ethercat_run(argv[1]);
}
