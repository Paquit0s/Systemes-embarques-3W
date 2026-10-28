#include "config_uart.h"
#include "mode_manager.h"
#include "grove_rtc_ds1307.h"
#include "grove_gps_air530z.h"
#include "stm32l4xx_hal.h"
#include "board_pins.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CONFIG_FLASH_ADDR  0x080FF800U  /* Dernière page 2 Ko (Flash 1 Mo STM32L476RG, Bank 2 Page 255/127) */
#define CONFIG_MAGIC       0x33475754U  /* "3WGT" */
#define FW_VERSION_STR     "3W-FW v2.0 (STM32L476RG @ 80 MHz)"

typedef struct {
    uint32_t magic;
    Config   cfg;
    uint32_t crc;
} FlashBlob;

static Config s_cfg = CONFIG_DEFAULTS_INIT;

/* =========================================================================
 * P2-4 : Réception UART2 non bloquante par Interruption (IT) + Ring Buffer
 * ========================================================================= */
#define UART_RX_RING_SIZE  128U

static volatile uint8_t  s_rx_ring[UART_RX_RING_SIZE];
static volatile uint32_t s_rx_head = 0U;
static volatile uint32_t s_rx_tail = 0U;
static uint8_t           s_rx_byte = 0U;

#define LINE_MAX 96
static char   s_line[LINE_MAX];
static size_t s_len = 0U;

/**
 * @brief Callback HAL de fin de réception d'un octet UART en mode IT (P2-4)
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uint32_t next = (s_rx_head + 1U) % UART_RX_RING_SIZE;
        if (next != s_rx_tail)
        {
            s_rx_ring[s_rx_head] = s_rx_byte;
            s_rx_head = next;
        }
        /* Réarme immédiatement la réception IT de l'octet suivant */
        (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
    }
    else if (huart->Instance == UART4)
    {
        GroveGPS_RxCpltHook();
    }
}

/**
 * @brief Callback HAL d'erreur UART : nettoie les flags (ex: Overrun) et relance l'IT
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
    }
    else if (huart->Instance == UART4)
    {
        GroveGPS_ErrorHook();
    }
}

static bool uart_ring_pop(uint8_t *out)
{
    if (s_rx_tail == s_rx_head)
    {
        return false;
    }
    *out = s_rx_ring[s_rx_tail];
    s_rx_tail = (s_rx_tail + 1U) % UART_RX_RING_SIZE;
    return true;
}

void ConfigUART_FlushInput(void)
{
    s_rx_tail = s_rx_head;
    s_len     = 0U;
}

static uint32_t crc32_simple(const void *p, size_t n)
{
    uint32_t h = 0x811C9DC5u;
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++)
    {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

bool ConfigUART_Load(void)
{
    const FlashBlob *b = (const FlashBlob *)CONFIG_FLASH_ADDR;
    if (b->magic != CONFIG_MAGIC) return false;
    if (b->crc != crc32_simple(&b->cfg, sizeof(Config))) return false;
    s_cfg = b->cfg;
    return true;
}

bool ConfigUART_Save(void)
{
    FlashBlob blob = { .magic = CONFIG_MAGIC, .cfg = s_cfg, .crc = 0U };
    blob.crc = crc32_simple(&blob.cfg, sizeof(Config));

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    FLASH_EraseInitTypeDef e = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Banks     = FLASH_BANK_2,
        .Page      = 255U, /* Dernière page de la Bank 2 (0x080FF800 sur STM32L476RG 1 Mo) */
        .NbPages   = 1U,
    };
    uint32_t err = 0U;
    if (HAL_FLASHEx_Erase(&e, &err) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return false;
    }

    const uint64_t *src = (const uint64_t *)&blob;
    uint32_t addr = CONFIG_FLASH_ADDR;
    size_t n_dwords = (sizeof(blob) + 7U) / 8U;
    for (size_t i = 0; i < n_dwords; i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, src[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return false;
        }
        addr += 8U;
    }
    HAL_FLASH_Lock();
    return true;
}

void ConfigUART_Init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_len     = 0U;
    (void)ConfigUART_Load(); /* Chargement Flash best-effort (conserve les défauts sinon) */

    /* P2-4 : Démarre l'écoute non bloquante sur interruption USART2 */
    (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
}

const Config *ConfigUART_Get(void)
{
    return &s_cfg;
}

/* ------------------------------------------------------------------ */
/* Parseur de commandes console (HELP/GET/SET/SAVE/VERSION/DEFAULTS/SETRTC) */
/* ------------------------------------------------------------------ */

static void print(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, (uint16_t)strlen(s), 100U);
}

static void print_help(void)
{
    print("\r\nCommandes disponibles :\r\n"
          "  HELP                         : Affiche cette aide\r\n"
          "  VERSION                      : Affiche la version du firmware\r\n"
          "  GET <param|ALL>              : Lit un ou tous les parametres\r\n"
          "  SET <param> <value>          : Modifie un parametre en RAM\r\n"
          "  DEFAULTS                     : Restaure les parametres par defaut\r\n"
          "  SAVE                         : Sauvegarde la configuration en Flash\r\n"
          "  SETRTC YYYY-MM-DD HH:MM:SS   : Met a l'heure l'horloge RTC DS1307\r\n"
          "  EXIT / QUIT                  : Quitte la configuration et passe en STANDARD\r\n"
          "Parametres :\r\n"
          "  log_interval (5..3600 s), timeout_bme680 (100..5000 ms),\r\n"
          "  timeout_light (10..1000 ms), timeout_gps (500..10000 ms),\r\n"
          "  timeout_rtc (10..1000 ms), temp_min/temp_max (-40..85 C),\r\n"
          "  hum_min/hum_max (0..100 %), press_min/press_max (300..1100 hPa),\r\n"
          "  light_min/light_max (0..4095), sd_limit (>=1024 octets),\r\n"
          "  config_timeout (10..86400 s, defaut=1800 s / 30 min)\r\n");
}

static void print_all_params(void)
{
    char buf[160];
    snprintf(buf, sizeof(buf),
             "log_interval=%lu s\r\ntimeout_bme680=%lu ms\r\ntimeout_light=%lu ms\r\n"
             "timeout_gps=%lu ms\r\ntimeout_rtc=%lu ms\r\n",
             (unsigned long)s_cfg.log_interval_s,
             (unsigned long)s_cfg.timeout_bme680_ms,
             (unsigned long)s_cfg.timeout_light_ms,
             (unsigned long)s_cfg.timeout_gps_ms,
             (unsigned long)s_cfg.timeout_rtc_ms);
    print(buf);

    snprintf(buf, sizeof(buf),
             "temp=[%.2f..%.2f] C\r\nhum=[%.2f..%.2f] %%\r\npress=[%.2f..%.2f] hPa\r\n"
             "light=[%u..%u]\r\nsd_limit=%lu B\r\nconfig_timeout=%lu s\r\n",
             (double)s_cfg.temp_min, (double)s_cfg.temp_max,
             (double)s_cfg.hum_min, (double)s_cfg.hum_max,
             (double)s_cfg.press_min, (double)s_cfg.press_max,
             (unsigned int)s_cfg.light_min, (unsigned int)s_cfg.light_max,
             (unsigned long)s_cfg.sd_limit_bytes,
             (unsigned long)s_cfg.config_timeout_s);
    print(buf);
}

static void cmd_get(const char *p)
{
    char buf[48];
    if      (!strcasecmp(p, "ALL"))            { print_all_params(); }
    else if (!strcmp(p, "log_interval"))       { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.log_interval_s); print(buf); }
    else if (!strcmp(p, "timeout_bme680"))     { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.timeout_bme680_ms); print(buf); }
    else if (!strcmp(p, "timeout_light"))      { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.timeout_light_ms); print(buf); }
    else if (!strcmp(p, "timeout_gps"))        { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.timeout_gps_ms); print(buf); }
    else if (!strcmp(p, "timeout_rtc"))        { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.timeout_rtc_ms); print(buf); }
    else if (!strcmp(p, "temp_min"))           { snprintf(buf, 48, "%.2f\r\n", (double)s_cfg.temp_min); print(buf); }
    else if (!strcmp(p, "temp_max"))           { snprintf(buf, 48, "%.2f\r\n", (double)s_cfg.temp_max); print(buf); }
    else if (!strcmp(p, "hum_min"))            { snprintf(buf, 48, "%.2f\r\n", (double)s_cfg.hum_min); print(buf); }
    else if (!strcmp(p, "hum_max"))            { snprintf(buf, 48, "%.2f\r\n", (double)s_cfg.hum_max); print(buf); }
    else if (!strcmp(p, "press_min"))          { snprintf(buf, 48, "%.2f\r\n", (double)s_cfg.press_min); print(buf); }
    else if (!strcmp(p, "press_max"))          { snprintf(buf, 48, "%.2f\r\n", (double)s_cfg.press_max); print(buf); }
    else if (!strcmp(p, "light_min"))          { snprintf(buf, 48, "%u\r\n", s_cfg.light_min); print(buf); }
    else if (!strcmp(p, "light_max"))          { snprintf(buf, 48, "%u\r\n", s_cfg.light_max); print(buf); }
    else if (!strcmp(p, "sd_limit"))           { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.sd_limit_bytes); print(buf); }
    else if (!strcmp(p, "config_timeout"))     { snprintf(buf, 48, "%lu\r\n", (unsigned long)s_cfg.config_timeout_s); print(buf); }
    else print("ERR parametre inconnu\r\n");
}

static bool set_u32(const char *v, uint32_t *dst, uint32_t lo, uint32_t hi)
{
    char *end;
    unsigned long x = strtoul(v, &end, 10);
    if (*end || x < lo || x > hi) return false;
    *dst = (uint32_t)x;
    return true;
}

static bool set_u16(const char *v, uint16_t *dst, uint16_t lo, uint16_t hi)
{
    char *end;
    unsigned long x = strtoul(v, &end, 10);
    if (*end || x < lo || x > hi) return false;
    *dst = (uint16_t)x;
    return true;
}

static bool set_f32(const char *v, float *dst, float lo, float hi)
{
    char *end;
    float x = strtof(v, &end);
    if (*end || x < lo || x > hi) return false;
    *dst = x;
    return true;
}

static bool cmd_set(const char *p, const char *v)
{
    if (!strcmp(p, "log_interval"))   return set_u32(v, &s_cfg.log_interval_s, 5U, 3600U);
    if (!strcmp(p, "timeout_bme680")) return set_u32(v, &s_cfg.timeout_bme680_ms, 100U, 5000U);
    if (!strcmp(p, "timeout_light"))  return set_u32(v, &s_cfg.timeout_light_ms, 10U, 1000U);
    if (!strcmp(p, "timeout_gps"))    return set_u32(v, &s_cfg.timeout_gps_ms, 500U, 10000U);
    if (!strcmp(p, "timeout_rtc"))    return set_u32(v, &s_cfg.timeout_rtc_ms, 10U, 1000U);
    if (!strcmp(p, "temp_min"))       return set_f32(v, &s_cfg.temp_min, -40.0f, 85.0f);
    if (!strcmp(p, "temp_max"))       return set_f32(v, &s_cfg.temp_max, -40.0f, 85.0f);
    if (!strcmp(p, "hum_min"))        return set_f32(v, &s_cfg.hum_min, 0.0f, 100.0f);
    if (!strcmp(p, "hum_max"))        return set_f32(v, &s_cfg.hum_max, 0.0f, 100.0f);
    if (!strcmp(p, "press_min"))      return set_f32(v, &s_cfg.press_min, 300.0f, 1100.0f);
    if (!strcmp(p, "press_max"))      return set_f32(v, &s_cfg.press_max, 300.0f, 1100.0f);
    if (!strcmp(p, "light_min"))      return set_u16(v, &s_cfg.light_min, 0U, 4095U);
    if (!strcmp(p, "light_max"))      return set_u16(v, &s_cfg.light_max, 0U, 4095U);
    if (!strcmp(p, "sd_limit"))       return set_u32(v, &s_cfg.sd_limit_bytes, 1024U, 0xFFFFFFFFU);
    if (!strcmp(p, "config_timeout")) return set_u32(v, &s_cfg.config_timeout_s, 10U, 86400U);
    return false;
}

static bool cmd_setrtc(const char *date_s, const char *time_s)
{
    if (!date_s || !time_s) return false;
    unsigned int yr = 0, mo = 0, dy = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(date_s, "%u-%u-%u", &yr, &mo, &dy) != 3) return false;
    if (sscanf(time_s, "%u:%u:%u", &hh, &mm, &ss) != 3) return false;
    if (yr < 2000U || yr > 2099U || mo < 1U || mo > 12U || dy < 1U || dy > 31U) return false;
    if (hh > 23U || mm > 59U || ss > 59U) return false;

    RTC_DateTime dt = {
        .year      = (uint16_t)yr,
        .month     = (uint8_t)mo,
        .day       = (uint8_t)dy,
        .dayOfWeek = 1U,
        .hours     = (uint8_t)hh,
        .minutes   = (uint8_t)mm,
        .seconds   = (uint8_t)ss
    };
    return GroveRTC_SetDateTime(&dt);
}

static void process_line(char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;

    /* P2-6 : Toute commande reçue en mode CONFIGURATION réinitialise le timer d'inactivité (30 min) */
    ModeManager_NotifyActivity();

    if (!strcasecmp(cmd, "HELP"))
    {
        print_help();
        return;
    }
    if (!strcasecmp(cmd, "VERSION"))
    {
        print(FW_VERSION_STR "\r\n");
        return;
    }
    if (!strcasecmp(cmd, "DEFAULTS"))
    {
        Config def = CONFIG_DEFAULTS_INIT;
        s_cfg = def;
        print("OK parametres par defaut restaures (tapez SAVE pour enregistrer)\r\n");
        return;
    }
    if (!strcasecmp(cmd, "SAVE"))
    {
        print(ConfigUART_Save() ? "OK sauvegarde Flash\r\n" : "ERR ecriture Flash\r\n");
        return;
    }
    if (!strcasecmp(cmd, "GET"))
    {
        char *p = strtok_r(NULL, " \t", &save);
        if (p) cmd_get(p);
        else print("ERR syntaxe (ex: GET ALL ou GET log_interval)\r\n");
        return;
    }
    if (!strcasecmp(cmd, "SET"))
    {
        char *p = strtok_r(NULL, " \t", &save);
        char *v = strtok_r(NULL, " \t", &save);
        if (!p || !v)
        {
            print("ERR syntaxe (ex: SET log_interval 30)\r\n");
            return;
        }
        print(cmd_set(p, v) ? "OK\r\n" : "ERR valeur hors domaine\r\n");
        return;
    }
    if (!strcasecmp(cmd, "SETRTC"))
    {
        char *d = strtok_r(NULL, " \t", &save);
        char *t = strtok_r(NULL, " \t", &save);
        print(cmd_setrtc(d, t) ? "OK RTC mise a l'heure\r\n" : "ERR format (SETRTC YYYY-MM-DD HH:MM:SS)\r\n");
        return;
    }
    if (!strcasecmp(cmd, "EXIT") || !strcasecmp(cmd, "QUIT"))
    {
        (void)ModeManager_RequestMode(MODE_STANDARD);
        print("OK sortie de configuration -> passage en mode STANDARD\r\n");
        return;
    }
    print("ERR commande inconnue (tapez HELP)\r\n");
}

/**
 * @brief Dépile le ring buffer alimenté par l'interruption USART2 (P2-4)
 *        Aucun appel bloquant HAL_UART_Receive n'est effectué dans la boucle principale.
 */
void ConfigUART_Process(void)
{
    uint8_t c;
    while (uart_ring_pop(&c))
    {
        /* P2-6 : Signale l'activité console pour maintenir le mode CONFIGURATION */
        ModeManager_NotifyActivity();

        if (c == '\r') continue;
        if (c == '\n')
        {
            s_line[s_len] = '\0';
            if (s_len > 0U)
            {
                process_line(s_line);
            }
            s_len = 0U;
        }
        else if (c == '\b' || c == 0x7FU)
        {
            if (s_len > 0U)
            {
                --s_len;
            }
        }
        else if (s_len < (LINE_MAX - 1U))
        {
            s_line[s_len++] = (char)c;
        }
    }
}
