/* pdo.h: PDO mapping types and discovery/display functions */

#ifndef PDO_H
#define PDO_H

#include <cstdint>

extern "C" {
#include "soem/soem.h"
}

/* Maximum PDO entries tracked per direction per slave */
constexpr int MAX_PDO_ENTRIES = 256;

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/* Decoded PDO map entry from the 32-bit packed format:
 *   bits [31:16] = CANopen object index
 *   bits [15:8]  = CANopen subindex
 *   bits [7:0]   = bit length (0 = padding/gap)
 */
struct pdo_entry_t {
    uint16_t obj_index;
    uint8_t  obj_subidx;
    uint8_t  bit_length;
    int      bit_offset;  /* bit offset within this slave's IO area */
};

struct pdo_map_t {
    int         count;
    pdo_entry_t entries[MAX_PDO_ENTRIES];
};

/* ------------------------------------------------------------------ */
/* Functions                                                           */
/* ------------------------------------------------------------------ */

/* Read one SyncManager's PDO assignment chain via CoE SDO.
 *
 * ctx:            SOEM context
 * slave:          slave index
 * assign_idx:     0x1C10 + sm_number (e.g. 0x1C12 for SM2/outputs)
 * map_bit_offset: running bit offset within this slave's IO area
 * map:            output structure to populate
 *
 * Returns total number of bits mapped.
 */
int read_pdo_assign(ecx_contextt *ctx, uint16_t slave, uint16_t assign_idx,
                    int map_bit_offset, pdo_map_t *map);

/* Discover RxPDO and TxPDO for one slave via CoE SDO.
 *
 * Uses ECT_SDO_SMCOMMTYPE (0x1C00) to discover SyncManager roles:
 *   sm_type 3 = process data output (master->slave, RxPDO)
 *   sm_type 4 = process data input  (slave->master, TxPDO)
 */
void enumerate_pdo_maps(ecx_contextt *ctx, int slave_num,
                        pdo_map_t *rx_map, pdo_map_t *tx_map);

/* Print a PDO map table with optional live values. */
void print_pdo_map(const char *direction, const pdo_map_t *map,
                   const uint8_t *io_data, int io_bytes);

/* Allocate and enumerate PDO mappings for all slaves (called in PRE-OP).
 *
 * Allocates rx_maps and tx_maps arrays (caller must free with pdo_maps_free),
 * then enumerates and prints the PDO mappings for each slave.
 *
 * Returns 0 on success, non-zero on allocation failure.
 */
int pdo_maps_enumerate(ecx_contextt *ctx, int slave_count,
                       pdo_map_t **rx_maps_out, pdo_map_t **tx_maps_out);

/* Free PDO map arrays allocated by pdo_maps_enumerate. */
void pdo_maps_free(pdo_map_t *rx_maps, pdo_map_t *tx_maps);

/* Print live PDO data for all slaves after a PDO exchange cycle. */
void pdo_print_live_data(ecx_contextt *ctx,
                         const pdo_map_t *rx_maps,
                         const pdo_map_t *tx_maps);

#endif /* PDO_H */
