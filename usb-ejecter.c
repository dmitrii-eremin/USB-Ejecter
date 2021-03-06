#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <windows.h>

#include <Setupapi.h>
#include <winioctl.h>
#include <winioctl.h>
#include <cfgmgr32.h>

#pragma comment(lib, "setupapi.lib")

bool EjectDriveByLetter(char letter);
bool EjectDriveByLetterEx(char letter, DWORD tries, DWORD timeout);
DEVINST GetDrivesDevInst(long number, UINT type, char *dos_device_name);

void PrintDriveType(UINT drive_type);

int main(int argc, char **argv)
{
    if (argc >= 2)
    {
        char letter = argv[1][0];
        EjectDriveByLetter(letter);
    }
    else
    {
        DWORD drives = GetLogicalDrives();
        char letter = 'A';
        while (drives)
        {
            char root_path[] = "X:\\";
            if (drives & 0x01)
            {
                root_path[0] = letter;
                UINT drive_type = GetDriveTypeA(root_path);

                if (drive_type == DRIVE_REMOVABLE)
                {
                    printf("Found removable: %s.\n", root_path);
                    EjectDriveByLetter(letter);
                    printf("\n");
                }
            }

            drives >>= 1;
            letter++;
        }
    }
    return 0;
}

bool EjectDriveByLetter(char letter)
{
    return EjectDriveByLetterEx(letter, 10, 500);
}

bool EjectDriveByLetterEx(char letter, DWORD tries, DWORD timeout)
{
    letter &= ~0x20;    // To uppercase
    if (letter < 'A' || letter > 'Z') {
        printf("Bad drive letter: %c:\\\n", letter);
        return false;
    }

    char root_path[] = "X:\\";
    char device_path[] = "X:";
    char volume_access_path[] = "\\\\.\\X:";

    root_path[0] = letter;
    device_path[0] = letter;
    volume_access_path[4] = letter;

    long device_number = -1;
    HANDLE volume = CreateFileA(volume_access_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE)
    {
        printf("Failed to open the volume %s. Last error code: 0x%08X.\n", root_path, GetLastError());
        return false;
    }
    printf("Volume %s opened successfully.\n", root_path);

    STORAGE_DEVICE_NUMBER sdn = { 0 };
    DWORD bytes = 0;
    BOOL result = DeviceIoControl(volume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &bytes, NULL);
    if (result != FALSE)
    {
        device_number = sdn.DeviceNumber;
    }
    CloseHandle(volume);

    if (device_number == -1)
    {
        printf("Failed to obtain a device number. Last error code: 0x%08X.\n", GetLastError());
        return false;
    }
    printf("Device number is: %d.\n", device_number);

    UINT drive_type = GetDriveTypeA(root_path);
    PrintDriveType(drive_type);

    char dos_device_name[MAX_PATH];
    DWORD dwResult = QueryDosDeviceA(device_path, dos_device_name, MAX_PATH);
    if (dwResult == 0)
    {
        printf("Failed to query dos device name. Last error code: 0x%08X.\n", GetLastError());
        return false;
    }
    printf("Dos device name: %s\n", dos_device_name);

    DEVINST devinst = GetDrivesDevInst(device_number, drive_type, dos_device_name);
    if (devinst == 0)
    {
        return false;
    }
    printf("Device instance (%d) is obtained.\n", (DWORD)devinst);

    PNP_VETO_TYPE veto_type = PNP_VetoTypeUnknown;

    DEVINST parent = 0;
    CM_Get_Parent(&parent, devinst, 0);

    bool success = false;

    for (DWORD i = 0; i < tries; i++)
    {
        char veto_name[MAX_PATH] = { 0 };
        printf("Trying to eject the device...\n");
        CONFIGRET res = CM_Request_Device_EjectA(parent, &veto_type, veto_name, MAX_PATH, 0);
        success = res == CR_SUCCESS && veto_type == PNP_VetoTypeUnknown;

        if (success)
        {
            printf("The device has been successfully ejected.\n");
            break;            
        }

        printf("Failed to eject the device. Result code: 0x%2X.\n", res);
        Sleep(timeout);
    }

    return success;
}

DEVINST GetDrivesDevInst(long number, UINT type, char *dos_device_name)
{
    bool is_floppy = strstr(dos_device_name, "\\Floppy") != NULL;

    const GUID *guid = NULL;
    switch (type)
    {
    case DRIVE_REMOVABLE:
        if (is_floppy)
        {
            guid = &GUID_DEVINTERFACE_FLOPPY;
        }
        else
        {
            guid = &GUID_DEVINTERFACE_DISK;
        }
        break;
    case DRIVE_FIXED:
        guid = &GUID_DEVINTERFACE_DISK;
        break;
    case DRIVE_CDROM:
        guid = &GUID_DEVINTERFACE_CDROM;
        break;
    default:
        printf("Failed to get drive device instance. Incorrect device type.\n");
        return 0;
    }

    HDEVINFO devinfo = SetupDiGetClassDevsA(guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devinfo == INVALID_HANDLE_VALUE)
    {
        printf("Failed to SetupDiGetClassDevsA(). Last error code: 0x%08X.\n", GetLastError());
        return 0;
    }

    DWORD index = 0;
    long res;

    BYTE buffer[1024];
    PSP_DEVICE_INTERFACE_DETAIL_DATA pspdidd = (PSP_DEVICE_INTERFACE_DETAIL_DATA)buffer;
    SP_DEVICE_INTERFACE_DATA         spdid;
    SP_DEVINFO_DATA                  spdd;
    DWORD                            size;

    spdid.cbSize = sizeof(spdid);

    while (true) {
        res = SetupDiEnumDeviceInterfaces(devinfo, NULL, guid, index, &spdid);
        if (!res) {
            break;
        }

        size = 0;
        SetupDiGetDeviceInterfaceDetail(devinfo, &spdid, NULL, 0, &size, NULL); // check the buffer size

        if (size != 0 && size <= sizeof(buffer)) {

            pspdidd->cbSize = sizeof(*pspdidd); // 5 Bytes!

            ZeroMemory(&spdd, sizeof(spdd));
            spdd.cbSize = sizeof(spdd);

            long res = SetupDiGetDeviceInterfaceDetail(devinfo, &spdid, pspdidd, size, &size, &spdd);
            if (res) {
                HANDLE drive_handle = CreateFile(pspdidd->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                if (drive_handle != INVALID_HANDLE_VALUE) {
                    STORAGE_DEVICE_NUMBER sdn;
                    DWORD bytes = 0;
                    res = DeviceIoControl(drive_handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &bytes, NULL);
                    if (res) {
                        if (number == (long)sdn.DeviceNumber) {
                            CloseHandle(drive_handle);
                            SetupDiDestroyDeviceInfoList(devinfo);
                            return spdd.DevInst;
                        }
                    }
                    CloseHandle(drive_handle);
                }
            }
        }
        index++;
    }

    SetupDiDestroyDeviceInfoList(devinfo);

    return 0;
}

void PrintDriveType(UINT drive_type)
{
    printf("Drive type is ");
    switch (drive_type)
    {
    case DRIVE_UNKNOWN:
        printf("DRIVE_UNKNOWN");
        break;
    case DRIVE_NO_ROOT_DIR:
        printf("DRIVE_NO_ROOT_DIR");
        break;
    case DRIVE_REMOVABLE:
        printf("DRIVE_REMOVABLE");
        break;
    case DRIVE_FIXED:
        printf("DRIVE_FIXED");
        break;
    case DRIVE_REMOTE:
        printf("DRIVE_REMOTE");
        break;
    case DRIVE_CDROM:
        printf("DRIVE_CDROM");
        break;
    case DRIVE_RAMDISK:
        printf("DRIVE_RAMDISK");
        break;
    default:
        printf("<Unknown drive type>");
        break;
    }
    printf(".\n");
}