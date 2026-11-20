/*
 * i2c_bus.c - minimal, hang-proof I2C access.
 *
 * Blocking HAL calls with short timeouts; on any error the bus is recovered
 * (9 clock pulses + STOP) so a sensor that glitches can never hang the
 * firmware. Buses are powered down and pins parked between uses.
 */
#include "i2c_bus.h"
#include "stm32l0xx_hal.h"

#define I2C_TIMEOUT_MS 25
#define I2C_TIMING_100K 0x00503D58u  /* 100 kHz @ 16 MHz PCLK (original value) */

static I2C_HandleTypeDef h[2];
static bool opened[2];
static uint16_t errors;

static const struct {
  I2C_TypeDef *inst;
  GPIO_TypeDef *port;
  uint16_t scl, sda;
  uint8_t af;
} cfg[2] = {
  {I2C1, GPIOB, GPIO_PIN_6, GPIO_PIN_7, GPIO_AF1_I2C1},
  {I2C2, GPIOB, GPIO_PIN_10, GPIO_PIN_11, GPIO_AF6_I2C2},
};

static void pins_analog(int bus) {
  GPIO_InitTypeDef g = {.Pin = cfg[bus].scl | cfg[bus].sda, .Mode = GPIO_MODE_ANALOG};
  HAL_GPIO_Init(cfg[bus].port, &g);
}

static void pins_af(int bus) {
  GPIO_InitTypeDef g = {.Pin = cfg[bus].scl | cfg[bus].sda, .Mode = GPIO_MODE_AF_OD,
                        .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW,
                        .Alternate = cfg[bus].af};
  HAL_GPIO_Init(cfg[bus].port, &g);
}

static void bus_recover(int bus) {
  GPIO_TypeDef *p = cfg[bus].port;
  GPIO_InitTypeDef g = {.Pin = cfg[bus].scl | cfg[bus].sda, .Mode = GPIO_MODE_OUTPUT_OD,
                        .Pull = GPIO_NOPULL, .Speed = GPIO_SPEED_FREQ_LOW};
  HAL_GPIO_WritePin(p, cfg[bus].scl | cfg[bus].sda, GPIO_PIN_SET);
  HAL_GPIO_Init(p, &g);
  for (int i = 0; i < 9; i++) {
    HAL_GPIO_WritePin(p, cfg[bus].scl, GPIO_PIN_RESET);
    for (volatile int d = 0; d < 40; d++) {}
    HAL_GPIO_WritePin(p, cfg[bus].scl, GPIO_PIN_SET);
    for (volatile int d = 0; d < 40; d++) {}
  }
  /* STOP: SDA low -> high while SCL high */
  HAL_GPIO_WritePin(p, cfg[bus].sda, GPIO_PIN_RESET);
  for (volatile int d = 0; d < 40; d++) {}
  HAL_GPIO_WritePin(p, cfg[bus].sda, GPIO_PIN_SET);
}

void i2c_open(int bus) {
  if (opened[bus]) return;
  if (bus == BUS_ONBOARD) __HAL_RCC_I2C1_CLK_ENABLE(); else __HAL_RCC_I2C2_CLK_ENABLE();
  I2C_HandleTypeDef *hi = &h[bus];
  hi->Instance = cfg[bus].inst;
  hi->Init.Timing = I2C_TIMING_100K;
  hi->Init.OwnAddress1 = 0;
  hi->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi->Init.OwnAddress2 = 0;
  hi->Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  pins_af(bus);
  HAL_I2C_Init(hi);
  HAL_I2CEx_ConfigAnalogFilter(hi, I2C_ANALOGFILTER_ENABLE);
  opened[bus] = true;
}

void i2c_close(int bus) {
  if (!opened[bus]) return;
  HAL_I2C_DeInit(&h[bus]);
  if (bus == BUS_ONBOARD) __HAL_RCC_I2C1_CLK_DISABLE(); else __HAL_RCC_I2C2_CLK_DISABLE();
  pins_analog(bus);
  opened[bus] = false;
}

/* HAL MSP hooks: pins are handled above */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi) { (void)hi; }
void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi) { (void)hi; }

static bool fail(int bus) {
  errors++;
  HAL_I2C_DeInit(&h[bus]);
  bus_recover(bus);
  opened[bus] = false;
  i2c_open(bus);
  return false;
}

static bool ok_or_fail(int bus, HAL_StatusTypeDef st) {
  if (st == HAL_OK) return true;
  /* plain NACK (absent sensor / busy) needs no recovery */
  if (st == HAL_ERROR && h[bus].ErrorCode == HAL_I2C_ERROR_AF) return false;
  return fail(bus);
}

bool i2c_probe(int bus, uint8_t addr) {
  i2c_open(bus);
  return HAL_I2C_IsDeviceReady(&h[bus], addr << 1, 2, I2C_TIMEOUT_MS) == HAL_OK;
}

bool i2c_write(int bus, uint8_t addr, const uint8_t *data, uint16_t len) {
  i2c_open(bus);
  return ok_or_fail(bus, HAL_I2C_Master_Transmit(&h[bus], addr << 1, (uint8_t *)data, len, I2C_TIMEOUT_MS));
}

bool i2c_read(int bus, uint8_t addr, uint8_t *data, uint16_t len) {
  i2c_open(bus);
  return ok_or_fail(bus, HAL_I2C_Master_Receive(&h[bus], addr << 1, data, len, I2C_TIMEOUT_MS));
}

bool i2c_write_reg(int bus, uint8_t addr, uint8_t reg, const uint8_t *data, uint16_t len) {
  i2c_open(bus);
  return ok_or_fail(bus, HAL_I2C_Mem_Write(&h[bus], addr << 1, reg, I2C_MEMADD_SIZE_8BIT,
                                           (uint8_t *)data, len, I2C_TIMEOUT_MS));
}

bool i2c_read_reg(int bus, uint8_t addr, uint8_t reg, uint8_t *data, uint16_t len) {
  i2c_open(bus);
  return ok_or_fail(bus, HAL_I2C_Mem_Read(&h[bus], addr << 1, reg, I2C_MEMADD_SIZE_8BIT,
                                          data, len, I2C_TIMEOUT_MS));
}

uint16_t i2c_error_count(void) { return errors; }

uint8_t crc8_sensirion(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0xFF;
  while (len--) {
    crc ^= *data++;
    for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool sensirion_cmd(int bus, uint8_t addr, uint16_t cmd, const uint16_t *args, uint8_t nargs) {
  uint8_t buf[2 + 3 * 4];
  uint8_t n = 0;
  buf[n++] = cmd >> 8;
  buf[n++] = cmd & 0xFF;
  for (uint8_t i = 0; i < nargs && i < 4; i++) {
    buf[n] = args[i] >> 8;
    buf[n + 1] = args[i] & 0xFF;
    buf[n + 2] = crc8_sensirion(&buf[n], 2);
    n += 3;
  }
  return i2c_write(bus, addr, buf, n);
}

bool sensirion_read_words(int bus, uint8_t addr, uint16_t *words, uint8_t nwords) {
  uint8_t buf[3 * 10];
  if (nwords > 10) return false;
  if (!i2c_read(bus, addr, buf, nwords * 3)) return false;
  for (uint8_t i = 0; i < nwords; i++) {
    if (crc8_sensirion(&buf[i * 3], 2) != buf[i * 3 + 2]) {
      errors++;
      return false;
    }
    words[i] = (uint16_t)(buf[i * 3] << 8 | buf[i * 3 + 1]);
  }
  return true;
}
