#include "config_uart.h"
#include "mode_manager.h"
#include "stm32l4xx_hal.h"
#include "board_pins.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CONFIG_FLASH_ADDR  0x080FF800U  /* ⚠ dernière page 2 Ko si flash 1 Mo */
#define CONFIG_MAGIC       0x33475754U  /* "3WGT" */

typedef struct {
    uint32_t magic;
    Config   cfg;
    uint32_t crc;   /* XOR simple pour validation */
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

static uint32_t crc32_simple(const void *p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

bool ConfigUART_Load(void) {
    const FlashBlob *b = (const FlashBlob*)CONFIG_FLASH_ADDR;
    if (b->magic != CONFIG_MAGIC) return false;
    if (b->crc != crc32_simple(&b->cfg, sizeof(Config))) return false;
    s_cfg = b->cfg;
    return true;
}

bool ConfigUART_Save(void) {
    FlashBlob blob = { .magic = CONFIG_MAGIC, .cfg = s_cfg, .crc = 0 };
    blob.crc = crc32_simple(&blob.cfg, sizeof(Config));

    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef e = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Banks = FLASH_BANK_2,               /* ⚠ adapt if page in bank 1 */
        .Page = 127,                          /* dernière page de la bank 2 */
        .NbPages = 1,
    };
    uint32_t err;
    if (HAL_FLASHEx_Erase(&e, &err) != HAL_OK) { HAL_FLASH_Lock(); return false; }

    const uint64_t *src = (const uint64_t*)&blob;
    uint32_t addr = CONFIG_FLASH_ADDR;
    for (size_t i = 0; i < sizeof(blob)/8; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, src[i]) != HAL_OK) {
            HAL_FLASH_Lock(); return false;
        }
        addr += 8;
    }
    HAL_FLASH_Lock();
    return true;
}

void ConfigUART_Init(void) {
    s_rx_head = 0U;
    s_rx_tail = 0U;
    ConfigUART_Load();   /* best-effort */

    /* P2-4 : Démarre l'écoute non bloquante sur interruption USART2 */
    (void)HAL_UART_Receive_IT(&huart2, &s_rx_byte, 1U);
}

const Config* ConfigUART_Get(void) { return &s_cfg; }

/* ------------------------------------------------------------------ */
/* Parseur de commandes (SET/GET/HELP/SAVE)                            */
/* ------------------------------------------------------------------ */

#define LINE_MAX 96
static char   s_line[LINE_MAX];
static size_t s_len = 0;

static void print(const char *s) {
    HAL_UART_Transmit(&huart2, (uint8_t*)s, strlen(s), 100);
}

static void print_help(void) {
    print("\r\nCommandes :\r\n"
          "  HELP\r\n"
          "  GET <param>\r\n"
          "  SET <param> <value>\r\n"
          "  SAVE\r\n"
          "Params : log_interval, timeout_bme680, timeout_light,\r\n"
          "         timeout_gps, timeout_rtc, temp_min, temp_max,\r\n"
          "         hum_min, hum_max, press_min, press_max,\r\n"
          "         light_min, light_max, sd_limit, config_timeout\r\n");
}

static void cmd_get(const char *p) {
    char buf[48];
    if      (!strcmp(p,"log_interval"))    { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.log_interval_s); print(buf); }
    else if (!strcmp(p,"timeout_bme680"))  { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.timeout_bme680_ms); print(buf); }
    else if (!strcmp(p,"timeout_light"))   { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.timeout_light_ms); print(buf); }
    else if (!strcmp(p,"timeout_gps"))     { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.timeout_gps_ms); print(buf); }
    else if (!strcmp(p,"timeout_rtc"))     { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.timeout_rtc_ms); print(buf); }
    else if (!strcmp(p,"temp_min"))        { snprintf(buf,48,"%.2f\r\n",(double)s_cfg.temp_min); print(buf); }
    else if (!strcmp(p,"temp_max"))        { snprintf(buf,48,"%.2f\r\n",(double)s_cfg.temp_max); print(buf); }
    else if (!strcmp(p,"hum_min"))         { snprintf(buf,48,"%.2f\r\n",(double)s_cfg.hum_min); print(buf); }
    else if (!strcmp(p,"hum_max"))         { snprintf(buf,48,"%.2f\r\n",(double)s_cfg.hum_max); print(buf); }
    else if (!strcmp(p,"press_min"))       { snprintf(buf,48,"%.2f\r\n",(double)s_cfg.press_min); print(buf); }
    else if (!strcmp(p,"press_max"))       { snprintf(buf,48,"%.2f\r\n",(double)s_cfg.press_max); print(buf); }
    else if (!strcmp(p,"light_min"))       { snprintf(buf,48,"%u\r\n",s_cfg.light_min); print(buf); }
    else if (!strcmp(p,"light_max"))       { snprintf(buf,48,"%u\r\n",s_cfg.light_max); print(buf); }
    else if (!strcmp(p,"sd_limit"))        { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.sd_limit_bytes); print(buf); }
    else if (!strcmp(p,"config_timeout"))  { snprintf(buf,48,"%lu\r\n",(unsigned long)s_cfg.config_timeout_s); print(buf); }
    else print("ERR unknown param\r\n");
}

static bool set_u32(const char *v, uint32_t *dst, uint32_t lo, uint32_t hi) {
    char *end; unsigned long x = strtoul(v, &end, 10);
    if (*end || x < lo || x > hi) return false;
    *dst = (uint32_t)x; return true;
}
static bool set_u16(const char *v, uint16_t *dst, uint16_t lo, uint16_t hi) {
    char *end; unsigned long x = strtoul(v, &end, 10);
    if (*end || x < lo || x > hi) return false;
    *dst = (uint16_t)x; return true;
}
static bool set_f32(const char *v, float *dst, float lo, float hi) {
    char *end; float x = strtof(v, &end);
    if (*end || x < lo || x > hi) return false;
    *dst = x; return true;
}

static bool cmd_set(const char *p, const char *v) {
    if (!strcmp(p,"log_interval"))   return set_u32(v,&s_cfg.log_interval_s,5,3600);
    if (!strcmp(p,"timeout_bme680")) return set_u32(v,&s_cfg.timeout_bme680_ms,100,5000);
    if (!strcmp(p,"timeout_light"))  return set_u32(v,&s_cfg.timeout_light_ms,10,1000);
    if (!strcmp(p,"timeout_gps"))    return set_u32(v,&s_cfg.timeout_gps_ms,500,10000);
    if (!strcmp(p,"timeout_rtc"))    return set_u32(v,&s_cfg.timeout_rtc_ms,10,1000);
    if (!strcmp(p,"temp_min"))       return set_f32(v,&s_cfg.temp_min,-40.0f,85.0f);
    if (!strcmp(p,"temp_max"))       return set_f32(v,&s_cfg.temp_max,-40.0f,85.0f);
    if (!strcmp(p,"hum_min"))        return set_f32(v,&s_cfg.hum_min,0.0f,100.0f);
    if (!strcmp(p,"hum_max"))        return set_f32(v,&s_cfg.hum_max,0.0f,100.0f);
    if (!strcmp(p,"press_min"))      return set_f32(v,&s_cfg.press_min,300.0f,1100.0f);
    if (!strcmp(p,"press_max"))      return set_f32(v,&s_cfg.press_max,300.0f,1100.0f);
    if (!strcmp(p,"light_min"))      return set_u16(v,&s_cfg.light_min,0,4095);
    if (!strcmp(p,"light_max"))      return set_u16(v,&s_cfg.light_max,0,4095);
    if (!strcmp(p,"sd_limit"))       return set_u32(v,&s_cfg.sd_limit_bytes,1024,0xFFFFFFFF);
    if (!strcmp(p,"config_timeout")) return set_u32(v,&s_cfg.config_timeout_s,60,86400);
    return false;
}

static void process_line(char *line) {
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;

    /* P2-6 : Toute commande reçue en mode CONFIGURATION réinitialise le timer de 30 min */
    ModeManager_NotifyActivity();

    if (!strcasecmp(cmd,"HELP")) { print_help(); return; }
    if (!strcasecmp(cmd,"SAVE")) {
        print(ConfigUART_Save() ? "OK\r\n" : "ERR flash\r\n");
        return;
    }
    if (!strcasecmp(cmd,"GET")) {
        char *p = strtok_r(NULL, " \t", &save);
        if (p) cmd_get(p); else print("ERR syntaxe\r\n");
        return;
    }
    if (!strcasecmp(cmd,"SET")) {
        char *p = strtok_r(NULL, " \t", &save);
        char *v = strtok_r(NULL, " \t", &save);
        if (!p || !v) { print("ERR syntaxe\r\n"); return; }
        print(cmd_set(p, v) ? "OK\r\n" : "ERR valeur\r\n");
        return;
    }
    print("ERR commande inconnue\r\n");
}

/**
 * @brief Dépile le ring buffer alimenté par l'interruption USART2 (P2-4)
 *        Aucun appel bloquant HAL_UART_Receive n'est effectué dans la boucle principale.
 */
void ConfigUART_Process(void) {
    uint8_t c;
    while (uart_ring_pop(&c)) {
        /* P2-6 : Signale l'activité console pour maintenir le mode CONFIGURATION */
        ModeManager_NotifyActivity();

        if (c == '\r') continue;
        if (c == '\n') {
            s_line[s_len] = '\0';
            if (s_len) process_line(s_line);
            s_len = 0;
        } else if (s_len < LINE_MAX - 1) {
            s_line[s_len++] = (char)c;
        }
    }
}
