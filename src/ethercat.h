/* ethercat.h: EtherCAT initialization, PDO exchange, and cleanup */

#ifndef ETHERCAT_H
#define ETHERCAT_H

extern "C" {
#include "soem/soem.h"
}

/* 4096 bytes covers virtually any realistic slave configuration.
 * Each slave rarely uses more than a few dozen bytes of PDO data. */
constexpr int IOMAP_SIZE = 4096;

/* Run the full EtherCAT scan-and-dump sequence on the given interface.
 *
 * 1. Opens a raw socket on the interface
 * 2. Discovers slaves (PRE-OP)
 * 3. Maps I/O
 * 4. Enumerates PDO mappings via CoE SDO
 * 5. Transitions to SAFE_OP
 * 6. Performs one PDO exchange cycle
 * 7. Prints live PDO data
 * 8. Cleans up
 *
 * Returns 0 on success, non-zero on failure.
 */
int ethercat_run(const char *ifname);

#endif /* ETHERCAT_H */
