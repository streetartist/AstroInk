// AstroInk VFS — mounts LittleFS (/system) and SD/FATFS (/sd).

#include "ai_vfs.h"
#include "board.h"
#include <string.h>
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

static const char *TAG = "ai_vfs";

#define SYSTEM_PARTITION_LABEL "storage"   // see partitions.csv
#define SD_MAX_OPEN_FILES      8

static bool s_system_mounted;
static sdmmc_card_t *s_card;

esp_err_t ai_vfs_mount_system(void)
{
    if (s_system_mounted) return ESP_OK;

    esp_vfs_littlefs_conf_t conf = {
        .base_path              = AI_VFS_SYSTEM,
        .partition_label        = SYSTEM_PARTITION_LABEL,
        .format_if_mount_failed = true,   // first boot: format a blank partition
        .dont_mount             = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(SYSTEM_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "mounted %s (LittleFS '%s': %u/%u bytes used)",
             AI_VFS_SYSTEM, SYSTEM_PARTITION_LABEL, (unsigned)used, (unsigned)total);
    s_system_mounted = true;
    return ESP_OK;
}

esp_err_t ai_vfs_mount_sd(void)
{
    if (s_card) return ESP_OK;

    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,  // never wipe a user's card automatically
        .max_files              = SD_MAX_OPEN_FILES,
        .allocation_unit_size   = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = BOARD_SD_BUS_WIDTH;
    slot.clk   = BOARD_SD_PIN_CLK;
    slot.cmd   = BOARD_SD_PIN_CMD;
    slot.d0    = BOARD_SD_PIN_D0;
    slot.d1    = BOARD_SD_PIN_D1;
    slot.d2    = BOARD_SD_PIN_D2;
    slot.d3    = BOARD_SD_PIN_D3;
    slot.cd    = SDMMC_SLOT_NO_CD;        // detect via mount result for now
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;  // board adds external pull-ups too

    esp_err_t err = esp_vfs_fat_sdmmc_mount(AI_VFS_SD, &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD mount failed (%s) — no card? continuing without /sd",
                 esp_err_to_name(err));
        s_card = NULL;
        return err;
    }

    ESP_LOGI(TAG, "mounted %s (SD %lluMB)",
             AI_VFS_SD, ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
    return ESP_OK;
}

void ai_vfs_unmount_sd(void)
{
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(AI_VFS_SD, s_card);
    s_card = NULL;
    ESP_LOGI(TAG, "unmounted %s", AI_VFS_SD);
}

esp_err_t ai_vfs_init(void)
{
    esp_err_t sys_err = ai_vfs_mount_system();
    ai_vfs_mount_sd();   // best-effort; non-fatal
    return sys_err;      // booting requires /system, not /sd
}

bool ai_vfs_system_mounted(void) { return s_system_mounted; }
bool ai_vfs_sd_mounted(void)     { return s_card != NULL; }
