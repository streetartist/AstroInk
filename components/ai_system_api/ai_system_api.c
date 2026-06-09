// AstroInk System API — subsystem init.

#include "ai_system_api.h"

esp_err_t ai_system_api_init(void)
{
    // KV (NVS) is the only stateful UI-independent subsystem for now.
    // ai_fs is stateless (wraps the already-mounted VFS); ai_sys needs no init.
    return ai_kv_init();
}
