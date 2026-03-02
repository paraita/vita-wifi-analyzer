#include "nfc_scanner.h"

#include <stdio.h>
#include <string.h>

/* SceNfc types.  No official stub library is available in the baseline VitaSDK,
   so we provide local fallback stubs that return -1 (hardware unavailable).
   If a real SceNfc_stub is added to the toolchain in future, link it and
   remove the static definitions below. */
typedef struct SceNfcTagInfo {
  uint8_t  uid[10];
  uint8_t  uid_len;
  uint8_t  type;
  uint16_t atqa;
  uint8_t  sak;
  uint8_t  _pad[4];
} SceNfcTagInfo;

static int sceNfcOpen(const char *name, int flags)             { (void)name; (void)flags; return -1; }
static int sceNfcClose(int fd)                                  { (void)fd; return 0; }
static int sceNfcStartDetection(int fd, int flags)             { (void)fd; (void)flags; return -1; }
static int sceNfcStopDetection(int fd)                          { (void)fd; return 0; }
static int sceNfcGetTagInfo(int fd, SceNfcTagInfo *info)       { (void)fd; (void)info; return -1; }

static void format_uid(const uint8_t *uid, uint8_t len, char *out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }
  uint8_t n = len;
  if (n > NFC_TAG_UID_MAX) {
    n = NFC_TAG_UID_MAX;
  }
  for (uint8_t i = 0; i < n; i++) {
    char part[4];
    snprintf(part, sizeof(part), "%s%02X", (i == 0) ? "" : ":", uid[i]);
    const size_t rem = out_len - strlen(out) - 1U;
    if (rem == 0U) {
      break;
    }
    strncat(out, part, rem);
  }
}

void nfc_scanner_init(NfcScanner *s) {
  memset(s, 0, sizeof(*s));
  s->fd = -1;
  const int fd = sceNfcOpen("NFC0:", 0);
  if (fd >= 0) {
    s->fd = fd;
    s->metrics.hw_available = 1;
  } else {
    s->metrics.last_error = fd;
  }
}

void nfc_scanner_term(NfcScanner *s) {
  if (s->metrics.detecting && s->fd >= 0) {
    (void)sceNfcStopDetection(s->fd);
    s->metrics.detecting = 0;
  }
  if (s->fd >= 0) {
    (void)sceNfcClose(s->fd);
    s->fd = -1;
  }
}

int nfc_scanner_set_detecting(NfcScanner *s, int enabled) {
  if (!s->metrics.hw_available || s->fd < 0) {
    return -1;
  }
  int rc;
  if (enabled) {
    rc = sceNfcStartDetection(s->fd, 0);
  } else {
    rc = sceNfcStopDetection(s->fd);
  }
  if (rc < 0) {
    s->metrics.last_error = rc;
    return rc;
  }
  s->metrics.detecting = enabled ? 1U : 0U;
  return 0;
}

void nfc_scanner_clear(NfcScanner *s) {
  s->metrics.tag_count = 0;
  s->metrics.total_seen = 0;
  memset(s->metrics.tags, 0, sizeof(s->metrics.tags));
}

void nfc_scanner_poll(NfcScanner *s, uint64_t now_us) {
  if (!s->metrics.hw_available || !s->metrics.detecting || s->fd < 0) {
    return;
  }
  if (now_us < s->next_poll_us) {
    return;
  }
  s->next_poll_us = now_us + 100000ULL; /* 100 ms between polls */

  SceNfcTagInfo info;
  memset(&info, 0, sizeof(info));
  const int rc = sceNfcGetTagInfo(s->fd, &info);
  if (rc < 0) {
    /* No tag present (normal) — not logged as error */
    return;
  }

  /* Check for existing entry with same UID */
  for (uint32_t i = 0; i < s->metrics.tag_count; i++) {
    NfcTagRecord *r = &s->metrics.tags[i];
    if (r->uid_len == info.uid_len &&
        memcmp(r->uid, info.uid, (size_t)info.uid_len) == 0) {
      r->seen_count++;
      r->last_seen_us = now_us;
      s->metrics.total_seen++;
      return;
    }
  }

  /* New tag — find or allocate slot */
  uint32_t slot;
  if (s->metrics.tag_count < NFC_SCANNER_MAX_TAGS) {
    slot = s->metrics.tag_count++;
  } else {
    /* Ring buffer: overwrite oldest */
    slot = 0;
    uint64_t oldest = s->metrics.tags[0].last_seen_us;
    for (uint32_t i = 1; i < NFC_SCANNER_MAX_TAGS; i++) {
      if (s->metrics.tags[i].last_seen_us < oldest) {
        oldest = s->metrics.tags[i].last_seen_us;
        slot = i;
      }
    }
  }

  NfcTagRecord *r = &s->metrics.tags[slot];
  memset(r, 0, sizeof(*r));
  uint8_t uid_len = info.uid_len;
  if (uid_len > NFC_TAG_UID_MAX) {
    uid_len = NFC_TAG_UID_MAX;
  }
  memcpy(r->uid, info.uid, (size_t)uid_len);
  r->uid_len   = uid_len;
  r->tag_type  = info.type;
  r->atqa      = info.atqa;
  r->sak       = info.sak;
  r->last_seen_us = now_us;
  r->seen_count   = 1;
  format_uid(r->uid, r->uid_len, r->uid_str, sizeof(r->uid_str));
  s->metrics.total_seen++;
}

void nfc_scanner_get_metrics(const NfcScanner *s, NfcScannerMetrics *out) {
  memcpy(out, &s->metrics, sizeof(*out));
}
