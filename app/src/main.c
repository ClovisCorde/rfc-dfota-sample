/**
 * @file main.c
 * @brief Implementation for Delta firmware updates.
 *
 * @author Kickmaker (Clovis Corde)
 * @date 2023
 * @copyright Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>

#ifdef CONFIG_DELTA_UPDATE
	#include <zephyr/delta/delta.h>
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/**
 * @brief Implementation of the callback delta_read_t to read the flash memory.
 * 
 * @param arg_p Pointer to the delta memory structure.
 * @param buf_p Buffer to store the read data.
 * @param size Number of bytes to read.
 * @return 0 on success, negative error code on failure.
 */
static int delta_mem_read(delta_memory_struct_t *arg_p, uint8_t *buf_p, size_t size) {
	int ret;
    uint64_t offset;

    if(arg_p->flash.slot == SLOT_0) {
        offset = arg_p->offset.source;
    } else {
        offset = arg_p->offset.patch;
    }

	ret = flash_area_open(arg_p->flash.slot, &arg_p->flash.flash_area);
    if(ret != 0){
        LOG_ERR("Can not open the flash area for %d", arg_p->flash.slot);
        return -ENODEV;
    }

	ret = flash_area_read(arg_p->flash.flash_area, offset, buf_p, size);
    if (ret != 0) {
            LOG_ERR("Can not read %d bytes in slot1 partition at offset : %llu", size, offset);
            return -EINVAL;
    }
    flash_area_close(arg_p->flash.flash_area);
    
    return ret;
}

/**
 * @brief Implementation of the callback delta_write_t to write on the flash memory.
 * 
 * @param arg_p Pointer to the delta memory structure.
 * @param buf_p Buffer containing the data to write.
 * @param size Number of bytes to write.
 * @param flush Whether to flush the data to storage.
 * @return 0 on success, negative error code on failure.
 */
static int delta_mem_write(delta_memory_struct_t *arg_p, uint8_t *buf_p, size_t size, bool flush) {
	int ret;

	ret = flash_img_buffered_write(&arg_p->flash.img_ctx, buf_p, size, flush);
    if(ret != 0) {
        LOG_ERR("Flash write error");
        return -EINVAL;
    }

    return ret;
}

/**
 * @brief Implementation of the callback delta_mem_erase_t to erase the flash memory.
 * 
 * @param arg_p Pointer to the delta memory structure.
 * @param offset Start offset of the region to erase.
 * @param size Number of bytes to erase.
 * @return 0 on success, negative error code on failure.
 */
static int delta_mem_erase(delta_memory_struct_t *arg_p, uint64_t offset, size_t size) {
    int ret;

	ret = flash_area_open(arg_p->flash.slot, &arg_p->flash.flash_area);
    if(ret != 0){
        LOG_ERR("Can not open the flash area for %d", arg_p->flash.slot);
        return -ENODEV;
    }

    ret = flash_area_erase(arg_p->flash.flash_area, offset, size);
    if(ret != 0){
        LOG_ERR("Can not erase the flash area for slot1 partition, ret = %d\n", ret);
        return -EINVAL;
    }
    flash_area_close(arg_p->flash.flash_area);
  
    return ret;
}

/**
 * @brief Implementation of the callback delta_seek_t to seek to a specific position in the delta memory for source and patch.
 * 
 * @param arg_p Pointer to the delta memory structure.
 * @param source_offset Offset for the source firmware.
 * @param patch_offset Offset for the patch data.
 * @return 0 on success.
 */
static int delta_mem_seek(delta_memory_struct_t *arg_p, size_t source_offset, size_t patch_offset) {
    arg_p->offset.source = source_offset;
    arg_p->offset.patch = patch_offset;

    return 0;
}

/**
 * @brief Apply the patch using the delta algorithm defined in backend and write the magic to define
 * the new firmware as a bootable image.
 * 
 * @param self_p Pointer to the delta api structure.
 * @return 0 on success.
 */
static int delta_apply_algo(struct delta_api_t *self_p)
{
	int ret;

	/* Apply the patch using a delta algorithm */
    ret = self_p->backend.patch(self_p);
    if(ret !=0) {
        LOG_ERR("apply patch failed\n");
        return ret;
    }

    ret = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
	if (ret != 0) {
		LOG_ERR("Boot request error : %d\n", ret);
		return ret;
	}

	return ret;
}

/**
 * @brief Initialize all the callbacks for the delta API and then apply the delta algorithm.
 * 
 * @param void
 * @return 0 on success, negative error code on failure.
 */
static int delta_apply_init(struct delta_api_t *delta_apply)
{
    int ret;

	/* Init flash img to write the new firmware (on slot 1) */
	ret = flash_img_init(&delta_apply->memory.flash.img_ctx);
	if(ret != 0) {
		LOG_ERR("Can't initialise flash img, ret = %d ", ret);
		return ret;
	}

    /* Init all the callbacks for the delta api */
	ret = delta_apply_patch_init(delta_apply,
							delta_mem_read,
							delta_mem_write,
							delta_mem_seek,
							delta_mem_erase);
	if(ret != 0) {
		LOG_ERR("delta apply patch failed during initialization, ret : %d\n", ret);
		return ret;
	}

    /* Init the offsets for source and patch at 0 */
    ret = delta_apply->seek(&delta_apply->memory, 0, 0);
	if(ret != 0) {
		LOG_ERR("delta api seek offset failed, ret = %d", ret);
		return ret;
	}

    return ret;
}

int main(void) {
	int ret;

	LOG_INF("Delta Firmware Update Sample");

	/* Init delta API*/
	struct delta_api_t delta_apply;

	ret = delta_apply_init(&delta_apply);
	if(ret != 0) {
		LOG_ERR("The delta API initialization failed, ret = %d", ret);
		return ret;
	}

	ret = delta_apply_algo(&delta_apply);
	if(ret != 0) {
		LOG_ERR("The delta application algorithm failed, ret = %d", ret);
		return ret;
	}
	LOG_INF("The new FW was successfully written, now rebooting...");

#ifdef CONFIG_APP_LOG_LEVEL_DBG
	// leave some time to display the logs before rebooting
    k_sleep(K_SECONDS(3));
#endif
	
	sys_reboot(SYS_REBOOT_COLD);

	return ret;
}
