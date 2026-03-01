#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_idf_version.h"
#include "sdkconfig.h"
#if SOC_SDMMC_IO_POWER_EXTERNAL
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif
#include "sd_card_example_common.h"
#include "sd_card_functions.h"

#if CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE && (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0))
#define WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT 1
#else
#define WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT 0
#endif

static const char *TAG = "sd_fns";
static sdmmc_card_t *card = NULL;

#if WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT
static esp_err_t sdmmc_host_init_dummy(void) { return ESP_OK; }
static esp_err_t sdmmc_host_deinit_dummy(void) { return ESP_OK; }
#endif

esp_err_t sd_card_mount(int slot, const char *mount_point)
{
    if (slot != SDMMC_HOST_SLOT_0)
    {
        ESP_LOGE(TAG, "Slot %d non supportato per SD su questa board.", slot);
        return ESP_FAIL;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = slot;
#if WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT
    host.init = &sdmmc_host_init_dummy;
    host.deinit = &sdmmc_host_deinit_dummy;
#endif

#if CONFIG_EXAMPLE_SD_PWR_CTRL_LDO_INTERNAL_IO
    if (!WORKAROUND_HOSTED_DOES_SDMMC_HOST_INIT)
    {
        sd_pwr_ctrl_ldo_config_t ldo_cfg = {.ldo_chan_id = 4};
        sd_pwr_ctrl_handle_t pwr_handle = NULL;
        if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_handle) == ESP_OK)
            host.pwr_ctrl_handle = pwr_handle;
    }
#endif

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = 4;
    slot_cfg.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mnt_cfg = {.max_files = 5, .allocation_unit_size = 16 * 1024};
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_cfg, &mnt_cfg, &card);

    if (ret == ESP_OK)
    {
        sdmmc_card_print_info(stdout, card);
    }
    else
    {
        ESP_LOGE(TAG, "Errore Mount SD: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t sd_card_unmount(const char *mount_point)
{
    if (!card)
        return ESP_FAIL;
    return esp_vfs_fat_sdcard_unmount(mount_point, card);
}

esp_err_t sd_card_write_file(const char *path, char *data)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return ESP_FAIL;
    fprintf(f, "%s", data);
    fclose(f);
    return ESP_OK;
}