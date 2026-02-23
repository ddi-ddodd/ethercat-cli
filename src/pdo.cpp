/* pdo.cpp: PDO mapping discovery and display functions */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cinttypes>

#include "pdo.h"

/* ------------------------------------------------------------------ */
/* PDO mapping: read one SyncManager's PDO assignment chain           */
/* ------------------------------------------------------------------ */
int read_pdo_assign(ecx_contextt *ctx, uint16_t slave, uint16_t assign_idx,
                    int map_bit_offset, pdo_map_t *map)
{
    int      rdl, wkc;
    uint16_t num_pdo = 0;
    int      total_bits = 0;

    /* Read subindex 0: count of PDOs assigned to this SM */
    rdl = sizeof(num_pdo);
    wkc = ecx_SDOread(ctx, slave, assign_idx, 0x00,
                      FALSE, &rdl, &num_pdo, EC_TIMEOUTRXM);
    if (wkc <= 0 || num_pdo == 0) {
        return 0;
    }

    for (uint16_t i = 1; i <= num_pdo; i++) {
        uint16_t pdo_obj_idx = 0;
        rdl = sizeof(pdo_obj_idx);
        wkc = ecx_SDOread(ctx, slave, assign_idx, static_cast<uint8_t>(i),
                          FALSE, &rdl, &pdo_obj_idx, EC_TIMEOUTRXM);
        if (wkc <= 0) {
            continue;
        }

        /* Read the PDO mapping object: subindex 0 = entry count */
        uint8_t subcnt = 0;
        rdl = sizeof(subcnt);
        wkc = ecx_SDOread(ctx, slave, pdo_obj_idx, 0x00,
                          FALSE, &rdl, &subcnt, EC_TIMEOUTRXM);
        if (wkc <= 0) {
            continue;
        }

        for (uint8_t j = 1; j <= subcnt; j++) {
            int32_t rdat32 = 0;
            rdl = sizeof(rdat32);
            wkc = ecx_SDOread(ctx, slave, pdo_obj_idx, j,
                              FALSE, &rdl, &rdat32, EC_TIMEOUTRXM);
            if (wkc <= 0) {
                continue;
            }

            /* Decode bit-packed entry:
             *   bits [31:16] = mapped object index
             *   bits [15:8]  = mapped object subindex
             *   bits [7:0]   = bit length (0 = padding gap)
             */
            auto bit_len    = static_cast<uint8_t>(rdat32 & 0xFF);
            auto obj_subidx = static_cast<uint8_t>((rdat32 >> 8) & 0xFF);
            auto obj_idx    = static_cast<uint16_t>((rdat32 >> 16) & 0xFFFF);

            if (map->count < MAX_PDO_ENTRIES) {
                pdo_entry_t &e = map->entries[map->count];
                e.obj_index  = obj_idx;
                e.obj_subidx = obj_subidx;
                e.bit_length = bit_len;
                e.bit_offset = map_bit_offset + total_bits;
                map->count++;
            }

            total_bits += bit_len;
        }
    }

    return total_bits;
}

/* ------------------------------------------------------------------ */
/* PDO mapping: enumerate RxPDO and TxPDO for one slave via CoE SDO  */
/* ------------------------------------------------------------------ */
void enumerate_pdo_maps(ecx_contextt *ctx, int slave_num,
                        pdo_map_t *rx_map, pdo_map_t *tx_map)
{
    int     rdl, wkc;
    uint8_t nSM = 0;
    int     outputs_bits = 0;
    int     inputs_bits  = 0;

    std::memset(rx_map, 0, sizeof(*rx_map));
    std::memset(tx_map, 0, sizeof(*tx_map));

    /* Read number of SyncManagers */
    rdl = sizeof(nSM);
    wkc = ecx_SDOread(ctx, static_cast<uint16_t>(slave_num), ECT_SDO_SMCOMMTYPE,
                      0x00, FALSE, &rdl, &nSM, EC_TIMEOUTRXM);
    if (wkc <= 0) {
        std::printf("    [WARNING] Cannot read SM comm type for slave %d"
                    " (no CoE mailbox?)\n", slave_num);
        return;
    }

    /* SM 0 and 1 are mailbox channels; process data starts at SM 2 */
    for (uint8_t sm = 2; sm < nSM; sm++) {
        uint8_t sm_type = 0;
        rdl = sizeof(sm_type);
        wkc = ecx_SDOread(ctx, static_cast<uint16_t>(slave_num), ECT_SDO_SMCOMMTYPE,
                          static_cast<uint8_t>(sm + 1), FALSE, &rdl, &sm_type,
                          EC_TIMEOUTRXM);
        if (wkc <= 0) {
            continue;
        }

        auto assign_idx = static_cast<uint16_t>(ECT_SDO_PDOASSIGN + sm);

        if (sm_type == 3) {
            /* Outputs: master writes, slave reads (RxPDO from slave's view) */
            int bits = read_pdo_assign(ctx, static_cast<uint16_t>(slave_num),
                                       assign_idx, outputs_bits, rx_map);
            outputs_bits += bits;
        } else if (sm_type == 4) {
            /* Inputs: slave writes, master reads (TxPDO from slave's view) */
            int bits = read_pdo_assign(ctx, static_cast<uint16_t>(slave_num),
                                       assign_idx, inputs_bits, tx_map);
            inputs_bits += bits;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Print a PDO map table with optional live values                    */
/* ------------------------------------------------------------------ */
void print_pdo_map(const char *direction, const pdo_map_t *map,
                   const uint8_t *io_data, int io_bytes)
{
    std::printf("  %s PDO Map (%d entries):\n", direction, map->count);

    if (map->count == 0) {
        std::printf("    (none)\n");
        return;
    }

    std::printf("  %-8s  %-5s  %-5s  %-9s  %s\n",
                "Index", "Sub", "Bits", "BitOffset", "Value");
    std::printf("  %-8s  %-5s  %-5s  %-9s  %s\n",
                "--------", "-----", "-----", "---------", "-----");

    for (int i = 0; i < map->count; i++) {
        const pdo_entry_t &e = map->entries[i];

        if (e.obj_index == 0x0000 || e.bit_length == 0) {
            std::printf("  0x0000    0x00   %-5d  %-9d  [padding]\n",
                        e.bit_length, e.bit_offset);
            continue;
        }

        std::printf("  0x%04X    0x%02X   %-5d  %-9d",
                    e.obj_index, e.obj_subidx,
                    e.bit_length, e.bit_offset);

        if (io_data != nullptr && e.bit_length > 0) {
            int byte_offset = e.bit_offset / 8;
            int bit_in_byte = e.bit_offset % 8;

            std::printf("  ");
            if (byte_offset >= io_bytes) {
                std::printf("[out of range]");
            } else {
                switch (e.bit_length) {
                case 1: {
                    uint8_t val = (io_data[byte_offset] >> bit_in_byte) & 0x01;
                    std::printf("0x%X (%u)", val, val);
                    break;
                }
                case 8: {
                    uint8_t val = io_data[byte_offset];
                    std::printf("0x%02X (%u)", val, val);
                    break;
                }
                case 16:
                    if (byte_offset + 1 < io_bytes) {
                        uint16_t val = static_cast<uint16_t>(io_data[byte_offset])
                                     | (static_cast<uint16_t>(io_data[byte_offset + 1]) << 8);
                        std::printf("0x%04X (%u)", val, val);
                    } else {
                        std::printf("[truncated]");
                    }
                    break;
                case 32:
                    if (byte_offset + 3 < io_bytes) {
                        uint32_t val = static_cast<uint32_t>(io_data[byte_offset])
                                     | (static_cast<uint32_t>(io_data[byte_offset + 1]) << 8)
                                     | (static_cast<uint32_t>(io_data[byte_offset + 2]) << 16)
                                     | (static_cast<uint32_t>(io_data[byte_offset + 3]) << 24);
                        std::printf("0x%08X (%u)", val, val);
                    } else {
                        std::printf("[truncated]");
                    }
                    break;
                default:
                    std::printf("[%d-bit raw]", e.bit_length);
                    break;
                }
            }
        } else {
            std::printf("  [no live data]");
        }
        std::printf("\n");
    }
}
