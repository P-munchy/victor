#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "bluetooth.h"
#include "tasks.h"
#include "messages.h"
#include "storage.h"

#include "publickeys.h"

#include "clad/robotInterface/messageRobotToEngine.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"
#include "clad/robotInterface/messageEngineToRobot.h"
#include "clad/robotInterface/messageEngineToRobot_send_helper.h"

//#define DISABLE_TASK_CHECK
//#define DISABLE_AUTHENTIFICATION

#define member_size(type, member) sizeof(((type *)0)->member)
  
// Softdevice settings
static const nrf_clock_lfclksrc_t m_clock_source = NRF_CLOCK_LFCLKSRC_SYNTH_250_PPM;
static bool  m_sd_enabled;

// Service settings
uint16_t                    Bluetooth::service_handle;
ble_gatts_char_handles_t    Bluetooth::receive_handles;
ble_gatts_char_handles_t    Bluetooth::transmit_handles;

// Current connection state settings
uint16_t                    Bluetooth::conn_handle;
static uint8_t              m_nonce[member_size(Anki::Cozmo::HelloPhone, nonce)];
static bool                 m_authenticated;
static bool                 m_task_enabled;

static const int MAX_CLAD_MESSAGE_LENGTH = 0x100 - 2;
static const int MAX_CLAD_OUTBOUND_SIZE = MAX_CLAD_MESSAGE_LENGTH - AES_KEY_LENGTH;
static const uint8_t HELLO_SIGNATURE[] = { 'C', 'Z', 'M', '0' };

struct BLE_CladBuffer {
  uint16_t  PADDING;
  
  union {
    uint8_t     raw[MAX_CLAD_MESSAGE_LENGTH + 2];
    struct {
      uint8_t   length;
      uint8_t   msgID;
      uint8_t   data[MAX_CLAD_MESSAGE_LENGTH];
    };
  };
   
  int  pointer;
  int  message_size;
  bool encrypted;
};

// Buffers for queueing and dequeueing
static BLE_CladBuffer rx_buffer;
static BLE_CladBuffer tx_buffer;
static bool tx_pending;
static bool tx_buffered;

extern "C" void conn_params_error_handler(uint32_t nrf_error)
{
  APP_ERROR_HANDLER(nrf_error);
}

void app_error_handler(uint32_t error_code, uint32_t line_num, const uint8_t * p_file_name)
{
  NVIC_SystemReset();
}

static void softdevice_assertion_handler(uint32_t pc, uint16_t line_num, const uint8_t * file_name)
{
  NVIC_SystemReset();
}

static void permissions_error(BLEError error) {
  // This should be logged
  sd_ble_gap_disconnect(Bluetooth::conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}

void Bluetooth::authChallenge(const Anki::Cozmo::HelloRobot& msg) {
  if (memcmp(msg.signature, HELLO_SIGNATURE, sizeof(m_nonce))) {
    permissions_error(BLE_ERROR_AUTHENTICATED_FAILED);
    return ;
  }

  if (memcmp(msg.nonce, m_nonce, sizeof(m_nonce))) {
    permissions_error(BLE_ERROR_AUTHENTICATED_FAILED);
    return ;
  }

  m_authenticated = true;
}

static void dh_complete(const void* state, int) {
  using namespace Anki::Cozmo;
  
  const DiffieHellman* dh = (const DiffieHellman*) state;
  
  // Transmit our encryped key
  EncodedAESKey msg;
  memcpy(msg.secret, dh->local_secret, SECRET_LENGTH);
  memcpy(msg.encoded_key, dh->encoded_key, AES_KEY_LENGTH);  
  RobotInterface::SendMessage(msg);

  // Display the pin number
  RobotInterface::DisplayNumber dn;
  dn.value = *(const uint32_t*)Tasks::aes_key();
  dn.digits = 8;
  dn.x = 0;
  dn.y = 16;
  RobotInterface::SendMessage(dn);
}

static void dh_setup(const void* state, int) {
  using namespace Anki::Cozmo;
  
  const DiffieHellman* dh = (const DiffieHellman*) state;
  
  // Finish DH process
  Task t;
  t.op = TASK_FINISH_DIFFIE_HELLMAN;
  t.state = dh;
  t.callback = dh_complete;
  Tasks::execute(&t);

  // Display the pin number
  RobotInterface::DisplayNumber dn;
  dn.value = dh->pin;
  dn.digits = 8;
  dn.x = 0;
  dn.y = 16;
  RobotInterface::SendMessage(dn);
}

void Bluetooth::enterPairing(const Anki::Cozmo::EnterPairing& msg) {  
  // This is our inital state for pairing
  static DiffieHellman dh_state = {
    &RSA_DIFFIE_MONT,
    &RSA_DIFFIE_EXP_MONT,
  };

  // Copy in our secret code, and run
  memcpy(dh_state.remote_secret, msg.secret, SECRET_LENGTH);
  
  Task t;
  
  t.op = TASK_START_DIFFIE_HELLMAN;
  t.state = &dh_state;
  t.callback = dh_setup;
  
  Tasks::execute(&t);
}

static bool message_encrypted(uint8_t op) {
  using namespace Anki::Cozmo::RobotInterface;
  
  // These are the only messages that may be sent unencrypted over the wire
  switch (op) {
    case EngineToRobot::Tag_enterPairing:
    case EngineToRobot::Tag_encodedAESKey:
      return false;
  }

  return true;
}

static bool message_authenticated(uint8_t op) {
  using namespace Anki::Cozmo::RobotInterface;

  // These are the only messages that may be used unauthenticated
  switch (op) {
    case EngineToRobot::Tag_enterPairing:
    case EngineToRobot::Tag_encodedAESKey:
    case EngineToRobot::Tag_helloRobotMessage:
    case EngineToRobot::Tag_helloPhoneMessage:
      return false;
  }

  return true;
}

static void frame_data_received(const void*, int length) {
  // Attempted to underflow the receive buffer
  if (rx_buffer.length > length) {
    permissions_error(BLE_ERROR_BUFFER_UNDERFLOW);
    return ;
  }

  #ifndef DISABLE_AUTHENTIFICATION
  // Attempted to send a bad message over the wire, disconnect user and 
  if (message_authenticated(rx_buffer.msgID) && !m_authenticated) {
    permissions_error(BLE_ERROR_NOT_AUTHENTICATED);
    return ;
  }
  #endif

  // Forward message up clad
  if (rx_buffer.msgID >= 0x30) {
    Anki::Cozmo::HAL::RadioSendMessage(rx_buffer.data, rx_buffer.length, rx_buffer.msgID);
  } else {
    Spine::processMessage(&rx_buffer);
  }
}

static void frame_receive(CozmoFrame& receive)
{
  bool final      = (receive.flags & END_OF_MESSAGE) != 0;
  bool start      = (receive.flags & START_OF_MESSAGE) != 0;
  bool encrypted  = (receive.flags & MESSAGE_ENCRYPTED) != 0;

  if (start) {
    rx_buffer.pointer = 0;
  }

  // Buffer overflow
  if (rx_buffer.pointer + COZMO_FRAME_DATA_LENGTH >= sizeof(rx_buffer.raw)) {
    permissions_error(BLE_ERROR_BUFFER_OVERFLOW);
    return ;
  }

  memcpy(&rx_buffer.raw[rx_buffer.pointer], receive.message, COZMO_FRAME_DATA_LENGTH);
  rx_buffer.pointer += COZMO_FRAME_DATA_LENGTH;

  // We have not finished receiving our message
  if (!final) {
    return ;
  }
  
  rx_buffer.message_size = rx_buffer.pointer;

  #ifndef DISABLE_TASK_CHECK
  // Attemped to send a protected message unencrypted
  if (message_encrypted(rx_buffer.msgID) != encrypted) {
    permissions_error(BLE_ERROR_MESSAGE_ENCRYPTION_WRONG);
    return ;
  }
  #endif

  // rx_buffer.pointer
  if (encrypted) {
    Task t;
    
    t.op = TASK_AES_DECODE;
    t.callback = frame_data_received;
    t.state = rx_buffer.raw;
    t.length = rx_buffer.message_size;

    Tasks::execute(&t);
  } else {
    // Feed unencrypted data through to the engine
    frame_data_received(NULL, rx_buffer.message_size);
  }
}

static void send_welcome_message(const void*, int) {
  using namespace Anki::Cozmo;
  
  HelloPhone msg; 
  memcpy(msg.signature, HELLO_SIGNATURE, sizeof(m_nonce));
  memcpy(msg.nonce, m_nonce, sizeof(m_nonce));

  RobotInterface::SendMessage(msg);
}

void Bluetooth::manage() {
  ble_app_timer_manage();

  if (!m_task_enabled) {
    return ;
  }

  // Manage outbound transmissions
  if (tx_pending) {
    // Calculate the length of the message we are trying to send
    CozmoFrame f;

    f.flags = 
      (tx_buffer.encrypted ? MESSAGE_ENCRYPTED : 0) |
      ((tx_buffer.pointer == 0) ? START_OF_MESSAGE : 0);

    memcpy(f.message, &tx_buffer.raw[tx_buffer.pointer], sizeof(f.message));

    if (tx_buffer.pointer + sizeof(f.message) >= tx_buffer.message_size) {
      f.flags |= END_OF_MESSAGE;
    }

    // Transmit our frame
    ble_gatts_hvx_params_t params;
    uint16_t len = sizeof(CozmoFrame);
    
    memset(&params, 0, sizeof(params));
    params.type   = BLE_GATT_HVX_NOTIFICATION;
    params.handle = Bluetooth::transmit_handles.value_handle;
    params.p_data = (uint8_t*) &f;
    params.p_len  = &len;
    
    uint32_t err_code = sd_ble_gatts_hvx(Bluetooth::conn_handle, &params);
    
    if (err_code == NRF_SUCCESS) {
      tx_buffer.pointer += sizeof(f.message);

      if (tx_buffer.pointer >= tx_buffer.message_size) {
        tx_pending = false;
        tx_buffered = false;
      }
    }
    
    return ;
  }
}

static void start_message_transmission(const void*, int size) {
  tx_buffer.message_size = size;
  tx_pending = true;
}

bool Bluetooth::transmit(const uint8_t* data, int length, uint8_t op) {
  // We are already sending a message, jerk.
  if (tx_buffered) {
    return false;
  }
  
  bool encrypted = message_encrypted(op);
  
  tx_buffer.length = length;
  tx_buffer.msgID = op;
  tx_buffer.encrypted = encrypted;
  tx_buffer.pointer = 0;
  tx_buffer.message_size = length + 2;
  tx_buffered = true;
  
  memcpy(tx_buffer.data, data, length);

  if (length > MAX_CLAD_OUTBOUND_SIZE) {
    return false;
  }

  if (encrypted) {
    Task t;

    t.op = TASK_AES_ENCODE;
    tx_buffer.message_size = tx_buffer.length + 2;
    t.callback = start_message_transmission;
    t.state = tx_buffer.raw;
    t.length = tx_buffer.message_size;
        
    Tasks::execute(&t);
  } else {
    tx_pending = true;
  }
  
  return true;
}

static void on_ble_event(ble_evt_t * p_ble_evt)
{
  static ble_gap_master_id_t  p_master_id;
  static ble_gap_sec_keyset_t keys_exchanged;
  
  uint32_t                    err_code;
  Task t;

  switch (p_ble_evt->header.evt_id)
  {
    case BLE_GAP_EVT_CONNECTED:
      Bluetooth::conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
      m_authenticated = false;
      tx_pending = false;
      tx_buffered = false;

      // Generate our welcome nonce
      t.op = TASK_GENERATE_RANDOM;
      t.state = &m_nonce;
      t.length = sizeof(m_nonce);
      t.callback = send_welcome_message;
      Tasks::execute(&t);

      m_task_enabled = true;

      using namespace Anki::Cozmo;
      // Disable test mode when we someone connects
      RobotInterface::EnterFactoryTestMode ftm;
      ftm.mode = RobotInterface::FTM_None;
      RobotInterface::SendMessage(ftm);  
  
      break;

    case BLE_GAP_EVT_DISCONNECTED:
      Bluetooth::conn_handle = BLE_CONN_HANDLE_INVALID;
      m_task_enabled = false;

      // Resume advertising
      err_code = sd_ble_gap_adv_start(&adv_params);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
      err_code = sd_ble_gap_sec_params_reply(Bluetooth::conn_handle,
                                     BLE_GAP_SEC_STATUS_SUCCESS,
                                     &m_sec_params,&keys_exchanged);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GATTS_EVT_SYS_ATTR_MISSING:
      err_code = sd_ble_gatts_sys_attr_set(Bluetooth::conn_handle, NULL, 0,BLE_GATTS_SYS_ATTR_FLAG_USR_SRVCS);
      APP_ERROR_CHECK(err_code);
      break;

    case BLE_GAP_EVT_SEC_INFO_REQUEST:
      //p_enc_info = keys_exchanged.keys_central.p_enc_key

      if (p_master_id.ediv == p_ble_evt->evt.gap_evt.params.sec_info_request.master_id.ediv)
      {
        err_code = sd_ble_gap_sec_info_reply(Bluetooth::conn_handle, &keys_exchanged.keys_central.p_enc_key->enc_info, &keys_exchanged.keys_central.p_id_key->id_info, NULL);
        APP_ERROR_CHECK(err_code);
        p_master_id.ediv = p_ble_evt->evt.gap_evt.params.sec_info_request.master_id.ediv;
      }
      else
      {
        // No keys found for this device
        err_code = sd_ble_gap_sec_info_reply(Bluetooth::conn_handle, NULL, NULL,NULL);
        APP_ERROR_CHECK(err_code);
      }
      break;

    case BLE_GAP_EVT_TIMEOUT:
      if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_ADVERTISING)
      {
        // XXX: Go into low power mode        
      }
      break;

    case BLE_GATTS_EVT_WRITE:
    {
      ble_gatts_evt_write_t * p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

      // Ignore probing messages, but don't disconnect
      if ((p_evt_write->handle == Bluetooth::receive_handles.value_handle)) {
        if (p_evt_write->len == sizeof(CozmoFrame)) {
          frame_receive(*(CozmoFrame*) p_evt_write->data);
        }
      }
      break;
    }
    
    default:
      // No implementation needed.
      break;
  }
}

extern "C" void on_conn_params_evt(ble_conn_params_evt_t * p_evt)
{
  uint32_t err_code;

  if(p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED)
  {
    err_code = sd_ble_gap_disconnect(Bluetooth::conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
    APP_ERROR_CHECK(err_code);
  }
}

// Initalization functions
static uint32_t receive_char_add(uint8_t uuid_type)
{
  static CozmoFrame value;
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;

  memset(&char_md, 0, sizeof(char_md));
  
  char_md.char_props.read   = 1;
  char_md.char_props.write  = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = NULL;
  char_md.p_sccd_md         = NULL;
  
  ble_uuid.type = uuid_type;
  ble_uuid.uuid = COZMO_UUID_RECEIVE_CHAR;
  
  memset(&attr_md, 0, sizeof(attr_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
  attr_md.vloc       = BLE_GATTS_VLOC_USER;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;
  
  memset(&attr_char_value, 0, sizeof(attr_char_value));

  attr_char_value.p_uuid       = &ble_uuid;
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(CozmoFrame);
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(CozmoFrame);
  attr_char_value.p_value      = (uint8_t*)&value;
  
  return sd_ble_gatts_characteristic_add(Bluetooth::service_handle, &char_md,
                                            &attr_char_value,
                                            &Bluetooth::receive_handles);
}

static uint32_t transmit_char_add(uint8_t uuid_type)
{
  static CozmoFrame value;
  ble_gatts_char_md_t char_md;
  ble_gatts_attr_md_t cccd_md;
  ble_gatts_attr_t    attr_char_value;
  ble_uuid_t          ble_uuid;
  ble_gatts_attr_md_t attr_md;

  memset(&cccd_md, 0, sizeof(cccd_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
  cccd_md.vloc = BLE_GATTS_VLOC_STACK;
  
  memset(&char_md, 0, sizeof(char_md));
  
  char_md.char_props.read   = 1;
  char_md.char_props.notify = 1;
  char_md.p_char_user_desc  = NULL;
  char_md.p_char_pf         = NULL;
  char_md.p_user_desc_md    = NULL;
  char_md.p_cccd_md         = &cccd_md;
  char_md.p_sccd_md         = NULL;
  
  ble_uuid.type = uuid_type;
  ble_uuid.uuid = COZMO_UUID_TRANSMIT_CHAR;
  
  memset(&attr_md, 0, sizeof(attr_md));

  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
  BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
  attr_md.vloc       = BLE_GATTS_VLOC_USER;
  attr_md.rd_auth    = 0;
  attr_md.wr_auth    = 0;
  attr_md.vlen       = 0;
  
  memset(&attr_char_value, 0, sizeof(attr_char_value));
  
  attr_char_value.p_uuid       = &ble_uuid;
  attr_char_value.p_attr_md    = &attr_md;
  attr_char_value.init_len     = sizeof(CozmoFrame);
  attr_char_value.init_offs    = 0;
  attr_char_value.max_len      = sizeof(CozmoFrame);
  attr_char_value.p_value      = (uint8_t*)&value;
  
  return sd_ble_gatts_characteristic_add(Bluetooth::service_handle, &char_md,
                                          &attr_char_value,
                                          &Bluetooth::transmit_handles);
}

uint32_t Bluetooth::init() {
  uint32_t err_code;

  conn_handle = BLE_CONN_HANDLE_INVALID;

  // Initialize SoftDevice.
  err_code = sd_softdevice_enable(m_clock_source, softdevice_assertion_handler);
  APP_ERROR_CHECK(err_code);
  m_sd_enabled = true;
  m_task_enabled = false;

  // Enable BLE event interrupt (interrupt priority has already been set by the stack).
  return sd_nvic_EnableIRQ(SWI2_IRQn);
}

bool Bluetooth::enabled(void) {
  return m_sd_enabled;
}

void Bluetooth::advertise(void) {
  uint32_t err_code;

  if (!m_sd_enabled) {
    err_code = sd_softdevice_enable(m_clock_source, softdevice_assertion_handler);
    APP_ERROR_CHECK(err_code);

    sd_nvic_EnableIRQ(SWI2_IRQn);

    m_sd_enabled      = true;
  }

  // Enable BLE stack 
  ble_enable_params_t ble_enable_params;
  memset(&ble_enable_params, 0, sizeof(ble_enable_params));
  ble_enable_params.gatts_enable_params.service_changed = IS_SRVC_CHANGED_CHARACT_PRESENT;
  err_code = sd_ble_enable(&ble_enable_params);
  APP_ERROR_CHECK(err_code);

  ble_gap_addr_t addr;
  
  err_code = sd_ble_gap_address_get(&addr);
  APP_ERROR_CHECK(err_code);
  err_code = sd_ble_gap_address_set(BLE_GAP_ADDR_CYCLE_MODE_NONE, &addr);
  APP_ERROR_CHECK(err_code);

  // GAP parameters init
  ble_gap_conn_sec_mode_t sec_mode;
  BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

  // Setup advertisment settings
  err_code = sd_ble_gap_device_name_set(&sec_mode, DEVICE_NAME, DEVICE_NAME_LENGTH);
  APP_ERROR_CHECK(err_code);

  err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
  APP_ERROR_CHECK(err_code);

  // Create vendor ID for services
  uint8_t uuid_type;

  err_code = sd_ble_uuid_vs_add(&COZMO_UUID_BASE, &uuid_type);
  APP_ERROR_CHECK(err_code);
  
  // Setup our service
  ble_uuid_t adv_uuids[] = {
    { COZMO_UUID_SERVICE, uuid_type }
  };

  err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &adv_uuids[0], &Bluetooth::service_handle);
  APP_ERROR_CHECK(err_code);
  
  err_code = receive_char_add(uuid_type);
  APP_ERROR_CHECK(err_code);
  
  err_code = transmit_char_add(uuid_type);
  APP_ERROR_CHECK(err_code);

  // Initialize advertising 
  manif_data.deviceid[0] = NRF_FICR->DEVICEID[0];
  manif_data.deviceid[1] = NRF_FICR->DEVICEID[1];

  ble_advdata_t scanrsp;
  memset(&scanrsp, 0, sizeof(scanrsp));
  scanrsp.uuids_complete.uuid_cnt = sizeof(adv_uuids) / sizeof(ble_uuid_t);
  scanrsp.uuids_complete.p_uuids  = adv_uuids;
  
  err_code = ble_advdata_set(&m_advdata, &scanrsp);
  APP_ERROR_CHECK(err_code);

  // Initialize connection parameters
  err_code = ble_conn_params_init(&cp_init);
  APP_ERROR_CHECK(err_code);
  
  // Set BLE power to +0db
  sd_ble_gap_tx_power_set(-4);
  
  // Start advertising
  err_code = sd_ble_gap_adv_start(&adv_params);
  APP_ERROR_CHECK(err_code);
}

void Bluetooth::shutdown(void) {
  if (!m_sd_enabled) { return ; }

  m_task_enabled = false;
  
  sd_softdevice_disable();
  m_sd_enabled = false;
}

extern "C" void SWI2_IRQHandler(void)
{
  uint32_t evt_id;
  uint32_t err_code;

  // Pull event from SOC.
  for (;;) {
    err_code = sd_evt_get(&evt_id);
    
    if (err_code == NRF_ERROR_NOT_FOUND) {
      break ;
    } else if (err_code != NRF_SUCCESS) {
      APP_ERROR_HANDLER(err_code);
    }
  }
  
  // Pull event from stack
  for (;;) {
    uint8_t ble_buffer[BLE_STACK_EVT_MSG_BUF_SIZE] __attribute__ ((aligned (4)));
    uint16_t evt_len = sizeof(ble_buffer);
    err_code = sd_ble_evt_get(ble_buffer, &evt_len);
    
    switch (err_code) {
      case NRF_SUCCESS:
        ble_conn_params_on_ble_evt((ble_evt_t *)ble_buffer);
        on_ble_event((ble_evt_t *)ble_buffer);
        break ;
      case NRF_ERROR_NOT_FOUND:
       return ;
      default:
        APP_ERROR_HANDLER(err_code);
        break ; 
    }
  }
}
