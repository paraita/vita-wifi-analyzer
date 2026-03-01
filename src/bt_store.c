#include "bt_store.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2common/kernel/iofilemgr.h>
#include <string.h>
#include <stdio.h>

#define BT_STORE_DIR "ux0:data/vita_wifi_scope"
#define BT_STORE_FILE BT_STORE_DIR "/bt_seen.txt"

const char *bt_store_guess_type(uint32_t class_code) {
  const uint8_t major = (uint8_t)((class_code >> 8) & 0x1F);
  if (major == 1) return "computer";
  if (major == 2) return "phone";
  if (major == 4) return "audio";
  if (major == 5) return "controller";
  return "unknown";
}

static BtSeenDevice *find_or_add(BtStore *s, const char *mac) {
  for (uint32_t i = 0; i < s->count; i++) {
    if (strcmp(s->devices[i].mac, mac) == 0) return &s->devices[i];
  }
  if (s->count >= BT_STORE_MAX_DEVICES) return NULL;
  BtSeenDevice *d = &s->devices[s->count++];
  memset(d, 0, sizeof(*d));
  snprintf(d->mac, sizeof(d->mac), "%s", mac);
  return d;
}

void bt_store_init(BtStore *s) {
  memset(s, 0, sizeof(*s));
}

void bt_store_update(BtStore *s, const BtMonitorMetrics *m, uint64_t now_us) {
  for (uint32_t i = 0; i < m->paired_count; i++) {
    const BtPairedDevice *p = &m->paired[i];
    BtSeenDevice *d = find_or_add(s, p->mac);
    if (!d) continue;
    if (d->first_seen_us == 0) d->first_seen_us = now_us;
    d->last_seen_us = now_us;
    d->seen_count++;
    d->class_code = p->bt_class;
    snprintf(d->type, sizeof(d->type), "%s", bt_store_guess_type(p->bt_class));
    if (p->name[0]) snprintf(d->name, sizeof(d->name), "%s", p->name);
  }

  for (uint32_t i = 0; i < m->event_count; i++) {
    const BtEventEntry *e = &m->events[i];
    BtSeenDevice *d = find_or_add(s, e->mac);
    if (!d) continue;
    if (d->first_seen_us == 0) d->first_seen_us = e->timestamp_us;
    d->last_seen_us = e->timestamp_us;
    d->seen_count++;
    if (d->type[0] == '\0') snprintf(d->type, sizeof(d->type), "unknown");
  }
}

int bt_store_load(BtStore *s) {
  bt_store_init(s);
  SceUID fd = sceIoOpen(BT_STORE_FILE, SCE_O_RDONLY, 0);
  if (fd < 0) return fd;
  char buf[8192];
  const int n = (int)sceIoRead(fd, buf, sizeof(buf) - 1);
  sceIoClose(fd);
  if (n <= 0) return -1;
  buf[n] = '\0';

  char *line = buf;
  while (line && *line && s->count < BT_STORE_MAX_DEVICES) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    BtSeenDevice *d = &s->devices[s->count];
    memset(d, 0, sizeof(*d));
    unsigned long long first = 0ULL;
    unsigned long long last = 0ULL;
    unsigned int seen = 0U;
    unsigned int cls = 0U;
    if (sscanf(line, "%17[^|]|%47[^|]|%15[^|]|%x|%llu|%llu|%u",
               d->mac, d->name, d->type, &cls, &first, &last, &seen) >= 4) {
      d->class_code = cls;
      d->first_seen_us = (uint64_t)first;
      d->last_seen_us = (uint64_t)last;
      d->seen_count = seen;
      s->count++;
    }
    if (!nl) break;
    line = nl + 1;
  }
  return 0;
}

int bt_store_save(BtStore *s, uint64_t now_us) {
  if (now_us - s->last_save_us < 8000000ULL) {
    return 0;
  }
  s->last_save_us = now_us;

  (void)sceIoMkdir("ux0:data", SCE_SO_IFDIR | 0777);
  (void)sceIoMkdir(BT_STORE_DIR, SCE_SO_IFDIR | 0777);
  SceUID fd = sceIoOpen(BT_STORE_FILE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) return fd;
  for (uint32_t i = 0; i < s->count; i++) {
    char line[256];
    const BtSeenDevice *d = &s->devices[i];
    const int len = snprintf(line, sizeof(line), "%s|%s|%s|%X|%llu|%llu|%u\n",
                             d->mac, d->name, d->type, d->class_code,
                             (unsigned long long)d->first_seen_us,
                             (unsigned long long)d->last_seen_us,
                             d->seen_count);
    if (len > 0) {
      (void)sceIoWrite(fd, line, (SceSize)len);
    }
  }
  sceIoClose(fd);
  return 0;
}
