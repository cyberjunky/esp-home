/*
 * bridge.c - USB CDC <-> ESP32-C3 UART0 bridge, for flashing ESPHome.
 *
 * Entered by holding the user button while the board resets. The board then
 * enumerates as a USB serial port; esptool / `esphome run` work as with any
 * ESP dev board: DTR/RTS drive EN and GPIO9 like the classic two-transistor
 * auto-reset circuit, and the host's baud rate is followed.
 * It leaves bridge mode (restart into normal mode) on: reset, a fresh press
 * of the user button, USB unplug, or 10 minutes without serial traffic.
 */
#include "bridge.h"
#include "board.h"
#include "esp.h"
#include "lowpower.h"
#include "usbd_cdc.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_def.h"

#define BRIDGE_IDLE_MS (10u * 60u * 1000u) /* no traffic for 10 min -> normal mode */

/* ---------------------------------------------------------------------- */
/* USB low-level glue (PCD <-> USBD)                                        */
/* ---------------------------------------------------------------------- */
static PCD_HandleTypeDef hpcd;
static USBD_HandleTypeDef husb;

void USB_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd); }

void HAL_PCD_MspInit(PCD_HandleTypeDef *h) {
  __HAL_RCC_USB_CLK_ENABLE();
  HAL_NVIC_SetPriority(USB_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USB_IRQn);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *h) { USBD_LL_SetupStage(h->pData, (uint8_t *)h->Setup); }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *h, uint8_t ep) { USBD_LL_DataOutStage(h->pData, ep, h->OUT_ep[ep].xfer_buff); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *h, uint8_t ep) { USBD_LL_DataInStage(h->pData, ep, h->IN_ep[ep].xfer_buff); }
void HAL_PCD_SOFCallback(PCD_HandleTypeDef *h) { USBD_LL_SOF(h->pData); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *h) {
  USBD_LL_SetSpeed(h->pData, USBD_SPEED_FULL);
  USBD_LL_Reset(h->pData);
}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *h) { USBD_LL_Suspend(h->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *h) { USBD_LL_Resume(h->pData); }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *h, uint8_t ep) { USBD_LL_IsoOUTIncomplete(h->pData, ep); }
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *h, uint8_t ep) { USBD_LL_IsoINIncomplete(h->pData, ep); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *h) { USBD_LL_DevConnected(h->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *h) { USBD_LL_DevDisconnected(h->pData); }

static USBD_StatusTypeDef st(HAL_StatusTypeDef s) {
  return s == HAL_OK ? USBD_OK : (s == HAL_BUSY ? USBD_BUSY : USBD_FAIL);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev) {
  hpcd.pData = pdev;
  pdev->pData = &hpcd;
  hpcd.Instance = USB;
  hpcd.Init.dev_endpoints = 8;
  hpcd.Init.speed = PCD_SPEED_FULL;
  hpcd.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd.Init.low_power_enable = DISABLE;
  hpcd.Init.lpm_enable = DISABLE;
  hpcd.Init.battery_charging_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd) != HAL_OK) return USBD_FAIL;
  /* packet memory layout (same as CubeMX CDC default) */
  HAL_PCDEx_PMAConfig(&hpcd, 0x00, PCD_SNG_BUF, 0x18);
  HAL_PCDEx_PMAConfig(&hpcd, 0x80, PCD_SNG_BUF, 0x58);
  HAL_PCDEx_PMAConfig(&hpcd, CDC_IN_EP, PCD_SNG_BUF, 0xC0);
  HAL_PCDEx_PMAConfig(&hpcd, CDC_OUT_EP, PCD_SNG_BUF, 0x110);
  HAL_PCDEx_PMAConfig(&hpcd, CDC_CMD_EP, PCD_SNG_BUF, 0x100);
  return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev) { return st(HAL_PCD_DeInit(pdev->pData)); }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev) { return st(HAL_PCD_Start(pdev->pData)); }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev) { return st(HAL_PCD_Stop(pdev->pData)); }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep, uint8_t type, uint16_t mps) {
  return st(HAL_PCD_EP_Open(pdev->pData, ep, mps, type));
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep) { return st(HAL_PCD_EP_Close(pdev->pData, ep)); }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep) { return st(HAL_PCD_EP_Flush(pdev->pData, ep)); }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep) { return st(HAL_PCD_EP_SetStall(pdev->pData, ep)); }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep) { return st(HAL_PCD_EP_ClrStall(pdev->pData, ep)); }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep) {
  PCD_HandleTypeDef *h = pdev->pData;
  return (ep & 0x80) ? h->IN_ep[ep & 0x7F].is_stall : h->OUT_ep[ep & 0x7F].is_stall;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t a) { return st(HAL_PCD_SetAddress(pdev->pData, a)); }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep, uint8_t *buf, uint16_t n) {
  return st(HAL_PCD_EP_Transmit(pdev->pData, ep, buf, n));
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep, uint8_t *buf, uint16_t n) {
  return st(HAL_PCD_EP_Receive(pdev->pData, ep, buf, n));
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep) { return HAL_PCD_EP_GetRxCount(pdev->pData, ep); }
void USBD_LL_Delay(uint32_t d) { HAL_Delay(d); }

static uint32_t cdc_mem[(sizeof(USBD_CDC_HandleTypeDef) / 4) + 1];
void *USBD_static_malloc(uint32_t size) { (void)size; return cdc_mem; }
void USBD_static_free(void *p) { (void)p; }

/* ---------------------------------------------------------------------- */
/* Descriptors                                                              */
/* ---------------------------------------------------------------------- */
#define VID 0x0483  /* ST VCP ids: Windows/macOS/Linux bind their CDC driver */
#define PID 0x5740

static uint8_t dev_desc[USB_LEN_DEV_DESC] __attribute__((aligned(4))) = {
  0x12, USB_DESC_TYPE_DEVICE, 0x00, 0x02, 0x02, 0x02, 0x00, USB_MAX_EP0_SIZE,
  LOBYTE(VID), HIBYTE(VID), LOBYTE(PID), HIBYTE(PID), 0x00, 0x02,
  USBD_IDX_MFC_STR, USBD_IDX_PRODUCT_STR, USBD_IDX_SERIAL_STR, USBD_MAX_NUM_CONFIGURATION};
static uint8_t lang_desc[USB_LEN_LANGID_STR_DESC] __attribute__((aligned(4))) = {
  USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING, 0x09, 0x04};
static uint8_t str_buf[USBD_MAX_STR_DESC_SIZ] __attribute__((aligned(4)));

static uint8_t *d_dev(USBD_SpeedTypeDef s, uint16_t *len) { *len = sizeof dev_desc; return dev_desc; }
static uint8_t *d_lang(USBD_SpeedTypeDef s, uint16_t *len) { *len = sizeof lang_desc; return lang_desc; }
static uint8_t *str(const char *s, uint16_t *len) { USBD_GetString((uint8_t *)s, str_buf, len); return str_buf; }
static uint8_t *d_mfc(USBD_SpeedTypeDef s, uint16_t *len) { return str("Omgevingsmonitor", len); }
static uint8_t *d_prod(USBD_SpeedTypeDef s, uint16_t *len) { return str("Omgevingsmonitor ESP bridge", len); }
static uint8_t *d_cfg(USBD_SpeedTypeDef s, uint16_t *len) { return str("CDC", len); }
static uint8_t *d_itf(USBD_SpeedTypeDef s, uint16_t *len) { return str("ESP32-C3 UART0", len); }
static uint8_t *d_serial(USBD_SpeedTypeDef s, uint16_t *len) {
  static char sn[13];
  uint32_t u = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
  for (int i = 0; i < 8; i++) sn[i] = "0123456789ABCDEF"[(u >> (28 - 4 * i)) & 0xF];
  sn[8] = 0;
  return str(sn, len);
}
static USBD_DescriptorsTypeDef descriptors = {d_dev, d_lang, d_mfc, d_prod, d_serial, d_cfg, d_itf};

/* ---------------------------------------------------------------------- */
/* CDC interface                                                            */
/* ---------------------------------------------------------------------- */
static uint8_t rx_pkt[CDC_DATA_FS_MAX_PACKET_SIZE];
static uint8_t tx_pkt[CDC_DATA_FS_MAX_PACKET_SIZE];
static volatile uint32_t rx_len;            /* >0: packet waiting for UART */
static volatile uint32_t new_baud;
static volatile bool lines_changed;
static volatile bool dtr, rts;
static uint8_t line_coding[7] = {0x00, 0xC2, 0x01, 0x00, 0, 0, 8}; /* 115200 8N1 */

static int8_t cdc_init(void) {
  USBD_CDC_SetTxBuffer(&husb, tx_pkt, 0);
  USBD_CDC_SetRxBuffer(&husb, rx_pkt);
  return USBD_OK;
}
static int8_t cdc_deinit(void) { return USBD_OK; }

static int8_t cdc_control(uint8_t cmd, uint8_t *pbuf, uint16_t length) {
  switch (cmd) {
    case CDC_SET_LINE_CODING:
      memcpy(line_coding, pbuf, 7);
      new_baud = (uint32_t)pbuf[0] | (uint32_t)pbuf[1] << 8 | (uint32_t)pbuf[2] << 16 | (uint32_t)pbuf[3] << 24;
      break;
    case CDC_GET_LINE_CODING:
      memcpy(pbuf, line_coding, 7);
      break;
    case CDC_SET_CONTROL_LINE_STATE: {
      USBD_SetupReqTypedef *req = (USBD_SetupReqTypedef *)pbuf;
      dtr = req->wValue & 1;
      rts = req->wValue & 2;
      lines_changed = true;
      break;
    }
    default:
      break;
  }
  return USBD_OK;
}

static int8_t cdc_receive(uint8_t *buf, uint32_t *len) {
  rx_len = *len; /* main loop forwards it, then re-arms reception (flow control) */
  return USBD_OK;
}

static USBD_CDC_ItfTypeDef cdc_fops = {cdc_init, cdc_deinit, cdc_control, cdc_receive};

/* ---------------------------------------------------------------------- */

void bridge_run(void) {
  board_clock_usb();
  board_led(LED_BLUE);

  esp_power_on(true); /* start in download mode; DTR/RTS take over later */
  esp_uart_open(115200);

  USBD_Init(&husb, &descriptors, DEVICE_FS);
  USBD_RegisterClass(&husb, &USBD_CDC);
  USBD_CDC_RegisterInterface(&husb, &cdc_fops);
  USBD_Start(&husb);

  uint32_t last_flush = HAL_GetTick();
  uint32_t last_activity = HAL_GetTick();
  uint32_t usb_gone_since = 0;
  bool button_released = false;
  uint16_t tx_n = 0;
  for (;;) {
    lp_kick();
    uint32_t now = HAL_GetTick();

    /* --- automatic exits: all restart into normal mode ---------------- */
    if (!board_usb_present()) { /* cable unplugged */
      if (!usb_gone_since) usb_gone_since = now;
      if (now - usb_gone_since > 500) NVIC_SystemReset();
    } else {
      usb_gone_since = 0;
    }
    if (!board_button_pressed()) {
      button_released = true; /* it was held to enter the bridge */
    } else if (button_released) {
      HAL_Delay(30);
      if (board_button_pressed()) { /* fresh press of User/VOC */
        board_led(LED_GREEN);
        while (board_button_pressed()) lp_kick();
        NVIC_SystemReset();
      }
    }
    /* only host activity counts: ESPHome's hello stream must not keep us here */
    if (rx_len || lines_changed || new_baud) last_activity = now;
    if (now - last_activity > BRIDGE_IDLE_MS) NVIC_SystemReset();

    if (lines_changed) {
      lines_changed = false;
      /* classic auto-reset: EN low only when RTS && !DTR, IO9 low only when DTR && !RTS */
      bool en = !(rts && !dtr);
      bool boot = !(dtr && !rts);
      esp_set_lines(en, boot);
    }
    if (new_baud) {
      uint32_t b = new_baud;
      new_baud = 0;
      if (b >= 1200 && b <= 2000000) esp_uart_open(b);
    }
    if (rx_len) { /* host -> ESP */
      esp_uart_write(rx_pkt, (uint16_t)rx_len);
      rx_len = 0;
      USBD_CDC_ReceivePacket(&husb);
    }

    /* ESP -> host: pack bytes, flush when full or after 1 ms idle */
    USBD_CDC_HandleTypeDef *cdc = (USBD_CDC_HandleTypeDef *)husb.pClassData;
    if (cdc && cdc->TxState == 0) {
      int c;
      while (tx_n < sizeof tx_pkt && (c = esp_uart_getc()) >= 0) tx_pkt[tx_n++] = (uint8_t)c;
      if (tx_n && (tx_n == sizeof tx_pkt || HAL_GetTick() - last_flush >= 1)) {
        USBD_CDC_SetTxBuffer(&husb, tx_pkt, tx_n);
        if (USBD_CDC_TransmitPacket(&husb) == USBD_OK) tx_n = 0;
        last_flush = HAL_GetTick();
      }
    }
  }
}
