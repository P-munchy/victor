/** @file Upgrade / flash controller
 * This module is responsible for reflashing the Espressif and other chips in Cozmo
 * @author Daniel Casner <daniel@anki.com>
 */
extern "C" {
#include "user_interface.h"
#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "client.h"
#include "foregroundTask.h"
#include "driver/i2spi.h"
#include "sha1.h"
#include "rboot.h"
}
#include "upgradeController.h"
#include "anki/cozmo/robot/esp.h"
#include "clad/robotInterface/otaMessages.h"
#include "clad/robotInterface/messageRobotToEngine.h"
#include "clad/robotInterface/messageRobotToEngine_send_helper.h"

#define MAX_RETRIES 2
#define SHA_CHECK_READ_LENGTH 512

namespace Anki {
namespace Cozmo {
namespace UpgradeController {

#define DEBUG_OTA

static uint8 retries;

/// Stores the state nessisary for the over the air upgrade task
typedef struct 
{
  SHA1_CTX ctx;     ///< SHA1 calculation state
  uint32   fwStart; ///< Starting address in flash of the firmware imageId
  uint32   fwSize;  ///< Number of bytes of firmware
  uint32   version; ///< Version number associated with this folder
  int32    index;   ///< Index through reading, writing etc.
  int32    phase;   ///< Operation phase
  uint8    sig[SHA1_DIGEST_LENGTH]; ///< The digest we are looking for
  RobotInterface::OTACommand cmd; ///< The command we are processing
} OTAUpgradeTaskState;

LOCAL bool TaskEraseFlash(uint32 param)
{
  RobotInterface::EraseFlash* msg = reinterpret_cast<RobotInterface::EraseFlash*>(param);
  // TODO: Add block based erase
  const uint16 sector = msg->start / SECTOR_SIZE;
  RobotInterface::FlashWriteAcknowledge resp;
  const SpiFlashOpResult rslt = spi_flash_erase_sector(sector);
  switch (rslt)
  {
    case SPI_FLASH_RESULT_OK:
    {
#ifdef DEBUG_OTA
      os_printf("Erased sector %x\r\n", sector);
#endif
      if (msg->size > SECTOR_SIZE) // This wasn't the last sector
      {
        msg->start += SECTOR_SIZE;
        msg->size  -= SECTOR_SIZE;
        retries = MAX_RETRIES;
        return true; // Repost ourselves to erase the next sector
      }
      else // We're done erasing
      {
        resp.address = msg->start;
        resp.length  = msg->size;
        resp.writeNotErase = false;
        RobotInterface::SendMessage(resp);
        break;
      }
    }
    case SPI_FLASH_RESULT_ERR:
    {
#ifdef DEBUG_OTA
      os_printf("Failed to erase sector %x\r\n", sector);
#endif
      resp.address = msg->start;
      resp.length  = 0; // Indicates failure
      resp.writeNotErase = false;
      RobotInterface::SendMessage(resp);
      break;
    }
    case SPI_FLASH_RESULT_TIMEOUT:
    {
      if (retries-- > 0)
      {
        foregroundTaskPost(TaskEraseFlash, param);
        return true;
      }
      else
      {
        os_printf("Timed out erasing sector %x\r\n", sector);
        resp.address = msg->start;
        resp.length  = 0; // Indicates failure
        resp.writeNotErase = false;
        RobotInterface::SendMessage(resp);
        break;
      }
    }
    default:
    {
      PRINT("ERROR: Unexpected SPI Flash Erase operation result\r\n");
      break;
    }
  }
  
  os_free(msg);
  return false;
}

LOCAL bool TaskWriteFlash(uint32 param)
{
  RobotInterface::WriteFlash* msg = reinterpret_cast<RobotInterface::WriteFlash*>(param);
  RobotInterface::FlashWriteAcknowledge resp;
  switch(spi_flash_write(msg->address, reinterpret_cast<uint32*>(msg->data), msg->data_length))
  {
    case SPI_FLASH_RESULT_OK:
    {
#ifdef DEBUG_OTA
      os_printf("Wrote to flash %x[%d]\r\n", msg->address, msg->data_length);
#endif
      resp.address = msg->address;
      resp.length  = msg->data_length;
      resp.writeNotErase = true;
      RobotInterface::SendMessage(resp);
      break;
    }
    case SPI_FLASH_RESULT_ERR:
    {
      os_printf("Failed to write to address %x\r\n", msg->address);
      resp.address = msg->address;
      resp.length  = 0; // Indicates failure
      resp.writeNotErase = true;
      RobotInterface::SendMessage(resp);
      break;
    }
    case SPI_FLASH_RESULT_TIMEOUT:
    {
      if (retries-- > 0)
      {
        foregroundTaskPost(TaskWriteFlash, param);
        return true;
      }
      else
      {
        os_printf("Timed out writting to address %x\r\n", msg->address);
        resp.address = msg->address;
        resp.length  = 0; // Indicates failure
        resp.writeNotErase = true;
        RobotInterface::SendMessage(resp);
        break;
      }
    }
    default:
    {
      PRINT("ERROR: Unexpected SPI Flash write operation result\r\n");
      break;
    }
  }
  
  os_free(msg);
  return false;
}

LOCAL bool TaskOtaAsset(uint32 param)
{
  OTAUpgradeTaskState* state = reinterpret_cast<OTAUpgradeTaskState*>(param);
  // TODO Do something with this new asset update
  PRINT("Asset OTA successful\r\n");
  os_free(state);
  return false;
}

LOCAL bool TaskOtaWiFi(uint32 param)
{
  BootloaderConfig blcfg;
  OTAUpgradeTaskState* state = reinterpret_cast<OTAUpgradeTaskState*>(param);
  
#ifdef DEBUG_OTA
  os_printf("TaskOtaWiFi\r\n");
#endif
  
  blcfg.header = BOOT_CONFIG_HEADER;
  blcfg.newImageStart = state->fwStart / SECTOR_SIZE;
  blcfg.newImageSize  = state->fwSize  / SECTOR_SIZE + 1;
  blcfg.version       = state->version;
  blcfg.chksum        = calc_chksum((uint8*)&blcfg, &blcfg.chksum);
  
  switch (spi_flash_write(BOOT_CONFIG_SECTOR * SECTOR_SIZE, reinterpret_cast<uint32*>(&blcfg), sizeof(BootloaderConfig)))
  {
    case SPI_FLASH_RESULT_OK:
    {
      // TODO tell RTIP we're going away
      os_printf("WiFi OTA rebooting for version %d\r\n", state->version);
      os_free(state);
      system_restart();
      return false;
    }
    case SPI_FLASH_RESULT_ERR:
    {
      PRINT("ERROR: Could not write bootloader config to flash for ota upgrade version %d.\r\n", state->version);
      os_free(state);
      return false;
    }
    case SPI_FLASH_RESULT_TIMEOUT:
    {
      if (retries-- > 0)
      {
        return true;
      }
      else
      {
        PRINT("ERROR: Timed out writing bootlaoder config to flash for ota upgrade version %d.\r\n", state->version);
        os_free(state);
        return false;
      }
    }
    default:
    {
      PRINT("ERROR: Unexpected flash write result writing bootloader config\r\n");
      os_free(state);
      return false;
    }
  }
}

LOCAL bool TaskOtaRTIP(uint32 param)
{
  OTAUpgradeTaskState* state = reinterpret_cast<OTAUpgradeTaskState*>(param);
  const uint16_t rtipState = i2spiGetRtipBootloaderState();
  
#ifdef DEBUG_OTA
  static int32_t  prevIndex = 0xFFFF;
  static int32_t  prevPhase = 0xFFFF;
  static uint16_t prevState = 0xFFFF;
  if ((rtipState != prevState) || (state->index != prevIndex) || (state->phase != prevPhase))
  {
    prevIndex = state->index;
    prevPhase = state->phase;
    prevState = rtipState;
    os_printf("TaskOtaRTIP: state = %x\tphase = %d\tindex = %d\r\n", prevState, prevPhase, prevIndex);
  }
#endif
  
  switch (rtipState)
  {
    case STATE_NACK:
    {
      if (retries-- == 0)
      {
        PRINT("RTIP OTA transfer failure! Aborting.\r\n");
        os_free(state);
        return false;
      }
      state->phase = 0;
      // Have retries left, fall through and try writing
    }
    case STATE_IDLE:
    {
      if (state->phase == 0)
      {
        FirmwareBlock chunk;
        switch (spi_flash_read(state->fwStart + state->index, reinterpret_cast<uint32*>(&chunk), sizeof(FirmwareBlock)))
        {
          case SPI_FLASH_RESULT_OK:
          {
            retries = MAX_RETRIES;
            i2spiBootloaderPushChunk(&chunk);
            state->phase = 1;
            return true;
          }
          case SPI_FLASH_RESULT_ERR:
          {
            PRINT("RTIP OTA flash readback failure, aborting\r\n");
            os_free(state);
            return false;
          }
          case SPI_FLASH_RESULT_TIMEOUT:
          {
            if (retries-- > 0)
            {
              return true;
            }
            else
            {
              PRINT("RTIP OTA flash readback timeout, aborting\r\n");
              os_free(state);
              return false;
            }
          }
        }
      }
      else if (state->phase == 2)
      {
        state->index += sizeof(FirmwareBlock);
        if (static_cast<uint32>(state->index) < state->fwSize) // Have more firmware left to write
        {
          retries = MAX_RETRIES;
          state->phase = 0;
          return true;
        }
        else // Done writing firmware
        {
          PRINT("RTIP OTA transfer complete\r\n");
          i2spiSwitchMode(I2SPI_NORMAL);
          os_free(state);
          return false;
        }
      }
      else
      {
        return true;
      }
    }
    case STATE_BUSY: // Just waiting
    {
      if (state->phase == 1) state->phase = 2;
      return true;
    }
    case STATE_SYNC: // We will read garbage instead of sync so default is the same
    default:
    {
      return true;
    }
  }
}

LOCAL bool TaskOtaBody(uint32 param)
{
  OTAUpgradeTaskState* state = reinterpret_cast<OTAUpgradeTaskState*>(param);
  /*
  const uint32_t bodyBootloaderCode  = i2spiGetBodyBootloaderState();
  const uint16_t bodyBootloaderState = bodyBootloaderCode & 0xFFFF;
  const uint16_t bodyBootloaderCount = bodyBootloaderCode >> 16;
  
#ifdef DEBUG_OTA
  static int32_t  prevIndex = 0xFFFF;
  static int32_t  prevPhase = 0xFFFF;
  static uint16_t prevState = 0xFFFF;
  if ((prevIndex != state->index) || (prevPhase != state->phase) || (prevState != bodyBootloaderState))
  {
    prevIndex = state->index;
    prevPhase = state->phase;
    prevState = bodyBootloaderState;
    os_printf("TaskOtaBody: state = %d (%d)\tphase = %d\tindex = %d\r\n",
              prevState, bodyBootloaderCount, prevPhase, prevIndex);
  }
#endif
  
  switch(bodyBootloaderState)
  {
    case STATE_SYNC:
    case STATE_BUSY:
    case STATE_RUNNING:
    case STATE_UNKNOWN:
    {
      return true; // Just wait
    }
    case STATE_IDLE: // Ready to receive packet
    {
      if (bodyBootloaderCount > state->phase) // Ready for another chunk
      {
        const int remainingWords = (state->fwSize - state->index)/4;
        if (remainingWords > 0)
        {
          uint32_t buffer[40];
          int sentCount = 0;
          struct msg {
            uint8_t  PADDING[3];
            uint8_t  command;
            uint32_t data;
          };
          switch (spi_flash_read(state->fwStart + state->index, buffer, sizeof(buffer)))
          {
            case SPI_FLASH_RESULT_OK:
            {
              retries = MAX_RETRIES;
              break;
            }
            case SPI_FLASH_RESULT_ERR:
            {
              PRINT("BODY OTA flash readback failure, aborting\r\n");
              os_free(state);
              return false;
            }
            case SPI_FLASH_RESULT_TIMEOUT:
            {
              if (retries-- > 0)
              {
                return true;
              }
              else
              {
                PRINT("BODY OTA flash readback timeout, aborting\r\n");
                os_free(state);
                return false;
              }
            }
          }
          msg.command = RobotInterface::EngineToRobot::Tag_bodyUpgradeData;
          msg.data    = buffer[sentCount];
          while ((sentCount < remainingWords) && (i2spiQueueMessage(&msg.command, 5))
          {
            sentCount++;
            msg.data = buffer[sentCount];
          }
          state->index += sentCount * 4;
        }
        else
        {
          PRINT("Done sending firmware to body, commanding reboot\r\n");
          uint8_t msg[2];
          msg[0] = COMMAND_HEADER;
          mdg[1] = COMMAND_DONE;
          i2spiQueueMessage(msg, 2);
        }
      }
    }
    default:
    {
      PRINT("Unexpected body bootloader state %d, %08x, aborting\r\n", bodyBootloaderState, bodyBootloaderCode);
    }
  }
  */
  os_free(state);
  return false;
}

LOCAL bool TaskCheckSig(uint32 param)
{
  OTAUpgradeTaskState* state = reinterpret_cast<OTAUpgradeTaskState*>(param);
  uint32 buffer[SHA_CHECK_READ_LENGTH/4]; // Allocate as uint32 for alignment
  const uint32 remaining  = state->fwSize - state->index;
  const uint32 readLength = SHA_CHECK_READ_LENGTH < remaining ? SHA_CHECK_READ_LENGTH : remaining;
  switch (spi_flash_read(state->fwStart + state->index, buffer, readLength))
  {
    case SPI_FLASH_RESULT_OK:
    {
      SHA1Update(&(state->ctx), reinterpret_cast<uint8*>(buffer), readLength);
      retries = MAX_RETRIES;
      break;
    }
    case SPI_FLASH_RESULT_ERR:
    {
      PRINT("ERROR reading back flash at %x for signature check\r\n", state->index);
      os_free(state);
      return false;
    }
    case SPI_FLASH_RESULT_TIMEOUT:
    {
      if (retries-- > 0)
      {
        return true; // Just retry
      }
      else
      {
        PRINT("ERROR timed out reading back flash at %x for signature check\r\n", state->index);
        os_free(state);
        return false;
      }
    }
    default:
    {
      PRINT("ERROR unexpected flash result when reading back at %x for signature check\r\n", state->index);
    }
  }
  
  if (remaining > SHA_CHECK_READ_LENGTH)
  {
    state->index += readLength;
    return true;
  }
  else
  {
    int i;
    uint8 digest[SHA1_DIGEST_LENGTH];
#ifdef DEBUG_OTA
    os_printf("SHA1 Final\r\n");
#endif
    SHA1Final(digest, &(state->ctx));
    for (i=0; i<SHA1_DIGEST_LENGTH; i++)
    {
      if (digest[i] != state->sig[i])
      {
        PRINT("Firmware signature missmatch at character %d, %02x != %02x\r\n", i, digest[i], state->sig[i]);
        os_free(state);
        return false;
      }
    }
#ifdef DEBUG_OTA
    os_printf("Signature matches\r\n");
#endif
    // If we got here, the signature matched
    retries = MAX_RETRIES;
    state->index = 0;
    switch(state->cmd)
    {
      case RobotInterface::OTA_none:
      {
        PRINT("Successfully confirmed flash signature for OTA none\r\n");
        os_free(state);
        return false;
      }
      case RobotInterface::OTA_asset:
      {
        foregroundTaskPost(TaskOtaAsset, param);
        return false;
      }
      case RobotInterface::OTA_WiFi:
      {
#ifdef DEBUG_OTA
        os_printf("WiFi signature OKAY, posting task\r\n");
#endif
        foregroundTaskPost(TaskOtaWiFi, param);
        return false;
      }
      case RobotInterface::OTA_RTIP:
      {
#ifdef DEBUG_OTA
        os_printf("RTIP signature OKAY, switching modes and posting task\r\n");
#endif
        uint8_t msg[2]; // This is based on the EnterBootloader message which is defined in otaMessages.clad
        msg[0] = RobotInterface::EngineToRobot::Tag_enterBootloader;
        msg[1] = WiFiToRTIP::BOOTLOAD_RTIP;
        i2spiQueueMessage(msg, 2); // Send message to put the RTIP in bootloader mode
        retries = MAX_RETRIES;
        foregroundTaskPost(TaskOtaRTIP, param);
        i2spiSwitchMode(I2SPI_BOOTLOADER);
        return false;
      }
      case RobotInterface::OTA_body:
      {
#ifdef DEBUG_OTA
        os_printf("Body signature OKAY, switching mode and posting task\r\n");
#endif
        uint8_t msg[2]; // This is based on the EnterBootloader message which is defined in otaMessages.clad
        msg[0] = RobotInterface::EngineToRobot::Tag_enterBootloader;
        msg[1] = WiFiToRTIP::BOOTLOAD_BODY;
        if (i2spiQueueMessage(msg, 2)) // Send message to put the RTIP in bootloader mode
        {
          retries = MAX_RETRIES;
          foregroundTaskPost(TaskOtaBody, param);
        }
        else
        {
          PRINT("Couldn't command body bootloader mode, aborting.\r\n");
          os_free(state);
        }
        return false;
      }
      default:
      {
        PRINT("ERROR: Unexpected OTA command %d\r\n", state->cmd);
        os_free(state);
        return false;
      }
    }
  }
}
  
void EraseFlash(RobotInterface::EraseFlash& msg)
{
  if (msg.start < FLASH_WRITE_START_ADDRESS) // Refuse to erase addresses that are too low
  {
    PRINT("WARNING: Refusing to erase flash address %x, below %x\r\n", msg.start, FLASH_WRITE_START_ADDRESS);
  }
  else
  {
    RobotInterface::EraseFlash* taskMsg = static_cast<RobotInterface::EraseFlash*>(os_zalloc(msg.Size()));
    if (taskMsg == NULL)
    {
      PRINT("Failed to allocate memory for flash erase task\r\n");
    }
    else
    {
      os_memcpy(taskMsg, &msg, msg.Size());
      retries = MAX_RETRIES;
      i2spiSwitchMode(I2SPI_PAUSED);
      foregroundTaskPost(TaskEraseFlash, reinterpret_cast<uint32>(taskMsg));
    }
  }
}

void WriteFlash(RobotInterface::WriteFlash& msg)
{
  if (msg.address < FLASH_WRITE_START_ADDRESS) // Refuse to write addresses that are too low
  {
    PRINT("WARNING Refusing to write flash address %x, below %x\r\n", msg.address, FLASH_WRITE_START_ADDRESS);
  }
  else
  {
    RobotInterface::WriteFlash* taskMsg = static_cast<RobotInterface::WriteFlash*>(os_zalloc(msg.Size()));
    if (taskMsg == NULL)
    {
      PRINT("Failed to allocate memory for flash write task\r\n");
    }
    else
    {
      os_memcpy(taskMsg, &msg, msg.Size());
      retries = MAX_RETRIES;
      foregroundTaskPost(TaskWriteFlash, reinterpret_cast<uint32>(taskMsg));
    }
  }
}
  
  void Trigger(RobotInterface::OTAUpgrade& msg)
  {
    OTAUpgradeTaskState* otaState = static_cast<OTAUpgradeTaskState*>(os_zalloc(sizeof(OTAUpgradeTaskState)));
    if (otaState == NULL)
    {
      PRINT("Failed to allocate memory for upgrade task\r\n");
    }
    else
    {
#ifdef DEBUG_OTA
      os_printf("OTA finish: fwStart=%x, fwSize=%x, version=%d, command=%d\r\n",
                msg.start, msg.size, msg.version, msg.command);
#endif
      retries = MAX_RETRIES;
      SHA1Init(&(otaState->ctx)); // Initalize the SHA1
      otaState->fwStart = msg.start;
      otaState->fwSize  = msg.size;
      otaState->version = msg.version;
      otaState->index   = 0;
      otaState->phase   = 0;
      otaState->cmd     = msg.command;
      os_memcpy(&(otaState->sig), msg.sig, SHA1_DIGEST_LENGTH);
      foregroundTaskPost(TaskCheckSig, reinterpret_cast<uint32>(otaState));
    }
    i2spiSwitchMode(I2SPI_NORMAL);
  }
}
}
}
