#pragma once
// AstroInk System API — persistent key/value store (architecture §4.4).
// Backed by NVS. Intended for small app/OS config strings.

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

// Initialize NVS and open the AstroInk KV namespace. Safe to call more than
// once. Other ai_kv_* calls are no-ops until this succeeds.
esp_err_t ai_kv_init(void);

// Store a string value under `key` (persisted immediately).
void ai_kv_set(const char *key, const char *value);

// Read the string for `key` into `out` (NUL-terminated, truncated to `max`).
// Returns the string length copied, or -1 if missing/error.
int  ai_kv_get(const char *key, char *out, int max);

// Buffer size needed to hold `key`'s value (string length + NUL), or -1 if
// missing/error. Lets callers size a buffer before ai_kv_get.
int  ai_kv_get_len(const char *key);

// Remove a key. No-op if absent.
void ai_kv_erase(const char *key);

#ifdef __cplusplus
}
#endif
