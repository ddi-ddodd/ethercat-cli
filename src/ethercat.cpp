/* ethercat.cpp: EtherCAT initialization, PDO exchange, and cleanup */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "ethercat.h"
#include "pdo.h"

static ecx_contextt ctx;

/* IOmap must be global so that slavelist[n].inputs/outputs pointers
 * (set by ecx_config_map_group) remain valid for the program's lifetime. */
static uint8_t IOmap[IOMAP_SIZE];

int ethercat_run(const char *ifname)
{
    std::printf("=== SOEM EtherCAT PDO Dump ===\n");
    std::printf("Interface: %s\n\n", ifname);

    /* ---- 1. Initialize SOEM (opens raw socket on the interface) ---- */
    if (ecx_init(&ctx, ifname) <= 0) {
        std::fprintf(stderr,
                "ERROR: Failed to initialize EtherCAT on '%s'.\n"
                "       Check that the interface name is correct.\n"
                "       Are you running as root?\n",
                ifname);
        return EXIT_FAILURE;
    }
    std::printf("EtherCAT socket opened on %s\n", ifname);

    /* ---- 2. Discover slaves (transitions them to PRE-OP) ---- */
    int slave_count = ecx_config_init(&ctx);
    if (slave_count <= 0) {
        std::fprintf(stderr,
                "ERROR: No EtherCAT slaves found on %s.\n"
                "       Check cabling and slave power.\n",
                ifname);
        ecx_close(&ctx);
        return EXIT_FAILURE;
    }
    std::printf("Found %d slave(s)\n\n", slave_count);

    /* ---- 3. Map I/O (sets slave[n].inputs / .outputs pointers) ---- */
    ecx_config_map_group(&ctx, IOmap, 0);

    /* ---- 4. Slave identity table ---- */
    std::printf("%-6s  %-24s  %-12s  %-12s  %s/%s\n",
                "Slave", "Name", "VendorID", "ProductCode", "OutBytes", "InBytes");
    std::printf("%-6s  %-24s  %-12s  %-12s  %s\n",
                "------", "------------------------",
                "------------", "------------", "--------");
    for (int s = 1; s <= ctx.slavecount; s++) {
        ec_slavet *sl = &ctx.slavelist[s];
        std::printf("%-6d  %-24s  0x%08X    0x%08X    %u / %u\n",
                    s, sl->name, sl->eep_man, sl->eep_id,
                    sl->Obytes, sl->Ibytes);
    }
    std::printf("\n");

    /* ---- 5. Enumerate PDO mappings via CoE SDO (slaves are in PRE-OP) ---- */
    std::printf("=== PDO Mappings (CoE SDO reads, slaves in PRE-OP) ===\n\n");

    auto *rx_maps = static_cast<pdo_map_t *>(
        std::calloc(static_cast<size_t>(slave_count + 1), sizeof(pdo_map_t)));
    auto *tx_maps = static_cast<pdo_map_t *>(
        std::calloc(static_cast<size_t>(slave_count + 1), sizeof(pdo_map_t)));
    if (!rx_maps || !tx_maps) {
        std::fprintf(stderr, "ERROR: Out of memory\n");
        std::free(rx_maps);
        std::free(tx_maps);
        ecx_close(&ctx);
        return EXIT_FAILURE;
    }

    for (int s = 1; s <= ctx.slavecount; s++) {
        ec_slavet *sl = &ctx.slavelist[s];
        std::printf("--- Slave %d: %s ---\n", s, sl->name);
        enumerate_pdo_maps(&ctx, s, &rx_maps[s], &tx_maps[s]);
        print_pdo_map("RxPDO (master->slave)", &rx_maps[s], nullptr, 0);
        print_pdo_map("TxPDO (slave->master)", &tx_maps[s], nullptr, 0);
        std::printf("\n");
    }

    /* ---- 6. Configure distributed clocks (safe even if unsupported) ---- */
    ecx_configdc(&ctx);

    /* ---- 7. Transition all slaves to SAFE_OP ---- */
    std::printf("=== Transitioning to SAFE_OP for live PDO exchange ===\n");

    ctx.slavelist[0].state = EC_STATE_SAFE_OP;
    ecx_writestate(&ctx, 0);

    uint16_t reached = ecx_statecheck(&ctx, 0, EC_STATE_SAFE_OP,
                                      EC_TIMEOUTSTATE * 3);
    if (reached != EC_STATE_SAFE_OP) {
        std::printf("WARNING: Not all slaves reached SAFE_OP. Individual states:\n");
        for (int s = 1; s <= ctx.slavecount; s++) {
            ec_slavet *sl = &ctx.slavelist[s];
            ecx_readstate(&ctx);
            std::printf("  Slave %d (%s): state=0x%02X  ALstatus=0x%04X\n",
                        s, sl->name, sl->state, sl->ALstatuscode);
        }
        std::printf("Continuing with partial data...\n");
    } else {
        std::printf("All slaves in SAFE_OP.\n");
    }

    /* ---- 8. One PDO exchange cycle ---- */
    std::memset(IOmap, 0, sizeof(IOmap));

    ecx_send_processdata(&ctx);
    int wkc = ecx_receive_processdata(&ctx, EC_TIMEOUTRET);

    int expected_wkc = (ctx.grouplist[0].outputsWKC * 2)
                       + ctx.grouplist[0].inputsWKC;

    std::printf("Working counter: %d  (expected: %d)  %s\n\n",
                wkc, expected_wkc,
                (wkc >= expected_wkc) ? "[OK]" : "[MISMATCH - check cabling]");

    /* ---- 9. Print live PDO values ---- */
    std::printf("=== Live PDO Data ===\n\n");

    for (int s = 1; s <= ctx.slavecount; s++) {
        ec_slavet *sl = &ctx.slavelist[s];
        std::printf("--- Slave %d: %s ---\n", s, sl->name);

        /* Outputs: master->slave (RxPDO from slave's perspective) */
        if (sl->Obytes > 0 && sl->outputs != nullptr) {
            std::printf("  Output data (%u bytes):\n", sl->Obytes);
            print_pdo_map("RxPDO", &rx_maps[s],
                          sl->outputs, static_cast<int>(sl->Obytes));
            std::printf("  Raw bytes: ");
            for (uint32_t b = 0; b < sl->Obytes; b++) {
                std::printf("%02X ", sl->outputs[b]);
            }
            std::printf("\n");
        } else {
            std::printf("  No output PDO data.\n");
        }

        /* Inputs: slave->master (TxPDO from slave's perspective) */
        if (sl->Ibytes > 0 && sl->inputs != nullptr) {
            std::printf("  Input data (%u bytes):\n", sl->Ibytes);
            print_pdo_map("TxPDO", &tx_maps[s],
                          sl->inputs, static_cast<int>(sl->Ibytes));
            std::printf("  Raw bytes: ");
            for (uint32_t b = 0; b < sl->Ibytes; b++) {
                std::printf("%02X ", sl->inputs[b]);
            }
            std::printf("\n");
        } else {
            std::printf("  No input PDO data.\n");
        }
        std::printf("\n");
    }

    /* ---- 10. Cleanup ---- */
    ctx.slavelist[0].state = EC_STATE_INIT;
    ecx_writestate(&ctx, 0);

    std::free(rx_maps);
    std::free(tx_maps);
    ecx_close(&ctx);

    std::printf("Done.\n");
    return EXIT_SUCCESS;
}
