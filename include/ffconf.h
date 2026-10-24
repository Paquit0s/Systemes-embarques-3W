/*---------------------------------------------------------------------------/
/  FatFs - FAT file system module configuration file (R0.12c / _FFCONF 68300)
/  Projet 3W — STM32L476RG (Mode SPI, noms courts 8.3 : AAMMJJ_n.LOG)
/---------------------------------------------------------------------------*/

#ifndef FFCONF_H
#define FFCONF_H

#define _FFCONF 68300

/* Function Configurations */
#define _FS_READONLY    0
#define _FS_MINIMIZE    0
#define _USE_STRFUNC    0
#define _USE_FIND       0
#define _USE_MKFS       0
#define _USE_FASTSEEK   0
#define _USE_EXPAND     0
#define _USE_CHMOD      0
#define _USE_LABEL      0
#define _USE_FORWARD    0

/* Locale and Namespace Configurations (8.3 filenames: e.g. 261024_0.LOG) */
#define _CODE_PAGE      437
#define _USE_LFN        0
#define _MAX_LFN        255
#define _LFN_UNICODE    0
#define _STRF_ENCODE    0
#define _FS_RPATH       0

/* Drive/Volume Configurations */
#define _VOLUMES        1
#define _STR_VOLUME_ID  0
#define _MULTI_PARTITION 0
#define _MIN_SS         512
#define _MAX_SS         512
#define _USE_TRIM       0
#define _FS_NOFSINFO    0

/* System Configurations */
#define _FS_TINY        0
#define _FS_EXFAT       0
#define _FS_NORTC       0
#define _NORTC_MON      1
#define _NORTC_MDAY     1
#define _NORTC_YEAR     2026
#define _FS_LOCK        0
#define _FS_REENTRANT   0

#endif /* FFCONF_H */
