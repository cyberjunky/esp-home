/*
 * i2c_bus.h - minimal, hang-proof I2C access for both buses.
 *   BUS_ONBOARD (I2C1, PB6/PB7): HIDS, SGP40 + optional AHT20/BMP280/ENS160
 *   BUS_EXT     (I2C2, PB10/PB11): SEN5x on the 5 V boost rail
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { BUS_ONBOARD = 0, BUS_EXT = 1 };

void i2c_open(int bus);
void i2c_close(int bus);               /* peripheral off, pins analog */
bool i2c_probe(int bus, uint8_t addr);
bool i2c_write(int bus, uint8_t addr, const uint8_t *data, uint16_t len);
bool i2c_read(int bus, uint8_t addr, uint8_t *data, uint16_t len);
bool i2c_write_reg(int bus, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len);
bool i2c_read_reg(int bus, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len);
uint16_t i2c_error_count(void);

/* Sensirion-style helpers (CRC-8 poly 0x31, init 0xFF, 16-bit words) */
uint8_t crc8_sensirion(const uint8_t *data, uint8_t len);
bool sensirion_cmd(int bus, uint8_t addr, uint16_t cmd, const uint16_t *args, uint8_t nargs);
bool sensirion_read_words(int bus, uint8_t addr, uint16_t *words, uint8_t nwords);
