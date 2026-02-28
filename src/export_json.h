#ifndef EXPORT_JSON_H
#define EXPORT_JSON_H

#include "lan_scanner.h"
#include <stddef.h>
#include <stdint.h>

#define EXPORT_DIR "ux0:data/vita_wifi_scope/exports"
#define EXPORT_INDEX_FILE "ux0:data/vita_wifi_scope/exports/index.json"

int export_json_write_snapshot(const LanScannerMetrics *scanner,
                               uint64_t now_us,
                               char *out_path,
                               size_t out_path_len);
int export_json_rebuild_index(void);

#endif
