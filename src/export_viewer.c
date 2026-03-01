#include "export_viewer.h"
#include "export_json.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2common/kernel/iofilemgr.h>
#include <stdio.h>
#include <string.h>

static int parse_u64(const char *s, uint64_t *out) {
  unsigned long long v = 0ULL;
  if (sscanf(s, "%llu", &v) == 1) {
    *out = (uint64_t)v;
    return 0;
  }
  return -1;
}

static void parse_json_summary(const char *buf, ExportSummary *out) {
  const char *p = strstr(buf, "\"timestamp_us\":");
  if (p != NULL) {
    p += 15;
    while (*p == ' ') p++;
    (void)parse_u64(p, &out->timestamp_us);
  }
  p = strstr(buf, "\"host_count\":");
  if (p != NULL) {
    p += 13;
    while (*p == ' ') p++;
    unsigned int hc = 0;
    if (sscanf(p, "%u", &hc) == 1) {
      out->host_count = hc;
    }
  }
  p = strstr(buf, "\"subnet\":");
  if (p != NULL) {
    p = strchr(p, '\"');
    if (p != NULL) p = strchr(p + 1, '\"');
    if (p != NULL) {
      p++;
      const char *end = strchr(p, '\"');
      if (end != NULL) {
        size_t n = (size_t)(end - p);
        if (n >= sizeof(out->subnet)) n = sizeof(out->subnet) - 1;
        memcpy(out->subnet, p, n);
        out->subnet[n] = '\0';
      }
    }
  }

  const char *cursor = buf;
  while ((cursor = strstr(cursor, "\"ip\":\"")) != NULL) {
    cursor += 6;
    const char *end = strchr(cursor, '\"');
    if (end == NULL) break;
    size_t n = (size_t)(end - cursor);
    if (out->all_host_count < LAN_SCANNER_MAX_HOSTS) {
      if (n >= sizeof(out->all_hosts[0])) n = sizeof(out->all_hosts[0]) - 1;
      memcpy(out->all_hosts[out->all_host_count], cursor, n);
      out->all_hosts[out->all_host_count][n] = '\0';
      out->all_host_count++;
      if (out->preview_count < EXPORT_VIEWER_HOST_PREVIEW) {
        memcpy(out->preview_hosts[out->preview_count],
               out->all_hosts[out->all_host_count - 1],
               sizeof(out->preview_hosts[out->preview_count]));
        out->preview_count++;
      }
    }
    cursor = end + 1;
  }
}

static void load_export_file(ExportSummary *out) {
  SceUID fd = sceIoOpen(out->path, SCE_O_RDONLY, 0);
  if (fd < 0) {
    return;
  }
  char buf[4096];
  const int rc = (int)sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (rc <= 0) return;
  buf[rc] = '\0';
  parse_json_summary(buf, out);
}

void export_viewer_init(ExportViewer *viewer) {
  memset(viewer, 0, sizeof(*viewer));
}

int export_viewer_reload(ExportViewer *viewer) {
  memset(viewer, 0, sizeof(*viewer));
  SceUID dfd = sceIoDopen(EXPORT_DIR);
  if (dfd < 0) {
    return dfd;
  }

  SceIoDirent ent;
  memset(&ent, 0, sizeof(ent));
  while (sceIoDread(dfd, &ent) > 0 && viewer->count < EXPORT_VIEWER_MAX_FILES) {
    if (strncmp(ent.d_name, "scan_", 5) == 0 && strstr(ent.d_name, ".json") != NULL) {
      ExportSummary *e = &viewer->exports[viewer->count++];
      memset(e, 0, sizeof(*e));
      snprintf(e->path, sizeof(e->path), "%s/%s", EXPORT_DIR, ent.d_name);
      snprintf(e->label, sizeof(e->label), "%s", ent.d_name);
      load_export_file(e);
    }
    memset(&ent, 0, sizeof(ent));
  }
  sceIoDclose(dfd);

  for (uint32_t i = 1; i < viewer->count; i++) {
    ExportSummary key = viewer->exports[i];
    int j = (int)i - 1;
    while (j >= 0 && viewer->exports[j].timestamp_us < key.timestamp_us) {
      viewer->exports[j + 1] = viewer->exports[j];
      j--;
    }
    viewer->exports[j + 1] = key;
  }
  return 0;
}
