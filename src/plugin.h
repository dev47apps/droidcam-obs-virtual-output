// Copyright (C) 2021 DEV47APPS, github.com/dev47apps
#pragma once

#include <obs-module.h>

#define xlog(log_level, format, ...) \
        blog(log_level, "[DroidcamVirtualOutput] " format, ##__VA_ARGS__)

#ifdef DEBUG
#define dlog(format, ...) xlog(LOG_INFO, format, ##__VA_ARGS__)
#else
#define dlog(format, ...) /* */
#endif
#define ilog(format, ...) xlog(LOG_INFO, format, ##__VA_ARGS__)
#define elog(format, ...) xlog(LOG_WARNING, format, ##__VA_ARGS__)

#define ARRAY_LEN(a) (sizeof(a) / sizeof(a[0]))
