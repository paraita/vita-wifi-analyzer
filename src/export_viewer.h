#ifndef EXPORT_VIEWER_H
#define EXPORT_VIEWER_H

#include "lan_scanner.h"
#include <stdint.h>

#define EXPORT_VIEWER_MAX_FILES 64
#define EXPORT_VIEWER_HOST_PREVIEW 6

typedef struct ExportSummary {
  char path[128];
  char label[48];
  char subnet[20];
  uint64_t timestamp_us;
  uint32_t host_count;
  char all_hosts[LAN_SCANNER_MAX_HOSTS][16];
  uint32_t all_host_count;
  char preview_hosts[EXPORT_VIEWER_HOST_PREVIEW][16];
  uint32_t preview_count;
} ExportSummary;

typedef struct ExportViewer {
  ExportSummary exports[EXPORT_VIEWER_MAX_FILES];
  uint32_t count;
} ExportViewer;

void export_viewer_init(ExportViewer *viewer);
int export_viewer_reload(ExportViewer *viewer);

#endif
