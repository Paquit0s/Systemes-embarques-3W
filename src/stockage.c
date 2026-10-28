#include "stockage.h"
#include "sd_diskio_spi.h"
#include "config_uart.h"
#include "led_control.h"
#include "setup.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

static const char *CSV_HEADER =
    "timestamp;temp_C;hum_pct;press_hPa;light_raw;lat_deg;lon_deg;status\r\n";

static FATFS s_fs;
static FIL   s_fil;
static bool  s_mounted   = false;
static bool  s_file_open = false;
static char  s_active_name[16] = {0};

static void make_base_date(const RTC_DateTime *dt, char out_yymmdd[7])
{
    if (dt != NULL &&
        dt->year >= 2000U &&
        dt->month >= 1U && dt->month <= 12U &&
        dt->day >= 1U && dt->day <= 31U)
    {
        snprintf(out_yymmdd, 7U, "%02u%02u%02u",
                 (unsigned int)(dt->year % 100U),
                 (unsigned int)dt->month,
                 (unsigned int)dt->day);
    }
    else
    {
        strcpy(out_yymmdd, "000101");
    }
}

static bool open_active_file(const char *filename)
{
    FRESULT fr = f_open(&s_fil, filename, FA_OPEN_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        return false;
    }

    FSIZE_t sz = f_size(&s_fil);
    if (sz > 0U)
    {
        fr = f_lseek(&s_fil, sz);
        if (fr != FR_OK)
        {
            (void)f_close(&s_fil);
            return false;
        }
    }
    else
    {
        /* Fichier neuf : écriture de l'en-tête CSV avec unités (P1-14) */
        UINT bw = 0U;
        UINT hlen = (UINT)strlen(CSV_HEADER);
        fr = f_write(&s_fil, CSV_HEADER, hlen, &bw);
        if (fr != FR_OK || bw < hlen)
        {
            (void)f_close(&s_fil);
            return false;
        }
        (void)f_sync(&s_fil);
    }

    strncpy(s_active_name, filename, sizeof(s_active_name) - 1U);
    s_active_name[sizeof(s_active_name) - 1U] = '\0';
    s_file_open = true;
    return true;
}

/**
 * @brief Archive le fichier courant AAMMJJ_0.LOG vers AAMMJJ_N.LOG
 *        puis repart sur un fichier AAMMJJ_0.LOG vierge (Livrable 1 & 2, P1-15).
 */
static bool rotate_archive_file(const char yymmdd[7])
{
    if (s_file_open)
    {
        (void)f_sync(&s_fil);
        (void)f_close(&s_fil);
        s_file_open = false;
    }

    char src_name[16];
    char arch_name[16];
    snprintf(src_name, sizeof(src_name), "%s_0.LOG", yymmdd);

    bool found_slot = false;
    FILINFO fno;

    for (unsigned int idx = 1U; idx <= 9U; ++idx)
    {
        snprintf(arch_name, sizeof(arch_name), "%s_%u.LOG", yymmdd, idx);
        if (f_stat(arch_name, &fno) == FR_NO_FILE)
        {
            found_slot = true;
            break;
        }
    }

    if (!found_slot)
    {
        for (char prefix = 'A'; prefix <= 'Z' && !found_slot; ++prefix)
        {
            for (unsigned int d = 0U; d <= 9U; ++d)
            {
                snprintf(arch_name, sizeof(arch_name), "%s%c%u.LOG", yymmdd, prefix, d);
                if (f_stat(arch_name, &fno) == FR_NO_FILE)
                {
                    found_slot = true;
                    break;
                }
            }
        }
    }

    if (!found_slot)
    {
        snprintf(arch_name, sizeof(arch_name), "%s_1.LOG", yymmdd);
        (void)f_unlink(arch_name);
    }

    FRESULT fr = f_rename(src_name, arch_name);
    if (fr != FR_OK)
    {
        return false;
    }

    printf("[SD] Rotation archive : %s -> %s\r\n", src_name, arch_name);
    s_active_name[0] = '\0';
    return open_active_file(src_name);
}

static size_t format_csv_line(const Mesure *m, char *buf, size_t cap)
{
    char ts_str[32];
    char t_str[16], h_str[16], p_str[16], l_str[16], lat_str[20], lon_str[20];

    if (m->timestamp.year >= 2000U && m->timestamp.month >= 1U && m->timestamp.day >= 1U)
    {
        snprintf(ts_str, sizeof(ts_str), "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned int)m->timestamp.year,
                 (unsigned int)m->timestamp.month,
                 (unsigned int)m->timestamp.day,
                 (unsigned int)m->timestamp.hours,
                 (unsigned int)m->timestamp.minutes,
                 (unsigned int)m->timestamp.seconds);
    }
    else
    {
        strcpy(ts_str, "NA");
    }

    if (m->statut_temp == STATUT_NA)
        strcpy(t_str, "NA");
    else
        snprintf(t_str, sizeof(t_str), "%.2f", (double)m->env.temp);

    if (m->statut_hum == STATUT_NA)
        strcpy(h_str, "NA");
    else
        snprintf(h_str, sizeof(h_str), "%.2f", (double)m->env.hum);

    if (m->statut_press == STATUT_NA)
        strcpy(p_str, "NA");
    else
        snprintf(p_str, sizeof(p_str), "%.2f", (double)m->env.press);

    if (m->statut_light == STATUT_NA)
        strcpy(l_str, "NA");
    else
        snprintf(l_str, sizeof(l_str), "%u", (unsigned int)m->light);

    if (m->statut_gps == STATUT_NA || !m->gps.valid)
    {
        strcpy(lat_str, "NA");
        strcpy(lon_str, "NA");
    }
    else
    {
        snprintf(lat_str, sizeof(lat_str), "%.6f", m->gps.latitude);
        snprintf(lon_str, sizeof(lon_str), "%.6f", m->gps.longitude);
    }

    const char *row_status = "OK";
    if (m->statut_temp == STATUT_INCOHERENT ||
        m->statut_hum == STATUT_INCOHERENT ||
        m->statut_press == STATUT_INCOHERENT ||
        m->statut_light == STATUT_INCOHERENT)
    {
        row_status = "INCOHERENT";
    }
    else if (m->statut_temp == STATUT_NA ||
             m->statut_hum == STATUT_NA ||
             m->statut_press == STATUT_NA ||
             m->statut_light == STATUT_NA ||
             m->statut_gps == STATUT_NA)
    {
        row_status = "PARTIAL";
    }

    int n = snprintf(buf, cap, "%s;%s;%s;%s;%s;%s;%s;%s\r\n",
                     ts_str, t_str, h_str, p_str, l_str, lat_str, lon_str, row_status);
    return (n > 0 && (size_t)n < cap) ? (size_t)n : 0U;
}

bool Stockage_Init(void)
{
    BSP_Watchdog_Refresh();

    if (s_file_open)
    {
        (void)f_sync(&s_fil);
        (void)f_close(&s_fil);
        s_file_open = false;
    }

    /* Démontage inconditionnel avant réinitialisation SPI + FatFs (P2-5 : reprise à chaud) */
    (void)f_mount(NULL, "", 0);
    s_mounted = false;
    SD_DiskIO_Invalidate();

    FRESULT fr = f_mount(&s_fs, "", 1);
    BSP_Watchdog_Refresh();

    if (fr != FR_OK)
    {
        printf("[SD] Echec montage FatFs (FRESULT=%d)\r\n", (int)fr);
        LedControl_ReportError(ERR_SD_ABSENTE, CAPTEUR_SD);
        return false;
    }

    s_mounted = true;
    s_active_name[0] = '\0';
    LedControl_ClearError(ERR_SD_ABSENTE);
    LedControl_ClearError(ERR_SD_ECRITURE);
    LedControl_ClearError(ERR_SD_PLEINE);
    printf("[SD] Carte SD montee avec succes.\r\n");
    return true;
}

bool Stockage_LogMesure(const Mesure *m)
{
    if (m == NULL)
    {
        return false;
    }

    BSP_Watchdog_Refresh();

    /* P2-5 : Reprise automatique si la carte SD avait été retirée ou absente */
    if (!s_mounted)
    {
        if (!Stockage_Init())
        {
            return false;
        }
    }

    char yymmdd[7];
    char target_name[16];
    make_base_date(&m->timestamp, yymmdd);
    snprintf(target_name, sizeof(target_name), "%s_0.LOG", yymmdd);

    if (s_file_open && strcmp(s_active_name, target_name) != 0)
    {
        (void)f_sync(&s_fil);
        (void)f_close(&s_fil);
        s_file_open = false;
    }

    if (!s_file_open)
    {
        if (!open_active_file(target_name))
        {
            (void)f_mount(NULL, "", 0);
            s_mounted = false;
            SD_DiskIO_Invalidate();
            LedControl_ReportError(ERR_SD_ECRITURE, CAPTEUR_SD);
            printf("[SD] Impossible d'ouvrir %s\r\n", target_name);
            return false;
        }
    }

    char line[192];
    size_t line_len = format_csv_line(m, line, sizeof(line));
    if (line_len == 0U)
    {
        return false;
    }

    const Config *cfg = ConfigUART_Get();
    uint32_t limit_bytes = (cfg != NULL && cfg->sd_limit_bytes >= 256U)
                         ? cfg->sd_limit_bytes : 1048576UL;

    if (f_size(&s_fil) > 0U && ((uint32_t)f_size(&s_fil) + (uint32_t)line_len) > limit_bytes)
    {
        if (!rotate_archive_file(yymmdd))
        {
            (void)f_mount(NULL, "", 0);
            s_mounted = false;
            SD_DiskIO_Invalidate();
            LedControl_ReportError(ERR_SD_ECRITURE, CAPTEUR_SD);
            printf("[SD] Echec rotation/archivage de %s\r\n", target_name);
            return false;
        }
    }

    UINT bw = 0U;
    FRESULT fr = f_write(&s_fil, line, (UINT)line_len, &bw);
    if (fr != FR_OK)
    {
        (void)f_close(&s_fil);
        (void)f_mount(NULL, "", 0);
        s_file_open = false;
        s_mounted   = false;
        SD_DiskIO_Invalidate();
        LedControl_ReportError(ERR_SD_ECRITURE, CAPTEUR_SD);
        printf("[SD] Erreur d'ecriture (FRESULT=%d) -> reprise armee\r\n", (int)fr);
        return false;
    }

    if (bw < (UINT)line_len)
    {
        (void)f_sync(&s_fil);
        LedControl_ReportError(ERR_SD_PLEINE, CAPTEUR_SD);
        printf("[SD] Carte SD pleine (ecrit %u/%u octets)\r\n", bw, (unsigned int)line_len);
        return false;
    }

    fr = f_sync(&s_fil);
    if (fr != FR_OK)
    {
        (void)f_close(&s_fil);
        (void)f_mount(NULL, "", 0);
        s_file_open = false;
        s_mounted   = false;
        SD_DiskIO_Invalidate();
        LedControl_ReportError(ERR_SD_ECRITURE, CAPTEUR_SD);
        printf("[SD] Erreur flush f_sync (FRESULT=%d) -> reprise armee\r\n", (int)fr);
        return false;
    }

    BSP_Watchdog_Refresh();
    LedControl_ClearError(ERR_SD_ABSENTE);
    LedControl_ClearError(ERR_SD_ECRITURE);
    LedControl_ClearError(ERR_SD_PLEINE);
    return true;
}

void Stockage_Flush(void)
{
    if (s_mounted && s_file_open)
    {
        (void)f_sync(&s_fil);
    }
}

void Stockage_Close(void)
{
    BSP_Watchdog_Refresh();

    if (s_file_open)
    {
        (void)f_sync(&s_fil);
        (void)f_close(&s_fil);
        s_file_open = false;
    }
    if (s_mounted)
    {
        (void)f_mount(NULL, "", 0);
        s_mounted = false;
    }
    s_active_name[0] = '\0';
    SD_DiskIO_Invalidate();
    printf("[SD] Stockage ferme proprement (retrait carte autorise).\r\n");
}
