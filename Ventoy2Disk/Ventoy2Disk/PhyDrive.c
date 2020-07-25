/******************************************************************************
 * PhyDrive.c
 *
 * Copyright (c) 2020, longpanda <admin@ventoy.net>
 * Copyright (c) 2011-2020, Pete Batard <pete@akeo.ie>
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
#include <winternl.h>
#include <commctrl.h>
#include <initguid.h>
#include <vds.h>
#include "resource.h"
#include "Language.h"
#include "Ventoy2Disk.h"
#include "fat_filelib.h"
#include "ff.h"

/* 
 * Some code and functions in the file are copied from rufus.
 * https://github.com/pbatard/rufus
 */
#define VDS_SET_ERROR SetLastError
#define IVdsServiceLoader_LoadService(This, pwszMachineName, ppService) (This)->lpVtbl->LoadService(This, pwszMachineName, ppService)
#define IVdsServiceLoader_Release(This) (This)->lpVtbl->Release(This)
#define IVdsService_QueryProviders(This, masks, ppEnum) (This)->lpVtbl->QueryProviders(This, masks, ppEnum)
#define IVdsService_WaitForServiceReady(This) ((This)->lpVtbl->WaitForServiceReady(This))
#define IVdsService_CleanupObsoleteMountPoints(This) ((This)->lpVtbl->CleanupObsoleteMountPoints(This))
#define IVdsService_Refresh(This) ((This)->lpVtbl->Refresh(This))
#define IVdsService_Reenumerate(This) ((This)->lpVtbl->Reenumerate(This)) 
#define IVdsSwProvider_QueryInterface(This, riid, ppvObject) (This)->lpVtbl->QueryInterface(This, riid, ppvObject)
#define IVdsProvider_Release(This) (This)->lpVtbl->Release(This)
#define IVdsSwProvider_QueryPacks(This, ppEnum) (This)->lpVtbl->QueryPacks(This, ppEnum)
#define IVdsSwProvider_Release(This) (This)->lpVtbl->Release(This)
#define IVdsPack_QueryDisks(This, ppEnum) (This)->lpVtbl->QueryDisks(This, ppEnum)
#define IVdsDisk_GetProperties(This, pDiskProperties) (This)->lpVtbl->GetProperties(This, pDiskProperties)
#define IVdsDisk_Release(This) (This)->lpVtbl->Release(This)
#define IVdsDisk_QueryInterface(This, riid, ppvObject) (This)->lpVtbl->QueryInterface(This, riid, ppvObject)
#define IVdsAdvancedDisk_QueryPartitions(This, ppPartitionPropArray, plNumberOfPartitions) (This)->lpVtbl->QueryPartitions(This, ppPartitionPropArray, plNumberOfPartitions)
#define IVdsAdvancedDisk_DeletePartition(This, ullOffset, bForce, bForceProtected) (This)->lpVtbl->DeletePartition(This, ullOffset, bForce, bForceProtected)
#define IVdsAdvancedDisk_Clean(This, bForce, bForceOEM, bFullClean, ppAsync) (This)->lpVtbl->Clean(This, bForce, bForceOEM, bFullClean, ppAsync)
#define IVdsAdvancedDisk_Release(This) (This)->lpVtbl->Release(This)
#define IEnumVdsObject_Next(This, celt, ppObjectArray, pcFetched) (This)->lpVtbl->Next(This, celt, ppObjectArray, pcFetched)
#define IVdsPack_QueryVolumes(This, ppEnum) (This)->lpVtbl->QueryVolumes(This, ppEnum)
#define IVdsVolume_QueryInterface(This, riid, ppvObject) (This)->lpVtbl->QueryInterface(This, riid, ppvObject)
#define IVdsVolume_Release(This) (This)->lpVtbl->Release(This)
#define IVdsVolumeMF3_QueryVolumeGuidPathnames(This, pwszPathArray, pulNumberOfPaths) (This)->lpVtbl->QueryVolumeGuidPathnames(This,pwszPathArray,pulNumberOfPaths)
#define IVdsVolumeMF3_FormatEx2(This, pwszFileSystemTypeName, usFileSystemRevision, ulDesiredUnitAllocationSize, pwszLabel, Options, ppAsync) (This)->lpVtbl->FormatEx2(This, pwszFileSystemTypeName, usFileSystemRevision, ulDesiredUnitAllocationSize, pwszLabel, Options, ppAsync)
#define IVdsVolumeMF3_Release(This) (This)->lpVtbl->Release(This)
#define IVdsVolume_GetProperties(This, pVolumeProperties) (This)->lpVtbl->GetProperties(This,pVolumeProperties)
#define IVdsAsync_Cancel(This) (This)->lpVtbl->Cancel(This)
#define IVdsAsync_QueryStatus(This,pHrResult,pulPercentCompleted) (This)->lpVtbl->QueryStatus(This,pHrResult,pulPercentCompleted)
#define IVdsAsync_Wait(This,pHrResult,pAsyncOut) (This)->lpVtbl->Wait(This,pHrResult,pAsyncOut)
#define IVdsAsync_Release(This) (This)->lpVtbl->Release(This)

#define IUnknown_QueryInterface(This, a, b) (This)->lpVtbl->QueryInterface(This,a,b)
#define IUnknown_Release(This) (This)->lpVtbl->Release(This)

/*
* Delete all the partitions from a disk, using VDS
* Mostly copied from https://social.msdn.microsoft.com/Forums/vstudio/en-US/b90482ae-4e44-4b08-8731-81915030b32a/createpartition-using-vds-interface-throw-error-enointerface-dcom?forum=vcgeneral
*/
BOOL DeletePartitions(DWORD DriveIndex, BOOL OnlyPart2)
{
    BOOL r = FALSE;
    HRESULT hr;
    ULONG ulFetched;
    wchar_t wPhysicalName[48];
    IVdsServiceLoader *pLoader;
    IVdsService *pService;
    IEnumVdsObject *pEnum;
    IUnknown *pUnk;

    swprintf_s(wPhysicalName, ARRAYSIZE(wPhysicalName), L"\\\\?\\PhysicalDrive%lu", DriveIndex);

    // Initialize COM
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_CONNECT,
        RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL);

    // Create a VDS Loader Instance
    hr = CoCreateInstance(&CLSID_VdsLoader, NULL, CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
        &IID_IVdsServiceLoader, (void **)&pLoader);
    if (hr != S_OK) {
        VDS_SET_ERROR(hr);
        Log("Could not create VDS Loader Instance: %u", LASTERR);
        goto out;
    }

    // Load the VDS Service
    hr = IVdsServiceLoader_LoadService(pLoader, L"", &pService);
    IVdsServiceLoader_Release(pLoader);
    if (hr != S_OK) {
        VDS_SET_ERROR(hr);
        Log("Could not load VDS Service: %u", LASTERR);
        goto out;
    }

    // Wait for the Service to become ready if needed
    hr = IVdsService_WaitForServiceReady(pService);
    if (hr != S_OK) {
        VDS_SET_ERROR(hr);
        Log("VDS Service is not ready: %u", LASTERR);
        goto out;
    }

    // Query the VDS Service Providers
    hr = IVdsService_QueryProviders(pService, VDS_QUERY_SOFTWARE_PROVIDERS, &pEnum);
    if (hr != S_OK) {
        VDS_SET_ERROR(hr);
        Log("Could not query VDS Service Providers: %u", LASTERR);
        goto out;
    }

    while (IEnumVdsObject_Next(pEnum, 1, &pUnk, &ulFetched) == S_OK) {
        IVdsProvider *pProvider;
        IVdsSwProvider *pSwProvider;
        IEnumVdsObject *pEnumPack;
        IUnknown *pPackUnk;

        // Get VDS Provider
        hr = IUnknown_QueryInterface(pUnk, &IID_IVdsProvider, (void **)&pProvider);
        IUnknown_Release(pUnk);
        if (hr != S_OK) {
            VDS_SET_ERROR(hr);
            Log("Could not get VDS Provider: %u", LASTERR);
            goto out;
        }

        // Get VDS Software Provider
        hr = IVdsSwProvider_QueryInterface(pProvider, &IID_IVdsSwProvider, (void **)&pSwProvider);
        IVdsProvider_Release(pProvider);
        if (hr != S_OK) {
            VDS_SET_ERROR(hr);
            Log("Could not get VDS Software Provider: %u", LASTERR);
            goto out;
        }

        // Get VDS Software Provider Packs
        hr = IVdsSwProvider_QueryPacks(pSwProvider, &pEnumPack);
        IVdsSwProvider_Release(pSwProvider);
        if (hr != S_OK) {
            VDS_SET_ERROR(hr);
            Log("Could not get VDS Software Provider Packs: %u", LASTERR);
            goto out;
        }

        // Enumerate Provider Packs
        while (IEnumVdsObject_Next(pEnumPack, 1, &pPackUnk, &ulFetched) == S_OK) {
            IVdsPack *pPack;
            IEnumVdsObject *pEnumDisk;
            IUnknown *pDiskUnk;

            hr = IUnknown_QueryInterface(pPackUnk, &IID_IVdsPack, (void **)&pPack);
            IUnknown_Release(pPackUnk);
            if (hr != S_OK) {
                VDS_SET_ERROR(hr);
                Log("Could not query VDS Software Provider Pack: %u", LASTERR);
                goto out;
            }

            // Use the pack interface to access the disks
            hr = IVdsPack_QueryDisks(pPack, &pEnumDisk);
            if (hr != S_OK) {
                VDS_SET_ERROR(hr);
                Log("Could not query VDS disks: %u", LASTERR);
                goto out;
            }

            // List disks
            while (IEnumVdsObject_Next(pEnumDisk, 1, &pDiskUnk, &ulFetched) == S_OK) {
                VDS_DISK_PROP diskprop;
                VDS_PARTITION_PROP* prop_array;
                LONG i, prop_array_size;
                IVdsDisk *pDisk;
                IVdsAdvancedDisk *pAdvancedDisk;

                // Get the disk interface.
                hr = IUnknown_QueryInterface(pDiskUnk, &IID_IVdsDisk, (void **)&pDisk);
                if (hr != S_OK) {
                    VDS_SET_ERROR(hr);
                    Log("Could not query VDS Disk Interface: %u", LASTERR);
                    goto out;
                }

                // Get the disk properties
                hr = IVdsDisk_GetProperties(pDisk, &diskprop);
                if (hr != S_OK) {
                    VDS_SET_ERROR(hr);
                    Log("Could not query VDS Disk Properties: %u", LASTERR);
                    goto out;
                }

                // Isolate the disk we want
                if (_wcsicmp(wPhysicalName, diskprop.pwszName) != 0) {
                    IVdsDisk_Release(pDisk);
                    continue;
                }

                // Instantiate the AdvanceDisk interface for our disk.
                hr = IVdsDisk_QueryInterface(pDisk, &IID_IVdsAdvancedDisk, (void **)&pAdvancedDisk);
                IVdsDisk_Release(pDisk);
                if (hr != S_OK) {
                    VDS_SET_ERROR(hr);
                    Log("Could not access VDS Advanced Disk interface: %u", LASTERR);
                    goto out;
                }

                // Query the partition data, so we can get the start offset, which we need for deletion
                hr = IVdsAdvancedDisk_QueryPartitions(pAdvancedDisk, &prop_array, &prop_array_size);
                if (hr == S_OK) {
                    Log("Deleting ALL partition(s) from disk '%S':", diskprop.pwszName);
                    // Now go through each partition
                    for (i = 0; i < prop_array_size; i++) {
                        
                        Log("* Partition %d (offset: %lld, size: %llu)", prop_array[i].ulPartitionNumber,
                            prop_array[i].ullOffset, (ULONGLONG)prop_array[i].ullSize);

                        if (OnlyPart2 && prop_array[i].ullOffset == 2048*512)
                        {
                            Log("Skip this partition...");
                            continue;
                        }


                        hr = IVdsAdvancedDisk_DeletePartition(pAdvancedDisk, prop_array[i].ullOffset, TRUE, TRUE);
                        if (hr != S_OK) {
                            r = FALSE;
                            VDS_SET_ERROR(hr);
                            Log("Could not delete partitions: %u", LASTERR);
                        }
                    }
                    r = TRUE;
                }
                else {
                    Log("No partition to delete on disk '%S'", diskprop.pwszName);
                    r = TRUE;
                }
                CoTaskMemFree(prop_array);

#if 0
                // Issue a Clean while we're at it
                HRESULT hr2 = E_FAIL;
                ULONG completed;
                IVdsAsync* pAsync;
                hr = IVdsAdvancedDisk_Clean(pAdvancedDisk, TRUE, FALSE, FALSE, &pAsync);
                while (SUCCEEDED(hr)) {
                    if (IS_ERROR(FormatStatus)) {
                        IVdsAsync_Cancel(pAsync);
                        break;
                    }
                    hr = IVdsAsync_QueryStatus(pAsync, &hr2, &completed);
                    if (SUCCEEDED(hr)) {
                        hr = hr2;
                        if (hr == S_OK)
                            break;
                        if (hr == VDS_E_OPERATION_PENDING)
                            hr = S_OK;
                    }
                    Sleep(500);
                }
                if (hr != S_OK) {
                    VDS_SET_ERROR(hr);
                    Log("Could not clean disk: %s", LASTERR);
                }
#endif
                IVdsAdvancedDisk_Release(pAdvancedDisk);
                goto out;
            }
        }
    }

out:
    return r;
}


static DWORD GetVentoyVolumeName(int PhyDrive, UINT32 StartSectorId, CHAR *NameBuf, UINT32 BufLen, BOOL DelSlash)
{
    size_t len;
    BOOL bRet;
    DWORD dwSize;
    HANDLE hDrive;
    HANDLE hVolume;
    UINT64 PartOffset;
    DWORD Status = ERROR_NOT_FOUND;
    DISK_EXTENT *pExtents = NULL;
    CHAR VolumeName[MAX_PATH] = { 0 };
    VOLUME_DISK_EXTENTS DiskExtents;

    PartOffset = 512ULL * StartSectorId;

	Log("GetVentoyVolumeName PhyDrive %d SectorStart:%u PartOffset:%llu", PhyDrive, StartSectorId, (ULONGLONG)PartOffset);

    hVolume = FindFirstVolumeA(VolumeName, sizeof(VolumeName));
    if (hVolume == INVALID_HANDLE_VALUE)
    {
        return 1;
    }

    do {

        len = strlen(VolumeName);
        Log("Find volume:%s", VolumeName);

        VolumeName[len - 1] = 0;

        hDrive = CreateFileA(VolumeName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hDrive == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        bRet = DeviceIoControl(hDrive,
            IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
            NULL,
            0,
            &DiskExtents,
            (DWORD)(sizeof(DiskExtents)),
            (LPDWORD)&dwSize,
            NULL);

        Log("IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS bRet:%u code:%u", bRet, LASTERR);
        Log("NumberOfDiskExtents:%u DiskNumber:%u", DiskExtents.NumberOfDiskExtents, DiskExtents.Extents[0].DiskNumber);

        if (bRet && DiskExtents.NumberOfDiskExtents == 1)
        {
            pExtents = DiskExtents.Extents;

            Log("This volume DiskNumber:%u offset:%llu", pExtents->DiskNumber, (ULONGLONG)pExtents->StartingOffset.QuadPart);
            if ((int)pExtents->DiskNumber == PhyDrive && pExtents->StartingOffset.QuadPart == PartOffset)
            {
                Log("This volume match");

                if (!DelSlash)
                {
                    VolumeName[len - 1] = '\\';
                }

                sprintf_s(NameBuf, BufLen, "%s", VolumeName);
                Status = ERROR_SUCCESS;
                CloseHandle(hDrive);
                break;
            }
        }

        CloseHandle(hDrive);
    } while (FindNextVolumeA(hVolume, VolumeName, sizeof(VolumeName)));

    FindVolumeClose(hVolume);

    Log("GetVentoyVolumeName return %u", Status);
    return Status;
}

static int GetLettersBelongPhyDrive(int PhyDrive, char *DriveLetters, size_t Length)
{
    int n = 0;
    DWORD DataSize = 0;
    CHAR *Pos = NULL;
    CHAR *StringBuf = NULL;

    DataSize = GetLogicalDriveStringsA(0, NULL);
    StringBuf = (CHAR *)malloc(DataSize + 1);
    if (StringBuf == NULL)
    {
        return 1;
    }

    GetLogicalDriveStringsA(DataSize, StringBuf);

    for (Pos = StringBuf; *Pos; Pos += strlen(Pos) + 1)
    {
        if (n < (int)Length && PhyDrive == GetPhyDriveByLogicalDrive(Pos[0]))
        {
            Log("%C: is belong to phydrive%d", Pos[0], PhyDrive);
            DriveLetters[n++] = Pos[0];
        }
    }

    free(StringBuf);
    return 0;
}

static HANDLE GetPhysicalHandle(int Drive, BOOLEAN bLockDrive, BOOLEAN bWriteAccess, BOOLEAN bWriteShare)
{
    int i;
    DWORD dwSize;
    DWORD LastError;
    UINT64 EndTime;
    HANDLE hDrive = INVALID_HANDLE_VALUE;
    CHAR PhyDrive[128];
    CHAR DevPath[MAX_PATH] = { 0 };

    safe_sprintf(PhyDrive, "\\\\.\\PhysicalDrive%d", Drive);

    if (0 == QueryDosDeviceA(PhyDrive + 4, DevPath, sizeof(DevPath)))
    {
        Log("QueryDosDeviceA failed error:%u", GetLastError());
        strcpy_s(DevPath, sizeof(DevPath), "???");
    }
    else
    {
        Log("QueryDosDeviceA success %s", DevPath);
    }

    for (i = 0; i < DRIVE_ACCESS_RETRIES; i++)
    {
        // Try without FILE_SHARE_WRITE (unless specifically requested) so that
        // we won't be bothered by the OS or other apps when we set up our data.
        // However this means we might have to wait for an access gap...
        // We keep FILE_SHARE_READ though, as this shouldn't hurt us any, and is
        // required for enumeration.
        hDrive = CreateFileA(PhyDrive,
            GENERIC_READ | (bWriteAccess ? GENERIC_WRITE : 0),
            FILE_SHARE_READ | (bWriteShare ? FILE_SHARE_WRITE : 0),
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
            NULL);

        LastError = GetLastError();
        Log("[%d] CreateFileA %s code:%u %p", i, PhyDrive, LastError, hDrive);

        if (hDrive != INVALID_HANDLE_VALUE)
        {
            break;
        }

        if ((LastError != ERROR_SHARING_VIOLATION) && (LastError != ERROR_ACCESS_DENIED))
        {
            break;
        }

        if (i == 0)
        {
            Log("Waiting for access on %s [%s]...", PhyDrive, DevPath);
        }
        else if (!bWriteShare && (i > DRIVE_ACCESS_RETRIES / 3))
        {
            // If we can't seem to get a hold of the drive for some time, try to enable FILE_SHARE_WRITE...
            Log("Warning: Could not obtain exclusive rights. Retrying with write sharing enabled...");
            bWriteShare = TRUE;

            // Try to report the process that is locking the drive
            // We also use bit 6 as a flag to indicate that SearchProcess was called.
            //access_mask = SearchProcess(DevPath, SEARCH_PROCESS_TIMEOUT, TRUE, TRUE, FALSE) | 0x40;

        }
        Sleep(DRIVE_ACCESS_TIMEOUT / DRIVE_ACCESS_RETRIES);
    }

    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Could not open %s %u", PhyDrive, LASTERR);
        goto End;
    }

    if (bWriteAccess)
    {
        Log("Opened %s for %s write access", PhyDrive, bWriteShare ? "shared" : "exclusive");
    }

    if (bLockDrive)
    {
        if (DeviceIoControl(hDrive, FSCTL_ALLOW_EXTENDED_DASD_IO, NULL, 0, NULL, 0, &dwSize, NULL))
        {
            Log("I/O boundary checks disabled");
        }

        EndTime = GetTickCount64() + DRIVE_ACCESS_TIMEOUT;

        do {
            if (DeviceIoControl(hDrive, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL))
            {
                Log("FSCTL_LOCK_VOLUME success");
                goto End;
            }
            Sleep(DRIVE_ACCESS_TIMEOUT / DRIVE_ACCESS_RETRIES);
        } while (GetTickCount64() < EndTime);

        // If we reached this section, either we didn't manage to get a lock or the user cancelled
        Log("Could not lock access to %s %u", PhyDrive, LASTERR);

        // See if we can report the processes are accessing the drive
        //if (!IS_ERROR(FormatStatus) && (access_mask == 0))
        //    access_mask = SearchProcess(DevPath, SEARCH_PROCESS_TIMEOUT, TRUE, TRUE, FALSE);
        // Try to continue if the only access rights we saw were for read-only
        //if ((access_mask & 0x07) != 0x01)
        //    safe_closehandle(hDrive);

        CHECK_CLOSE_HANDLE(hDrive);
    }

End:

    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Can get handle of %s, maybe some process control it.", DevPath);
    }

    return hDrive;
}

int GetPhyDriveByLogicalDrive(int DriveLetter)
{
    BOOL Ret;
    DWORD dwSize;
    HANDLE Handle;
    VOLUME_DISK_EXTENTS DiskExtents;
    CHAR PhyPath[128];

    safe_sprintf(PhyPath, "\\\\.\\%C:", (CHAR)DriveLetter);

    Handle = CreateFileA(PhyPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        Log("Could not open the disk<%s>, error:%u", PhyPath, LASTERR);
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
        Log("DeviceIoControl IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed %s, error:%u", PhyPath, LASTERR);
        CHECK_CLOSE_HANDLE(Handle);
        return -1;
    }
    CHECK_CLOSE_HANDLE(Handle);

    Log("LogicalDrive:%s PhyDrive:%d Offset:%llu ExtentLength:%llu",
        PhyPath,
        DiskExtents.Extents[0].DiskNumber,
        DiskExtents.Extents[0].StartingOffset.QuadPart,
        DiskExtents.Extents[0].ExtentLength.QuadPart
        );

    return (int)DiskExtents.Extents[0].DiskNumber;
}

int GetAllPhysicalDriveInfo(PHY_DRIVE_INFO *pDriveList, DWORD *pDriveCount)
{
    int i;
    int Count;
    int id;
    int Letter = 'A';
    BOOL  bRet;
    DWORD dwBytes;
    DWORD DriveCount = 0;
    HANDLE Handle = INVALID_HANDLE_VALUE;
    CHAR PhyDrive[128];
    PHY_DRIVE_INFO *CurDrive = pDriveList;
    GET_LENGTH_INFORMATION LengthInfo;
    STORAGE_PROPERTY_QUERY Query;
    STORAGE_DESCRIPTOR_HEADER DevDescHeader;
    STORAGE_DEVICE_DESCRIPTOR *pDevDesc;
    int PhyDriveId[VENTOY_MAX_PHY_DRIVE];

    Count = GetPhysicalDriveCount();

    for (i = 0; i < Count && i < VENTOY_MAX_PHY_DRIVE; i++)
    {
        PhyDriveId[i] = i;
    }

    dwBytes = GetLogicalDrives();
    Log("Logical Drives: 0x%x", dwBytes);
    while (dwBytes)
    {
        if (dwBytes & 0x01)
        {
            id = GetPhyDriveByLogicalDrive(Letter);
            Log("%C --> %d", Letter, id);
            if (id >= 0)
            {
                for (i = 0; i < Count; i++)
                {
                    if (PhyDriveId[i] == id)
                    {
                        break;
                    }
                }

                if (i >= Count)
                {
                    Log("Add phy%d to list", i);
                    PhyDriveId[Count] = id;
                    Count++;
                }
            }
        }

        Letter++;
        dwBytes >>= 1;
    }

    for (i = 0; i < Count && DriveCount < VENTOY_MAX_PHY_DRIVE; i++)
    {
        CHECK_CLOSE_HANDLE(Handle);

        safe_sprintf(PhyDrive, "\\\\.\\PhysicalDrive%d", PhyDriveId[i]);
        Handle = CreateFileA(PhyDrive, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);        
        Log("Create file Handle:%p %s status:%u", Handle, PhyDrive, LASTERR);

        if (Handle == INVALID_HANDLE_VALUE)
        {
            continue;
        }

        bRet = DeviceIoControl(Handle,
                               IOCTL_DISK_GET_LENGTH_INFO, NULL,
                               0,
                               &LengthInfo,
                               sizeof(LengthInfo),
                               &dwBytes,
                               NULL);
        if (!bRet)
        {
            Log("DeviceIoControl IOCTL_DISK_GET_LENGTH_INFO failed error:%u", LASTERR);
            continue;
        }

        Log("PHYSICALDRIVE%d size %llu bytes", i, (ULONGLONG)LengthInfo.Length.QuadPart);

        Query.PropertyId = StorageDeviceProperty;
        Query.QueryType = PropertyStandardQuery;

        bRet = DeviceIoControl(Handle,
                               IOCTL_STORAGE_QUERY_PROPERTY,
                               &Query,
                               sizeof(Query),
                               &DevDescHeader,
                               sizeof(STORAGE_DESCRIPTOR_HEADER),
                               &dwBytes,
                               NULL);
        if (!bRet)
        {
            Log("DeviceIoControl1 error:%u dwBytes:%u", LASTERR, dwBytes);
            continue;
        }

        if (DevDescHeader.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR))
        {
            Log("Invalid DevDescHeader.Size:%u", DevDescHeader.Size);
            continue;
        }

        pDevDesc = (STORAGE_DEVICE_DESCRIPTOR *)malloc(DevDescHeader.Size);
        if (!pDevDesc)
        {
            Log("failed to malloc error:%u len:%u", LASTERR, DevDescHeader.Size);
            continue;
        }

        bRet = DeviceIoControl(Handle,
                               IOCTL_STORAGE_QUERY_PROPERTY,
                               &Query,
                               sizeof(Query),
                               pDevDesc,
                               DevDescHeader.Size,
                               &dwBytes,
                               NULL);
        if (!bRet)
        {
            Log("DeviceIoControl2 error:%u dwBytes:%u", LASTERR, dwBytes);
            free(pDevDesc);
            continue;
        }

        CurDrive->PhyDrive = i;
        CurDrive->SizeInBytes = LengthInfo.Length.QuadPart;
        CurDrive->DeviceType = pDevDesc->DeviceType;
        CurDrive->RemovableMedia = pDevDesc->RemovableMedia;
        CurDrive->BusType = pDevDesc->BusType;

        if (pDevDesc->VendorIdOffset)
        {
            safe_strcpy(CurDrive->VendorId, (char *)pDevDesc + pDevDesc->VendorIdOffset);
            TrimString(CurDrive->VendorId);
        }

        if (pDevDesc->ProductIdOffset)
        {
            safe_strcpy(CurDrive->ProductId, (char *)pDevDesc + pDevDesc->ProductIdOffset);
            TrimString(CurDrive->ProductId);
        }

        if (pDevDesc->ProductRevisionOffset)
        {
            safe_strcpy(CurDrive->ProductRev, (char *)pDevDesc + pDevDesc->ProductRevisionOffset);
            TrimString(CurDrive->ProductRev);
        }

        if (pDevDesc->SerialNumberOffset)
        {
            safe_strcpy(CurDrive->SerialNumber, (char *)pDevDesc + pDevDesc->SerialNumberOffset);
            TrimString(CurDrive->SerialNumber);
        }

        CurDrive++;
        DriveCount++;

        free(pDevDesc);

        CHECK_CLOSE_HANDLE(Handle);
    }

    for (i = 0, CurDrive = pDriveList; i < (int)DriveCount; i++, CurDrive++)
    {
        Log("PhyDrv:%d BusType:%-4s Removable:%u Size:%dGB(%llu) Name:%s %s",
            CurDrive->PhyDrive, GetBusTypeString(CurDrive->BusType), CurDrive->RemovableMedia,
            GetHumanReadableGBSize(CurDrive->SizeInBytes), CurDrive->SizeInBytes,
            CurDrive->VendorId, CurDrive->ProductId);
    }

    *pDriveCount = DriveCount;

    return 0;
}


static HANDLE g_FatPhyDrive;
static UINT64 g_Part2StartSec;
static int GetVentoyVersionFromFatFile(CHAR *VerBuf, size_t BufLen)
{
    int rc = 1;
    int size = 0;
    char *buf = NULL;
    void *flfile = NULL;

    flfile = fl_fopen("/grub/grub.cfg", "rb");
    if (flfile)
    {
        fl_fseek(flfile, 0, SEEK_END);
        size = (int)fl_ftell(flfile);

        fl_fseek(flfile, 0, SEEK_SET);

        buf = (char *)malloc(size + 1);
        if (buf)
        {
            fl_fread(buf, 1, size, flfile);
            buf[size] = 0;

            rc = 0;
            sprintf_s(VerBuf, BufLen, "%s", ParseVentoyVersionFromString(buf));
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
        Log("ReadFile error bRet:%u WriteSize:%u dwSize:%u ErrCode:%u\n", bRet, ReadSize, dwSize, LASTERR);
    }

    return 1;
}


int GetVentoyVerInPhyDrive(const PHY_DRIVE_INFO *pDriveInfo, UINT64 Part2StartSector, CHAR *VerBuf, size_t BufLen)
{
    int rc = 0;
    HANDLE hDrive;

    hDrive = GetPhysicalHandle(pDriveInfo->PhyDrive, FALSE, FALSE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        return 1;
    }
    
    g_FatPhyDrive = hDrive;
	g_Part2StartSec = Part2StartSector;

    Log("Parse FAT fs...");

    fl_init();

    if (0 == fl_attach_media(VentoyFatDiskRead, NULL))
    {
        Log("attach media success...");
        rc = GetVentoyVersionFromFatFile(VerBuf, BufLen);
    }
    else
    {
        Log("attach media failed...");
        rc = 1;
    }

    Log("GetVentoyVerInPhyDrive rc=%d...", rc);
    if (rc == 0)
    {
        Log("VentoyVerInPhyDrive %d is <%s>...", pDriveInfo->PhyDrive, VerBuf);
    }

    fl_shutdown();

    CHECK_CLOSE_HANDLE(hDrive);

    return rc;
}





static unsigned int g_disk_unxz_len = 0;
static BYTE *g_part_img_pos = NULL;
static BYTE *g_part_img_buf[VENTOY_EFI_PART_SIZE / SIZE_1MB];


static int VentoyFatMemRead(uint32 Sector, uint8 *Buffer, uint32 SectorCount)
{
	uint32 i;
	uint32 offset;
	BYTE *MbBuf = NULL;

	for (i = 0; i < SectorCount; i++)
	{
		offset = (Sector + i) * 512;

		if (g_part_img_buf[1] == NULL)
		{
			MbBuf = g_part_img_buf[0] + offset;
			memcpy(Buffer + i * 512, MbBuf, 512);
		}
		else
		{
			MbBuf = g_part_img_buf[offset / SIZE_1MB];
			memcpy(Buffer + i * 512, MbBuf + (offset % SIZE_1MB), 512);
		}
	}

	return 1;
}


static int VentoyFatMemWrite(uint32 Sector, uint8 *Buffer, uint32 SectorCount)
{
	uint32 i;
	uint32 offset;
	BYTE *MbBuf = NULL;

	for (i = 0; i < SectorCount; i++)
	{
		offset = (Sector + i) * 512;

		if (g_part_img_buf[1] == NULL)
		{
			MbBuf = g_part_img_buf[0] + offset;
			memcpy(MbBuf, Buffer + i * 512, 512);
		}
		else
		{
			MbBuf = g_part_img_buf[offset / SIZE_1MB];
			memcpy(MbBuf + (offset % SIZE_1MB), Buffer + i * 512, 512);
		}
	}

	return 1;
}

int VentoyProcSecureBoot(BOOL SecureBoot)
{
	int rc = 0;
	int size;
	char *filebuf = NULL;
	void *file = NULL;

	Log("VentoyProcSecureBoot %d ...", SecureBoot);
	
	if (SecureBoot)
	{
		Log("Secure boot is enabled ...");
		return 0;
	}

	fl_init();

	if (0 == fl_attach_media(VentoyFatMemRead, VentoyFatMemWrite))
	{
		file = fl_fopen("/EFI/BOOT/grubx64_real.efi", "rb");
		Log("Open ventoy efi file %p ", file);
		if (file)
		{
			fl_fseek(file, 0, SEEK_END);
			size = (int)fl_ftell(file);
			fl_fseek(file, 0, SEEK_SET);

			Log("ventoy efi file size %d ...", size);

			filebuf = (char *)malloc(size);
			if (filebuf)
			{
				fl_fread(filebuf, 1, size, file);
			}

			fl_fclose(file);

			Log("Now delete all efi files ...");
			fl_remove("/EFI/BOOT/BOOTX64.EFI");
			fl_remove("/EFI/BOOT/grubx64.efi");
			fl_remove("/EFI/BOOT/grubx64_real.efi");
			fl_remove("/EFI/BOOT/MokManager.efi");
            fl_remove("/ENROLL_THIS_KEY_IN_MOKMANAGER.cer");

			file = fl_fopen("/EFI/BOOT/BOOTX64.EFI", "wb");
			Log("Open bootx64 efi file %p ", file);
			if (file)
			{
				if (filebuf)
				{
					fl_fwrite(filebuf, 1, size, file);
				}
				
				fl_fflush(file);
				fl_fclose(file);
			}

			if (filebuf)
			{
				free(filebuf);
			}
		}
	}
	else
	{
		rc = 1;
	}

	fl_shutdown();

	return rc;
}



static int disk_xz_flush(void *src, unsigned int size)
{
    unsigned int i;
    BYTE *buf = (BYTE *)src;

    for (i = 0; i < size; i++)
    {
        *g_part_img_pos = *buf++;

        g_disk_unxz_len++;
        if ((g_disk_unxz_len % SIZE_1MB) == 0)
        {
            g_part_img_pos = g_part_img_buf[g_disk_unxz_len / SIZE_1MB];
        }
        else
        {
            g_part_img_pos++;
        }
    }

    return (int)size;
}

static void unxz_error(char *x)
{
    Log("%s", x);
}

static BOOL TryWritePart2(HANDLE hDrive, UINT64 StartSectorId)
{
    BOOL bRet;
    DWORD TrySize = 16 * 1024;
    DWORD dwSize;
    BYTE *Buffer = NULL;
    unsigned char *data = NULL;
    LARGE_INTEGER liCurrentPosition;

    liCurrentPosition.QuadPart = StartSectorId * 512;
    SetFilePointerEx(hDrive, liCurrentPosition, &liCurrentPosition, FILE_BEGIN);
    
    Buffer = malloc(TrySize);

    bRet = WriteFile(hDrive, Buffer, TrySize, &dwSize, NULL);

    free(Buffer);

    Log("Try write part2 bRet:%u dwSize:%u code:%u", bRet, dwSize, LASTERR);

    if (bRet && dwSize == TrySize)
    {
        return TRUE;
    }

    return FALSE;
}

static int FormatPart2Fat(HANDLE hDrive, UINT64 StartSectorId)
{
    int i;
    int rc = 0;
    int len = 0;
    int writelen = 0;
    int partwrite = 0;
    DWORD dwSize = 0;
    BOOL bRet;
    unsigned char *data = NULL;
    LARGE_INTEGER liCurrentPosition;
	LARGE_INTEGER liNewPosition;

	Log("FormatPart2Fat %llu...", StartSectorId);

    rc = ReadWholeFileToBuf(VENTOY_FILE_DISK_IMG, 0, (void **)&data, &len);
    if (rc)
    {
        Log("Failed to read img file %p %u", data, len);
        return 1;
    }

    liCurrentPosition.QuadPart = StartSectorId * 512;
	SetFilePointerEx(hDrive, liCurrentPosition, &liNewPosition, FILE_BEGIN);

	Log("Set file pointer: %llu  New pointer:%llu", liCurrentPosition.QuadPart, liNewPosition.QuadPart);

    memset(g_part_img_buf, 0, sizeof(g_part_img_buf));

    g_part_img_buf[0] = (BYTE *)malloc(VENTOY_EFI_PART_SIZE);
    if (g_part_img_buf[0])
    {
        Log("Malloc whole img buffer success, now decompress ...");
        unxz(data, len, NULL, NULL, g_part_img_buf[0], &writelen, unxz_error);

        if (len == writelen)
        {
            Log("decompress finished success");

			VentoyProcSecureBoot(g_SecureBoot);

            for (i = 0; i < VENTOY_EFI_PART_SIZE / SIZE_1MB; i++)
            {
                dwSize = 0;
				bRet = WriteFile(hDrive, g_part_img_buf[0] + i * SIZE_1MB, SIZE_1MB, &dwSize, NULL);
                Log("Write part data bRet:%u dwSize:%u code:%u", bRet, dwSize, LASTERR);

                if (!bRet)
                {
                    rc = 1;
                    goto End;
                }

                PROGRESS_BAR_SET_POS(PT_WRITE_VENTOY_START + i);                
            }
        }
        else
        {
            rc = 1;
            Log("decompress finished failed");
            goto End;
        }
    }
    else
    {
        Log("Failed to malloc whole img size %u, now split it", VENTOY_EFI_PART_SIZE);

        partwrite = 1;
        for (i = 0; i < VENTOY_EFI_PART_SIZE / SIZE_1MB; i++)
        {
            g_part_img_buf[i] = (BYTE *)malloc(SIZE_1MB);
            if (g_part_img_buf[i] == NULL)
            {
                rc = 1;
                goto End;
            }
        }

        Log("Malloc part img buffer success, now decompress ...");

        g_part_img_pos = g_part_img_buf[0];

        unxz(data, len, NULL, disk_xz_flush, NULL, NULL, unxz_error);

        if (g_disk_unxz_len == VENTOY_EFI_PART_SIZE)
        {
            Log("decompress finished success");
			
			VentoyProcSecureBoot(g_SecureBoot);

            for (int i = 0; i < VENTOY_EFI_PART_SIZE / SIZE_1MB; i++)
            {
                dwSize = 0;
                bRet = WriteFile(hDrive, g_part_img_buf[i], SIZE_1MB, &dwSize, NULL);
                Log("Write part data bRet:%u dwSize:%u code:%u", bRet, dwSize, LASTERR);

                if (!bRet)
                {
                    rc = 1;
                    goto End;
                }

                PROGRESS_BAR_SET_POS(PT_WRITE_VENTOY_START + i);
            }
        }
        else
        {
            rc = 1;
            Log("decompress finished failed");
            goto End;
        }
    }

End:

    if (data) free(data);

    if (partwrite)
    {
        for (i = 0; i < VENTOY_EFI_PART_SIZE / SIZE_1MB; i++)
        {
            if (g_part_img_buf[i]) free(g_part_img_buf[i]);
        }
    }
    else
    {
        if (g_part_img_buf[0]) free(g_part_img_buf[0]);
    }

    return rc;
}

static int WriteGrubStage1ToPhyDrive(HANDLE hDrive, int PartStyle)
{
    int Len = 0;
    int readLen = 0;
    BOOL bRet;
    DWORD dwSize;
    BYTE *ImgBuf = NULL;
    BYTE *RawBuf = NULL;

    Log("WriteGrubStage1ToPhyDrive ...");

    RawBuf = (BYTE *)malloc(SIZE_1MB);
    if (!RawBuf)
    {
        return 1;
    }

    if (ReadWholeFileToBuf(VENTOY_FILE_STG1_IMG, 0, (void **)&ImgBuf, &Len))
    {
        Log("Failed to read stage1 img");
        free(RawBuf);
        return 1;
    }

    unxz(ImgBuf, Len, NULL, NULL, RawBuf, &readLen, unxz_error);

    if (PartStyle)
    {
        Log("Write GPT stage1 ...");
        RawBuf[500] = 35;//update blocklist
        SetFilePointer(hDrive, 512 * 34, NULL, FILE_BEGIN);        
        bRet = WriteFile(hDrive, RawBuf, SIZE_1MB - 512 * 34, &dwSize, NULL);
    }
    else
    {
        Log("Write MBR stage1 ...");
        SetFilePointer(hDrive, 512, NULL, FILE_BEGIN);
        bRet = WriteFile(hDrive, RawBuf, SIZE_1MB - 512, &dwSize, NULL);
    }

    Log("WriteFile Ret:%u dwSize:%u ErrCode:%u", bRet, dwSize, GetLastError());

    free(RawBuf);
    free(ImgBuf);
    return 0;
}



static int FormatPart1exFAT(UINT64 DiskSizeBytes)
{
    MKFS_PARM Option;
    FRESULT Ret;
    FATFS fs;

    Option.fmt = FM_EXFAT;
    Option.n_fat = 1;
    Option.align = 8;
    Option.n_root = 1;

    // < 32GB select 32KB as cluster size
    // > 32GB select 128KB as cluster size
    if (DiskSizeBytes / 1024 / 1024 / 1024 <= 32)
    {
        Option.au_size = 32768;
    }
    else
    {
        Option.au_size = 131072;
    }

    Log("Formatting Part1 exFAT ...");

    Ret = f_mkfs(TEXT("0:"), &Option, 0, 8 * 1024 * 1024);

    if (FR_OK == Ret)
    {
        Log("Formatting Part1 exFAT success");

        Ret = f_mount(&fs, TEXT("0:"), 1);
        Log("mount part %d", Ret);

        if (FR_OK == Ret)
        {
            Ret = f_setlabel(TEXT("Ventoy"));
            Log("f_setlabel %d", Ret);

            Ret = f_mount(0, TEXT("0:"), 1);
            Log("umount part %d", Ret);
            return 0;
        }
        else
        {
            Log("mount exfat failed %d", Ret);
            return 1;
        }
    }
    else
    {
        Log("Formatting Part1 exFAT failed");
        return 1;
    }
}



int ClearVentoyFromPhyDrive(HWND hWnd, PHY_DRIVE_INFO *pPhyDrive, char *pDrvLetter)
{
    int i;
    int rc = 0;
    int state = 0;
    HANDLE hDrive;
    DWORD dwSize;
    BOOL bRet;
    CHAR MountDrive;
    CHAR DriveName[] = "?:\\";
    CHAR DriveLetters[MAX_PATH] = { 0 };
    LARGE_INTEGER liCurrentPosition;
    char *pTmpBuf = NULL;
    MBR_HEAD MBR;

    *pDrvLetter = 0;

    Log("ClearVentoyFromPhyDrive PhyDrive%d <<%s %s %dGB>>",
        pPhyDrive->PhyDrive, pPhyDrive->VendorId, pPhyDrive->ProductId,
        GetHumanReadableGBSize(pPhyDrive->SizeInBytes));

    PROGRESS_BAR_SET_POS(PT_LOCK_FOR_CLEAN);

    Log("Lock disk for clean ............................. ");

    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, FALSE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Failed to open physical disk");
        return 1;
    }

    GetLettersBelongPhyDrive(pPhyDrive->PhyDrive, DriveLetters, sizeof(DriveLetters));

    if (DriveLetters[0] == 0)
    {
        Log("No drive letter was assigned...");
        DriveName[0] = GetFirstUnusedDriveLetter();
        Log("GetFirstUnusedDriveLetter %C: ...", DriveName[0]);
    }
    else
    {
        // Unmount all mounted volumes that belong to this drive
        // Do it in reverse so that we always end on the first volume letter
        for (i = (int)strlen(DriveLetters); i > 0; i--)
        {
            DriveName[0] = DriveLetters[i - 1];
            bRet = DeleteVolumeMountPointA(DriveName);
            Log("Delete mountpoint %s ret:%u code:%u", DriveName, bRet, GetLastError());
        }
    }

    MountDrive = DriveName[0];
    Log("Will use '%C:' as volume mountpoint", DriveName[0]);

    // It kind of blows, but we have to relinquish access to the physical drive
    // for VDS to be able to delete the partitions that reside on it...
    DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL);
    CHECK_CLOSE_HANDLE(hDrive);

    PROGRESS_BAR_SET_POS(PT_DEL_ALL_PART);

    if (!DeletePartitions(pPhyDrive->PhyDrive, FALSE))
    {
        Log("Notice: Could not delete partitions: %u", GetLastError());
    }

    Log("Deleting all partitions ......................... OK");

    PROGRESS_BAR_SET_POS(PT_LOCK_FOR_WRITE);

    Log("Lock disk for write ............................. ");
    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, TRUE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Failed to GetPhysicalHandle for write.");
        rc = 1;
        goto End;
    }

    // clear first and last 1MB space
    pTmpBuf = malloc(SIZE_1MB);
    if (!pTmpBuf)
    {
        Log("Failed to alloc memory.");
        rc = 1;
        goto End;
    }
    memset(pTmpBuf, 0, SIZE_1MB);   

    SET_FILE_POS(512);
    bRet = WriteFile(hDrive, pTmpBuf, SIZE_1MB - 512, &dwSize, NULL);
    Log("Write fisrt 1MB ret:%d size:%u err:%d", bRet, dwSize, LASTERR);
    if (!bRet)
    {
        rc = 1;
        goto End;
    }

    SET_FILE_POS(SIZE_1MB);
    bRet = WriteFile(hDrive, pTmpBuf, SIZE_1MB, &dwSize, NULL);
    Log("Write 2nd 1MB ret:%d size:%u err:%d", bRet, dwSize, LASTERR);
    if (!bRet)
    {
        rc = 1;
        goto End;
    }

    SET_FILE_POS(0);
    bRet = ReadFile(hDrive, &MBR, sizeof(MBR), &dwSize, NULL);
    Log("Read MBR ret:%d size:%u err:%d", bRet, dwSize, LASTERR);
    if (!bRet)
    {
        rc = 1;
        goto End;
    }

    //clear boot code and partition table (reserved disk signature)
    memset(MBR.BootCode, 0, 440);
    memset(MBR.PartTbl, 0, sizeof(MBR.PartTbl));

    VentoyFillLocation(pPhyDrive->SizeInBytes, 2048, (UINT32)(pPhyDrive->SizeInBytes / 512 - 2048), MBR.PartTbl);

    MBR.PartTbl[0].Active = 0x00; // bootable
    MBR.PartTbl[0].FsFlag = 0x07; // exFAT/NTFS/HPFS

    SET_FILE_POS(0);
    bRet = WriteFile(hDrive, &MBR, 512, &dwSize, NULL);
    Log("Write MBR ret:%d size:%u err:%d", bRet, dwSize, LASTERR);
    if (!bRet)
    {
        rc = 1;
        goto End;
    }

    Log("Clear Ventoy successfully finished");

	//Refresh Drive Layout
	DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dwSize, NULL);

End:
    
    PROGRESS_BAR_SET_POS(PT_MOUNT_VOLUME);
    
    if (pTmpBuf)
    {
        free(pTmpBuf);
    }

    if (rc == 0)
    {
        Log("Mounting Ventoy Partition ....................... ");
        Sleep(1000);

        state = 0;
        memset(DriveLetters, 0, sizeof(DriveLetters));
        GetLettersBelongPhyDrive(pPhyDrive->PhyDrive, DriveLetters, sizeof(DriveLetters));
        Log("Logical drive letter after write ventoy: <%s>", DriveLetters);

        for (i = 0; i < sizeof(DriveLetters) && DriveLetters[i]; i++)
        {
            DriveName[0] = DriveLetters[i];
            Log("%s is ventoy part1, already mounted", DriveName);
            state = 1;
        }

        if (state != 1)
        {
            Log("need to mount ventoy part1...");
            if (0 == GetVentoyVolumeName(pPhyDrive->PhyDrive, MBR.PartTbl[0].StartSectorId, DriveLetters, sizeof(DriveLetters), FALSE))
            {
                DriveName[0] = MountDrive;
                bRet = SetVolumeMountPointA(DriveName, DriveLetters);
                Log("SetVolumeMountPoint <%s> <%s> bRet:%u code:%u", DriveName, DriveLetters, bRet, GetLastError());

                *pDrvLetter = MountDrive;
            }
            else
            {
                Log("Failed to find ventoy volume");
            }
        }

        Log("OK\n");
    }
    else
    {
        FindProcessOccupyDisk(hDrive, pPhyDrive);
    }

    CHECK_CLOSE_HANDLE(hDrive);
    return rc;
}

int InstallVentoy2PhyDrive(PHY_DRIVE_INFO *pPhyDrive, int PartStyle)
{
    int i;
    int rc = 0;
    int state = 0;
    HANDLE hDrive;
    DWORD dwSize;
    BOOL bRet;
    CHAR MountDrive;
    CHAR DriveName[] = "?:\\";
    CHAR DriveLetters[MAX_PATH] = { 0 };
    MBR_HEAD MBR;
    VTOY_GPT_INFO *pGptInfo = NULL;

    Log("InstallVentoy2PhyDrive %s PhyDrive%d <<%s %s %dGB>>",
        PartStyle ? "GPT" : "MBR", pPhyDrive->PhyDrive, pPhyDrive->VendorId, pPhyDrive->ProductId,
        GetHumanReadableGBSize(pPhyDrive->SizeInBytes));

    if (PartStyle)
    {
        pGptInfo = malloc(sizeof(VTOY_GPT_INFO));
        memset(pGptInfo, 0, sizeof(VTOY_GPT_INFO));
    }

    PROGRESS_BAR_SET_POS(PT_LOCK_FOR_CLEAN);

    VentoyFillMBR(pPhyDrive->SizeInBytes, &MBR, PartStyle);//also used to format 1st partition in GPT mode
    if (PartStyle)
    {
        VentoyFillGpt(pPhyDrive->SizeInBytes, pGptInfo);
    }

    Log("Lock disk for clean ............................. ");

    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, FALSE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Failed to open physical disk");
        free(pGptInfo);
        return 1;
    }

    GetLettersBelongPhyDrive(pPhyDrive->PhyDrive, DriveLetters, sizeof(DriveLetters));

    if (DriveLetters[0] == 0)
    {
        Log("No drive letter was assigned...");
        DriveName[0] = GetFirstUnusedDriveLetter();
        Log("GetFirstUnusedDriveLetter %C: ...", DriveName[0]);
    }
    else
    {
        // Unmount all mounted volumes that belong to this drive
        // Do it in reverse so that we always end on the first volume letter
        for (i = (int)strlen(DriveLetters); i > 0; i--)
        {
            DriveName[0] = DriveLetters[i - 1];
            bRet = DeleteVolumeMountPointA(DriveName);
            Log("Delete mountpoint %s ret:%u code:%u", DriveName, bRet, GetLastError());
        }
    }

    MountDrive = DriveName[0];
    Log("Will use '%C:' as volume mountpoint", DriveName[0]);

    // It kind of blows, but we have to relinquish access to the physical drive
    // for VDS to be able to delete the partitions that reside on it...
    DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL);
    CHECK_CLOSE_HANDLE(hDrive);

    PROGRESS_BAR_SET_POS(PT_DEL_ALL_PART);

    if (!DeletePartitions(pPhyDrive->PhyDrive, FALSE))
    {
        Log("Notice: Could not delete partitions: %u", GetLastError());
    }

    Log("Deleting all partitions ......................... OK");

    PROGRESS_BAR_SET_POS(PT_LOCK_FOR_WRITE);

    Log("Lock disk for write ............................. ");
    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, TRUE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Failed to GetPhysicalHandle for write.");
        rc = 1;
        goto End;
    }

    //Refresh Drive Layout
    DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dwSize, NULL);

    disk_io_set_param(hDrive, MBR.PartTbl[0].StartSectorId + MBR.PartTbl[0].SectorCount);

    PROGRESS_BAR_SET_POS(PT_FORMAT_PART1);

    if (PartStyle == 1 && pPhyDrive->PartStyle == 0)
    {
        Log("Wait for format part1 ...");
        Sleep(1000 * 5);
    }

    Log("Formatting part1 exFAT ...");
    if (0 != FormatPart1exFAT(pPhyDrive->SizeInBytes))
    {
        Log("FormatPart1exFAT failed.");
        rc = 1;
        goto End;
    }

    PROGRESS_BAR_SET_POS(PT_FORMAT_PART2);
    Log("Writing part2 FAT img ...");
    if (0 != FormatPart2Fat(hDrive, MBR.PartTbl[1].StartSectorId))
    {
        Log("FormatPart2Fat failed.");
        rc = 1;
        goto End;
    }

    PROGRESS_BAR_SET_POS(PT_WRITE_STG1_IMG);
    Log("Writting Boot Image ............................. ");
    if (WriteGrubStage1ToPhyDrive(hDrive, PartStyle) != 0)
    {
        Log("WriteGrubStage1ToPhyDrive failed.");
        rc = 1;
        goto End;
    }

    PROGRESS_BAR_SET_POS(PT_WRITE_PART_TABLE);
    Log("Writting Partition Table ........................ ");
    SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);

    if (PartStyle)
    {
        VTOY_GPT_HDR BackupHead;
        LARGE_INTEGER liCurrentPosition;

        SET_FILE_POS(pPhyDrive->SizeInBytes - 512);
        VentoyFillBackupGptHead(pGptInfo, &BackupHead);
        if (!WriteFile(hDrive, &BackupHead, sizeof(VTOY_GPT_HDR), &dwSize, NULL))
        {
            rc = 1;
            Log("Write GPT Backup Head Failed, dwSize:%u (%u) ErrCode:%u", dwSize, sizeof(VTOY_GPT_INFO), GetLastError());
            goto End;
        }

        SET_FILE_POS(pPhyDrive->SizeInBytes - 512 * 33);
        if (!WriteFile(hDrive, pGptInfo->PartTbl, sizeof(pGptInfo->PartTbl), &dwSize, NULL))
        {
            rc = 1;
            Log("Write GPT Backup Part Table Failed, dwSize:%u (%u) ErrCode:%u", dwSize, sizeof(VTOY_GPT_INFO), GetLastError());
            goto End;
        }

        SET_FILE_POS(0);
        if (!WriteFile(hDrive, pGptInfo, sizeof(VTOY_GPT_INFO), &dwSize, NULL))
        {
            rc = 1;
            Log("Write GPT Info Failed, dwSize:%u (%u) ErrCode:%u", dwSize, sizeof(VTOY_GPT_INFO), GetLastError());
            goto End;
        }

        Log("Write GPT Info OK ...");
    }
    else
    {
        if (!WriteFile(hDrive, &MBR, sizeof(MBR), &dwSize, NULL))
        {
            rc = 1;
            Log("Write MBR Failed, dwSize:%u ErrCode:%u", dwSize, GetLastError());
            goto End;
        }
        Log("Write MBR OK ...");
    }
    

    //Refresh Drive Layout
    DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dwSize, NULL);

End:

    PROGRESS_BAR_SET_POS(PT_MOUNT_VOLUME);

    if (rc == 0)
    {
        Log("Mounting Ventoy Partition ....................... ");
        Sleep(1000);

        state = 0;
        memset(DriveLetters, 0, sizeof(DriveLetters));
        GetLettersBelongPhyDrive(pPhyDrive->PhyDrive, DriveLetters, sizeof(DriveLetters));
        Log("Logical drive letter after write ventoy: <%s>", DriveLetters);

        for (i = 0; i < sizeof(DriveLetters) && DriveLetters[i]; i++)
        {
            DriveName[0] = DriveLetters[i];
            if (IsVentoyLogicalDrive(DriveName[0]))
            {
                Log("%s is ventoy part2, delete mountpoint", DriveName);
                DeleteVolumeMountPointA(DriveName);
            }
            else
            {
                Log("%s is ventoy part1, already mounted", DriveName);
                state = 1;
            }
        }

        if (state != 1)
        {
            Log("need to mount ventoy part1...");
            if (0 == GetVentoyVolumeName(pPhyDrive->PhyDrive, MBR.PartTbl[0].StartSectorId, DriveLetters, sizeof(DriveLetters), FALSE))
            {
                DriveName[0] = MountDrive;
                bRet = SetVolumeMountPointA(DriveName, DriveLetters);
                Log("SetVolumeMountPoint <%s> <%s> bRet:%u code:%u", DriveName, DriveLetters, bRet, GetLastError());
            }
            else
            {
                Log("Failed to find ventoy volume");
            }
        }
        Log("OK\n");
    }
    else
    {
        FindProcessOccupyDisk(hDrive, pPhyDrive);
    }

    if (pGptInfo)
    {
        free(pGptInfo);
    }

    CHECK_CLOSE_HANDLE(hDrive);
    return rc;
}

int UpdateVentoy2PhyDrive(PHY_DRIVE_INFO *pPhyDrive)
{
    int i;
    int rc = 0;
    BOOL ForceMBR = FALSE;
    HANDLE hVolume;
    HANDLE hDrive;
    DWORD Status;
    DWORD dwSize;
    BOOL bRet;
    CHAR DriveName[] = "?:\\";
    CHAR DriveLetters[MAX_PATH] = { 0 };
    UINT64 StartSector;
	UINT64 ReservedMB = 0;
    MBR_HEAD BootImg;
    MBR_HEAD MBR;
    VTOY_GPT_INFO *pGptInfo = NULL;

    Log("UpdateVentoy2PhyDrive %s PhyDrive%d <<%s %s %dGB>>",
        pPhyDrive->PartStyle ? "GPT" : "MBR", pPhyDrive->PhyDrive, pPhyDrive->VendorId, pPhyDrive->ProductId,
        GetHumanReadableGBSize(pPhyDrive->SizeInBytes));

    PROGRESS_BAR_SET_POS(PT_LOCK_FOR_CLEAN);

    Log("Lock disk for umount ............................ ");

    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, FALSE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Failed to open physical disk");
        return 1;
    }

    if (pPhyDrive->PartStyle)
    {
        pGptInfo = malloc(sizeof(VTOY_GPT_INFO));
        if (!pGptInfo)
        {
            return 1;
        }

        memset(pGptInfo, 0, sizeof(VTOY_GPT_INFO));

        // Read GPT Info
        SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);
        ReadFile(hDrive, pGptInfo, sizeof(VTOY_GPT_INFO), &dwSize, NULL);

        //MBR will be used to compare with local boot image
        memcpy(&MBR, &pGptInfo->MBR, sizeof(MBR_HEAD));

        StartSector = pGptInfo->PartTbl[1].StartLBA;
        Log("GPT StartSector in PartTbl:%llu", (ULONGLONG)StartSector);

        ReservedMB = (pPhyDrive->SizeInBytes / 512 - (StartSector + VENTOY_EFI_PART_SIZE / 512) - 33) / 2048;
        Log("GPT Reserved Disk Space:%llu MB", (ULONGLONG)ReservedMB);
    }
    else
    {
        // Read MBR
        SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);
        ReadFile(hDrive, &MBR, sizeof(MBR), &dwSize, NULL);

        StartSector = MBR.PartTbl[1].StartSectorId;
        Log("MBR StartSector in PartTbl:%llu", (ULONGLONG)StartSector);

        ReservedMB = (pPhyDrive->SizeInBytes / 512 - (StartSector + VENTOY_EFI_PART_SIZE / 512)) / 2048;
        Log("MBR Reserved Disk Space:%llu MB", (ULONGLONG)ReservedMB);
    }

    GetLettersBelongPhyDrive(pPhyDrive->PhyDrive, DriveLetters, sizeof(DriveLetters));

    if (DriveLetters[0] == 0)
    {
        Log("No drive letter was assigned...");
    }
    else
    {
        // Unmount all mounted volumes that belong to this drive
        // Do it in reverse so that we always end on the first volume letter
        for (i = (int)strlen(DriveLetters); i > 0; i--)
        {
            DriveName[0] = DriveLetters[i - 1];
            if (IsVentoyLogicalDrive(DriveName[0]))
            {
                Log("%s is ventoy logical drive", DriveName);
                bRet = DeleteVolumeMountPointA(DriveName);
                Log("Delete mountpoint %s ret:%u code:%u", DriveName, bRet, LASTERR);
                break;
            }
        }
    }

    // It kind of blows, but we have to relinquish access to the physical drive
    // for VDS to be able to delete the partitions that reside on it...
    DeviceIoControl(hDrive, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL);
    CHECK_CLOSE_HANDLE(hDrive);

    PROGRESS_BAR_SET_POS(PT_LOCK_FOR_WRITE);

    Log("Lock disk for update ............................ ");
    hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, TRUE, FALSE);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        Log("Failed to GetPhysicalHandle for write.");
        rc = 1;
        goto End;
    }

    PROGRESS_BAR_SET_POS(PT_LOCK_VOLUME);

    Log("Lock volume for update .......................... ");
    hVolume = INVALID_HANDLE_VALUE;
	Status = GetVentoyVolumeName(pPhyDrive->PhyDrive, (UINT32)StartSector, DriveLetters, sizeof(DriveLetters), TRUE);
    if (ERROR_SUCCESS == Status)
    {
        Log("Now lock and dismount volume <%s>", DriveLetters);
        hVolume = CreateFileA(DriveLetters,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
            NULL);

        if (hVolume == INVALID_HANDLE_VALUE)
        {
            Log("Failed to create file volume, errcode:%u", LASTERR);
            rc = 1;
            goto End;
        }

        bRet = DeviceIoControl(hVolume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL);
        Log("FSCTL_LOCK_VOLUME bRet:%u code:%u", bRet, LASTERR);

        bRet = DeviceIoControl(hVolume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL);
        Log("FSCTL_DISMOUNT_VOLUME bRet:%u code:%u", bRet, LASTERR);
    }
    else if (ERROR_NOT_FOUND == Status)
    {
        Log("Volume not found, maybe not supported");
    }
    else
    {
        rc = 1;
        goto End;
    }


    if (!TryWritePart2(hDrive, StartSector))
    {
		if (pPhyDrive->PartStyle == 0)
		{
			ForceMBR = TRUE;
			Log("Try write failed, now delete partition 2...");

			CHECK_CLOSE_HANDLE(hDrive);

			Log("Now delete partition 2...");
			DeletePartitions(pPhyDrive->PhyDrive, TRUE);

			hDrive = GetPhysicalHandle(pPhyDrive->PhyDrive, TRUE, TRUE, FALSE);
			if (hDrive == INVALID_HANDLE_VALUE)
			{
				Log("Failed to GetPhysicalHandle for write.");
				rc = 1;
				goto End;
			}
		}
    }

    PROGRESS_BAR_SET_POS(PT_FORMAT_PART2);

    Log("Write Ventoy to disk ............................ ");
    if (0 != FormatPart2Fat(hDrive, StartSector))
    {
        rc = 1;
        goto End;
    }

    if (hVolume != INVALID_HANDLE_VALUE)
    {
        bRet = DeviceIoControl(hVolume, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &dwSize, NULL);
        Log("FSCTL_UNLOCK_VOLUME bRet:%u code:%u", bRet, LASTERR);
        CHECK_CLOSE_HANDLE(hVolume);
    }

    Log("Updating Boot Image ............................. ");
    if (WriteGrubStage1ToPhyDrive(hDrive, pPhyDrive->PartStyle) != 0)
    {
        rc = 1;
        goto End;
    }

    // Boot Image
    VentoyGetLocalBootImg(&BootImg);

    // Use Old UUID
    memcpy(BootImg.BootCode + 0x180, MBR.BootCode + 0x180, 16);
    if (pPhyDrive->PartStyle)
    {
        BootImg.BootCode[92] = 0x22;
    }

    if (ForceMBR == FALSE && memcmp(BootImg.BootCode, MBR.BootCode, 440) == 0)
    {
        Log("Boot image has no difference, no need to write.");
    }
    else
    {
        Log("Boot image need to write %u.", ForceMBR);

        SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);

        memcpy(MBR.BootCode, BootImg.BootCode, 440);
        bRet = WriteFile(hDrive, &MBR, 512, &dwSize, NULL);
        Log("Write Boot Image ret:%u dwSize:%u Error:%u", bRet, dwSize, LASTERR);
    }

    if (pPhyDrive->PartStyle == 0)
    {
        if (0x00 == MBR.PartTbl[0].Active && 0x80 == MBR.PartTbl[1].Active)
        {
            Log("Need to chage 1st partition active and 2nd partition inactive.");

            MBR.PartTbl[0].Active = 0x80;
            MBR.PartTbl[1].Active = 0x00;

            SetFilePointer(hDrive, 0, NULL, FILE_BEGIN);
            bRet = WriteFile(hDrive, &MBR, 512, &dwSize, NULL);
            Log("Write NEW MBR ret:%u dwSize:%u Error:%u", bRet, dwSize, LASTERR);
        }
    }

    //Refresh Drive Layout
    DeviceIoControl(hDrive, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &dwSize, NULL);

End:

    if (rc == 0)
    {
        Log("OK");
    }
    else
    {
        FindProcessOccupyDisk(hDrive, pPhyDrive);
    }

    CHECK_CLOSE_HANDLE(hDrive);

    if (pGptInfo)
    {
        free(pGptInfo);
    }
    
    return rc;
}


