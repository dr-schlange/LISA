/*
   LISA (v0.3.0)

   Copyright (c) 2026 Dr Schlange
   Licensed under GNU GPLv3
 */

// This file is LLM generated and manually modified
//
// UAC2 microphone: sends LISA's output to the host.
// TinyUSB reads its config while the core compiles, so this file is force
// included with -include (see the Makefile). Without that flag audio is off.

// Config macros: plain macros only, C and asm files read them. Skipped in the
// sketch (USB_AUDIO_SKETCH), the core never sees that copy.
#if !defined(USB_AUDIO_SKETCH) && !defined(USB_AUDIO_CFG_H)
#define USB_AUDIO_CFG_H
// Must equal SAMPLE_RATE, the core cannot see constants_config.h.
#define USB_AUDIO_RATE 44100
#define USB_AUDIO_CHANNELS 2
#define USB_AUDIO_BYTES 2

#define CFG_TUD_AUDIO 1
#define CFG_TUD_AUDIO_ENABLE_EP_IN 1

// Full speed: 1 ms frames, 44.1 samples per frame so 45 + 1 spare
#define CFG_TUD_AUDIO_EP_SZ_IN                                                 \
  TUD_AUDIO_EP_SIZE(0, USB_AUDIO_RATE, USB_AUDIO_BYTES, USB_AUDIO_CHANNELS)
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX CFG_TUD_AUDIO_EP_SZ_IN
// 8 packets (~8 ms). TinyUSB keeps the FIFO half full, core 1 writes in bursts.
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ (8 * CFG_TUD_AUDIO_EP_SZ_IN)
#endif

// Descriptor and code, only in the sketch
#if defined(USB_AUDIO_SKETCH) && !defined(USB_AUDIO_IMPL_H)
#define USB_AUDIO_IMPL_H
#include <Adafruit_TinyUSB.h>

#if CFG_TUD_AUDIO

static_assert(USB_AUDIO_RATE == SAMPLE_RATE,
              "USB audio rate must match the I2S rate");

#define UAC_CLK_ID 0x04
#define UAC_IT_ID 0x01
#define UAC_OT_ID 0x03

class UsbAudio : public Adafruit_USBD_Interface {
public:
  void begin() { TinyUSBDevice.addInterface(*this); }

  uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t *buf,
                                  uint16_t bufsize) override {
    (void)itfnum_deprecated;
    uint8_t itfnum = 0;
    uint8_t ep_in = 0;
    if (buf) {
      itfnum = TinyUSBDevice.allocInterface(2);
      ep_in = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    }
    const uint32_t lr =
        AUDIO20_CHANNEL_CONFIG_FRONT_LEFT | AUDIO20_CHANNEL_CONFIG_FRONT_RIGHT;
    uint8_t const desc[] = {
        TUD_AUDIO20_DESC_IAD(itfnum, 0x02, 0x00),
        TUD_AUDIO20_DESC_STD_AC(itfnum, 0x00, 0x00),
        TUD_AUDIO20_DESC_CS_AC(0x0200, AUDIO20_FUNC_MICROPHONE,
                               TUD_AUDIO20_DESC_CLK_SRC_LEN +
                                   TUD_AUDIO20_DESC_INPUT_TERM_LEN +
                                   TUD_AUDIO20_DESC_OUTPUT_TERM_LEN,
                               0x00),
        TUD_AUDIO20_DESC_CLK_SRC(
            UAC_CLK_ID, AUDIO20_CLOCK_SOURCE_ATT_INT_FIX_CLK,
            (AUDIO20_CTRL_R << AUDIO20_CLOCK_SOURCE_CTRL_CLK_FRQ_POS),
            UAC_IT_ID, 0x00),
        TUD_AUDIO20_DESC_INPUT_TERM(UAC_IT_ID, AUDIO_TERM_TYPE_IN_GENERIC_MIC,
                                    0x00, UAC_CLK_ID, USB_AUDIO_CHANNELS, lr,
                                    0x00, 0x0000, 0x00),
        TUD_AUDIO20_DESC_OUTPUT_TERM(UAC_OT_ID, AUDIO_TERM_TYPE_USB_STREAMING,
                                     0x00, UAC_IT_ID, UAC_CLK_ID, 0x0000, 0x00),
        // Streaming interface: alt 0 is idle, alt 1 streams
        TUD_AUDIO20_DESC_STD_AS_INT((uint8_t)(itfnum + 1), 0x00, 0x00, 0x00),
        TUD_AUDIO20_DESC_STD_AS_INT((uint8_t)(itfnum + 1), 0x01, 0x01, 0x00),
        TUD_AUDIO20_DESC_CS_AS_INT(
            UAC_OT_ID, AUDIO20_CTRL_NONE, AUDIO20_FORMAT_TYPE_I,
            AUDIO20_DATA_FORMAT_TYPE_I_PCM, USB_AUDIO_CHANNELS, lr, 0x00),
        TUD_AUDIO20_DESC_TYPE_I_FORMAT(USB_AUDIO_BYTES, USB_AUDIO_BYTES * 8),
        TUD_AUDIO20_DESC_STD_AS_ISO_EP(
            ep_in,
            (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS |
                      (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS |
                      (uint8_t)TUSB_ISO_EP_ATT_DATA),
            CFG_TUD_AUDIO_EP_SZ_IN, 0x01),
        TUD_AUDIO20_DESC_CS_AS_ISO_EP(
            AUDIO20_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO20_CTRL_NONE,
            AUDIO20_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0x0000)};

    const uint16_t len = sizeof(desc);
    if (buf) {
      if (bufsize < len)
        return 0;
      memcpy(buf, desc, len);
    }
    return len;
  }
};

static UsbAudio usb_audio;

static inline void setup_usb_audio() {
  // Default config buffer is 256 B, the total is ~350
  static uint8_t cfg_buffer[512];
  TinyUSBDevice.setConfigurationBuffer(cfg_buffer, sizeof(cfg_buffer));
  usb_audio.begin();
}

// Alt 1 streams, alt 0 is idle. Core 1 writes only while streaming, so the FIFO
// is empty when a stream starts (TinyUSB clears it after the close callback).
static volatile bool usb_audio_streaming = false;

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *req) {
  (void)rhport;
  const bool streaming = TU_U16_LOW(req->wValue) != 0;
  if (streaming)
    tud_audio_n_clear_ep_in_ff(0);
  usb_audio_streaming = streaming;
  return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,
                                   tusb_control_request_t const *req) {
  (void)rhport;
  (void)req;
  usb_audio_streaming = false;
  return true;
}

// The host asks the clock for its rate, it is fixed
bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *req) {
  const uint8_t ctrl = TU_U16_HIGH(req->wValue);
  const uint8_t entity = TU_U16_HIGH(req->wIndex);
  if (entity != UAC_CLK_ID)
    return false;

  if (ctrl == AUDIO20_CS_CTRL_SAM_FREQ) {
    if (req->bRequest == AUDIO20_CS_REQ_CUR) {
      static uint32_t rate = USB_AUDIO_RATE;
      // Not tud_control_xfer: TinyUSB takes the rate for packet sizing from here
      return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &rate,
                                                        sizeof(rate));
    }
    if (req->bRequest == AUDIO20_CS_REQ_RANGE) {
      static audio20_control_range_4_n_t(1) range;
      range.wNumSubRanges = 1;
      range.subrange[0].bMin = USB_AUDIO_RATE;
      range.subrange[0].bMax = USB_AUDIO_RATE;
      range.subrange[0].bRes = 0;
      return tud_audio_buffer_and_schedule_control_xfer(rhport, req, &range,
                                                        sizeof(range));
    }
  }
  return false;
}

// Core 1, once per block. frames[] is (left << 16) | right, USB wants L,R pairs.
// Only fills the FIFO (no lock), the USB IRQ on core 0 drains it.
static __force_inline void usb_audio_push(const uint32_t frames[AUDIO_BLOCK]) {
  static int16_t pcm[AUDIO_BLOCK * 2];
  // The FIFO is overwritable: skip the block rather than overwrite data the USB
  // IRQ may be reading
  tu_fifo_t *ff = tud_audio_n_get_ep_in_ff(0);
  if (!usb_audio_streaming || !tud_audio_mounted() || !ff ||
      tu_fifo_remaining(ff) < sizeof(pcm))
    return;
  for (int i = 0; i < AUDIO_BLOCK; i++) {
    pcm[2 * i] = (int16_t)(frames[i] >> 16);
    pcm[2 * i + 1] = (int16_t)(frames[i] & 0xffff);
  }
  tud_audio_write(pcm, sizeof(pcm));
}

#else

static inline void setup_usb_audio() {}
static inline void usb_audio_push(const uint32_t *) {}

#endif

#endif
