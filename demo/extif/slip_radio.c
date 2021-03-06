/*
 * Copyright (c) 2011, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * \file
 *         Slip-radio driver
 * \author
 *         Niclas Finne <nfi@sics.se>
 *         Joakim Eriksson <joakime@sics.se>
 */

#include "emb6.h"
#include "bsp.h"
#include "uip.h"
#include "uip-ds6.h"
#include "slip.h"
#include "packetbuf.h"

#define DEBUG DEBUG_NONE
#include "uip-debug.h"
#include "cmd.h"
#include "slip_radio.h"
#include "packetutils.h"

#ifdef SLIP_RADIO_CONF_SENSORS
extern const struct slip_radio_sensors SLIP_RADIO_CONF_SENSORS;
#endif

void slip_send_packet(const uint8_t *ptr, int len);

 /* max 16 packets at the same time??? */
uint8_t packet_ids[16];
int packet_pos;
static int slip_radio_cmd_handler(const uint8_t *data, int len);


int cmd_handler_rf230(const uint8_t *data, int len);

/*---------------------------------------------------------------------------*/
#ifdef CMD_CONF_HANDLERS
CMD_HANDLERS(CMD_CONF_HANDLERS);
#else
CMD_HANDLERS(slip_radio_cmd_handler);
#endif
/*---------------------------------------------------------------------------*/
static void
packet_sent(void *ptr, int status, int transmissions)
{
  uint8_t buf[20];
  uint8_t sid;
  int pos;
  sid = *((uint8_t *)ptr);
  PRINTF("Slip-radio: packet sent! sid: %d, status: %d, tx: %d\n",
       sid, status, transmissions);
  /* packet callback from lower layers */
  /*  neighbor_info_packet_sent(status, transmissions); */
  pos = 0;
  buf[pos++] = '!';
  buf[pos++] = 'R';
  buf[pos++] = sid;
  buf[pos++] = status; /* one byte ? */
  buf[pos++] = transmissions;
  cmd_send(buf, pos);
}
/*---------------------------------------------------------------------------*/
static int
slip_radio_cmd_handler(const uint8_t *data, int len)
{
  int i;
  const s_ns_t * ps_ns = NULL;

  if(data[0] == '!') {
    /* should send out stuff to the radio - ignore it as IP */
    /* --- s e n d --- */
    if(data[1] == 'S') {
      int pos;
      packet_ids[packet_pos] = data[2];

      packetbuf_clear();
      pos = packetutils_deserialize_atts(&data[3], len - 3);
      if(pos < 0) {
        PRINTF("slip-radio: illegal packet attributes\n");
        return 1;
      }
      pos += 3;
      len -= pos;
      if(len > PACKETBUF_SIZE) {
        len = PACKETBUF_SIZE;
      }
      memcpy(packetbuf_dataptr(), &data[pos], len);
      packetbuf_set_datalen(len);

      PRINTF("slip-radio: sending %u (%d bytes)\n",
             data[2], packetbuf_datalen());

      ps_ns = emb6_get();

      if (ps_ns != NULL) {
          /* parse frame before sending to get addresses, etc. */
          ps_ns->frame->parse();
          ps_ns->dllsec->send(packet_sent, &packet_ids[packet_pos]);
      }

      packet_pos++;
      if(packet_pos >= sizeof(packet_ids)) {
          packet_pos = 0;
      }
      return 1;
    } else if(data[1] == 'R' && len == 2) {
      PRINTF("Rebooting\n");
      bsp_watchdog(EN_BSP_WD_RESET);
      return 1;
    }
  } else if(uip_buf[0] == '?') {
    PRINTF("Got request message of type %c\n", uip_buf[1]);
    if(data[1] == 'M' && len == 2) {
      /* this is just a test so far... just to see if it works */
      uip_buf[0] = '!';
      uip_buf[1] = 'M';
      for(i = 0; i < 8; i++) {
        uip_buf[2 + i] = uip_lladdr.addr[i];
      }
      uip_len = 10;
      cmd_send(uip_buf, uip_len);
      return 1;
    }
  }

  return 0;
}
/*---------------------------------------------------------------------------*/
void
slip_radio_cmd_output(const uint8_t *data, int data_len)
{
  slip_send_packet(data, data_len);
}
/*---------------------------------------------------------------------------*/
static void
slip_input_callback(void)
{
  cmd_input(uip_buf, uip_len);
  uip_len = 0;
}
/*---------------------------------------------------------------------------*/
int8_t demo_extifInit(void)
{
    bsp_periphIRQRegister( EN_HAL_PERIPHIRQ_SLIPUART_RX, slip_input_byte, NULL );
    slip_set_input_callback(slip_input_callback);
    packet_pos = 0;
    slip_init();
    return 0;
}

int8_t demo_extifConf(s_ns_t* p_netstk)
{
  int8_t ret = -1;

  if (p_netstk != NULL) {
    if (p_netstk->c_configured == 0) {
      p_netstk->hc     = &hc_driver_slipnet;
      p_netstk->frame  = &framer_noframer;
      p_netstk->dllsec = &dllsec_driver_null;
      p_netstk->dllc   = &dllc_driver_null;
      ret = 0;
    } else {
      if ((p_netstk->hc     == &hc_driver_slipnet) &&
          (p_netstk->frame  == &framer_noframer) &&
          (p_netstk->dllsec == &dllsec_driver_null) &&
          (p_netstk->dllc   == &dllc_driver_null)) {
        ret = 0;
      } else {
        p_netstk = NULL;
        ret = -1;
      }
    }
  }

  return ret;
}

/*---------------------------------------------------------------------------*/
//#if !SLIP_RADIO_CONF_NO_PUTCHAR
//#undef putchar
//int
//putchar(int c)
//{
//#define SLIP_END     0300
//  static char debug_frame = 0;
//
//  if(!debug_frame) {            /* Start of debug output */
//    printf("%d",SLIP_END);
//    printf("\r");     /* Type debug line == '\r' */
//    debug_frame = 1;
//  }
//
//  /* Need to also print '\n' because for example COOJA will not show
//     any output before line end */
//  printf("%d",(char)c);
//
//  /*
//   * Line buffered output, a newline marks the end of debug output and
//   * implicitly flushes debug output.
//   */
//  if(c == '\n') {
//      printf("%d",SLIP_END);
//      debug_frame = 0;
//  }
//  return c;
//}
//#endif
/*---------------------------------------------------------------------------*/
