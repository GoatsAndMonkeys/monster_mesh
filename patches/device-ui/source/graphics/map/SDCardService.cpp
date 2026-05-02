#include "lvgl.h"

#include "graphics/map/MapTileSettings.h"
#include "graphics/map/SDCardService.h"
#include "util/ILog.h"

#ifdef ARCH_PORTDUINO
#include "PortduinoFS.h"
static fs::FS &SD = PortduinoFS; // Portduino does not (yet) support SD device, use normal file system
#elif defined(HAS_SD_MMC)
#include "SD_MMC.h"
static fs::SDMMCFS &SD = SD_MMC;
#else
#include "SD.h"
#endif

#define DRIVE_LETTER "S"

SDCardService::SDCardService() : ITileService(DRIVE_LETTER ":")
{
    // MonsterMesh: do NOT register the LVGL "S:" filesystem driver. Map
    // tiles are disabled, and any LVGL widget that opens "S:..." would
    // trigger SD.open() from inside the LVGL flush path — that contends
    // with LoRa on the shared SPI bus and crashes the device.
}

SDCardService::~SDCardService()
{
#ifndef ARCH_PORTDUINO
    SD.end();
#endif
}

bool SDCardService::load(const char *name, void *img)
{
    // MonsterMesh: map tiles disabled — SD reads contend with LoRa SPI bus.
    // Map button is being repurposed for MonsterMesh terminal.
    return false;
}

void *SDCardService::fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    String s(path);
    File file = SD.open(path, mode == LV_FS_MODE_RD ? FILE_READ : FILE_WRITE);
    if (!file) {
        // ILOG_WARN("SD.open() %s failed!", path);
        return nullptr;
    } else {
        // ILOG_DEBUG("SD.open() %s ok", path);
        SdFile *lf = new SdFile{file};
        return static_cast<void *>(lf);
    }
}

lv_fs_res_t SDCardService::fs_close(lv_fs_drv_t *drv, void *file_p)
{
    // ILOG_DEBUG("SD.close()");
    SdFile *lf = static_cast<SdFile *>(file_p);
    lf->file.close();
    delete lf;
    return LV_FS_RES_OK;
}

lv_fs_res_t SDCardService::fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br)
{
    *br = static_cast<SdFile *>(file_p)->file.read((uint8_t *)buf, btr);
    // ILOG_DEBUG("SD.read(): %d/%d bytes", *br, btr);
    return (*br <= 0) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

lv_fs_res_t SDCardService::fs_write(lv_fs_drv_t *drv, void *file_p, const void *buf, uint32_t btw, uint32_t *bw)
{
    *bw = static_cast<SdFile *>(file_p)->file.write((uint8_t *)buf, btw);
    // ILOG_DEBUG("SD.write(): %d/btw bytes", *bw, btw);
    return (*bw <= 0) ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

lv_fs_res_t SDCardService::fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence)
{
    // ILOG_DEBUG("SD.seek(): pos %d", pos);
    return static_cast<SdFile *>(file_p)->file.seek(pos, (SeekMode)whence) ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}

lv_fs_res_t SDCardService::fs_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *pos_p)
{
    *pos_p = static_cast<SdFile *>(file_p)->file.position();
    // ILOG_DEBUG("SD.tell(): pos %d", *pos_p);
    return (int32_t)(*pos_p) < 0 ? LV_FS_RES_UNKNOWN : LV_FS_RES_OK;
}

void *SDCardService::fs_dir_open(lv_fs_drv_t *drv, const char *path)
{
    return nullptr; // TODO
}

lv_fs_res_t SDCardService::fs_dir_read(lv_fs_drv_t *drv, void *rddir_p, char *fn, uint32_t fn_len)
{
    return LV_FS_RES_NOT_IMP; // TODO
}

lv_fs_res_t SDCardService::fs_dir_close(lv_fs_drv_t *drv, void *rddir_p)
{
    return LV_FS_RES_NOT_IMP; // TODO
}
