/*
 * sensors.h - all supported sensors, auto-detected at boot.
 *
 * On-board : WSEN-HIDS (T/RH, 0x44), SGP40 (VOC, 0x59), ICS-43434 (I2S mic)
 * Optional : AHT20 (0x38), BMP280/BME280 (0x76/0x77), ENS160 (0x52/0x53)
 *            on I2C1; SEN50/54/55 (0x69) on I2C2 with the 5 V boost.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* presence bits */
#define HAS_HIDS    (1u << 0)
#define HAS_SGP40   (1u << 1)
#define HAS_MIC     (1u << 2)
#define HAS_AHT20   (1u << 3)
#define HAS_BMP280  (1u << 4)
#define HAS_BME280  (1u << 5)
#define HAS_ENS160  (1u << 6)
#define HAS_SEN5X   (1u << 7)

/* "valid this round" bits */
#define V_HIDS      (1u << 0)
#define V_VOC_RAW   (1u << 1)
#define V_VOC_INDEX (1u << 2)
#define V_SOUND     (1u << 3)
#define V_AHT20     (1u << 4)
#define V_BMP       (1u << 5)
#define V_BME_RH    (1u << 6)
#define V_ENS160    (1u << 7)
#define V_PM        (1u << 8)
#define V_SEN_RHT   (1u << 9)
#define V_SEN_VOC   (1u << 10)
#define V_SEN_NOX   (1u << 11)

struct readings {
  uint32_t valid;
  int16_t t_c100;       uint16_t rh_100;        /* HIDS                     */
  uint16_t voc_raw;     uint16_t voc_index;     /* SGP40 + gas index algo   */
  uint16_t la_10;       uint16_t lmax_10;       /* dB(A) x10: Leq and Lmax  */
  uint16_t la90_10;     uint16_t la10_10;       /* dB(A) x10: LA90, LA10    */
  int16_t aht_t_c100;   uint16_t aht_rh_100;    /* AHT20                    */
  int16_t bmp_t_c100;   uint32_t p_pa;          /* BMP/BME280               */
  uint16_t bme_rh_100;
  uint8_t ens_aqi;      uint8_t ens_status;     /* ENS160: status 0=ok      */
  uint16_t ens_tvoc;    uint16_t ens_eco2;
  uint16_t pm1_10, pm25_10, pm4_10, pm10_10;    /* SEN5x, ug/m3 x10         */
  int16_t sen_t_c100;   uint16_t sen_rh_100;
  uint16_t sen_voc_10;  uint16_t sen_nox_10;
};

extern uint32_t g_present;     /* HAS_* bits */
extern uint8_t g_sen5x_type;   /* 50, 54, 55 or 0 */
extern bool g_voc_restored;    /* VOC baseline restored after a reset */

void sensors_probe(void);

/* --- fast tick (every 10 s): HIDS + SGP40 + VOC index ------------------ */
void sensors_fast_tick(struct readings *r);

/* --- per report: optional sensors -------------------------------------- */
void sensors_report(struct readings *r, bool usb, uint8_t soc);
/* lets the caller keep SEN5x / ENS160 running on USB or shut them down */
void sensors_power_policy(bool usb);
bool sen5x_battery_due(uint64_t now_ms, uint8_t soc);

/* individual drivers */
bool hids_measure(int16_t *t_c100, uint16_t *rh_100);
bool sgp40_probe(void);
bool sgp40_measure_raw(uint16_t rh_100, int16_t t_c100, uint16_t *raw);
bool aht20_probe(void);
bool aht20_measure(int16_t *t_c100, uint16_t *rh_100);
bool bmp280_probe(bool *is_bme);
bool bmp280_measure(int16_t *t_c100, uint32_t *p_pa, uint16_t *rh_100);
bool ens160_probe(void);
void ens160_mode(bool run);
bool ens160_compensate(int16_t t_c100, uint16_t rh_100);
bool ens160_read(uint8_t *aqi, uint16_t *tvoc, uint16_t *eco2, uint8_t *status);
bool sen5x_power_on_probe(uint8_t *type);
bool sen5x_start(void);
bool sen5x_stop(void);
bool sen5x_read(struct readings *r);
void sen5x_power_off(void);
