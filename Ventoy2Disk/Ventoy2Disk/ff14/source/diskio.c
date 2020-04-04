/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include <Windows.h>

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

/* Definitions of physical drive number for each drive */
#define DEV_RAM		0	/* Example: Map Ramdisk to physical drive 0 */
#define DEV_MMC		1	/* Example: Map MMC/SD card to physical drive 1 */
#define DEV_USB		2	/* Example: Map USB MSD to physical drive 2 */

void Log(const char *fmt, ...);

static UINT8 g_MbrSector[512];
HANDLE g_hPhyDrive;
UINT64 g_SectorCount;

void disk_io_set_param(HANDLE Handle, UINT64 SectorCount)
{
    g_hPhyDrive = Handle;
    g_SectorCount = SectorCount;
}

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
    return RES_OK;
#if 0
	DSTATUS stat;
	int result;

	switch (pdrv) {
	case DEV_RAM :
		result = RAM_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_MMC :
		result = MMC_disk_status();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_status();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;
#endif
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
    return RES_OK;
#if 0
	DSTATUS stat;
	int result;

	switch (pdrv) {
	case DEV_RAM :
		result = RAM_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_MMC :
		result = MMC_disk_initialize();

		// translate the reslut code here

		return stat;

	case DEV_USB :
		result = USB_disk_initialize();

		// translate the reslut code here

		return stat;
	}
	return STA_NOINIT;
#endif
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
    DWORD dwSize;
    BOOL bRet;
    LARGE_INTEGER liCurrentPosition;

    liCurrentPosition.QuadPart = sector * 512;
    SetFilePointerEx(g_hPhyDrive, liCurrentPosition, &liCurrentPosition, FILE_BEGIN);

    bRet = ReadFile(g_hPhyDrive, buff, count * 512, &dwSize, NULL);

    if (dwSize != count * 512)
    {
        Log("ReadFile error bRet:%u WriteSize:%u dwSize:%u ErrCode:%u", bRet, count * 512, dwSize, GetLastError());
    }

    if (sector == 0)
    {
        memcpy(buff, g_MbrSector, sizeof(g_MbrSector));
    }

    return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
    DWORD dwSize;
    BOOL bRet;
    LARGE_INTEGER liCurrentPosition;

    // skip MBR
    if (sector == 0)
    {
        memcpy(g_MbrSector, buff, sizeof(g_MbrSector));

        if (count == 1)
        {
            return RES_OK;
        }

        sector++;
        count--;
    }

    liCurrentPosition.QuadPart = sector * 512;
    SetFilePointerEx(g_hPhyDrive, liCurrentPosition, &liCurrentPosition, FILE_BEGIN);

    bRet = WriteFile(g_hPhyDrive, buff, count * 512, &dwSize, NULL);

    if (dwSize != count * 512)
    {
        Log("WriteFile error bRet:%u WriteSize:%u dwSize:%u ErrCode:%u", bRet, count * 512, dwSize, GetLastError());
    }

    return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    switch (cmd)
    {
        case CTRL_SYNC:
        {
            //FILE_FLAG_NO_BUFFERING & FILE_FLAG_WRITE_THROUGH was set, no need to sync
            break;
        }
        case GET_SECTOR_COUNT:
        {
            *(LBA_t *)buff = g_SectorCount;
            break;
        }
        case GET_SECTOR_SIZE:
        {
            *(WORD *)buff = 512;
            break;
        }
        case GET_BLOCK_SIZE:
        {
            *(DWORD *)buff = 8;
            break;
        }
    }

    return RES_OK;

#if 0
	DRESULT res;
	int result;

	switch (pdrv) {
	case DEV_RAM :

		// Process of the command for the RAM drive

		return res;

	case DEV_MMC :

		// Process of the command for the MMC/SD card

		return res;

	case DEV_USB :

		// Process of the command the USB drive

		return res;
	}

	return RES_PARERR;
#endif
}

