/******************************************************************************
 * Ventoy2Disk.c
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

#include <Windows.h>
#include "resource.h"
#include "Language.h"
#include "Ventoy2Disk.h"

PHY_DRIVE_INFO *g_PhyDriveList = NULL;
DWORD g_PhyDriveCount = 0;
static int g_FilterRemovable = 0;
static int g_FilterUSB = 1;
int g_ForceOperation = 1;

int ParseCmdLineOption(LPSTR lpCmdLine)
{
    int i;
    char cfgfile[MAX_PATH];

    if (lpCmdLine && lpCmdLine[0])
    {
        Log("CmdLine:<%s>", lpCmdLine);
    }

    for (i = 0; i < __argc; i++)
    {
        if (strncmp(__argv[i], "-U", 2) == 0 ||
			strncmp(__argv[i], "-u", 2) == 0)
        {
            g_FilterUSB = 0;
        }
        else if (strncmp(__argv[i], "-F", 2) == 0)
        {
            g_ForceOperation = 1;
        }
    }

    GetCurrentDirectoryA(sizeof(cfgfile), cfgfile);
    strcat_s(cfgfile, sizeof(cfgfile), "\\Ventoy2Disk.ini");

    if (0 == GetPrivateProfileIntA("Filter", "USB", 1, cfgfile))
    {
        g_FilterUSB = 0;
    }

    if (1 == GetPrivateProfileIntA("Operation", "Force", 0, cfgfile))
    {
        g_ForceOperation = 1;
    }

    Log("Control Flag: %d %d %d", g_FilterRemovable, g_FilterUSB, g_ForceOperation);

    return 0;
}

static BOOL IsVentoyPhyDrive(int PhyDrive, UINT64 SizeBytes)
{
    int i;
    BOOL bRet;
    DWORD dwSize;
    HANDLE hDrive;
    MBR_HEAD MBR;
    UINT32 PartStartSector;
    UINT32 PartSectorCount;
    CHAR PhyDrivePath[128];

    safe_sprintf(PhyDrivePath, "\\\\.\\PhysicalDrive%d", PhyDrive);
    hDrive = CreateFileA(PhyDrivePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    Log("Create file Handle:%p %s status:%u", hDrive, PhyDrivePath, LASTERR);

    if (hDrive == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }

    bRet = ReadFile(hDrive, &MBR, sizeof(MBR), &dwSize, NULL);
    CHECK_CLOSE_HANDLE(hDrive);

    Log("Read MBR Ret:%u Size:%u code:%u", bRet, dwSize, LASTERR);

    if ((!bRet) || (dwSize != sizeof(MBR)))
    {
        return FALSE;
    }

    if (MBR.Byte55 != 0x55 || MBR.ByteAA != 0xAA)
    {
        Log("Byte55 ByteAA not match 0x%x 0x%x", MBR.Byte55, MBR.ByteAA);
        return FALSE;
    }

    for (i = 0; i < 4; i++)
    {
        Log("=========== Partition Table %d ============", i + 1);
        Log("PartTbl.Active = 0x%x", MBR.PartTbl[i].Active);
        Log("PartTbl.FsFlag = 0x%x", MBR.PartTbl[i].FsFlag);
        Log("PartTbl.StartSectorId = %u", MBR.PartTbl[i].StartSectorId);
        Log("PartTbl.SectorCount = %u", MBR.PartTbl[i].SectorCount);
        Log("PartTbl.StartHead = %u", MBR.PartTbl[i].StartHead);
        Log("PartTbl.StartSector = %u", MBR.PartTbl[i].StartSector);
        Log("PartTbl.StartCylinder = %u", MBR.PartTbl[i].StartCylinder);
        Log("PartTbl.EndHead = %u", MBR.PartTbl[i].EndHead);
        Log("PartTbl.EndSector = %u", MBR.PartTbl[i].EndSector);
        Log("PartTbl.EndCylinder = %u", MBR.PartTbl[i].EndCylinder);
    }

    if (MBR.PartTbl[2].SectorCount > 0 || MBR.PartTbl[3].SectorCount > 0)
    {
        Log("Part3 Part4 are not empty");
        return FALSE;
    }

    PartStartSector = 2048;
    PartSectorCount = (UINT32)((SizeBytes - VENTOY_EFI_PART_SIZE - SIZE_1MB) / 512);

    if (MBR.PartTbl[0].FsFlag != 0x07 ||
        MBR.PartTbl[0].StartSectorId != PartStartSector ||
        MBR.PartTbl[0].SectorCount != PartSectorCount)
    {
        Log("Part1 not match %u %u", PartStartSector, PartSectorCount);
        return FALSE;
    }

    PartStartSector = (UINT32)((SizeBytes - VENTOY_EFI_PART_SIZE) / 512);
    PartSectorCount = VENTOY_EFI_PART_SIZE / 512;

    if (MBR.PartTbl[1].Active != 0x80 ||
        MBR.PartTbl[1].FsFlag != 0xEF ||
        MBR.PartTbl[1].StartSectorId != PartStartSector ||
        MBR.PartTbl[1].SectorCount != PartSectorCount)
    {
        Log("Part2 not match %u %u", PartStartSector, PartSectorCount);
        return FALSE;
    }

    Log("PhysicalDrive%d is ventoy disk", PhyDrive);
    return TRUE;
}


static int FilterPhysicalDrive(PHY_DRIVE_INFO *pDriveList, DWORD DriveCount)
{
    DWORD i; 
    DWORD LogDrive;
    int Letter = 'A';
    int Id = 0;
    int LetterCount = 0;
    PHY_DRIVE_INFO *CurDrive;
    int LogLetter[VENTOY_MAX_PHY_DRIVE];
    int PhyDriveId[VENTOY_MAX_PHY_DRIVE];

    for (LogDrive = GetLogicalDrives(); LogDrive > 0; LogDrive >>= 1)
    {
        if (LogDrive & 0x01)
        {
            LogLetter[LetterCount] = Letter;
            PhyDriveId[LetterCount] = GetPhyDriveByLogicalDrive(Letter);

            Log("Logical Drive:%C  ===> PhyDrive:%d", LogLetter[LetterCount], PhyDriveId[LetterCount]);
            LetterCount++;
        }
        
        Letter++;
    }    

    for (i = 0; i < DriveCount; i++)
    {
        CurDrive = pDriveList + i;

        CurDrive->Id = -1;
        CurDrive->FirstDriveLetter = -1;

        // Too big for MBR
        if (CurDrive->SizeInBytes > 2199023255552ULL)
        {
            Log("<%s %s> is filtered for too big for MBR.", CurDrive->VendorId, CurDrive->ProductId);
            continue;
        }

        if (g_FilterRemovable && (!CurDrive->RemovableMedia))
        {
            Log("<%s %s> is filtered for not removable.", CurDrive->VendorId, CurDrive->ProductId);
            continue;
        }

        if (g_FilterUSB && CurDrive->BusType != BusTypeUsb)
        {
            Log("<%s %s> is filtered for not USB type.", CurDrive->VendorId, CurDrive->ProductId);
            continue;
        }
        
        CurDrive->Id = Id++;

        for (Letter = 0; Letter < LetterCount; Letter++)
        {
            if (PhyDriveId[Letter] == CurDrive->PhyDrive)
            {
                CurDrive->FirstDriveLetter = LogLetter[Letter];
                break;
            }
        }

        if (IsVentoyPhyDrive(CurDrive->PhyDrive, CurDrive->SizeInBytes))
        {
            GetVentoyVerInPhyDrive(CurDrive, CurDrive->VentoyVersion, sizeof(CurDrive->VentoyVersion));
        }
    }

    // for safe
    for (i = 0; i < DriveCount; i++)
    {
        CurDrive = pDriveList + i;
        if (CurDrive->Id < 0)
        {
            CurDrive->PhyDrive = 0x00FFFFFF;
        }
    }

    return Id;
}

PHY_DRIVE_INFO * GetPhyDriveInfoById(int Id)
{
    DWORD i;
    for (i = 0; i < g_PhyDriveCount; i++)
    {
        if (g_PhyDriveList[i].Id >= 0 && g_PhyDriveList[i].Id == Id)
        {
            return g_PhyDriveList + i;
        }
    }

    return NULL;
}

int SortPhysicalDrive(PHY_DRIVE_INFO *pDriveList, DWORD DriveCount)
{
	DWORD i, j;
	PHY_DRIVE_INFO TmpDriveInfo;

	for (i = 0; i < DriveCount; i++)
	{
		for (j = i + 1; j < DriveCount; j++)
		{
			if (pDriveList[i].BusType == BusTypeUsb && pDriveList[j].BusType == BusTypeUsb)
			{
				if (pDriveList[i].RemovableMedia == FALSE && pDriveList[j].RemovableMedia == TRUE)
				{
					memcpy(&TmpDriveInfo, pDriveList + i, sizeof(PHY_DRIVE_INFO));
					memcpy(pDriveList + i, pDriveList + j, sizeof(PHY_DRIVE_INFO));
					memcpy(pDriveList + j, &TmpDriveInfo, sizeof(PHY_DRIVE_INFO));
				}
			}
		}
	}

	return 0;
}

int Ventoy2DiskInit(void)
{
    g_PhyDriveList = (PHY_DRIVE_INFO *)malloc(sizeof(PHY_DRIVE_INFO)* VENTOY_MAX_PHY_DRIVE);
    if (NULL == g_PhyDriveList)
    {
        Log("Failed to alloc phy drive memory");
        return FALSE;
    }
    memset(g_PhyDriveList, 0, sizeof(PHY_DRIVE_INFO)* VENTOY_MAX_PHY_DRIVE);

    GetAllPhysicalDriveInfo(g_PhyDriveList, &g_PhyDriveCount);

	SortPhysicalDrive(g_PhyDriveList, g_PhyDriveCount);

    FilterPhysicalDrive(g_PhyDriveList, g_PhyDriveCount);

    return 0;
}

int Ventoy2DiskDestroy(void)
{
    free(g_PhyDriveList);
    return 0;
}
