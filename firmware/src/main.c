/*
 * Bristol SEDS pico-tracker
 * Copyright (C) 2014  Richard Meadows <richardeoin>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "samd20.h"
#include "semihosting.h"
#include "hw_config.h"
#include "system/system.h"
#include "sercom/usart.h"
#include "system/port.h"
#include "tc/tc_driver.h"
#include "gps.h"
#include "mfsk.h"
#include "ubx_messages.h"
#include "system/wdt.h"
#include "timepulse.h"
#include "telemetry.h"
#include "si_trx.h"
#include "si_trx_defs.h"
#include "analogue.h"
#include "spi_bitbang.h"
#include "rtty.h"
#include "system/interrupt.h"

#define CALLSIGN	"UBSEDSx"

/* Set the modulation mode */
//#define RTTY
#define CONTESTIA


/**
 * Initialises the status LED
 */
static inline void led_init(void)
{
  port_pin_set_config(LED0_PIN,
		      PORT_PIN_DIR_OUTPUT,	/* Direction */
		      PORT_PIN_PULL_NONE,	/* Pull */
		      false);			/* Powersave */
  port_pin_set_output_level(LED0_PIN, 1);	/* LED is active low */
}
/**
 * Turns the status LED on
 */
static inline void led_on(void)
{
  port_pin_set_output_level(LED0_PIN, 0);	/* LED is active low */
}
/**
 * Turns the status lED off
 */
static inline void led_off(void)
{
  port_pin_set_output_level(LED0_PIN, 1);	/* LED is active low */
}



void set_timer(uint32_t time)
{
  bool capture_channel_enables[]    = {false, false};
  uint32_t compare_channel_values[] = {time, 0x0000};

  tc_init(TC2,
	  GCLK_GENERATOR_0,
	  TC_COUNTER_SIZE_32BIT,
	  TC_CLOCK_PRESCALER_DIV1,
	  TC_WAVE_GENERATION_NORMAL_FREQ,
	  TC_RELOAD_ACTION_GCLK,
	  TC_COUNT_DIRECTION_UP,
	  TC_WAVEFORM_INVERT_OUTPUT_NONE,
	  false,			/* Oneshot = false */
	  false,			/* Run in standby = false */
	  0x0000,			/* Initial value */
	  time+1,			/* Top value */
	  capture_channel_enables,	/* Capture Channel Enables */
	  compare_channel_values);	/* Compare Channels Values */

  struct tc_events ev;
  memset(&ev, 0, sizeof(ev));
  ev.generate_event_on_compare_channel[0] = true;
  ev.event_action = TC_EVENT_ACTION_RETRIGGER;

  tc_enable_events(TC2, &ev);

  irq_register_handler(TC2_IRQn, 3);

  tc_enable(TC2);
  tc_start_counter(TC2);
}

void wdt_init() {
  /* 64 seconds timeout. So 2^(15+6) cycles of the wdt clock */
  system_gclk_gen_set_config(WDT_GCLK,
			     GCLK_SOURCE_OSCULP32K, /* Source 		*/
			     false,		/* High When Disabled	*/
			     128,		/* Division Factor	*/
			     false,		/* Run in standby	*/
			     true);		/* Output Pin Enable	*/
  system_gclk_gen_enable(WDT_GCLK);

  /* Set the watchdog timer. On 256Hz gclk 4  */
  wdt_set_config(true,			/* Lock WDT		*/
  		 true,			/* Enable WDT		*/
  		 GCLK_GENERATOR_4,	/* Clock Source		*/
  		 WDT_PERIOD_16384CLK,	/* Timeout Period	*/
  		 WDT_PERIOD_NONE,	/* Window Period	*/
  		 WDT_PERIOD_NONE);	/* Early Warning Period	*/
}

/**
 * Power Management
 */
void powermananger_init(void)
{
  system_apb_clock_clear_mask(SYSTEM_CLOCK_APB_APBA,
			      PM_APBAMASK_EIC | /* EIC is unused */
			      PM_APBAMASK_RTC); /* RTC is unused */
}


/**
 * Telemetry String
 * =============================================================================
 */
void output_telemetry_string(void)
{
  double lat_fmt = 0.0;
  double lon_fmt = 0.0;
  uint32_t altitude = 0;

  /**
   * Callsign, Time
   * ---------------------------------------------------------------------------
   */

  /* GPS Time */
  gps_update_time();

  /* Sleep Wait */
  while (gps_update_time_pending()) {
    system_sleep();
  }
  for (int i = 0; i < 100*1000; i++);

  /* Time */
  struct ubx_nav_timeutc time = gps_get_nav_timeutc();
  uint8_t hours = time.payload.hour;
  uint8_t minutes = time.payload.min;
  uint8_t seconds = time.payload.sec;

  /* init double buffers */
  ARRAY_DBUFFER_INIT(&rtty_dbuffer_string);

  /* sprintf - initial string */
  uint16_t len = sprintf(ARRAY_DBUFFER_WRITE_PTR(&rtty_dbuffer_string),
			 "$$%s,%02u:%02u:%02u,",
			 CALLSIGN, hours, minutes, seconds);

  /* swap buffers */
  ARRAY_DBUFFER_SWAP(&rtty_dbuffer_string);

  /* start */
  rtty_start();

  /**
   * Position, Status, Analogue, Checksum
   * ---------------------------------------------------------------------------
   */

  /* Analogue */
  float battery = get_battery();
  float temperature = si_trx_get_temperature();

  /* Sleep Wait */
  while (rtty_get_index() < (len - 4)) {
    system_sleep();
  }

  /* Request updates from the gps */
  gps_update_position();
  if (gps_is_locked()) {
    led_on();
  } else {
    led_off();
  }

  /* Wait for the gps update. Move on if it's urgent */
  while (gps_update_position_pending() && rtty_get_index() < (len - 1)) {
    system_sleep();
  }

  if (gps_is_locked()) {
    led_off();
  } else {
    led_on();
  }

  /* GPS Status */
  struct ubx_nav_sol sol = gps_get_nav_sol();
  uint8_t lock = sol.payload.gpsFix;
  uint8_t satillite_count = sol.payload.numSV;

  /* GPS Position */
  if (lock == 0x2 || lock == 0x3 || lock == 0x4) {
    struct ubx_nav_posllh pos = gps_get_nav_posllh();
    lat_fmt = (double)pos.payload.lat / 10000000.0;
    lon_fmt = (double)pos.payload.lon / 10000000.0;
    altitude = pos.payload.height / 1000;
  }

  /* sprintf - full string */
  len = sprintf(ARRAY_DBUFFER_WRITE_PTR(&rtty_dbuffer_string),
		"$$%s,%02u:%02u:%02u,%02.6f,%03.6f,%ld,%u,%.2f,%.1f",
		CALLSIGN, hours, minutes, seconds, lat_fmt, lon_fmt,
		altitude, satillite_count, battery, temperature);

  /* sprintf - checksum */
  len += sprintf(ARRAY_DBUFFER_WRITE_PTR(&rtty_dbuffer_string) + len,
		 "*%04X\n",
		 crc_checksum(ARRAY_DBUFFER_WRITE_PTR(&rtty_dbuffer_string)));

  /* swap buffers */
  ARRAY_DBUFFER_SWAP(&rtty_dbuffer_string);

  /**
   * End
   * ---------------------------------------------------------------------------
   */

  /* Set the final length */
  rtty_set_length(len);

  /* Sleep Wait */
  while (rtty_active()) {
    system_sleep();
  }
}

uint8_t started = 0;
/* We transmit 64 tones */
int8_t tones[] = {
  0x1a, 0x0c, 0x07, 0x1b, 0x00, 0x13, 0x12, 0x0d,
  0x12, 0x0d, 0x1f, 0x11, 0x1c, 0x1b, 0x18, 0x1e,
  0x0e, 0x02, 0x0e, 0x0a, 0x05, 0x08, 0x13, 0x13,
  0x1f, 0x10, 0x09, 0x0d, 0x07, 0x10, 0x1a, 0x1c,
  0x0b, 0x10, 0x01, 0x0e, 0x0f, 0x19, 0x0a, 0x1d,
  0x06, 0x1b, 0x0c, 0x13, 0x02, 0x0f, 0x06, 0x0c,
  0x1d, 0x15, 0x17, 0x09, 0x15, 0x14, 0x1f, 0x00,
  0x08, 0x06, 0x05, 0x09, 0x12, 0x13, 0x1e, 0x0a
};

/**
 * MAIN
 * =============================================================================
 */
int main(void)
{
  /**
   * Internal initialisation
   * ---------------------------------------------------------------------------
   */

  /* Clock up to 14MHz with 0 wait states */
  system_flash_set_waitstates(SYSTEM_WAIT_STATE_1_8V_14MHZ);

  /* Up the clock rate to 4MHz */
  system_clock_source_osc8m_set_config(SYSTEM_OSC8M_DIV_2, /* Prescaler */
				       false,		   /* Run in Standby */
				       false);		   /* Run on Demand */

  /* Restart the GCLK Module */
  system_gclk_init();

  /* Get the current CPU Clock */
  SystemCoreClock = system_cpu_clock_get_hz();

  /* Configure Sleep Mode */
  //system_set_sleepmode(SYSTEM_SLEEPMODE_STANDBY);
  system_set_sleepmode(SYSTEM_SLEEPMODE_IDLE_2); /* Disable CPU, AHB and APB */

  /* Configure the Power Manager */
  //powermananger_init();



  /**
   * System initialisation
   * ---------------------------------------------------------------------------
   */

  /* Set the wdt here. We should get to the first reset in one min */
  //wdt_init();
  //wdt_reset_count();

  led_init();
  gps_init();

  /* Initialise Si4060 interface */
  si_trx_init();

  /* Start transmitting */
#ifdef RTTY
  /* RTTY Mode: We modulate using the external pin */
  si_trx_on(SI_MODEM_MOD_TYPE_2FSK, 0);
#endif
#ifdef CONTESTIA
  /* Contestia: We switch channel to modulate */
  si_trx_on(SI_MODEM_MOD_TYPE_CW, 31.25);
#endif




  /* Prepare a tone sequence */
  char hello[] = "HELLO";
//  olivia_mfsk_encode_block(hello, tones);
  contestia_mfsk_encode_block(hello, tones);


  started = 1;


  /* Timer 0 clocks out data */
#ifdef RTTY
  timer0_tick_init(50);
#endif
#ifdef CONTESTIA
  timer0_tick_init(31.25);
#endif

  led_on();

  while (1) {
    /* Watchdog */
    //wdt_reset_count();

    /* Send the next packet */
    output_telemetry_string();
  }
}

uint32_t tone_index = 0;
uint8_t binary_code;
uint8_t grey_code;

/**
 * Called at the symbol rate
 */
void TC0_Handler(void)
{
  if (tc_get_status(TC0) & TC_STATUS_CHANNEL_0_MATCH) {
    tc_clear_status(TC0, TC_STATUS_CHANNEL_0_MATCH);

#ifdef RTTY
    rtty_tick();
#endif
#ifdef CONTESTIA
    if (started) {
      if (tone_index < 32) {

        binary_code = tones[tone_index];
        grey_code = (binary_code >> 1) ^ binary_code;

        si_trx_switch_channel(grey_code);

      } else if (tone_index < 64) {

        si_trx_state_ready();

      /* } else if (tone_index < 96) { */

      /*   si_trx_switch_channel((tone_index & 1) ? 0 : 31); */

      }

      tone_index++;
      if (tone_index >= 64)
      {
        tone_index = 0;
      }
    }
#endif
  }
}
