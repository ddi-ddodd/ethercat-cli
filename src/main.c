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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

/* Single SOEM umbrella header */
#include "soem/soem.h"

/* ------------------------------------------------------------------ */
/* Interface listing                                                   */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
/* On Windows SOEM uses Npcap/pcap_open(); enumerate via pcap_findalldevs. */
#include <pcap.h>

static void list_interfaces(void)
{
    pcap_if_t *alldevs, *d;
    char errbuf[PCAP_ERRBUF_SIZE];

    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        fprintf(stderr, "pcap_findalldevs failed: %s\n", errbuf);
        return;
    }

    printf("Available network interfaces (pass the device name to this tool):\n\n");
    for (d = alldevs; d != NULL; d = d->next) {
        printf("  Device : %s\n", d->name);
        if (d->description)
            printf("  Desc   : %s\n", d->description);
        printf("\n");
    }

    pcap_freealldevs(alldevs);
}

#else
/* On Linux SOEM uses raw AF_PACKET sockets; list interfaces from the kernel. */
#include <ifaddrs.h>
#include <net/if.h>

static void list_interfaces(void)
{
    struct ifaddrs *ifap, *ifa;

    if (getifaddrs(&ifap) == -1) {
        perror("getifaddrs");
        return;
    }

    printf("Available network interfaces (pass the interface name to this tool):\n\n");

    /* Track which names we have already printed to avoid duplicates
     * (getifaddrs returns one entry per address family per interface). */
    char seen[64][IF_NAMESIZE];
    int  nseen = 0;

    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name)
            continue;

        /* skip loopback */
        if (ifa->ifa_flags & IFF_LOOPBACK)
            continue;

        int already = 0;
        for (int i = 0; i < nseen; i++) {
            if (strncmp(seen[i], ifa->ifa_name, IF_NAMESIZE) == 0) {
                already = 1;
                break;
            }
        }
        if (already)
            continue;

        if (nseen < 64)
            strncpy(seen[nseen++], ifa->ifa_name, IF_NAMESIZE - 1);

        const char *state = (ifa->ifa_flags & IFF_UP) ? "UP" : "DOWN";
        printf("  Interface : %s  [%s]\n", ifa->ifa_name, state);
    }
    printf("\n");

    freeifaddrs(ifap);
}
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/* 4096 bytes covers virtually any realistic slave configuration.
 * Each slave rarely uses more than a few dozen bytes of PDO data. */
#define IOMAP_SIZE     4096

/* Maximum PDO entries tracked per direction per slave */
#define MAX_PDO_ENTRIES 256

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/* Decoded PDO map entry from the 32-bit packed format:
 *   bits [31:16] = CANopen object index
 *   bits [15:8]  = CANopen subindex
 *   bits [7:0]   = bit length (0 = padding/gap)
 */
typedef struct {
    uint16_t obj_index;
    uint8_t  obj_subidx;
    uint8_t  bit_length;
    int      bit_offset;  /* bit offset within this slave's IO area */
} pdo_entry_t;

typedef struct {
    int         count;
    pdo_entry_t entries[MAX_PDO_ENTRIES];
} pdo_map_t;

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static ecx_contextt ctx;

/* IOmap must be global so that slavelist[n].inputs/outputs pointers
 * (set by ecx_config_map_group) remain valid for the program's lifetime. */
static uint8_t IOmap[IOMAP_SIZE];

/* ------------------------------------------------------------------ */
/* PDO mapping: read one SyncManager's PDO assignment chain           */
/*                                                                     */
/* assign_idx: 0x1C10 + sm_number  (e.g. 0x1C12 for SM2/outputs)    */
/* map_bit_offset: running bit offset within this slave's IO area     */
/* map: output structure to populate                                  */
/*                                                                     */
/* Returns total number of bits mapped.                               */
/* ------------------------------------------------------------------ */
static int read_pdo_assign(uint16_t slave, uint16_t assign_idx,
                            int map_bit_offset, pdo_map_t *map)
{
    int      rdl, wkc;
    uint16_t num_pdo = 0;
    int      total_bits = 0;

    /* Read subindex 0: count of PDOs assigned to this SM */
    rdl = sizeof(num_pdo);
    wkc = ecx_SDOread(&ctx, slave, assign_idx, 0x00,
                      FALSE, &rdl, &num_pdo, EC_TIMEOUTRXM);
    if (wkc <= 0 || num_pdo == 0) {
        return 0;
    }

    for (uint16_t i = 1; i <= num_pdo; i++) {
        uint16_t pdo_obj_idx = 0;
        rdl = sizeof(pdo_obj_idx);
        wkc = ecx_SDOread(&ctx, slave, assign_idx, (uint8_t)i,
                          FALSE, &rdl, &pdo_obj_idx, EC_TIMEOUTRXM);
        if (wkc <= 0) {
            continue;
        }

        /* Read the PDO mapping object: subindex 0 = entry count */
        uint8_t subcnt = 0;
        rdl = sizeof(subcnt);
        wkc = ecx_SDOread(&ctx, slave, pdo_obj_idx, 0x00,
                          FALSE, &rdl, &subcnt, EC_TIMEOUTRXM);
        if (wkc <= 0) {
            continue;
        }

        for (uint8_t j = 1; j <= subcnt; j++) {
            int32_t rdat32 = 0;
            rdl = sizeof(rdat32);
            wkc = ecx_SDOread(&ctx, slave, pdo_obj_idx, j,
                              FALSE, &rdl, &rdat32, EC_TIMEOUTRXM);
            if (wkc <= 0) {
                continue;
            }

            /* Decode bit-packed entry:
             *   bits [31:16] = mapped object index
             *   bits [15:8]  = mapped object subindex
             *   bits [7:0]   = bit length (0 = padding gap)
             */
            uint8_t  bit_len    = (uint8_t)(rdat32 & 0xFF);
            uint8_t  obj_subidx = (uint8_t)((rdat32 >> 8) & 0xFF);
            uint16_t obj_idx    = (uint16_t)((rdat32 >> 16) & 0xFFFF);

            if (map->count < MAX_PDO_ENTRIES) {
                pdo_entry_t *e = &map->entries[map->count];
                e->obj_index  = obj_idx;
                e->obj_subidx = obj_subidx;
                e->bit_length = bit_len;
                e->bit_offset = map_bit_offset + total_bits;
                map->count++;
            }

            total_bits += bit_len;
        }
    }

    return total_bits;
}

/* ------------------------------------------------------------------ */
/* PDO mapping: enumerate RxPDO and TxPDO for one slave via CoE SDO  */
/*                                                                     */
/* Uses ECT_SDO_SMCOMMTYPE (0x1C00) to discover SyncManager roles:   */
/*   sm_type 3 = process data output (master->slave, RxPDO)          */
/*   sm_type 4 = process data input  (slave->master, TxPDO)          */
/* ------------------------------------------------------------------ */
static void enumerate_pdo_maps(int slave_num,
                                pdo_map_t *rx_map,
                                pdo_map_t *tx_map)
{
    int     rdl, wkc;
    uint8_t nSM = 0;
    int     outputs_bits = 0;
    int     inputs_bits  = 0;

    memset(rx_map, 0, sizeof(*rx_map));
    memset(tx_map, 0, sizeof(*tx_map));

    /* Read number of SyncManagers */
    rdl = sizeof(nSM);
    wkc = ecx_SDOread(&ctx, (uint16_t)slave_num, ECT_SDO_SMCOMMTYPE,
                      0x00, FALSE, &rdl, &nSM, EC_TIMEOUTRXM);
    if (wkc <= 0) {
        printf("    [WARNING] Cannot read SM comm type for slave %d"
               " (no CoE mailbox?)\n", slave_num);
        return;
    }

    /* SM 0 and 1 are mailbox channels; process data starts at SM 2 */
    for (uint8_t sm = 2; sm < nSM; sm++) {
        uint8_t sm_type = 0;
        rdl = sizeof(sm_type);
        wkc = ecx_SDOread(&ctx, (uint16_t)slave_num, ECT_SDO_SMCOMMTYPE,
                          (uint8_t)(sm + 1), FALSE, &rdl, &sm_type,
                          EC_TIMEOUTRXM);
        if (wkc <= 0) {
            continue;
        }

        uint16_t assign_idx = (uint16_t)(ECT_SDO_PDOASSIGN + sm);

        if (sm_type == 3) {
            /* Outputs: master writes, slave reads (RxPDO from slave's view) */
            int bits = read_pdo_assign((uint16_t)slave_num, assign_idx,
                                       outputs_bits, rx_map);
            outputs_bits += bits;
        } else if (sm_type == 4) {
            /* Inputs: slave writes, master reads (TxPDO from slave's view) */
            int bits = read_pdo_assign((uint16_t)slave_num, assign_idx,
                                       inputs_bits, tx_map);
            inputs_bits += bits;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Print a PDO map table with optional live values                    */
/* ------------------------------------------------------------------ */
static void print_pdo_map(const char *direction, const pdo_map_t *map,
                           const uint8_t *io_data, int io_bytes)
{
    printf("  %s PDO Map (%d entries):\n", direction, map->count);

    if (map->count == 0) {
        printf("    (none)\n");
        return;
    }

    printf("  %-8s  %-5s  %-5s  %-9s  %s\n",
           "Index", "Sub", "Bits", "BitOffset", "Value");
    printf("  %-8s  %-5s  %-5s  %-9s  %s\n",
           "--------", "-----", "-----", "---------", "-----");

    for (int i = 0; i < map->count; i++) {
        const pdo_entry_t *e = &map->entries[i];

        if (e->obj_index == 0x0000 || e->bit_length == 0) {
            printf("  0x0000    0x00   %-5d  %-9d  [padding]\n",
                   e->bit_length, e->bit_offset);
            continue;
        }

        printf("  0x%04X    0x%02X   %-5d  %-9d",
               e->obj_index, e->obj_subidx,
               e->bit_length, e->bit_offset);

        if (io_data != NULL && e->bit_length > 0) {
            int byte_offset = e->bit_offset / 8;
            int bit_in_byte = e->bit_offset % 8;

            printf("  ");
            if (byte_offset >= io_bytes) {
                printf("[out of range]");
            } else {
                switch (e->bit_length) {
                case 1: {
                    uint8_t val = (io_data[byte_offset] >> bit_in_byte) & 0x01;
                    printf("0x%X (%u)", val, val);
                    break;
                }
                case 8: {
                    uint8_t val = io_data[byte_offset];
                    printf("0x%02X (%u)", val, val);
                    break;
                }
                case 16:
                    if (byte_offset + 1 < io_bytes) {
                        uint16_t val = (uint16_t)io_data[byte_offset]
                                     | ((uint16_t)io_data[byte_offset + 1] << 8);
                        printf("0x%04X (%u)", val, val);
                    } else {
                        printf("[truncated]");
                    }
                    break;
                case 32:
                    if (byte_offset + 3 < io_bytes) {
                        uint32_t val = (uint32_t)io_data[byte_offset]
                                     | ((uint32_t)io_data[byte_offset + 1] << 8)
                                     | ((uint32_t)io_data[byte_offset + 2] << 16)
                                     | ((uint32_t)io_data[byte_offset + 3] << 24);
                        printf("0x%08X (%u)", val, val);
                    } else {
                        printf("[truncated]");
                    }
                    break;
                default:
                    printf("[%d-bit raw]", e->bit_length);
                    break;
                }
            }
        } else {
            printf("  [no live data]");
        }
        printf("\n");
    }
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 2) {
#ifdef _WIN32
        fprintf(stderr, "Usage: %s --list\n", argv[0]);
        fprintf(stderr, "       %s <\\Device\\NPF_{GUID}>\n\n", argv[0]);
        fprintf(stderr, "Run '%s --list' to see available device names.\n", argv[0]);
        fprintf(stderr, "Requires Administrator and Npcap installed.\n");
#else
        fprintf(stderr, "Usage: %s --list\n", argv[0]);
        fprintf(stderr, "       sudo %s <interface>\n\n", argv[0]);
        fprintf(stderr, "Run '%s --list' to see available interfaces.\n", argv[0]);
        fprintf(stderr, "Requires root or CAP_NET_RAW (raw socket access).\n");
#endif
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "--list") == 0) {
        list_interfaces();
        return EXIT_SUCCESS;
    }

    const char *ifname = argv[1];

    printf("=== SOEM EtherCAT PDO Dump ===\n");
    printf("Interface: %s\n\n", ifname);

    /* ---- 1. Initialize SOEM (opens raw socket on the interface) ---- */
    if (ecx_init(&ctx, ifname) <= 0) {
        fprintf(stderr,
                "ERROR: Failed to initialize EtherCAT on '%s'.\n"
                "       Check that the interface name is correct.\n"
                "       Are you running as root?\n",
                ifname);
        return EXIT_FAILURE;
    }
    printf("EtherCAT socket opened on %s\n", ifname);

    /* ---- 2. Discover slaves (transitions them to PRE-OP) ---- */
    int slave_count = ecx_config_init(&ctx);
    if (slave_count <= 0) {
        fprintf(stderr,
                "ERROR: No EtherCAT slaves found on %s.\n"
                "       Check cabling and slave power.\n",
                ifname);
        ecx_close(&ctx);
        return EXIT_FAILURE;
    }
    printf("Found %d slave(s)\n\n", slave_count);

    /* ---- 3. Map I/O (sets slave[n].inputs / .outputs pointers) ---- */
    ecx_config_map_group(&ctx, IOmap, 0);

    /* ---- 4. Slave identity table ---- */
    printf("%-6s  %-24s  %-12s  %-12s  %s/%s\n",
           "Slave", "Name", "VendorID", "ProductCode", "OutBytes", "InBytes");
    printf("%-6s  %-24s  %-12s  %-12s  %s\n",
           "------", "------------------------",
           "------------", "------------", "--------");
    for (int s = 1; s <= ctx.slavecount; s++) {
        ec_slavet *sl = &ctx.slavelist[s];
        printf("%-6d  %-24s  0x%08X    0x%08X    %u / %u\n",
               s, sl->name, sl->eep_man, sl->eep_id,
               sl->Obytes, sl->Ibytes);
    }
    printf("\n");

    /* ---- 5. Enumerate PDO mappings via CoE SDO (slaves are in PRE-OP) ---- */
    printf("=== PDO Mappings (CoE SDO reads, slaves in PRE-OP) ===\n\n");

    pdo_map_t *rx_maps = calloc((size_t)(slave_count + 1), sizeof(pdo_map_t));
    pdo_map_t *tx_maps = calloc((size_t)(slave_count + 1), sizeof(pdo_map_t));
    if (!rx_maps || !tx_maps) {
        fprintf(stderr, "ERROR: Out of memory\n");
        free(rx_maps);
        free(tx_maps);
        ecx_close(&ctx);
        return EXIT_FAILURE;
    }

    for (int s = 1; s <= ctx.slavecount; s++) {
        ec_slavet *sl = &ctx.slavelist[s];
        printf("--- Slave %d: %s ---\n", s, sl->name);
        enumerate_pdo_maps(s, &rx_maps[s], &tx_maps[s]);
        print_pdo_map("RxPDO (master->slave)", &rx_maps[s], NULL, 0);
        print_pdo_map("TxPDO (slave->master)", &tx_maps[s], NULL, 0);
        printf("\n");
    }

    /* ---- 6. Configure distributed clocks (safe even if unsupported) ---- */
    ecx_configdc(&ctx);

    /* ---- 7. Transition all slaves to SAFE_OP ---- */
    printf("=== Transitioning to SAFE_OP for live PDO exchange ===\n");

    ctx.slavelist[0].state = EC_STATE_SAFE_OP;
    ecx_writestate(&ctx, 0);

    uint16_t reached = ecx_statecheck(&ctx, 0, EC_STATE_SAFE_OP,
                                      EC_TIMEOUTSTATE * 3);
    if (reached != EC_STATE_SAFE_OP) {
        printf("WARNING: Not all slaves reached SAFE_OP. Individual states:\n");
        for (int s = 1; s <= ctx.slavecount; s++) {
            ec_slavet *sl = &ctx.slavelist[s];
            ecx_readstate(&ctx);
            printf("  Slave %d (%s): state=0x%02X  ALstatus=0x%04X\n",
                   s, sl->name, sl->state, sl->ALstatuscode);
        }
        printf("Continuing with partial data...\n");
    } else {
        printf("All slaves in SAFE_OP.\n");
    }

    /* ---- 8. One PDO exchange cycle ---- */
    memset(IOmap, 0, sizeof(IOmap));

    ecx_send_processdata(&ctx);
    int wkc = ecx_receive_processdata(&ctx, EC_TIMEOUTRET);

    int expected_wkc = (ctx.grouplist[0].outputsWKC * 2)
                       + ctx.grouplist[0].inputsWKC;

    printf("Working counter: %d  (expected: %d)  %s\n\n",
           wkc, expected_wkc,
           (wkc >= expected_wkc) ? "[OK]" : "[MISMATCH - check cabling]");

    /* ---- 9. Print live PDO values ---- */
    printf("=== Live PDO Data ===\n\n");

    for (int s = 1; s <= ctx.slavecount; s++) {
        ec_slavet *sl = &ctx.slavelist[s];
        printf("--- Slave %d: %s ---\n", s, sl->name);

        /* Outputs: master->slave (RxPDO from slave's perspective) */
        if (sl->Obytes > 0 && sl->outputs != NULL) {
            printf("  Output data (%u bytes):\n", sl->Obytes);
            print_pdo_map("RxPDO", &rx_maps[s],
                          sl->outputs, (int)sl->Obytes);
            printf("  Raw bytes: ");
            for (uint32_t b = 0; b < sl->Obytes; b++) {
                printf("%02X ", sl->outputs[b]);
            }
            printf("\n");
        } else {
            printf("  No output PDO data.\n");
        }

        /* Inputs: slave->master (TxPDO from slave's perspective) */
        if (sl->Ibytes > 0 && sl->inputs != NULL) {
            printf("  Input data (%u bytes):\n", sl->Ibytes);
            print_pdo_map("TxPDO", &tx_maps[s],
                          sl->inputs, (int)sl->Ibytes);
            printf("  Raw bytes: ");
            for (uint32_t b = 0; b < sl->Ibytes; b++) {
                printf("%02X ", sl->inputs[b]);
            }
            printf("\n");
        } else {
            printf("  No input PDO data.\n");
        }
        printf("\n");
    }

    /* ---- 10. Cleanup ---- */
    ctx.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&ctx, 0);

    free(rx_maps);
    free(tx_maps);
    ecx_close(&ctx);

    printf("Done.\n");
    return EXIT_SUCCESS;
}
