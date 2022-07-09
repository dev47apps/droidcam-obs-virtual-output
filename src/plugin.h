// Copyright (C) 2022 DEV47APPS, github.com/dev47apps
#pragma once
#include <obs-module.h>

#define xlog(log_level, format, ...) \
        blog(log_level, "[DroidcamVirtualOut] " format, ##__VA_ARGS__)

#ifdef DEBUG
#define dlog(format, ...) xlog(LOG_INFO, format, ##__VA_ARGS__)
#else
#define dlog(format, ...) /* */
#endif
#define ilog(format, ...) xlog(LOG_INFO, format, ##__VA_ARGS__)
#define elog(format, ...) xlog(LOG_WARNING, format, ##__VA_ARGS__)

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))

#ifdef _WIN32
#define _WIN32_WINNT 0x0501
#define _WIN32_IE    0x0500
#define _WIN32_DCOM
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool CreateSharedMem(LPHANDLE phFileMapping, LPVOID* ppSharedMem,
    const LPCWSTR name, DWORD size);

int GetRegValInt(const LPCWSTR path, const LPCWSTR entry);
void SetRegValInt(const LPCWSTR path, const LPCWSTR entry, int data);
#endif
