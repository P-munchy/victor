 /* #include <stdbool.h> */
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <termios.h>
/* #include <string.h> */



#include "schema/messages.h"
#include "spine_crc.h"
#include "spine_hal.h"


typedef uint32_t crc_t;


#define SKIP_CRC_CHECK 0

#define SPINE_MAX_BYTES 1048

#define BODY_TAG_PREFIX ((uint8_t*)&SyncKey)
#define SPINE_TAG_LEN sizeof(SpineSync)
#define SPINE_PID_LEN sizeof(PayloadId)
#define SPINE_LEN_LEN sizeof(uint16_t)
#define SPINE_HEADER_LEN (SPINE_TAG_LEN+SPINE_PID_LEN+SPINE_LEN_LEN)
#define SPINE_CRC_LEN sizeof(crc_t)

static_assert(1, "true");
static_assert(SPINE_HEADER_LEN == sizeof(struct SpineMessageHeader), "bad define");
static_assert(SPINE_MAX_BYTES >= SPINE_HEADER_LEN + sizeof(struct BodyToHead) + SPINE_CRC_LEN, "bad define");
static_assert(SPINE_MAX_BYTES >= SPINE_HEADER_LEN + sizeof(struct HeadToBody) + SPINE_CRC_LEN, "bad_define");


static const SpineSync SyncKey = SYNC_BODY_TO_HEAD;

static struct HalGlobals {
  uint8_t inbuffer[SPINE_MAX_BYTES];
  struct SpineMessageHeader outheader;
  int fd;
  int errcount;
} gHal;


/************* Error Handling *****************/
#define spine_error(code, fmt, args...)   (LOGE( fmt, ##args)?(code):(code))
#ifdef CONSOLE_DEBUG_PRINTF
#define spine_debug(fmt, args...)  printf(fmt, ##args)
#else
#define spine_debug(fmt, args...)  (LOGD( fmt, ##args))
#endif

#define EXTENDED_SPINE_DEBUG 0
#if EXTENDED_SPINE_DEBUG
#define spine_debug_x spine_debug
#else
#define spine_debug_x(fmt, args...)
#endif

/************* SERIAL INTERFACE ***************/

static void hal_serial_close()
{
  LOGD("close(fd = %d)", gHal.fd);
  //TODO: restore?:  tcsetattr(gHal.fd, TCSANOW, &gHal.oldcfg))
  close(gHal.fd);
  gHal.fd = 0;
}


SpineErr hal_serial_open(const char* devicename, long baudrate)
{
  if (gHal.fd != 0) {
    return spine_error(err_ALREADY_OPEN, "hal serial port in use, close other first");
  }

  spine_debug("opening serial port\n");

  gHal.fd = open(devicename, O_RDWR | O_NONBLOCK);
  if (gHal.fd < 0) {
    return spine_error(err_CANT_OPEN_FILE, "Can't open %s", devicename);
  }

  spine_debug("configuring serial port\n");

  /* Configure device */
  {
    struct termios cfg;
    if (tcgetattr(gHal.fd, &cfg)) {
      hal_serial_close();
      return spine_error(err_TERMIOS_FAIL, "tcgetattr() failed");
    }
//?    memcpy(gHal.oldcfg,cfg,sizeof(gHal.oldcfg)); //make backup;

    cfmakeraw(&cfg);

    platform_set_baud(gHal.fd, cfg, baudrate);

    cfg.c_cflag |= (CS8 | CSTOPB);    // Use N82 bit words

    LOGD("configuring port %s (fd=%d)", devicename, gHal.fd);

    if (tcsetattr(gHal.fd, TCSANOW, &cfg)) {
      hal_serial_close();
      return spine_error(err_TERMIOS_FAIL, "tcsetattr() failed");
    }
  }
  spine_debug("serial port OK\n");
  return err_OK;
}


int hal_serial_read(uint8_t* buffer, int len)   //->bytes_recieved
{

  int result = read(gHal.fd, buffer, len);
  if (result < 0) {
    if (errno == EAGAIN) { //nonblocking no-data
      usleep(1000); //wait a msec.
      result = 0; //not an error
    }
  }
  int i;
  return result;
}


int hal_serial_send(const uint8_t* buffer, int len)
{
  if (len) {
    return write(gHal.fd, buffer, len);
  }
  return 0;
}


/************* PROTOCOL SYNC ***************/

enum MsgDir {
  dir_SEND,
  dir_READ,
};

//checks for valid tag, returns expected length, -1 on err
static int get_payload_len(PayloadId payload_type, enum MsgDir dir)
{
  switch (payload_type) {
  case PAYLOAD_MODE_CHANGE:
    return 0;
    break;
  case PAYLOAD_DATA_FRAME:
    return (dir == dir_SEND) ? sizeof(struct HeadToBody) : sizeof(struct BodyToHead);
    break;
  case PAYLOAD_VERSION:
    return (dir == dir_SEND) ? 0 : sizeof(struct VersionInfo);
    break;
  case PAYLOAD_ACK:
    return sizeof(struct AckMessage);
    break;
  case PAYLOAD_ERASE:
    return 0;
    break;
  case PAYLOAD_VALIDATE:
    return 0;
    break;
  case PAYLOAD_DFU_PACKET:
    return sizeof(struct WriteDFU);
    break;
  default:
    break;
  }
  return -1;
}




//Creates header for frame
static const uint8_t* spine_construct_header(PayloadId payload_type,  uint16_t payload_len)
{
  int expected_len = get_payload_len(payload_type, dir_SEND);
  assert(expected_len >= 0); //valid type
  assert(expected_len == payload_len);
  assert(payload_len <= (SPINE_MAX_BYTES - SPINE_HEADER_LEN - SPINE_CRC_LEN));

  struct SpineMessageHeader* hdr = &gHal.outheader;
  hdr->sync_bytes = SYNC_HEAD_TO_BODY;
  hdr->payload_type = payload_type;
  hdr->bytes_to_follow = payload_len;
  return (uint8_t*) hdr;
}


//Function: Examines `buf[idx]` and determines if it is part of sync seqence.
//Prereq: The first `idx` chars in `buf` are partial valid sync sequence.
//Returns: Length of partial valid sync sequence. possible values = 0..idx+1
static int spine_sync(const uint8_t* buf, unsigned int idx)
{
  if (idx < SPINE_TAG_LEN) {
    if (buf[idx] != BODY_TAG_PREFIX[idx]) {
      return 0; //none of the characters so far are good.
    }
  }
  idx++; //accept rest of characters unless proven otherwise
  if (idx == SPINE_HEADER_LEN) {
    struct SpineMessageHeader* candidate = (struct SpineMessageHeader*)buf;
    int expected_len = get_payload_len(candidate->payload_type, dir_READ);
    if (expected_len < 0 || (expected_len != candidate->bytes_to_follow)) {
      LOGI("spine_header %x %x %x : %d", candidate->sync_bytes,
           candidate->payload_type,
           candidate->bytes_to_follow,
           expected_len);
      //bad header,
      //we need to check the length bytes for the beginning of a sync word
      unsigned int pos = idx - SPINE_LEN_LEN;
      int match = 0;
      while (pos < idx) {
        if (buf[pos] == BODY_TAG_PREFIX[match]) {
          match++;
          pos++;
        }
        else if (match > 0) {  match = 0; }
        else { pos++; }
      }
      // at this point we know that the last `match` chars rcvd into length position are a possible sync tag.
      // but there is no need to copy them to beginning of `buf` b/c we can't get here unless
      //  buf already contains good tag. So just return the number of matches
      return match;
    }
  }
  return idx;
}


/************* PUBLIC INTERFACE ***************/

SpineErr hal_init(const char* devicename, long baudrate)
{
  gHal.errcount = 0;
  gHal.fd = 0;
  return hal_serial_open(devicename, baudrate);
}


//gathers most recently queued frame,
//Spins until valid frame header is recieved.
const struct SpineMessageHeader* hal_read_frame()
{
  static uint16_t loopcount = 0;
  unsigned int index = 0;

  //spin here pulling single characters until whole sync rcvd
  while (index < SPINE_HEADER_LEN) {

    int rslt = hal_serial_read(gHal.inbuffer + index, 1);
    if (rslt > 0) {
      index = spine_sync(gHal.inbuffer, index);
    }
    else if (rslt < 0) {
      if ((gHal.errcount++ & 0x3FF) == 0) { //TODO: at somepoint maybe we handle this?
        LOGI("spine_read_error %d", rslt);
      }
    }
  } //endwhile


  //At this point we have a valid message header. (spine_sync rejects bad lengths and payloadTypes)
  // Collect the right number of bytes.
  unsigned int payload_length = ((struct SpineMessageHeader*)gHal.inbuffer)->bytes_to_follow;
  unsigned int total_message_length = SPINE_HEADER_LEN + payload_length + SPINE_CRC_LEN;

  spine_debug_x("%d byte payload\n", payload_length);

  while (index < total_message_length) {
    int rslt = hal_serial_read(gHal.inbuffer + index, total_message_length - index);
    //spine_debug_x("%d bytes rcvd\n", rslt);
    if (rslt >= 0) {
      index += rslt;
    }
    else { // rslt < 0
      if ((gHal.errcount++ & 0x3FF) == 0) { //TODO: at somepoint maybe we handle this?
        LOGI("spine_payload_read_error %d", rslt);
      }
    }
  }

  spine_debug_x("%d bytes rcvd\n", index);

  //now we just have to validate CRC;
  crc_t expected_crc = *((crc_t*)(gHal.inbuffer + SPINE_HEADER_LEN + payload_length));
  crc_t true_crc = calc_crc(gHal.inbuffer + SPINE_HEADER_LEN, payload_length);
  if (expected_crc != true_crc && !SKIP_CRC_CHECK) {
    //TODO: if we need to recover maximal data after dropped bytes,
    //      then we need to run this whole payload through the sync detector again.
    //      For now, move on to following frame.
    spine_debug("\nspine_crc_error: calc %08x vs data %08x\n", true_crc, expected_crc);
    LOGI("spine_crc_error %08x != %08x", true_crc, expected_crc);
    unsigned int i;
    unsigned int dropped_bytes = 0;
    for (i = 0; i < payload_length + SPINE_CRC_LEN; i++) {
      spine_debug_x(" %02x", gHal.inbuffer[i + SPINE_HEADER_LEN]);
      if (gHal.inbuffer[i + SPINE_HEADER_LEN] == 0xAA) {
        dropped_bytes = i;
      }
    }
    dropped_bytes = payload_length + SPINE_CRC_LEN - dropped_bytes;
    spine_debug("\n%u dropped bytes\n", dropped_bytes);
    return NULL;
  }

  spine_debug_x("found frame!\r");
  return ((struct SpineMessageHeader*)gHal.inbuffer);
}


//pulls off frames until it gets one of matching type

const void* hal_get_frame(uint16_t type, int32_t timeout_ms)
{
  const struct SpineMessageHeader* hdr;
  do {
    hdr = hal_read_frame();
    if (timeout_ms>0 && --timeout_ms==0) {
      LOGE("TIMEOUT in hal_get_frame() TIMEOUT");
      return NULL;
    }
  }
  while (!hdr || hdr->payload_type != type);
  return hdr;
}

const void* hal_wait_for_frame(uint16_t type)
{
  const void* ret;
  do {
    ret = hal_get_frame(type, 0xFFFFffff);
  } while (!ret);
  return ret;
}


void hal_send_frame(PayloadId type, const void* data, int len)
{
  const uint8_t* hdr = spine_construct_header(type, len);
  crc_t crc = calc_crc(data, len);
  if (hdr) {
    hal_serial_send(hdr, SPINE_HEADER_LEN);
    hal_serial_send(data, len);
    hal_serial_send((uint8_t*)&crc, sizeof(crc));
  }
}

void hal_set_mode(int new_mode)
{
  printf("Sending Mode Change %x\n", PAYLOAD_MODE_CHANGE);
  hal_send_frame(PAYLOAD_MODE_CHANGE, NULL, 0);
}
