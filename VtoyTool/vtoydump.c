/******************************************************************************
 * vtoydump.c  ---- Dump ventoy os parameters 
 *
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <dirent.h>

#define IS_DIGIT(x) ((x) >= '0' && (x) <= '9')

#ifndef USE_DIET_C
typedef unsigned long long uint64_t;
typedef unsigned int    uint32_t;
typedef unsigned short  uint16_t;
typedef unsigned char   uint8_t;
#endif

#define VENTOY_GUID { 0x77772020, 0x2e77, 0x6576, { 0x6e, 0x74, 0x6f, 0x79, 0x2e, 0x6e, 0x65, 0x74 }}

typedef enum ventoy_fs_type
{
    ventoy_fs_exfat = 0, /* 0: exfat */
    ventoy_fs_ntfs,      /* 1: NTFS */
    ventoy_fs_ext,       /* 2: ext2/ext3/ext4 */
    ventoy_fs_xfs,       /* 3: XFS */
    ventoy_fs_udf,       /* 4: UDF */
    ventoy_fs_fat,       /* 5: FAT */

    ventoy_fs_max
}ventoy_fs_type;

#pragma pack(1)

typedef struct ventoy_guid
{
    uint32_t   data1;
    uint16_t   data2;
    uint16_t   data3;
    uint8_t    data4[8];
}ventoy_guid;


typedef struct ventoy_image_disk_region
{
    uint32_t   image_sector_count; /* image sectors contained in this region */
    uint32_t   image_start_sector; /* image sector start */
    uint64_t   disk_start_sector;  /* disk sector start */
}ventoy_image_disk_region;

typedef struct ventoy_image_location
{
    ventoy_guid  guid;
    
    /* image sector size, currently this value is always 2048 */
    uint32_t   image_sector_size;

    /* disk sector size, normally the value is 512 */
    uint32_t   disk_sector_size;

    uint32_t   region_count;
    
    /*
     * disk region data
     * If the image file has more than one fragments in disk, 
     * there will be more than one region data here.
     * You can calculate the region count by 
     */
    ventoy_image_disk_region regions[1];

    /* ventoy_image_disk_region regions[2~region_count-1] */
}ventoy_image_location;

typedef struct ventoy_os_param
{
    ventoy_guid    guid;             // VENTOY_GUID
    uint8_t        chksum;           // checksum

    uint8_t   vtoy_disk_guid[16];
    uint64_t  vtoy_disk_size;       // disk size in bytes
    uint16_t  vtoy_disk_part_id;    // begin with 1
    uint16_t  vtoy_disk_part_type;  // 0:exfat   1:ntfs  other: reserved
    char      vtoy_img_path[384];   // It seems to be enough, utf-8 format
    uint64_t  vtoy_img_size;        // image file size in bytes

    /* 
     * Ventoy will write a copy of ventoy_image_location data into runtime memory
     * this is the physically address and length of that memory.
     * Address 0 means no such data exist.
     * Address will be aligned by 4KB.
     *
     */
    uint64_t  vtoy_img_location_addr;
    uint32_t  vtoy_img_location_len;

    uint64_t  vtoy_reserved[4];     // Internal use by ventoy

    uint8_t   reserved[31];
}ventoy_os_param;

#pragma pack()

#ifndef O_BINARY
#define O_BINARY 0
#endif
#if defined(_dragon_fly) || defined(_free_BSD) || defined(_QNX)
#define MMAP_FLAGS          MAP_SHARED
#else
#define MMAP_FLAGS          MAP_PRIVATE
#endif

#define SEARCH_MEM_START 0x80000
#define SEARCH_MEM_LEN   0x1c000

static int verbose = 0;
#define debug(fmt, ...) if(verbose) printf(fmt, ##__VA_ARGS__)

static ventoy_guid vtoy_guid = VENTOY_GUID;

static const char *g_ventoy_fs[ventoy_fs_max] = 
{
    "exfat", "ntfs", "ext*", "xfs", "udf", "fat"
};

static int vtoy_check_os_param(ventoy_os_param *param)
{
    uint32_t i;
    uint8_t  chksum = 0;
    uint8_t *buf = (uint8_t *)param;
    
    if (memcmp(&param->guid, &vtoy_guid, sizeof(ventoy_guid)))
    {
        uint8_t *data1 = (uint8_t *)(&param->guid);
        uint8_t *data2 = (uint8_t *)(&vtoy_guid);
        
        for (i = 0; i < 16; i++)
        {
            if (data1[i] != data2[i])
            {
                debug("guid not equal i = %u, 0x%02x, 0x%02x\n", i, data1[i], data2[i]);
            }
        }
        return 1;
    }

    for (i = 0; i < sizeof(ventoy_os_param); i++)
    {
        chksum += buf[i];
    }

    if (chksum)
    {
        debug("Invalid checksum 0x%02x\n", chksum);
        return 1;
    }

    return 0;
}

static int vtoy_os_param_from_file(const char *filename, ventoy_os_param *param)
{
    int fd = 0;
    int rc = 0;

    fd = open(filename, O_RDONLY | O_BINARY);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to open file %s error %d\n", filename, errno);
        return errno;
    }

    read(fd, param, sizeof(ventoy_os_param));

    if (vtoy_check_os_param(param) == 0)
    {
        debug("find ventoy os param in file %s\n", filename);
    }
    else
    {
        debug("ventoy os pararm NOT found in file %s\n", filename);
        rc = 1;
    }
    
    close(fd);
    return rc;
}

static void vtoy_dump_os_param(ventoy_os_param *param)
{
    printf("################# dump os param ################\n");

    printf("param->chksum = 0x%x\n", param->chksum);
    printf("param->vtoy_disk_guid = %02x %02x %02x %02x\n", 
        param->vtoy_disk_guid[0], param->vtoy_disk_guid[1], 
        param->vtoy_disk_guid[2], param->vtoy_disk_guid[3]);
    printf("param->vtoy_disk_size = %llu\n", (unsigned long long)param->vtoy_disk_size);
    printf("param->vtoy_disk_part_id = %u\n", param->vtoy_disk_part_id);
    printf("param->vtoy_disk_part_type = %u\n", param->vtoy_disk_part_type);
    printf("param->vtoy_img_path = <%s>\n", param->vtoy_img_path);
    printf("param->vtoy_img_size = <%llu>\n", (unsigned long long)param->vtoy_img_size);
    printf("param->vtoy_img_location_addr = <0x%llx>\n", (unsigned long long)param->vtoy_img_location_addr);
    printf("param->vtoy_img_location_len = <%u>\n", param->vtoy_img_location_len);
    printf("param->vtoy_reserved[0] = 0x%llx\n", (unsigned long long)param->vtoy_reserved[0]);
    printf("param->vtoy_reserved[1] = 0x%llx\n", (unsigned long long)param->vtoy_reserved[1]);
    
    printf("\n");
}

static int vtoy_get_disk_guid(const char *diskname, uint8_t *vtguid)
{
    int i = 0;
    int fd = 0;
    char devdisk[128] = {0};

    snprintf(devdisk, sizeof(devdisk) - 1, "/dev/%s", diskname);
    
    fd = open(devdisk, O_RDONLY | O_BINARY);
    if (fd >= 0)
    {
        lseek(fd, 0x180, SEEK_SET);
        read(fd, vtguid, 16);
        close(fd);

        debug("GUID for %s: <", devdisk);
        for (i = 0; i < 16; i++)
        {
            debug("%02x", vtguid[i]);
        }
        debug(">\n");
        
        return 0;
    }
    else
    {
        debug("failed to open %s %d\n", devdisk, errno);
        return errno;
    }
}

static unsigned long long vtoy_get_disk_size_in_byte(const char *disk)
{
    int fd;
    int rc;
    unsigned long long size = 0;
    char diskpath[256] = {0};
    char sizebuf[64] = {0};

    // Try 1: get size from sysfs
    snprintf(diskpath, sizeof(diskpath) - 1, "/sys/block/%s/size", disk);
    if (access(diskpath, F_OK) >= 0)
    {
        debug("get disk size from sysfs for %s\n", disk);
        
        fd = open(diskpath, O_RDONLY | O_BINARY);
        if (fd >= 0)
        {
            read(fd, sizebuf, sizeof(sizebuf));
            size = strtoull(sizebuf, NULL, 10);
            close(fd);
            return (size * 512);
        }
    }
    else
    {
        debug("%s not exist \n", diskpath);
    }

    // Try 2: get size from ioctl
    snprintf(diskpath, sizeof(diskpath) - 1, "/dev/%s", disk);
    fd = open(diskpath, O_RDONLY);
    if (fd >= 0)
    {
        debug("get disk size from ioctl for %s\n", disk);
        rc = ioctl(fd, BLKGETSIZE64, &size);
        if (rc == -1)
        {
            size = 0;
            debug("failed to ioctl %d\n", rc);
        }
        close(fd);
    }
    else
    {
        debug("failed to open %s %d\n", diskpath, errno);
    }

    debug("disk %s size %llu bytes\n", disk, (unsigned long long)size);
    return size;
}

static int vtoy_is_possible_blkdev(const char *name)
{
    if (name[0] == '.')
    {
        return 0;
    }

    /* /dev/ramX */
    if (name[0] == 'r' && name[1] == 'a' && name[2] == 'm')
    {
        return 0;
    }

    /* /dev/loopX */
    if (name[0] == 'l' && name[1] == 'o' && name[2] == 'o' && name[3] == 'p')
    {
        return 0;
    }

    /* /dev/dm-X */
    if (name[0] == 'd' && name[1] == 'm' && name[2] == '-' && IS_DIGIT(name[3]))
    {
        return 0;
    }

    /* /dev/srX */
    if (name[0] == 's' && name[1] == 'r' && IS_DIGIT(name[2]))
    {
        return 0;
    }
    
    return 1;
}

static int vtoy_find_disk_by_size(unsigned long long size, char *diskname)
{
    unsigned long long cursize = 0;
    DIR* dir = NULL;
    struct dirent* p = NULL;
    int rc = 0;

    dir = opendir("/sys/block");
    if (!dir)
    {
        return 0;
    }
    
    while ((p = readdir(dir)) != NULL)
    {
        if (!vtoy_is_possible_blkdev(p->d_name))
        {
            debug("disk %s is filted by name\n", p->d_name);
            continue;
        }
    
        cursize = vtoy_get_disk_size_in_byte(p->d_name);
        debug("disk %s size %llu\n", p->d_name, (unsigned long long)cursize);
        if (cursize == size)
        {
            sprintf(diskname, "%s", p->d_name);
            rc++;
        }
    }
    closedir(dir);
    return rc;    
}

static int vtoy_find_disk_by_guid(uint8_t *guid, char *diskname)
{
    int rc = 0;
    int count = 0;
    DIR* dir = NULL;
    struct dirent* p = NULL;
    uint8_t vtguid[16];

    dir = opendir("/sys/block");
    if (!dir)
    {
        return 0;
    }
    
    while ((p = readdir(dir)) != NULL)
    {
        if (!vtoy_is_possible_blkdev(p->d_name))
        {
            debug("disk %s is filted by name\n", p->d_name);        
            continue;
        }
    
        memset(vtguid, 0, sizeof(vtguid));
        rc = vtoy_get_disk_guid(p->d_name, vtguid);
        if (rc == 0 && memcmp(vtguid, guid, 16) == 0)
        {
            sprintf(diskname, "%s", p->d_name);
            count++;
        }
    }
    closedir(dir);
    
    return count;    
}

static int vtoy_printf_iso_path(ventoy_os_param *param)
{
    printf("%s\n", param->vtoy_img_path);
    return 0;
}

static int vtoy_print_os_param(ventoy_os_param *param, char *diskname)
{
    int   cnt = 0;
    char *path = param->vtoy_img_path;
    const char *fs;

    cnt = vtoy_find_disk_by_size(param->vtoy_disk_size, diskname);
    if (cnt > 1)
    {
        cnt = vtoy_find_disk_by_guid(param->vtoy_disk_guid, diskname);
    }
    else if (cnt == 0)
    {
        cnt = vtoy_find_disk_by_guid(param->vtoy_disk_guid, diskname);
        debug("find 0 disk by size, try with guid cnt=%d...\n", cnt);
    }

    if (param->vtoy_disk_part_type < ventoy_fs_max)
    {
        fs = g_ventoy_fs[param->vtoy_disk_part_type];
    }
    else
    {
        fs = "unknown";
    }

    if (1 == cnt)
    {
        printf("/dev/%s#%s#%s\n", diskname, fs, path);
        return 0;
    }
    else
    {
        return 1;
    }
}

static int vtoy_check_device(ventoy_os_param *param, const char *device)
{
    unsigned long long size; 
    uint8_t vtguid[16] = {0};

    debug("vtoy_check_device for <%s>\n", device);

    size = vtoy_get_disk_size_in_byte(device);
    vtoy_get_disk_guid(device, vtguid);

    debug("param->vtoy_disk_size=%llu size=%llu\n", 
        (unsigned long long)param->vtoy_disk_size, (unsigned long long)size);

    if ((param->vtoy_disk_size == size || param->vtoy_disk_size == size + 512) && 
        memcmp(vtguid, param->vtoy_disk_guid, 16) == 0)
    {
        debug("<%s> is right ventoy disk\n", device);
        return 0;
    }
    else
    {
        debug("<%s> is NOT right ventoy disk\n", device);
        return 1;
    }
}

/*
 *  Find disk and image path from ventoy runtime data.
 *  By default data is read from phymem(legacy bios) or efivar(UEFI), if -f is input, data is read from file.
 *  
 *  -f datafile     os param data file. 
 *  -c /dev/xxx     check ventoy disk
 *  -v              be verbose
 *  -l              also print image disk location 
 */
int vtoydump_main(int argc, char **argv)
{
    int rc;
    int ch;
    int print_path = 0;
    char filename[256] = {0};
    char diskname[256] = {0};
    char device[64] = {0};
    ventoy_os_param *param = NULL;

    while ((ch = getopt(argc, argv, "c:f:p:v::")) != -1)
    {
        if (ch == 'f')
        {
            strncpy(filename, optarg, sizeof(filename) - 1);
        }
        else if (ch == 'v')
        {
            verbose = 1;
        }
        else if (ch == 'c')
        {
            strncpy(device, optarg, sizeof(device) - 1);
        }
        else if (ch == 'p')
        {
            print_path = 1;
            strncpy(filename, optarg, sizeof(filename) - 1);
        }
        else
        {
            fprintf(stderr, "Usage: %s -f datafile [ -v ] \n", argv[0]);
            return 1;
        }
    }

    if (filename[0] == 0)
    {
        fprintf(stderr, "Usage: %s -f datafile [ -v ] \n", argv[0]);
        return 1;
    }

    param = malloc(sizeof(ventoy_os_param));
    if (NULL == param)
    {
        fprintf(stderr, "failed to alloc memory with size %d error %d\n", 
                (int)sizeof(ventoy_os_param), errno);
        return 1;
    }
    
    memset(param, 0, sizeof(ventoy_os_param));

    debug("get os pararm from file %s\n", filename);
    rc = vtoy_os_param_from_file(filename, param);
    if (rc)
    {
        debug("ventoy os param not found %d\n", rc);
        goto end;
    }

    if (verbose)
    {
        vtoy_dump_os_param(param);
    }

    if (print_path)
    {
        rc = vtoy_printf_iso_path(param);
    }
    else if (device[0])
    {
        rc = vtoy_check_device(param, device);
    }
    else
    {
        // print os param, you can change the output format in the function
        rc = vtoy_print_os_param(param, diskname);
    }

end:
    if (param)
    {
        free(param);
    }
    return rc;
}

// wrapper main
#ifndef BUILD_VTOY_TOOL
int main(int argc, char **argv)
{
    return vtoydump_main(argc, argv);
}
#endif

