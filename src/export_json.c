#include "export_json.h"

#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2common/kernel/iofilemgr.h>
#include <stdio.h>
#include <string.h>

static int ensure_dirs(void) {
  (void)sceIoMkdir("ux0:data", SCE_SO_IFDIR | 0777);
  (void)sceIoMkdir("ux0:data/vita_wifi_scope", SCE_SO_IFDIR | 0777);
  (void)sceIoMkdir(EXPORT_DIR, SCE_SO_IFDIR | 0777);
  return 0;
}

static int write_all(SceUID fd, const char *data) {
  const size_t len = strlen(data);
  return (sceIoWrite(fd, data, len) == (SceSSize)len) ? 0 : -1;
}

static void append_source_flags(char *out, size_t out_len, uint32_t flags) {
  out[0] = '\0';
  if (flags & DISCOVERY_SRC_MDNS) strncat(out, "mDNS ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_SSDP) strncat(out, "SSDP ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_NBNS) strncat(out, "NBNS ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_ICMP) strncat(out, "ICMP ", out_len - strlen(out) - 1);
  if (flags & DISCOVERY_SRC_TCP) strncat(out, "TCP ", out_len - strlen(out) - 1);
}

int export_json_write_snapshot(const LanScannerMetrics *scanner,
                               uint64_t now_us,
                               char *out_path,
                               size_t out_path_len) {
  ensure_dirs();

  char path[128];
  snprintf(path, sizeof(path), EXPORT_DIR "/scan_%llu.json", (unsigned long long)now_us);
  if (out_path != NULL && out_path_len > 0) {
    snprintf(out_path, out_path_len, "%s", path);
  }

  SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) {
    return fd;
  }

  char line[512];
  snprintf(line, sizeof(line),
           "{\n"
           "  \"schema_version\": 1,\n"
           "  \"timestamp_us\": %llu,\n"
           "  \"subnet\": \"%s\",\n"
           "  \"host_count\": %u,\n"
           "  \"hosts\": [\n",
           (unsigned long long)now_us, scanner->subnet_cidr, scanner->host_count);
  if (write_all(fd, line) < 0) goto fail;

  for (uint32_t i = 0; i < scanner->host_count; i++) {
    const LanHostResult *h = &scanner->hosts[i];
    char src[64];
    append_source_flags(src, sizeof(src), h->source_flags);
    snprintf(line, sizeof(line),
             "    {\"ip\":\"%s\",\"hostname\":\"%s\",\"service_hint\":\"%s\",\"sources\":\"%s\",\"is_gateway\":%u,\"open_port_count\":%u}",
             h->ip, h->hostname, h->service_hint, src, (unsigned int)h->is_gateway, (unsigned int)h->open_port_count);
    if (write_all(fd, line) < 0) goto fail;
    if (i + 1U < scanner->host_count) {
      if (write_all(fd, ",\n") < 0) goto fail;
    } else {
      if (write_all(fd, "\n") < 0) goto fail;
    }
  }
  if (write_all(fd, "  ]\n}\n") < 0) goto fail;
  sceIoClose(fd);
  return 0;

fail:
  sceIoClose(fd);
  return -1;
}

int export_json_rebuild_index(void) {
  ensure_dirs();
  SceUID dfd = sceIoDopen(EXPORT_DIR);
  if (dfd < 0) {
    return dfd;
  }

  SceUID fd = sceIoOpen(EXPORT_INDEX_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) {
    sceIoDclose(dfd);
    return fd;
  }

  if (write_all(fd, "{\n  \"exports\": [\n") < 0) {
    sceIoClose(fd);
    sceIoDclose(dfd);
    return -1;
  }

  SceIoDirent ent;
  memset(&ent, 0, sizeof(ent));
  int first = 1;
  while (sceIoDread(dfd, &ent) > 0) {
    if (strncmp(ent.d_name, "scan_", 5) != 0) {
      memset(&ent, 0, sizeof(ent));
      continue;
    }
    if (!first) {
      if (write_all(fd, ",\n") < 0) break;
    }
    first = 0;
    char line[320];
    snprintf(line, sizeof(line), "    \"%s/%s\"", EXPORT_DIR, ent.d_name);
    if (write_all(fd, line) < 0) break;
    memset(&ent, 0, sizeof(ent));
  }
  (void)write_all(fd, "\n  ]\n}\n");

  sceIoClose(fd);
  sceIoDclose(dfd);
  return 0;
}
