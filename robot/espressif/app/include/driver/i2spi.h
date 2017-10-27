#ifndef _I2S_H_
#define _I2S_H_

#include "os_type.h"
#include "anki/cozmo/robot/drop.h" ///< I2SPI transaction contract
#include "anki/cozmo/robot/rec_protocol.h" ///< Protocol for upgrades

#define AUDIO_BUFFER_SIZE (1024)
#define AUDIO_BUFFER_SIZE_MASK (AUDIO_BUFFER_SIZE-1)
ASSERT_IS_POWER_OF_TWO(AUDIO_BUFFER_SIZE);

#define SCREEN_BUFFER_SIZE (32)
#define SCREEN_BUFFER_SIZE_MASK (SCREEN_BUFFER_SIZE-1)
ASSERT_IS_POWER_OF_TWO(SCREEN_BUFFER_SIZE);
ct_assert(MAX_SCREEN_BYTES_PER_DROP == sizeof(uint32_t)); // Required for samples to be optimized as uint32_t

/** Initalize the I2S peripheral, IO pins and DMA for bi-directional transfer
 * @return 0 on success or non-0 on an error
 */
int8_t i2spiInit(void);

/** Queues a buffer to transmit over I2S
 * @param msgData A pointer to the data to be sent to the RTIP
 * @param msgLen The number of bytes of message data pointed to by msgData.
 *               Must be no more than DROP_TO_RTIP_MAX_VAR_PAYLOAD
 * @return true if the data was successfully queued or false if it could not be queued.
 */
bool i2spiQueueMessage(const uint8_t* msgData, const int msgLen);

/** Check if the I2SPI message queue is empty
 * @return True if there are no clad messages waiting to be sent
 */
bool i2spiMessageQueueIsEmpty(void);

/** Periodic update function for RTIP RX queue estimate
 */
void i2spiUpdateRtipQueueEstimate(void);

/** Get any received CLAD messages
 * @param[out] data A a pointer to a clad buffer
 * @return The number of bytes written to data, 0 if no data was available
 */
int i2spiGetCladMessage(uint8_t* data);

/** Check the status of the RTIP bootloader
 */
int16_t i2spiGetRtipBootloaderState(void);

/** Check the status of the body Bootloader
 */
uint32_t i2spiGetBodyBootloaderCode(void);

/** Push a chunk of firmware to the RTIP
 * Does not actually send the data imeediately, queues it for sending, pointer must remain valid
 * @param chunk a Pointer to data to be sent
 */
bool i2spiBootloaderPushChunk(FirmwareBlock* chunk);

/** Push a bootload command done message to the RTIP
 */
bool i2spiBootloaderCommandDone(void);

/** Return the number of samples available to push into the screen buffer
 */
int8_t i2spiGetScreenBufferAvailable(void);

/** Push a sample into the screen data buffer
 * @param data The sample to push
 * @param rect True if the sample is rect data, false if pixel data
 */
void i2spiPushScreenData(const uint32_t* data, const bool rect);

/** Queue the specified number of samples of silence
 * @param Number of samples of silence
 */
void i2spiSetAudioSilenceSamples(const int16_t silence);

/** Get how many samples of silence are currently queued.
 */
int16_t i2spiGetAudioSilenceSamples(void);

/** Get how many samples are available to push into the audio buffer
 */
int16_t i2spiGetAudioBufferAvailable(void);

/** Push data into the audio buffer
 * @NOTE At least length bytes must be available before calling this function
 * @param buffer Pointer to the data to queued
 * @param length How many bytes to push into the queue
 */
void i2spiBufferAudio(uint8_t* buffer, const int16_t length);

/** Set the app connected flag in outgoing drops
 */
void i2spiSetAppConnected(const bool connected);

typedef enum
{
  I2SPI_SYNC = 0,
  I2SPI_NORMAL,
  I2SPI_BOOTLOADER,
  I2SPI_NULL,
} I2SPIMode;

/** Switch I2SPI operating mode.
 * @warning Use with extreme caution.
 * @param mode The new mode to switch to
 * @return true if the more was accepted false if the transition was invalid
 */
bool i2spiSwitchMode(const I2SPIMode mode);
/// Returns the current operating mode
I2SPIMode i2spiGetMode(void);

/** Various possible ERROR states for the i2spi bus
 * @note Explicit values are used so reports are easier to read
 */
typedef enum
{
  I2SPIE_None           = 0, ///< No error
  I2SPIE_too_much_drift = 1,
  I2SPIE_rx_overflow    = 2,
  I2SPIE_bad_rx_payload = 3,
  I2SPIE_bad_rx_csum    = 4,
  I2SPIE_drop_count     = 8,
  I2SPIE_buf_logic      = 0x20,
  I2SPIE_bad_footer     = 0x40,
} I2SPIError;

/// Count how many tx overruns we've had
uint32_t i2spiGetTxOverflowCount(void);
/// Count how many tx overruns we've had
uint32_t i2spiGetRxOverflowCount(void);
/// Count how many times the drop phase has jumped more than we expected it to
uint32_t i2spiGetPhaseErrorCount(void);
/// Count the integral drift in the I2SPI system
int32_t i2spiGetIntegralDrift(void);
/// Get the latest error from the driver and clear the error report
I2SPIError i2spiGetErrorCode(int32_t* data);
///log info about a desynch.
void i2spiLogDesync(const u8* buffer, int buffer_bytes);
/// Retrieve data to stuff into wifi debug struct
void i2spiGetDebugTelemetry(uint16_t* videoQueued, uint16_t* dropCount,
                            uint16_t* errCount, int16_t* integralDrift,
                            uint16_t* rtipRxH, uint16_t* rtipRxT,
                            uint16_t* rtipTxH, uint16_t* rtipTxT);

#endif
