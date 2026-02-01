/** @file disk_d88.c
 *
 * @author Sasaji
 * @brief 
 * @version 0.1
 * @date 2024-10-01
 * 
 * @copyright Copyright (c) Sasaji 2024
 */

#include "disk_d88.h"

#include <stdio.h>
#include <string.h>
#include <diskio.h>
#include <pico/sync.h>
#include "common.h"
#include "pio_ctrls.h"
#include "display.h"
#include "display_disk.h"
#include "fdc_common.h"
#include "utils.h"

#define DISK_D88_MAX_DRIVES MAX_DRIVES
static DISK_D88 disks[DISK_D88_MAX_DRIVES];

#define VALID_DRIVE_NUMBER(drv, rc) if ((drv) < 0 || (drv) >= DISK_D88_MAX_DRIVES) return (rc)

// 2sec.
#define DELAY_WRITE_USEC    2000000

#define TO_LE32(val) (val)

static const uint8_t *mark_fm_list[] = {
    "\xfc", // address mark
    "\xfb", // data mark
    "\xf8", // deleted data mark
    NULL
};

static const uint8_t *mark_mfm_list[] = {
    "\xa1\xa1\xa1\xfc", // address mark
    "\xa1\xa1\xa1\xfb", // data mark
    "\xa1\xa1\xa1\xf8", // deleted data mark
    NULL
};

/*======================================================================*/

static int seek_and_read_header(DISK_D88 *disk, FSIZE_t *offset, int *nsec);

/*======================================================================*/

void disk_d88_init()
{
    memset(disks, 0, sizeof(disks));
    for(int i=0; i<DISK_D88_MAX_DRIVES; i++) {
        disks[i].drive_number = (uint8_t)i;
        disks[i].sides_per_disk = 2;
        disks[i].tracks_per_side = 40;
        disks[i].delay_write_event_id = -1;
        disks[i].curr_sector_number = 1;
    }
}

void __not_in_flash_func(disk_d88_task)()
{
    for(int i=0; i<DISK_D88_MAX_DRIVES; i++) {
        DISK_D88 *disk = &disks[i];
        if (disk->flags.modified && disk->flags.need_flush) {
            disk_d88_flush_on(disk);
            disk->flags.need_flush = 0;
        }
    }
}

void disk_d88_info_on(DISK_D88 *disk)
{
    printf("Drive number:%d\n",disk->drive_number);
    printf("  Mounted: %d\n", disk->flags.mounted);
    printf("  Media type: 0x%x\n", disk->header.media_type);
    printf("  Write protected: 0x%x\n", disk->header.protect);
    printf("  Disk size: %u\n", TO_LE32(disk->header.size));
    printf("  Sides per disk: %d\n",disk->sides_per_disk);
    printf("  Tracks per side: %d\n",disk->tracks_per_side);
    printf("  Current:\n");
    int curr_track_offset = TO_LE32(disk->header.trkptr[disk->curr_track_number * disk->sides_per_disk]);
    printf("    Track: %d (Offset: 0x%x)\n", disk->curr_track_number, curr_track_offset);
    printf("    Side: %d Sector: %d\n", disk->curr_side_number, disk->curr_sector_number);
    printf("    Sector Header C:%d H:%d R:%d N:%d size:%d (Offset: 0x%x)\n"
       , disk->curr_sector.c
       , disk->curr_sector.h
       , disk->curr_sector.r
       , disk->curr_sector.n
       , disk->curr_sector.size
       , disk->curr_sector_offset
    );
    printf("      Density: 0x%x  Deleted: 0x%x  Calcd CRC:0x%04x\n", disk->curr_sector.dens, disk->curr_sector.del, disk->calcd_crc);
    printf("    Number of sector: %d\n", disk->curr_sector.nsec);
}

void disk_d88_info(int drv)
{
    if (drv < 0 || drv >= DISK_D88_MAX_DRIVES) {
        printf("Invalid drive number:%d\n",drv);
        return;
    }
    disk_d88_info_on(&disks[drv]);
}

void disk_d88_cat_on(DISK_D88 *disk)
{
    printf(" C:%d H:%d R:%d N:%d Size:%d Density:0x%x Pos:%d CalcCRC:0x%04x (Offset:0x%x)\n"
        , disk->curr_sector.c
        , disk->curr_sector.h
        , disk->curr_sector.r
        , disk->curr_sector.n
        , disk->curr_sector.size
        , disk->curr_sector.dens
        , disk->data_pos
        , disk->calcd_crc
        , disk->curr_sector_offset
    );
    dump_data(disk->data, disk->curr_sector.size, 0);
}

void disk_d88_cat(int drv)
{
    if (drv < 0 || drv >= DISK_D88_MAX_DRIVES) {
        printf("Invalid drive number:%d\n",drv);
        return;
    }
    disk_d88_cat_on(&disks[drv]);
}

void disk_d88_get_data_on(DISK_D88 *disk)
{
    uint32_t siz = (128 << disk->curr_sector.n);
    uint32_t pos = 0;
    uint32_t n;
    printf("^\n");
    for(; pos<siz; pos++) {
        printf("%02x", disk->data[pos]);
        if ((pos & 15) == 15) printf("\n");
    }
    printf("$\n");
}

void disk_d88_get_data(int drv)
{
    disk_d88_get_data_on(&disks[drv]);
}

/*======================================================================*/

DISK_D88 *__not_in_flash_func(disk_d88_get_disk)(int drv)
{
    VALID_DRIVE_NUMBER(drv, NULL);
    return &disks[drv];
}

static const int c_chk_trks[] = { 1, 39, 79, 153, 159, -1 };

static int disk_d88_parse_disk(DISK_D88 *disk)
{
    int max_trks = 0;
    int max_sids = 0;
    int nsec;
    for(int i=0; ; i++) {
        int trk = c_chk_trks[i];
        if (trk < 0) break;

        FSIZE_t offset = (FSIZE_t)TO_LE32(disk->header.trkptr[trk]); 
        if (!offset) continue;

        if (seek_and_read_header(disk, &offset, &nsec)) {
            continue;
        }

        max_trks = trk;

        if (max_sids < (int)disk->curr_sector.h) {
            max_sids = (int)disk->curr_sector.h;
        }
    }

    max_sids++;
    if (max_sids > 2) max_sids = 2;
    max_trks++;
    if (max_trks >= 80) max_sids = 2;

    max_trks /= max_sids;
    if (max_trks > 0) {
        if (max_trks <= 40) max_trks = 40;
        else if (max_trks <= 77) max_trks = 77;
        else if (max_trks <= 80) max_trks = 80;
    }
    disk->sides_per_disk = max_sids;
    disk->tracks_per_side = max_trks;

    if (disk_d88_is_verbose_mode) {
        printf("D88_DBG: disk_d88::disk_d88_parse_disk: sides:%d tracks:%d\n", max_sids, max_trks);
    }
    return max_trks > 0 ? FR_OK : FR_DISK_ERR;
}

/*======================================================================*/

void __not_in_flash_func(disk_d88_disp_lcd_on)(DISK_D88 *disk)
{
    bool wp = disk_d88_is_write_protected_on(disk);
    lcd_disk_trk_sid_sec_number(disk->drive_number, disk->curr_track_number, disk->curr_side_number, disk->curr_sector_number);
    lcd_disk_status(disk->drive_number, disk->tracks_per_side, disk->sides_per_disk, true, wp);
}

void __not_in_flash_func(disk_d88_disp_lcd)(int drv)
{
    disk_d88_disp_lcd_on(&disks[drv]);
}

/*======================================================================*/

int disk_d88_mount_on(DISK_D88 *disk, int side_number, const char *path, int offset)
{
    (void)offset;

    if (!disk) {
        return FR_INVALID_OBJECT;
    }
    if (disk->flags.mounted) {
        disk_d88_unmount_on(disk);
    }

    int rc = FR_NO_FILE;
    BYTE mode = FA_READ | FA_WRITE | FA_OPEN_EXISTING;
    for(int i=0; i<2; i++) {
        if (f_open(&disk->fil, path, mode) == FR_OK) {
            rc = FR_OK;
            break;
        }
        if (disk_d88_is_verbose_mode) {
            printf("D88_DBG: disk_d88::disk_d88_mount_on: '%s' cannot open on mode %u.\n", path, mode);
        }
        // retry read only mode
        mode = FA_READ | FA_OPEN_EXISTING;
    }
    if (rc != FR_OK) {
        return rc;
    }

    disk->flags.mounted = 1;
    disk->curr_side_number = side_number;

    // read header
    UINT br = 0;
    if (f_read(&disk->fil, &disk->header, sizeof(disk->header), &br) != FR_OK) {
        if (disk_d88_is_verbose_mode) {
            printf("D88_DBG: disk_d88::disk_d88_mount_on: '%s' cannot read the header.\n", path);
        }
        goto error;
    }
    if (TO_LE32(disk->header.size) <= 0) {
        if (disk_d88_is_verbose_mode) {
            printf("D88_DBG: disk_d88::disk_d88_mount_on: '%s' disk size is zero.\n", path);
        }
        goto error;
    }

    if (disk_d88_is_verbose_mode) {
        printf("D88_DBG: disk_d88::disk_d88_mount_on: Mounted '%s'.\n", path);
    }

    // parse disk
    if (disk_d88_parse_disk(disk) != FR_OK) {
        goto error;
    }

    disk_d88_disp_lcd_on(disk);
    disk_d88_step_on(disk, disk->curr_track_number, side_number);
    return FR_OK;

error:
    disk_d88_unmount_on(disk);

    lcd_disk_status(disk->drive_number, disk->tracks_per_side, disk->sides_per_disk, false, false);
    return FR_DISK_ERR;
}

int disk_d88_mount(int drv, int side_number, const char *path, int offset)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_mount_on(&disks[drv], side_number, path, offset);
}

int disk_d88_unmount_on(DISK_D88 *disk)
{
    if (!disk) {
        return FR_INVALID_OBJECT;
    }
    if (disk->flags.modified) {
        disk_d88_flush_on(disk);
    }
    int rc = FR_NOT_READY;
    if (disk->flags.mounted) {
        rc = f_close(&disk->fil);
    }
    if (disk_d88_is_verbose_mode && disk->flags.mounted) {
        printf("D88_DBG: disk_d88::disk_d88_mount_on: Unmounted.\n");
    }
    disk->flags.mounted = 0;
    return rc;
}

int disk_d88_unmount(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_unmount_on(&disks[drv]);
}

/*======================================================================*/

void disk_d88_append_header(FIL *fp, int disk_type)
{
    // create .d88 header
    d88_hdr_t header;
    d88_sct_t sector;
    UINT bw = 0;
    memset(&header, 0, sizeof(header));
    memset(&sector, 0, sizeof(sector));

    switch(disk_type) {
    case DISK_TYPE_8INCH:
        header.media_type = MEDIA_TYPE_2HD;
        break;
    default:
        header.media_type = MEDIA_TYPE_2D;
        break;
    }


    FRESULT re = f_write(fp, &header, (UINT)sizeof(header), &bw);
    if (re != FR_OK) return;

    uint8_t max_c, max_h, max_r, max_n, h_msk, intl, fil, dir_trk, fat_sid, fat_sec, sys_sta, sys_cnt, sd_trk, sd_sid;
    switch(disk_type) {
    case DISK_TYPE_8INCH:
        // 2HD 77tracks 2sides 26sectors 256bytes
        max_c = 77; max_h = 2; max_r = 26; max_n = 1; h_msk = 1; intl = 1;
        fil = 0x40; dir_trk = 37; fat_sec = 2;
        fat_sid = 0; fat_sec = 2; sys_sta = 1; sys_cnt = 2;
        // single density on track0 and side0
        sd_trk = 0; sd_sid = 0;
        break;
    case DISK_TYPE_3INCH:
        // 1Sx2 40tracks 2sides 16sectors 128bytes
        max_c = 40; max_h = 2; max_r = 16; max_n = 0; h_msk = 0; intl = 3;
        fil = 0xe5; dir_trk = 20;
        fat_sid = 0x80; fat_sec = 1; sys_sta = 5; sys_cnt = 20;
        // single density on all tracks
        sd_trk = 0x80; sd_sid = 0x80;
        break;
    default:
        // 2D 40tracks 2sides 16sectors 256bytes
        max_c = 40; max_h = 2; max_r = 16; max_n = 1; h_msk = 1; intl = 6;
        fil = 0x40; dir_trk = 20;
        fat_sid = 0; fat_sec = 2; sys_sta = 1; sys_cnt = 3;
#ifdef _MBS1
        sd_trk = 0xff; sd_sid = 0xff;
#else
        // single density on track0 and side0
        sd_trk = 0; sd_sid = 0;
#endif
        break;
    }

    // calc interleave
    uint8_t rarr[28];
    uint8_t r = 0;
    for(uint8_t i=0; i<28; i++) {
        rarr[i]=0;
    }
    for(uint8_t i=0; i<max_r; i++) {
        rarr[r]=i + 1;
        r += intl;
        if (r >= max_r) {
            r -= max_r;
            while (rarr[r] > 0) r++;
        }
    }

    sector.nsec = max_r;
    int tidx = 0;
    for(uint8_t c=0; c<max_c && tidx < D88_MAX_TRACKS; c++) {
        sector.c = c;
        for(uint8_t h=0; h<max_h && tidx < D88_MAX_TRACKS; h++) {
            sector.h = (h & h_msk);
            header.trkptr[tidx] = TO_LE32(f_tell(fp));
            if ((c == sd_trk && h == sd_sid) || (sd_trk == 0x80 && sd_sid == 0x80)) {
                // single density
                sector.dens = 0x40;
                sector.n = 0;
                sector.size = 128;
            } else {
                // double density
                sector.dens = 0;
                sector.n = max_n;
                sector.size = (128 << max_n);
            }
            for(r=0; r<max_r; r++) {
                sector.r = rarr[r];
                f_write(fp, &sector, (UINT)sizeof(sector), &bw);
                if (c == dir_trk) {
                    // FAT and directory
                    uint16_t s = 0;
                    if (sector.r == fat_sec && (h == fat_sid || fat_sid == 0x80)) {
                        // FAT area
                        // first code in FAT area
                        f_putc((TCHAR)0, fp);
                        s++;
                        while(s < sys_sta) {
                            f_putc((TCHAR)0xff, fp);
                            s++;
                        }
                        // mark reserved code as system area
                        while(s < (sys_sta + sys_cnt)) {
                            f_putc((TCHAR)0xfe, fp);
                            s++;
                        }
                    }
                    // directory track
                    for(uint16_t n=s; n<sector.size; n++) {
                        f_putc((TCHAR)0xff, fp);
                    }
                } else {
                    // data
                    for(uint16_t n=0; n<sector.size; n++) {
                        f_putc((TCHAR)fil, fp);
                    }
                }
                display_progress();
            }
            sector.size = TO_LE16(sector.size);
            tidx++;
        }
    }
    // update header
    header.size = TO_LE32(f_tell(fp));
    // rewrite header to file
    f_lseek(fp, 0);
    f_write(fp, &header, (UINT)sizeof(header), &bw);
}

/*======================================================================*/

int __not_in_flash_func(disk_d88_step_on)(DISK_D88 *disk, int track_number, int side_number)
{
    if (!disk) {
        return FR_NOT_READY;
    }
    if (!disk->sides_per_disk) {
        return FR_DISK_ERR;
    }
    int track_index = track_number * disk->sides_per_disk + side_number;
    if (track_index < 0) {
        track_index = 0;
        track_number = 0;
    } else if (track_index >= D88_MAX_TRACKS) {
        track_index = D88_MAX_TRACKS - 1;
        track_number = track_index / disk->sides_per_disk;
    }
//    disk->curr_track_offset = (int)TO_LE32(disk->header.trkptr[track_index]);
    disk->curr_track_number = track_number;
    if (disk->flags.mounted) {
        lcd_disk_trk_sid_number(disk->drive_number, disk->curr_track_number, side_number);
    }
    return FR_OK;
}

int __not_in_flash_func(disk_d88_step)(int drv, int track_number, int side_number)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_step_on(&disks[drv], track_number, side_number);
}

int __not_in_flash_func(disk_d88_step_inout_on)(DISK_D88 *disk, int direction, int side_number)
{
    if (!disk) {
        return FR_NOT_READY;
    }
    if (!disk->sides_per_disk) {
        return FR_DISK_ERR;
    }
    int track_number = disk->curr_track_number + direction;
    int track_index = track_number * disk->sides_per_disk + side_number;
    if (track_index < 0) {
        track_index = 0;
        track_number = 0;
    } else if (track_index >= D88_MAX_TRACKS) {
        track_index = D88_MAX_TRACKS - 1;
        track_number = track_index / disk->sides_per_disk;
    }
//    disk->curr_track_offset = (int)TO_LE32(disk->header.trkptr[track_index]);
    disk->curr_track_number = track_number;
    if (disk->flags.mounted) {
        lcd_disk_trk_sid_number(disk->drive_number, disk->curr_track_number, side_number);
    }
    display_buzzer();
#ifdef USE_PIO_BUZZER
    pio_buzzer_out();
#endif
    return FR_OK;
}

int __not_in_flash_func(disk_d88_step_in)(int drv, int side_number)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    DISK_D88 *disk = &disks[drv];
    return disk_d88_step_inout_on(disk, 1, side_number);
}

int __not_in_flash_func(disk_d88_step_out)(int drv, int side_number)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    DISK_D88 *disk = &disks[drv];
    return disk_d88_step_inout_on(disk, -1, side_number);
}

int __not_in_flash_func(disk_d88_restore)(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_step_on(&disks[drv], 0, 0);
}

/*======================================================================*/

/// @brief 
/// @param data 
/// @param crc 
/// @return 
static uint16_t __not_in_flash_func(disk_d88_calc_crc16)(uint8_t data, uint16_t crc)
{
	for (int count = 7; count >= 0; count--) {
		uint16_t bit = (((crc >> 15) ^ (data >> 7)) & 1);
		crc <<= 1;
		crc |= bit;
		data <<= 1;
		if (bit) {
			crc ^= 0x1020;
		}
	}
	return crc;
}

/// @brief 
/// @param id 
/// @param dden 
/// @return 
static uint16_t __not_in_flash_func(calc_crc_one_address)(const d88_sct_t *id, bool dden)
{
    uint16_t crc = 0xffff;
    const uint8_t *p;
    size_t i;
    if (dden) {
        p = mark_mfm_list[0];
        for(i=0; i<4; i++) {
            crc = disk_d88_calc_crc16(*p, crc);
            p++;
        }
    } else {
        p = mark_fm_list[0];
        crc = disk_d88_calc_crc16(*p, crc);
    }
    crc = disk_d88_calc_crc16(id->c, crc);
    crc = disk_d88_calc_crc16(id->h, crc);
    crc = disk_d88_calc_crc16(id->r, crc);
    crc = disk_d88_calc_crc16(id->n, crc);
    return crc;
}

/// @brief Calculate CRC-CCITT from the sector data
/// @param data 
/// @param size 
/// @param dden 
/// @param deleted 
/// @return 
static uint16_t __not_in_flash_func(calc_crc_one_sector)(const uint8_t *data, size_t size, bool dden, bool deleted)
{
    uint16_t crc = 0xffff;
    const uint8_t *p;
    size_t i;
    if (dden) {
        p = deleted ? mark_mfm_list[2] : mark_mfm_list[1];
        for(i=0; i<4; i++) {
            crc = disk_d88_calc_crc16(*p, crc);
            p++;
        }
    } else {
        p = deleted ? mark_fm_list[2] : mark_fm_list[1];
        crc = disk_d88_calc_crc16(*p, crc);
    }
    p = data;
    for(i=0; i<size; i++) {
        crc = disk_d88_calc_crc16(*p, crc);
        p++;
    }
    return crc;
}

void __not_in_flash_func(disk_d88_get_crc_on)(DISK_D88 *disk)
{
    size_t siz = (128 << disk->curr_sector.n);
    bool dden = (disk->curr_sector.dens == 0);
    bool deld = (disk->curr_sector.del != 0);
    uint16_t crc = calc_crc_one_sector(disk->data, siz, dden, deld);
    printf("%04x\n", crc);
}

void __not_in_flash_func(disk_d88_get_crc)(int drv)
{
    disk_d88_get_crc_on(&disks[drv]);
}

/*======================================================================*/

/// @brief Read sector header
/// @param disk 
/// @param[in,out] offset position in the disk to read a sector header and return the position of the sector data
/// @param[out] nsec number of sector in current track
/// @return 
static int __not_in_flash_func(seek_and_read_header)(DISK_D88 *disk, FSIZE_t *offset, int *nsec)
{
    UINT br = 0;
    if (f_lseek(&disk->fil, *offset) != FR_OK) {
        return 1;
    }
    *offset += (FSIZE_t)sizeof(disk->curr_sector);
    disk->curr_sector_offset = *offset;
    // read sector header
    if (f_read(&disk->fil, &disk->curr_sector, sizeof(disk->curr_sector), &br) != FR_OK) {
        return 1;
    }
    if (br == 0) {
        return 1;
    }
    if (disk->curr_sector.nsec == 0) {
        return 1;
    }
    *nsec = disk->curr_sector.nsec;
    if (disk->curr_sector.size == 0) {
        return 1;
    }
    return 0;
}

/// @brief 
/// @param disk 
/// @param track_number     track number -1:not compare the number in a id field
/// @param side_number      side number -1:not compare the number in a id field
/// @param sector_number    sector number -1:not compare the number in a id field
/// @param density          0:single density (FM) 1:double density(MFM) -1:not care
/// @param deleted          0:not deleted data 1:deleted data -1:not care
/// @param density          0:single density (FM) 1:double density(MFM) -1:not care
/// @param calc_crc         calcrate CRC
/// @param[out] sector_pos  position of the sector in the current track   
/// @return bit0: 1 if record is not found.
///         bit1: 1 if crc error is occured.
///         bit2: 1 if deleted mark is detected. 
int __not_in_flash_func(disk_d88_read_sector_on)(DISK_D88 *disk, int track_number, int side_number, int sector_number, int density, int deleted, bool calc_crc, int *sector_pos)
{
    FSIZE_t offset = 0;
    if (!disk || !disk->flags.mounted) {
        return 1;
    }
    if (disk->flags.modified) {
        disk_d88_flush_on(disk);
    }
    int side_number_real = (side_number >= 0 ? side_number : - side_number - 1);
    lcd_disk_sid_sec_number(disk->drive_number, side_number_real, sector_number);
//    offset = (FSIZE_t)disk->curr_track_offset;
    offset = (FSIZE_t)TO_LE32(disk->header.trkptr[disk->curr_track_number * disk->sides_per_disk + side_number_real]);
    density = (density > 0 ? 0 : (density < 0 ? -1 : 0x40));
    deleted = (deleted > 0 ? 0x10 : (deleted < 0 ? -1 : 0));
    int nsec = 1;
    int found = 0;
    for(int n = 0; n<nsec; n++) {
        // read sector header
        if (seek_and_read_header(disk, &offset, &nsec)) {
            return 1;
        }
        // found sector?
        if ((disk->curr_sector.c == track_number || track_number < 0)
         && (disk->curr_sector.h == side_number || side_number < 0)
         && (disk->curr_sector.r == sector_number || sector_number < 0)
         && (disk->curr_sector.dens == density || density < 0)
         && (disk->curr_sector.del == deleted || deleted < 0)
        ) {
            // read data
            UINT br = 0;
            int siz = disk->curr_sector.size;
            if (siz > DISK_D88_MAX_BUFFER) {
                siz = DISK_D88_MAX_BUFFER;
            }
            f_read(&disk->fil, disk->data, siz, &br);
//            offset += (FSIZE_t)siz;
            disk->data_pos = 0;
            found = 1;
            if (calc_crc) {
                int ssiz = (128 << disk->curr_sector.n);
                if (ssiz > DISK_D88_MAX_BUFFER) {
                    ssiz = DISK_D88_MAX_BUFFER;
                }
                disk->calcd_crc = calc_crc_one_sector(disk->data, ssiz, !disk->curr_sector.dens, disk->curr_sector.del);
            } else {
                disk->calcd_crc = 0;
            }
            if (sector_pos) *sector_pos = n;
            break;
        } else {
            // skip
            offset += (FSIZE_t)disk->curr_sector.size;
        }
    }
    if (!found) {
        // no data found
        if (sector_pos) *sector_pos = -1;
        return 1;
    }
    return disk->curr_sector.del ? 4 : 0;
}

int __not_in_flash_func(disk_d88_read_sector)(int drv, int track_number, int side_number, int sector_number, int density, int deleted, bool calc_crc, int *sector_pos)
{
    VALID_DRIVE_NUMBER(drv, 1);
    return disk_d88_read_sector_on(&disks[drv], track_number, side_number, sector_number, density, deleted, calc_crc, sector_pos);
}

/// @brief 
/// @param disk 
/// @param side_number 
/// @param sector_pos 
/// @return bit0: 1 if record is not found.
///         bit1: 1 if crc error is occured.
///         bit2: 1 if deleted mark is detected. 
int __not_in_flash_func(disk_d88_read_sector_id_on)(DISK_D88 *disk, int side_number, int sector_pos)
{
    FSIZE_t offset = 0;
    if (!disk || !disk->flags.mounted) {
        return 1;
    }
    if (disk->flags.modified) {
        disk_d88_flush_on(disk);
    }
//    offset = (FSIZE_t)disk->curr_track_offset;
    offset = (FSIZE_t)TO_LE32(disk->header.trkptr[disk->curr_track_number * disk->sides_per_disk + side_number]);
    int nsec = 1;
    int found = 0;
    for(int n = 0; n<nsec; n++) {
        // read sector header
        if (seek_and_read_header(disk, &offset, &nsec)) {
            return 1;
        }
        // found sector?
        if (n == sector_pos) {
            // read data
            UINT br = 0;
            int siz = disk->curr_sector.size;
            if (siz > DISK_D88_MAX_BUFFER) {
                siz = DISK_D88_MAX_BUFFER;
            }
            f_read(&disk->fil, disk->data, siz, &br);
//            offset += (FSIZE_t)siz;
            disk->data_pos = 0;
            found = 1;
            disk->calcd_address_crc = calc_crc_one_address(&disk->curr_sector, !disk->curr_sector.dens);
            break;
        } else {
            // skip
            offset += (FSIZE_t)disk->curr_sector.size;
        }
    }
    if (!found) {
        // no data found
        return 1;
    }
    return disk->curr_sector.del ? 4 : 0;
}

int __not_in_flash_func(disk_d88_read_sector_id)(int drv, int side_number, int sector_pos)
{
    VALID_DRIVE_NUMBER(drv, 1);
    return disk_d88_read_sector_id_on(&disks[drv], side_number, sector_pos);
}

int __not_in_flash_func(disk_d88_read_data_on)(DISK_D88 *disk, uint8_t *data)
{
    if (!disk || !disk->flags.mounted) {
        return FR_NOT_READY;
    }
    if (disk->data_pos >= DISK_D88_MAX_BUFFER) {
        return FR_DISK_ERR;
    }
    *data = disk->data[disk->data_pos];
    disk->data_pos++;
    return FR_OK;
}

int __not_in_flash_func(disk_d88_read_data)(int drv, uint8_t *data)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_read_data_on(&disks[drv], data);
}

int __not_in_flash_func(disk_d88_read_address_on)(DISK_D88 *disk, uint8_t *data)
{
    if (!disk || !disk->flags.mounted) {
        return FR_NOT_READY;
    }
    if (disk->data_pos >= DISK_D88_MAX_BUFFER) {
        return FR_DISK_ERR;
    }
    switch(disk->data_pos) {
    case 4:
        *data = (disk->calcd_address_crc >> 8) & 0xff;
        break;
    case 5:
        *data = (disk->calcd_address_crc & 0xff);
        break;
    default:
        *data = disk->curr_sector.b[disk->data_pos];
        break;
    }
    disk->data_pos++;
    return FR_OK;
}

int __not_in_flash_func(disk_d88_read_address)(int drv, uint8_t *data)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_read_address_on(&disks[drv], data);
}

int __not_in_flash_func(disk_d88_write_data_on)(DISK_D88 *disk, uint8_t data)
{
    if (!disk || !disk->flags.mounted) {
        return FR_NOT_READY;
    }
    if (disk_d88_is_write_protected_on(disk)) {
        return FR_WRITE_PROTECTED;
    }
    if (disk->data_pos >= DISK_D88_MAX_BUFFER) {
        return FR_DISK_ERR;
    }
    disk->data[disk->data_pos] = data;
    disk->data_pos++;
    disk->flags.modified = 1;
    return FR_OK;
}

int __not_in_flash_func(disk_d88_write_data)(int drv, uint8_t data)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_write_data_on(&disks[drv], data);
}

int __not_in_flash_func(disk_d88_flush_on)(DISK_D88 *disk)
{
    if (!disk || !disk->flags.mounted) {
        return FR_NOT_READY;
    }
    if (disk_d88_is_write_protected_on(disk)) {
        return FR_WRITE_PROTECTED;
    }
    if (!disk->flags.modified) {
        return FR_OK;
    }
    UINT bw = 0;
    if (f_lseek(&disk->fil, disk->curr_sector_offset) != FR_OK) {
        return FR_DISK_ERR;
    }
    if (f_write(&disk->fil, disk->data, disk->curr_sector.size, &bw) != FR_OK) {
        return FR_DISK_ERR;
    }
    disk->flags.modified = 0;
    return FR_OK;
}

int __not_in_flash_func(disk_d88_flush)(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_flush_on(&disks[drv]);
}

static int64_t __no_inline_not_in_flash_func(disk_d88_delay_write_event_callback)(alarm_id_t id, void *user_data)
{
    DISK_D88 *disk = (DISK_D88 *)user_data;
    if (disk) {
        disk->delay_write_event_id = -1;
        disk->flags.need_flush = 1;
    }
    return 0;
}

int __not_in_flash_func(disk_d88_delay_write_event_on)(DISK_D88 *disk)
{
    if (disk->flags.modified) {
        event_cancel_event(&disk->delay_write_event_id);
        disk->flags.need_flush = 0;
	    disk->delay_write_event_id = event_register_event(DELAY_WRITE_USEC, disk_d88_delay_write_event_callback, (uint32_t)(intptr_t)disk);
    }
    return FR_OK;
}

int __not_in_flash_func(disk_d88_delay_write_event)(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_delay_write_event_on(&disks[drv]);
}

int disk_d88_read_track_on(DISK_D88 *disk, uint8_t *data)
{
    // TODO
    (void)disk; (void)data;
}

int disk_d88_read_track(int drv, uint8_t *data)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_read_track_on(&disks[drv], data);
}

int disk_d88_write_track_on(DISK_D88 *disk, uint8_t data)
{
    // TODO
    (void)disk; (void)data;
}

int disk_d88_write_track(int drv, uint8_t data)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_write_track_on(&disks[drv], data);
}

int disk_d88_verify_track_on(DISK_D88 *disk, int track_number, int side_number)
{
    FSIZE_t offset = 0;
    if (!disk || !disk->flags.mounted) {
        return 1;
    }
    if (disk->flags.modified) {
        disk_d88_flush_on(disk);
    }
//    offset = (FSIZE_t)disk->curr_track_offset;
    offset = (FSIZE_t)TO_LE32(disk->header.trkptr[disk->curr_track_number * disk->sides_per_disk + side_number]);
    int nsec = 1;
    int found = 0;
    for(int n = 0; n<nsec; n++) {
        // read sector header
        if (seek_and_read_header(disk, &offset, &nsec)) {
            return 1;
        }
        // found sector?
        if (disk->curr_sector.c != track_number) {
            found = 1;
            break;
        } else {
            // skip
            offset += (FSIZE_t)disk->curr_sector.size;
        }
    }
    if (!found) {
        // ok
        return 0;
    }
    return 1;
}

int disk_d88_verify_track(int drv, int track_number, int side_number)
{
    VALID_DRIVE_NUMBER(drv, 1);
    return disk_d88_verify_track_on(&disks[drv], track_number, side_number);
}

/*======================================================================*/

bool __not_in_flash_func(disk_d88_is_not_ready_on)(DISK_D88 *disk)
{
    return (!disk || !disk->flags.mounted);
}

bool __not_in_flash_func(disk_d88_is_not_ready)(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_NOT_READY);
    return disk_d88_is_not_ready_on(&disks[drv]);
}

bool __not_in_flash_func(disk_d88_is_write_protected_on)(DISK_D88 *disk)
{
    return (disk->header.protect || !(disk->fil.flag & FA_WRITE));
}

bool __not_in_flash_func(disk_d88_is_write_protected)(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_is_write_protected_on(&disks[drv]);
}

bool __not_in_flash_func(disk_d88_is_track0_on)(DISK_D88 *disk)
{
    return (disk->curr_track_number == 0);
}

bool __not_in_flash_func(disk_d88_is_track0)(int drv)
{
    VALID_DRIVE_NUMBER(drv, false);
    return disk_d88_is_track0_on(&disks[drv]);
}

#if 0
int disk_d88_is_index_hole_on(DISK_D88 *disk)
{
    return 1;
}

int disk_d88_is_index_hole(int drv)
{
    if (drv < 0 || drv >= DISK_D88_MAX_DRIVES) return 0;
    return disk_d88_is_index_hole_on(&disks[drv]);
}
#endif

#if 0
int disk_d88_set_motor_on(DISK_D88 *disk, int onoff)
{
    disk->flags.motoron = onoff ? 1 : 0;
}

int disk_d88_set_motor(int drv, int onoff)
{
    if (drv < 0 || drv >= DISK_D88_MAX_DRIVES) return FR_INVALID_DRIVE;
    return disk_d88_set_motor_on(&disks[drv], onoff);
}
#endif

int __not_in_flash_func(disk_d88_set_deleted_mark_on)(DISK_D88 *disk, int val)
{
    disk->curr_sector.del = val ? 0x10 : 0;
}

int __not_in_flash_func(disk_d88_set_deleted_mark)(int drv, int val)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_set_deleted_mark_on(&disks[drv], val);
}

/// @brief Return a track size
/// @param disk : d88 disk information
/// @param density : bit0=1:double density bit1=1:high density bit2=1:360rpm
/// @return size 3125/6250/12500
int __not_in_flash_func(disk_d88_get_track_size_on)(DISK_D88 *disk, int density)
{
    int val = 15625; // bytes per sec
    if (density & 1) val <<= 1; // double density
    if (density & 2) val <<= 1; // high density
    if (density & 4) {
        val /= 6;   // 360rpm
    } else {
        val /= 5;   // 300rpm
    }
    return val;
}

int __not_in_flash_func(disk_d88_get_track_size)(int drv, int density)
{
    VALID_DRIVE_NUMBER(drv, 0);
    return disk_d88_get_track_size_on(&disks[drv], density);
}

int __not_in_flash_func(disk_d88_get_sector_size_on)(DISK_D88 *disk)
{
    return (128 << disk->curr_sector.n);
}

int __not_in_flash_func(disk_d88_get_sector_size)(int drv)
{
    VALID_DRIVE_NUMBER(drv, 0);
    return disk_d88_get_sector_size_on(&disks[drv]);
}

int __not_in_flash_func(disk_d88_get_sector_nums_on)(DISK_D88 *disk)
{
    return disk->curr_sector.nsec;
}

int __not_in_flash_func(disk_d88_get_sector_nums)(int drv)
{
    VALID_DRIVE_NUMBER(drv, FR_INVALID_DRIVE);
    return disk_d88_get_sector_nums_on(&disks[drv]);
}

void __not_in_flash_func(disk_d88_set_side_number_on)(DISK_D88 *disk, int side_number)
{
    disk->curr_side_number = side_number;
}

void __not_in_flash_func(disk_d88_set_side_number)(int drv, int side_number)
{
    disk_d88_set_side_number_on(&disks[drv], side_number);
}

/*======================================================================*/

bool disk_d88_is_verbose_mode = false;

void disk_d88_verbose_mode(bool onoff)
{
    disk_d88_is_verbose_mode = onoff;
}
