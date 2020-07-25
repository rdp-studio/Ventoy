/******************************************************************************
* vtoyjump.c
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
#include <Windows.h>
#include <virtdisk.h>
#include <winioctl.h>
#include <VersionHelpers.h>
#include "vtoyjump.h"
#include "fat_filelib.h"

static ventoy_os_param g_os_param;
static ventoy_windows_data g_windows_data;
static UINT8 g_os_param_reserved[32];
static BOOL g_64bit_system = FALSE;
static ventoy_guid g_ventoy_guid = VENTOY_GUID;

void Log(const char *Fmt, ...)
{
	va_list Arg;
	int Len = 0;
	FILE *File = NULL;
	SYSTEMTIME Sys;
	char szBuf[1024];

	GetLocalTime(&Sys);
	Len += sprintf_s(szBuf, sizeof(szBuf),
		"[%4d/%02d/%02d %02d:%02d:%02d.%03d] ",
		Sys.wYear, Sys.wMonth, Sys.wDay,
		Sys.wHour, Sys.wMinute, Sys.wSecond,
		Sys.wMilliseconds);

	va_start(Arg, Fmt);
	Len += vsnprintf_s(szBuf + Len, sizeof(szBuf)-Len, sizeof(szBuf)-Len, Fmt, Arg);
	va_end(Arg);

	fopen_s(&File, "ventoy.log", "a+");
	if (File)
	{
		fwrite(szBuf, 1, Len, File);
		fwrite("\n", 1, 1, File);
		fclose(File);
	}
}


static int LoadNtDriver(const char *DrvBinPath)
{
	int i;
	int rc = 0;
	BOOL Ret;
	DWORD Status;
	SC_HANDLE hServiceMgr;
	SC_HANDLE hService;
	char name[256] = { 0 };

	for (i = (int)strlen(DrvBinPath) - 1; i >= 0; i--)
	{
		if (DrvBinPath[i] == '\\' || DrvBinPath[i] == '/')
		{
			sprintf_s(name, sizeof(name), "%s", DrvBinPath + i + 1);
			break;
		}
	}

	Log("Load NT driver: %s %s", DrvBinPath, name);

	hServiceMgr = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (hServiceMgr == NULL)
	{
		Log("OpenSCManager failed Error:%u", GetLastError());
		return 1;
	}

	Log("OpenSCManager OK");

	hService = CreateServiceA(hServiceMgr,
		name,
		name,
		SERVICE_ALL_ACCESS,
		SERVICE_KERNEL_DRIVER,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_NORMAL,
		DrvBinPath,
		NULL, NULL, NULL, NULL, NULL);
	if (hService == NULL)
	{
		Status = GetLastError();
		if (Status != ERROR_IO_PENDING && Status != ERROR_SERVICE_EXISTS)
		{
			Log("CreateService failed v %u", Status);
			CloseServiceHandle(hServiceMgr);
			return 1;
		}

		hService = OpenServiceA(hServiceMgr, name, SERVICE_ALL_ACCESS);
		if (hService == NULL)
		{
			Log("OpenService failed %u", Status);
			CloseServiceHandle(hServiceMgr);
			return 1;
		}
	}

	Log("CreateService imdisk OK");

	Ret = StartServiceA(hService, 0, NULL);
	if (Ret)
	{
		Log("StartService OK");
	}
	else
	{
		Status = GetLastError();
		if (Status == ERROR_SERVICE_ALREADY_RUNNING)
		{
			rc = 0;
		}
		else
		{
			Log("StartService error  %u", Status);
			rc = 1;
		}
	}

	CloseServiceHandle(hService);
	CloseServiceHandle(hServiceMgr);

	Log("Load NT driver %s", rc ? "failed" : "success");

	return rc;
}

static int ReadWholeFile2Buf(const char *Fullpath, void **Data, DWORD *Size)
{
	int rc = 1;
	DWORD FileSize;
	DWORD dwSize;
	HANDLE Handle;
	BYTE *Buffer = NULL;

	Log("ReadWholeFile2Buf <%s>", Fullpath);

	Handle = CreateFileA(Fullpath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		Log("Could not open the file<%s>, error:%u", Fullpath, GetLastError());
		goto End;
	}

	FileSize = SetFilePointer(Handle, 0, NULL, FILE_END);

	Buffer = malloc(FileSize);
	if (!Buffer)
	{
		Log("Failed to alloc memory size:%u", FileSize);
		goto End;
	}

	SetFilePointer(Handle, 0, NULL, FILE_BEGIN);
	if (!ReadFile(Handle, Buffer, FileSize, &dwSize, NULL))
	{
		Log("ReadFile failed, dwSize:%u  error:%u", dwSize, GetLastError());
		goto End;
	}

	*Data = Buffer;
	*Size = FileSize;

	Log("Success read file size:%u", FileSize);

	rc = 0;

End:
	SAFE_CLOSE_HANDLE(Handle);

	return rc;
}

static BOOL CheckPeHead(BYTE *Head)
{
	UINT32 PeOffset;

	if (Head[0] != 'M' || Head[1] != 'Z')
	{
		return FALSE;
	}

	PeOffset = *(UINT32 *)(Head + 60);
	if (*(UINT32 *)(Head + PeOffset) != 0x00004550)
	{
		return FALSE;
	}

	return TRUE;
}

static BOOL IsPe64(BYTE *buffer)
{
	DWORD pe_off;

	if (!CheckPeHead(buffer))
	{
		return FALSE;
	}

	pe_off = *(UINT32 *)(buffer + 60);
	if (*(UINT16 *)(buffer + pe_off + 24) == 0x020b)
	{
		return TRUE;
	}

	return FALSE;
}


static BOOL CheckOsParam(ventoy_os_param *param)
{
	UINT32 i;
	BYTE Sum = 0;

	if (memcmp(&param->guid, &g_ventoy_guid, sizeof(ventoy_guid)))
	{
		return FALSE;
	}

	for (i = 0; i < sizeof(ventoy_os_param); i++)
	{
		Sum += *((BYTE *)param + i);
	}
	
	if (Sum)
	{
		return FALSE;
	}

	if (param->vtoy_img_location_addr % 4096)
	{
		return FALSE;
	}

	return TRUE;
}

static int SaveBuffer2File(const char *Fullpath, void *Buffer, DWORD Length)
{
	int rc = 1;
	DWORD dwSize;
	HANDLE Handle;

	Log("SaveBuffer2File <%s> len:%u", Fullpath, Length);

	Handle = CreateFileA(Fullpath, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, 0, CREATE_NEW, 0, 0);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		Log("Could not create new file, error:%u", GetLastError());
		goto End;
	}

	WriteFile(Handle, Buffer, Length, &dwSize, NULL);

	rc = 0;

End:
	SAFE_CLOSE_HANDLE(Handle);

	return rc;
}

static BOOL IsPathExist(BOOL Dir, const char *Fmt, ...)
{
	va_list Arg;
	HANDLE hFile;
	DWORD Attr;
	CHAR FilePath[MAX_PATH];

	va_start(Arg, Fmt);
	vsnprintf_s(FilePath, sizeof(FilePath), sizeof(FilePath), Fmt, Arg);
	va_end(Arg);

	hFile = CreateFileA(FilePath, FILE_READ_EA, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (INVALID_HANDLE_VALUE == hFile)
	{
		return FALSE;
	}

	CloseHandle(hFile);

	Attr = GetFileAttributesA(FilePath);

	if (Dir)
	{
		if ((Attr & FILE_ATTRIBUTE_DIRECTORY) == 0)
		{
			return FALSE;
		}
	}
	else
	{
		if (Attr & FILE_ATTRIBUTE_DIRECTORY)
		{
			return FALSE;
		}
	}

	return TRUE;
}

static int GetPhyDiskUUID(const char LogicalDrive, UINT8 *UUID, DISK_EXTENT *DiskExtent)
{
	BOOL Ret;
	DWORD dwSize;
	HANDLE Handle;
	VOLUME_DISK_EXTENTS DiskExtents;
	CHAR PhyPath[128];
	UINT8 SectorBuf[512];

	Log("GetPhyDiskUUID %C", LogicalDrive);

	sprintf_s(PhyPath, sizeof(PhyPath), "\\\\.\\%C:", LogicalDrive);
	Handle = CreateFileA(PhyPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		Log("Could not open the disk<%s>, error:%u", PhyPath, GetLastError());
		return 1;
	}

	Ret = DeviceIoControl(Handle,
		IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
		NULL,
		0,
		&DiskExtents,
		(DWORD)(sizeof(DiskExtents)),
		(LPDWORD)&dwSize,
		NULL);
	if (!Ret || DiskExtents.NumberOfDiskExtents == 0)
	{
		Log("DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed, error:%u", GetLastError());
		CloseHandle(Handle);
		return 1;
	}
	CloseHandle(Handle);

	memcpy(DiskExtent, DiskExtents.Extents, sizeof(DiskExtent));
	Log("%C: is in PhysicalDrive%d ", LogicalDrive, DiskExtents.Extents[0].DiskNumber);

	sprintf_s(PhyPath, sizeof(PhyPath), "\\\\.\\PhysicalDrive%d", DiskExtents.Extents[0].DiskNumber);
	Handle = CreateFileA(PhyPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		Log("Could not open the disk<%s>, error:%u", PhyPath, GetLastError());
		return 1;
	}

	if (!ReadFile(Handle, SectorBuf, sizeof(SectorBuf), &dwSize, NULL))
	{
		Log("ReadFile failed, dwSize:%u  error:%u", dwSize, GetLastError());
		CloseHandle(Handle);
		return 1;
	}
	
	memcpy(UUID, SectorBuf + 0x180, 16);
	CloseHandle(Handle);
	return 0;
}

int VentoyMountISOByAPI(const char *IsoPath)
{
	HANDLE Handle;
	DWORD Status;
	WCHAR wFilePath[512] = { 0 };
	VIRTUAL_STORAGE_TYPE StorageType;
	OPEN_VIRTUAL_DISK_PARAMETERS OpenParameters;
	ATTACH_VIRTUAL_DISK_PARAMETERS AttachParameters;

	Log("VentoyMountISOByAPI <%s>", IsoPath);

	MultiByteToWideChar(CP_ACP, 0, IsoPath, (int)strlen(IsoPath), wFilePath, (int)(sizeof(wFilePath) / sizeof(WCHAR)));

	memset(&StorageType, 0, sizeof(StorageType));
	memset(&OpenParameters, 0, sizeof(OpenParameters));
	memset(&AttachParameters, 0, sizeof(AttachParameters));

	OpenParameters.Version = OPEN_VIRTUAL_DISK_VERSION_1;
	AttachParameters.Version = ATTACH_VIRTUAL_DISK_VERSION_1;

	Status = OpenVirtualDisk(&StorageType, wFilePath, VIRTUAL_DISK_ACCESS_READ, 0, &OpenParameters, &Handle);
	if (Status != ERROR_SUCCESS)
	{
		if (ERROR_VIRTDISK_PROVIDER_NOT_FOUND == Status)
		{
			Log("VirtualDisk for ISO file is not supported in current system");
		}
		else
		{
			Log("Failed to open virtual disk ErrorCode:%u", Status);
		}
		return 1;
	}

	Log("OpenVirtualDisk success");

	Status = AttachVirtualDisk(Handle, NULL, ATTACH_VIRTUAL_DISK_FLAG_READ_ONLY | ATTACH_VIRTUAL_DISK_FLAG_PERMANENT_LIFETIME, 0, &AttachParameters, NULL);
	if (Status != ERROR_SUCCESS)
	{
		Log("Failed to attach virtual disk ErrorCode:%u", Status);
		CloseHandle(Handle);
		return 1;
	}

	CloseHandle(Handle);
	return 0;
}


static HANDLE g_FatPhyDrive;
static UINT64 g_Part2StartSec;

static int CopyFileFromFatDisk(const CHAR* SrcFile, const CHAR *DstFile)
{
	int rc = 1;
	int size = 0;
	char *buf = NULL;
	void *flfile = NULL;

	Log("CopyFileFromFatDisk (%s)==>(%s)", SrcFile, DstFile);

	flfile = fl_fopen(SrcFile, "rb");
	if (flfile)
	{
		fl_fseek(flfile, 0, SEEK_END);
		size = (int)fl_ftell(flfile);
		fl_fseek(flfile, 0, SEEK_SET);

		buf = (char *)malloc(size);
		if (buf)
		{
			fl_fread(buf, 1, size, flfile);

			rc = 0;
			SaveBuffer2File(DstFile, buf, size);
			free(buf);
		}

		fl_fclose(flfile);
	}

	return rc;
}

static int VentoyFatDiskRead(uint32 Sector, uint8 *Buffer, uint32 SectorCount)
{
	DWORD dwSize;
	BOOL bRet;
	DWORD ReadSize;
	LARGE_INTEGER liCurrentPosition;

	liCurrentPosition.QuadPart = Sector + g_Part2StartSec;
	liCurrentPosition.QuadPart *= 512;
	SetFilePointerEx(g_FatPhyDrive, liCurrentPosition, &liCurrentPosition, FILE_BEGIN);

	ReadSize = (DWORD)(SectorCount * 512);

	bRet = ReadFile(g_FatPhyDrive, Buffer, ReadSize, &dwSize, NULL);
	if (bRet == FALSE || dwSize != ReadSize)
	{
		Log("ReadFile error bRet:%u WriteSize:%u dwSize:%u ErrCode:%u\n", bRet, ReadSize, dwSize, GetLastError());
	}

	return 1;
}

static CHAR GetMountLogicalDrive(void)
{
	CHAR Letter = 'Y';
	DWORD Drives;
	DWORD Mask = 0x1000000;

	Drives = GetLogicalDrives();
    Log("Drives=0x%x", Drives);
    
	while (Mask)
	{
		if ((Drives & Mask) == 0)
		{
			break;
		}

		Letter--;
		Mask >>= 1;
	}

	return Letter;
}

UINT64 GetVentoyEfiPartStartSector(HANDLE hDrive)
{
	BOOL bRet;
	DWORD dwSize; 
	MBR_HEAD MBR;	
	VTOY_GPT_INFO *pGpt = NULL;
	UINT64 StartSector = 0;

	SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);

	bRet = ReadFile(hDrive, &MBR, sizeof(MBR), &dwSize, NULL);
	Log("Read MBR Ret:%u Size:%u code:%u", bRet, dwSize, LASTERR);

	if ((!bRet) || (dwSize != sizeof(MBR)))
	{
		0;
	}

	if (MBR.PartTbl[0].FsFlag == 0xEE)
	{
		Log("GPT partition style");

		pGpt = malloc(sizeof(VTOY_GPT_INFO));
		if (!pGpt)
		{
			return 0;
		}

		SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);
		bRet = ReadFile(hDrive, pGpt, sizeof(VTOY_GPT_INFO), &dwSize, NULL);		
		if ((!bRet) || (dwSize != sizeof(VTOY_GPT_INFO)))
		{
			Log("Failed to read gpt info %d %u %d", bRet, dwSize, LASTERR);
			return 0;
		}

		StartSector = pGpt->PartTbl[1].StartLBA;
		free(pGpt);
	}
	else
	{
		Log("MBR partition style");
		StartSector = MBR.PartTbl[1].StartSectorId;
	}

	Log("GetVentoyEfiPart StartSector: %llu", StartSector);
	return StartSector;
}

int VentoyMountISOByImdisk(const char *IsoPath, DWORD PhyDrive)
{
	int rc = 1;
	BOOL bRet;
	CHAR Letter;
	DWORD dwBytes;
	HANDLE hDrive;
	CHAR PhyPath[MAX_PATH];
	STARTUPINFOA Si;
	PROCESS_INFORMATION Pi;
	GET_LENGTH_INFORMATION LengthInfo;

	Log("VentoyMountISOByImdisk %s", IsoPath);

	sprintf_s(PhyPath, sizeof(PhyPath), "\\\\.\\PhysicalDrive%d", PhyDrive);
	hDrive = CreateFileA(PhyPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
	if (hDrive == INVALID_HANDLE_VALUE)
	{
		Log("Could not open the disk<%s>, error:%u", PhyPath, GetLastError());
		goto End;
	}

	bRet = DeviceIoControl(hDrive, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &LengthInfo, sizeof(LengthInfo), &dwBytes, NULL);
	if (!bRet)
	{
		Log("Could not get phy disk %s size, error:%u", PhyPath, GetLastError());
		goto End;
	}

	g_FatPhyDrive = hDrive;
	g_Part2StartSec = GetVentoyEfiPartStartSector(hDrive);

	Log("Parse FAT fs...");

	fl_init();

	if (0 == fl_attach_media(VentoyFatDiskRead, NULL))
	{
		if (g_64bit_system)
		{
			CopyFileFromFatDisk("/ventoy/imdisk/64/imdisk.sys", "ventoy\\imdisk.sys");
			CopyFileFromFatDisk("/ventoy/imdisk/64/imdisk.exe", "ventoy\\imdisk.exe");
			CopyFileFromFatDisk("/ventoy/imdisk/64/imdisk.cpl", "ventoy\\imdisk.cpl");
		}
		else
		{
			CopyFileFromFatDisk("/ventoy/imdisk/32/imdisk.sys", "ventoy\\imdisk.sys");
			CopyFileFromFatDisk("/ventoy/imdisk/32/imdisk.exe", "ventoy\\imdisk.exe");
			CopyFileFromFatDisk("/ventoy/imdisk/32/imdisk.cpl", "ventoy\\imdisk.cpl");
		}
		
		GetCurrentDirectoryA(sizeof(PhyPath), PhyPath);
		strcat_s(PhyPath, sizeof(PhyPath), "\\ventoy\\imdisk.sys");

		if (LoadNtDriver(PhyPath) == 0)
		{
			rc = 0;

			Letter = GetMountLogicalDrive();
            sprintf_s(PhyPath, sizeof(PhyPath), "ventoy\\imdisk.exe -a -o ro -f %s -m %C:", IsoPath, Letter);

			Log("mount iso to %C: use imdisk cmd <%s>", Letter, PhyPath);

            GetStartupInfoA(&Si);

            Si.dwFlags |= STARTF_USESHOWWINDOW;
            Si.wShowWindow = SW_HIDE;

			CreateProcessA(NULL, PhyPath, NULL, NULL, FALSE, 0, NULL, NULL, &Si, &Pi);
			WaitForSingleObject(Pi.hProcess, INFINITE);
		}
	}
	fl_shutdown();

End:

	SAFE_CLOSE_HANDLE(hDrive);

	return rc;
}

static int MountIsoFile(CONST CHAR *IsoPath, DWORD PhyDrive)
{
    if (IsWindows8OrGreater())
    {
        Log("This is Windows 8 or latter...");
        if (VentoyMountISOByAPI(IsoPath) == 0)
        {
            Log("Mount iso by API success");
            return 0;
        }
        else
        {
            Log("Mount iso by API failed, maybe not supported, try imdisk");
            return VentoyMountISOByImdisk(IsoPath, PhyDrive);
        }
    }
    else
    {
        Log("This is before Windows 8 ...");
        if (VentoyMountISOByImdisk(IsoPath, PhyDrive) == 0)
        {
            Log("Mount iso by imdisk success");
            return 0;
        }
        else
        {
            return VentoyMountISOByAPI(IsoPath);
        }
    }
}

static int GetPhyDriveByLogicalDrive(int DriveLetter)
{
    BOOL Ret;
    DWORD dwSize;
    HANDLE Handle;
    VOLUME_DISK_EXTENTS DiskExtents;
    CHAR PhyPath[128];

    sprintf_s(PhyPath, sizeof(PhyPath), "\\\\.\\%C:", (CHAR)DriveLetter);

    Handle = CreateFileA(PhyPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        Log("Could not open the disk<%s>, error:%u", PhyPath, GetLastError());
        return -1;
    }

    Ret = DeviceIoControl(Handle,
        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
        NULL,
        0,
        &DiskExtents,
        (DWORD)(sizeof(DiskExtents)),
        (LPDWORD)&dwSize,
        NULL);

    if (!Ret || DiskExtents.NumberOfDiskExtents == 0)
    {
        Log("DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed %s, error:%u", PhyPath, GetLastError());
        SAFE_CLOSE_HANDLE(Handle);
        return -1;
    }
    SAFE_CLOSE_HANDLE(Handle);

    Log("LogicalDrive:%s PhyDrive:%d Offset:%llu ExtentLength:%llu",
        PhyPath,
        DiskExtents.Extents[0].DiskNumber,
        DiskExtents.Extents[0].StartingOffset.QuadPart,
        DiskExtents.Extents[0].ExtentLength.QuadPart
        );

    return (int)DiskExtents.Extents[0].DiskNumber;
}


static int DeleteVentoyPart2MountPoint(DWORD PhyDrive)
{
    CHAR Letter = 'A';
    DWORD Drives;
    DWORD PhyDisk;
    CHAR DriveName[] = "?:\\";

    Log("DeleteVentoyPart2MountPoint Phy%u ...", PhyDrive);

    Drives = GetLogicalDrives();
    while (Drives)
    {
        if ((Drives & 0x01) && IsPathExist(FALSE, "%C:\\ventoy\\ventoy.cpio", Letter))
        {
            Log("File %C:\\ventoy\\ventoy.cpio exist", Letter);

            PhyDisk = GetPhyDriveByLogicalDrive(Letter);
            Log("PhyDisk=%u for %C", PhyDisk, Letter);

            if (PhyDisk == PhyDrive)
            {
                DriveName[0] = Letter;
                DeleteVolumeMountPointA(DriveName);
                return 0;
            }
        }

        Letter++;
        Drives >>= 1;
    }

    return 1;
}

static BOOL check_tar_archive(const char *archive, CHAR *tarName)
{
    int len;
    int nameLen;
    const char *pos = archive;
    const char *slash = archive;

    while (*pos)
    {
        if (*pos == '\\' || *pos == '/')
        {
            slash = pos;
        }
        pos++;
    }

    len = (int)strlen(slash);

    if (len > 7 && (strncmp(slash + len - 7, ".tar.gz", 7) == 0 || strncmp(slash + len - 7, ".tar.xz", 7) == 0))
    {
        nameLen = (int)sprintf_s(tarName, MAX_PATH, "X:%s", slash);
        tarName[nameLen - 3] = 0;
        return TRUE;
    }
    else if (len > 8 && strncmp(slash + len - 8, ".tar.bz2", 8) == 0)
    {
        nameLen = (int)sprintf_s(tarName, MAX_PATH, "X:%s", slash);
        tarName[nameLen - 4] = 0;
        return TRUE;
    }
    else if (len > 9 && strncmp(slash + len - 9, ".tar.lzma", 9) == 0)
    {
        nameLen = (int)sprintf_s(tarName, MAX_PATH, "X:%s", slash);
        tarName[nameLen - 5] = 0;
        return TRUE;
    }

    return FALSE;
}

static int DecompressInjectionArchive(const char *archive, DWORD PhyDrive)
{
    int rc = 1;
    BOOL bRet;
    DWORD dwBytes;
    HANDLE hDrive;
    HANDLE hOut;
    DWORD flags = CREATE_NO_WINDOW;
    CHAR StrBuf[MAX_PATH];
    CHAR tarName[MAX_PATH];
    STARTUPINFOA Si;
    PROCESS_INFORMATION Pi;
    PROCESS_INFORMATION NewPi;
    GET_LENGTH_INFORMATION LengthInfo;
    SECURITY_ATTRIBUTES Sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    Log("DecompressInjectionArchive %s", archive);

    sprintf_s(StrBuf, sizeof(StrBuf), "\\\\.\\PhysicalDrive%d", PhyDrive);
    hDrive = CreateFileA(StrBuf, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Could not open the disk<%s>, error:%u", StrBuf, GetLastError());
        goto End;
    }

    bRet = DeviceIoControl(hDrive, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &LengthInfo, sizeof(LengthInfo), &dwBytes, NULL);
    if (!bRet)
    {
        Log("Could not get phy disk %s size, error:%u", StrBuf, GetLastError());
        goto End;
    }

    g_FatPhyDrive = hDrive;
	g_Part2StartSec = GetVentoyEfiPartStartSector(hDrive);

    Log("Parse FAT fs...");

    fl_init();

    if (0 == fl_attach_media(VentoyFatDiskRead, NULL))
    {
        if (g_64bit_system)
        {
            CopyFileFromFatDisk("/ventoy/7z/64/7za.exe", "ventoy\\7za.exe");
        }
        else
        {
            CopyFileFromFatDisk("/ventoy/7z/32/7za.exe", "ventoy\\7za.exe");
        }

        sprintf_s(StrBuf, sizeof(StrBuf), "ventoy\\7za.exe x -y -aoa -oX:\\ %s", archive);

        Log("extract inject to X:");
        Log("cmdline:<%s>", StrBuf);

        GetStartupInfoA(&Si);

        hOut = CreateFileA("ventoy\\7z.log",
            FILE_APPEND_DATA,
            FILE_SHARE_WRITE | FILE_SHARE_READ,
            &Sa,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        Si.dwFlags |= STARTF_USESTDHANDLES;

        if (hOut != INVALID_HANDLE_VALUE)
        {
            Si.hStdError = hOut;
            Si.hStdOutput = hOut;
        }

        CreateProcessA(NULL, StrBuf, NULL, NULL, TRUE, flags, NULL, NULL, &Si, &Pi);
        WaitForSingleObject(Pi.hProcess, INFINITE);

        //
        // decompress tar archive, for tar.gz/tar.xz/tar.bz2
        //
        if (check_tar_archive(archive, tarName))
        {
            Log("Decompress tar archive...<%s>", tarName);

            sprintf_s(StrBuf, sizeof(StrBuf), "ventoy\\7za.exe x -y -aoa -oX:\\ %s", tarName);

            CreateProcessA(NULL, StrBuf, NULL, NULL, TRUE, flags, NULL, NULL, &Si, &NewPi);
            WaitForSingleObject(NewPi.hProcess, INFINITE);

            Log("Now delete %s", tarName);
            DeleteFileA(tarName);
        }

        SAFE_CLOSE_HANDLE(hOut);
    }
    fl_shutdown();

End:

    SAFE_CLOSE_HANDLE(hDrive);

    return rc;
}

static int ProcessUnattendedInstallation(const char *script)
{
    DWORD dw;
    HKEY hKey;
    LSTATUS Ret;
    CHAR Letter;
    CHAR CurDir[MAX_PATH];

    Log("Copy unattended XML ...");
    
    GetCurrentDirectory(sizeof(CurDir), CurDir);
    Letter = CurDir[0];
    if ((Letter >= 'A' && Letter <= 'Z') || (Letter >= 'a' && Letter <= 'z'))
    {
        Log("Current Drive Letter: %C", Letter);
    }
    else
    {
        Letter = 'X';
    }
    
    sprintf_s(CurDir, sizeof(CurDir), "%C:\\Autounattend.xml", Letter);
    Log("Copy file <%s> --> <%s>", script, CurDir);
    CopyFile(script, CurDir, FALSE);

    Ret = RegCreateKeyEx(HKEY_LOCAL_MACHINE, "System\\Setup", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, &dw);
    if (ERROR_SUCCESS == Ret)
    {
        Ret = RegSetValueEx(hKey, "UnattendFile", 0, REG_SZ, CurDir, (DWORD)(strlen(CurDir) + 1));
    }

    return 0;
}

static int VentoyHook(ventoy_os_param *param)
{
    int rc;
	CHAR Letter = 'A';
	DISK_EXTENT DiskExtent;
	DWORD Drives = GetLogicalDrives();
	UINT8 UUID[16];
	CHAR IsoPath[MAX_PATH];

	Log("Logical Drives=0x%x Path:<%s>", Drives, param->vtoy_img_path);

	while (Drives)
	{
        if (Drives & 0x01)
        {
            sprintf_s(IsoPath, sizeof(IsoPath), "%C:\\%s", Letter, param->vtoy_img_path);
            if (IsPathExist(FALSE, "%s", IsoPath))
            {
                Log("File exist under %C:", Letter);
                if (GetPhyDiskUUID(Letter, UUID, &DiskExtent) == 0)
                {
                    if (memcmp(UUID, param->vtoy_disk_guid, 16) == 0)
                    {
                        Log("Disk UUID match");
                        break;
                    }
                }
            }
            else
            {
                Log("File NOT exist under %C:", Letter);
            }
        }

		Drives >>= 1;
		Letter++;
	}

	if (Drives == 0)
	{
		Log("Failed to find ISO file");
		return 1;
	}

	Log("Find ISO file <%s>", IsoPath);
    
    rc = MountIsoFile(IsoPath, DiskExtent.DiskNumber);
    Log("Mount ISO FILE: %s", rc == 0 ? "SUCCESS" : "FAILED");

    // for protect
    rc = DeleteVentoyPart2MountPoint(DiskExtent.DiskNumber);
    Log("Delete ventoy mountpoint: %s", rc == 0 ? "SUCCESS" : "NO NEED");
    
    if (g_windows_data.auto_install_script[0])
    {
        sprintf_s(IsoPath, sizeof(IsoPath), "%C:%s", Letter, g_windows_data.auto_install_script);
        if (IsPathExist(FALSE, "%s", IsoPath))
        {
            Log("use auto install script %s...", IsoPath);
            ProcessUnattendedInstallation(IsoPath);
        }
        else
        {
            Log("auto install script %s not exist", IsoPath);
        }
    }
    else
    {
        Log("auto install no need");
    }

    if (g_windows_data.injection_archive[0])
    {
        sprintf_s(IsoPath, sizeof(IsoPath), "%C:%s", Letter, g_windows_data.injection_archive);
        if (IsPathExist(FALSE, "%s", IsoPath))
        {
            Log("decompress injection archive %s...", IsoPath);
            DecompressInjectionArchive(IsoPath, DiskExtent.DiskNumber);
        }
        else
        {
            Log("injection archive %s not exist", IsoPath);
        }
    }
    else
    {
        Log("no injection archive found");
    }

    return 0;
}

const char * GetFileNameInPath(const char *fullpath)
{
	int i;
	const char *pos = NULL;

	if (strstr(fullpath, ":"))
	{
		for (i = (int)strlen(fullpath); i > 0; i--)
		{
			if (fullpath[i - 1] == '/' || fullpath[i - 1] == '\\')
			{
				return fullpath + i;
			}
		}
	}
	
	return fullpath;
}

int VentoyJump(INT argc, CHAR **argv, CHAR *LunchFile)
{
	int rc = 1;
	DWORD Pos;
	DWORD PeStart;
    DWORD FileSize;
	BYTE *Buffer = NULL; 
	CHAR ExeFileName[MAX_PATH];

	sprintf_s(ExeFileName, sizeof(ExeFileName), "%s", argv[0]);
	if (!IsPathExist(FALSE, "%s", ExeFileName))
	{
		Log("File %s NOT exist, now try %s.exe", ExeFileName, ExeFileName);
		sprintf_s(ExeFileName, sizeof(ExeFileName), "%s.exe", argv[0]);

		Log("File %s exist ? %s", ExeFileName, IsPathExist(FALSE, "%s", ExeFileName) ? "YES" : "NO");
	}

	if (ReadWholeFile2Buf(ExeFileName, (void **)&Buffer, &FileSize))
	{
		goto End;
	}
	
	g_64bit_system = IsPe64(Buffer);

	if (!IsPathExist(TRUE, "ventoy"))
	{
		if (!CreateDirectoryA("ventoy", NULL))
		{
			Log("Failed to create ventoy directory err:%u", GetLastError());
			goto End;
		}
	}

	for (PeStart = 0; PeStart < FileSize; PeStart += 16)
	{
		if (CheckOsParam((ventoy_os_param *)(Buffer + PeStart)) && 
            CheckPeHead(Buffer + PeStart + sizeof(ventoy_os_param) + sizeof(ventoy_windows_data)))
		{
			Log("Find os pararm at %u", PeStart);

            memcpy(&g_os_param, Buffer + PeStart, sizeof(ventoy_os_param));
            memcpy(&g_windows_data, Buffer + PeStart + sizeof(ventoy_os_param), sizeof(ventoy_windows_data));            
            memcpy(g_os_param_reserved, g_os_param.vtoy_reserved, sizeof(g_os_param_reserved));

            if (g_os_param_reserved[0] == 1)
			{
				Log("break here for debug .....");
				goto End;
			}

			// convert / to \\   
            for (Pos = 0; Pos < sizeof(g_os_param.vtoy_img_path) && g_os_param.vtoy_img_path[Pos]; Pos++)
			{
                if (g_os_param.vtoy_img_path[Pos] == '/')
				{
                    g_os_param.vtoy_img_path[Pos] = '\\';
				}
			}

			PeStart += sizeof(ventoy_os_param) + sizeof(ventoy_windows_data);
			sprintf_s(LunchFile, MAX_PATH, "ventoy\\%s", GetFileNameInPath(ExeFileName));
			SaveBuffer2File(LunchFile, Buffer + PeStart, FileSize - PeStart);
			break;
		}
	}

	if (PeStart >= FileSize)
	{
		Log("OS param not found");
		goto End;
	}

    if (g_os_param_reserved[0] == 2)
    {
        Log("skip hook for debug .....");
        rc = 0;
        goto End;
    }

    rc = VentoyHook(&g_os_param);

End:

	if (Buffer)
	{
		free(Buffer);
	}

	return rc;
}

int main(int argc, char **argv)
{
    int i = 0;
    int rc = 0;
	CHAR *Pos = NULL;
	CHAR CurDir[MAX_PATH];
	CHAR LunchFile[MAX_PATH];
	STARTUPINFOA Si;
	PROCESS_INFORMATION Pi;

	if (argv[0] && argv[0][0] && argv[0][1] == ':')
	{
		GetCurrentDirectoryA(sizeof(CurDir), CurDir);

		strcpy_s(LunchFile, sizeof(LunchFile), argv[0]);
		Pos = (char *)GetFileNameInPath(LunchFile);

		strcat_s(CurDir, sizeof(CurDir), "\\");
		strcat_s(CurDir, sizeof(CurDir), Pos);
		
		if (_stricmp(argv[0], CurDir) != 0)
		{
			*Pos = 0;
			SetCurrentDirectoryA(LunchFile);
		}
	}

	Log("######## VentoyJump ##########");
	Log("argc = %d argv[0] = <%s>", argc, argv[0]);

	if (Pos && *Pos == 0)
	{
		Log("Old current directory = <%s>", CurDir);
		Log("New current directory = <%s>", LunchFile);
	}
	else
	{
		GetCurrentDirectoryA(sizeof(CurDir), CurDir);
		Log("Current directory = <%s>", CurDir);
	}

    GetStartupInfoA(&Si);

    memset(LunchFile, 0, sizeof(LunchFile));
    rc = VentoyJump(argc, argv, LunchFile);

    if (g_os_param_reserved[0] == 3)
    {
        Log("Open log for debug ...");
        sprintf_s(LunchFile, sizeof(LunchFile), "%s", "notepad.exe ventoy.log");
    }
    else
    {
        Si.dwFlags |= STARTF_USESHOWWINDOW;
        Si.wShowWindow = SW_HIDE;
        Log("Ventoy jump %s ...", rc == 0 ? "success" : "failed");
    }

	CreateProcessA(NULL, LunchFile, NULL, NULL, FALSE, 0, NULL, NULL, &Si, &Pi);

    while (rc)
    {
        Log("Ventoy hook failed, now wait and retry ...");
        Sleep(1000);

        rc = VentoyHook(&g_os_param);
    }

	WaitForSingleObject(Pi.hProcess, INFINITE);

	return 0;
}
