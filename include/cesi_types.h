#ifndef CESI_TYPES_H
#define CESI_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t dayOfWeek;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} RTC_DateTime;

typedef struct
{
    bool valid;
    char raw_sentence[96];

    double latitude;
    double longitude;

    uint8_t fix_quality;
    uint8_t satellites;
    /* Parsing GGA complet (P1-12) */
    char    utc_time[12];   /* Format "HH:MM:SS" */
    uint8_t utc_hour;
    uint8_t utc_min;
    uint8_t utc_sec;
    uint8_t utc_hours;
    uint8_t utc_minutes;
    uint8_t utc_seconds;
    float   altitude_m;     /* Altitude au-dessus du niveau de la mer (mètres) */
    float   hdop;           /* Dilution horizontale de la précision */
} GPS_Data;

typedef struct
{
    float temp;
    float hum;
    float press;
    float gas_resistance_ohm;
} Env_Data;

/* ============================================================
 * Ajouts Phase 1 — ne pas supprimer les types existants
 * ============================================================ */

/* --- P1-1 : Modes système --- */
typedef enum {
    MODE_STANDARD = 0,
    MODE_CONFIGURATION,
    MODE_ECO,
    MODE_MAINTENANCE,
    MODE_COUNT
} ModeSysteme;

/* --- P1-2 : Codes d'erreur (un par cause, + identification capteur) --- */
typedef enum {
    ERR_NONE = 0,
    ERR_RTC_INIT,
    ERR_RTC_LECTURE,
    ERR_GPS_PAS_FIX,
    ERR_GPS_TIMEOUT,
    ERR_CAPTEUR_ACCES,
    ERR_CAPTEUR_TIMEOUT,
    ERR_CAPTEUR_INCOHERENT,
    ERR_SD_ABSENTE,
    ERR_SD_PLEINE,
    ERR_SD_ECRITURE,
    ERR_CONFIG_INVALIDE,
    ERR_MEMOIRE_FLASH,
    ERR_COUNT
} CodeErreur;

typedef enum {
    CAPTEUR_AUCUN = 0,
    CAPTEUR_BME680,
    CAPTEUR_LIGHT,
    CAPTEUR_GPS,
    CAPTEUR_RTC,
    CAPTEUR_SD
} CapteurId;

/* --- P1-4 : Statut par grandeur --- */
typedef enum {
    STATUT_OK = 0,
    STATUT_NA,
    STATUT_INCOHERENT
} StatutMesure;

/* --- P1-3 : Configuration (valeurs par défaut = spec Livrable 1 & 2) --- */
typedef struct {
    uint32_t log_interval_s;
    uint32_t timeout_bme680_ms;
    uint32_t timeout_light_ms;
    uint32_t timeout_gps_ms;
    uint32_t timeout_rtc_ms;
    float    temp_min, temp_max;
    float    hum_min, hum_max;
    float    hygr_mint, hygr_maxt;
    float    press_min, press_max;
    uint16_t light_min, light_max;
    uint16_t lumin_low, lumin_high;
    uint8_t  lumin_enabled;
    uint8_t  temp_air_enabled;
    uint8_t  hygr_enabled;
    uint8_t  pressure_enabled;
    uint32_t sd_limit_bytes;
    uint32_t config_timeout_s;
} Config;

/* --- P1-4 : Mesure --- */
typedef struct {
    RTC_DateTime timestamp;
    Env_Data     env;
    uint16_t     light;
    GPS_Data     gps;
    StatutMesure statut_temp;
    StatutMesure statut_hum;
    StatutMesure statut_press;
    StatutMesure statut_light;
    StatutMesure statut_gps;
} Mesure;

/* --- P1-6/7 : Événements boutons --- */
typedef enum {
    EVENT_NONE = 0,
    EVENT_BTN_PRESS,
    EVENT_BTN_RELEASE,
    EVENT_BTN_LONG
} EventType;

typedef enum {
    BTN_RED = 0,    /* PC13 / PB3 (Bouton Rouge) */
    BTN_GREEN,      /* PA10 (Bouton Vert) */
    BTN_COUNT
} ButtonId;

typedef struct {
    EventType type;
    ButtonId  button;
    uint32_t  tick;
    uint32_t  duration_ms;
} Event;

/* --- P1-3 : valeurs par défaut (conformes au Cahier des Charges) --- */
#define CONFIG_DEFAULTS_INIT {               \
    .log_interval_s    = 60,                 \
    .timeout_bme680_ms = 1000,               \
    .timeout_light_ms  = 100,                \
    .timeout_gps_ms    = 2000,               \
    .timeout_rtc_ms    = 100,                \
    .temp_min  = -10.0f, .temp_max  = 60.0f, \
    .hum_min   = 0.0f,   .hum_max   = 100.0f,\
    .hygr_mint = 0.0f,   .hygr_maxt = 50.0f, \
    .press_min = 850.0f, .press_max = 1080.0f, \
    .light_min = 0,      .light_max = 4095,  \
    .lumin_low = 255,    .lumin_high = 768,  \
    .lumin_enabled     = 1,                  \
    .temp_air_enabled  = 1,                  \
    .hygr_enabled      = 1,                  \
    .pressure_enabled  = 1,                  \
    .sd_limit_bytes    = 2048,               \
    .config_timeout_s  = 1800                \
}
#endif
