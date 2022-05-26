/*
Copyright (C) 2021 DEV47APPS, github.com/dev47apps

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "plugin.h"
// #pragma comment(lib, "advapi32")

bool CreateSharedMem(LPHANDLE phFileMapping, LPVOID* ppSharedMem,
    const LPCWSTR name, DWORD size)
{
    *phFileMapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, // use paging file
        NULL,                 // default security attributes
        PAGE_READWRITE,
        0,    // size: high 32-bits
        size, // size: low 32-bits
        name);

    if(*phFileMapping == NULL) {
        *ppSharedMem = NULL;
        elog("CreateFileMapping Failed !! ");
        return false;
    }

    *ppSharedMem = MapViewOfFile(*phFileMapping, FILE_MAP_WRITE, 0,0,0);
    if(*ppSharedMem == NULL) {
        elog("MapViewOfFile Failed !! ");
        CloseHandle(*phFileMapping);
        *phFileMapping = NULL;
        return false;
    }

    return true;
}

#if 0
int GetRegValInt(const LPCWSTR path, const LPCWSTR entry) {
    HKEY key;
    DWORD data = 0;
    DWORD size = sizeof(data);

    if (ERROR_SUCCESS == RegOpenKeyExW(HKEY_CURRENT_USER, path, 0,
            KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key))
    {
        RegQueryValueExW(key, entry, 0, 0, (BYTE*) &data, &size);
        RegCloseKey(key);
    }

    return (int) data;
}

void SetRegValInt(const LPCWSTR path, const LPCWSTR entry, int data) {
    HKEY key;
    DWORD value = data;

    if (ERROR_SUCCESS == RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, 0, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, 0, &key, 0))
    {
        RegSetValueExW(key, entry, 0, REG_DWORD, (BYTE*) &value, sizeof(value));
        RegCloseKey(key);
    }
}
#endif
