#ifndef _I2S_H_
#define _I2S_H_

#include "os_type.h"
#include "anki/cozmo/robot/drop.h" ///< I2SPI transaction contract
#include "anki/cozmo/robot/rec_protocol.h" ///< Protocol for upgrades

/// Buffer size must match I2S TX FIFO depth
#define DMA_BUF_SIZE (512) /// This must be 512 Espressif DMA to work and for logic in i2spi.c to work
/// How often we will garuntee servicing the DMA buffers
#define DMA_SERVICE_INTERVAL_MS (5)
/// How many buffers are required given the above constraints.
#define DMA_BUF_COUNT ((I2SPI_RAW_BYTES_PER_SECOND * DMA_SERVICE_INTERVAL_MS / 1000 / DMA_BUF_SIZE))
/// Buffer size for sending messages to the RTIP
#define I2SPI_MESSAGE_BUF_SIZE (1024)
ASSERT_IS_POWER_OF_TWO(I2SPI_MESSAGE_BUF_SIZE); // Required for mask below
/// Size mask for index math on message buffer
#define I2SPI_MESSAGE_BUF_SIZE_MASK (I2SPI_MESSAGE_BUF_SIZE-1)

/// Task priority level for processing I2SPI data
#define I2SPI_PRIO USER_TASK_PRIO_2

/// I2SPI interface operating modes
typedef enum {
  I2SPI_NORMAL,     ///< Normal drop communication, synchronization is implied
  I2SPI_BOOTLOADER, ///< Bootloader communication, synchronization is implied
  I2SPI_PAUSED,     ///< Communication paused, 0xFFFFffff is sent continuously.
  I2SPI_REBOOT,     ///< Inform the K02 we want to reboot
  I2SPI_RECOVERY,   ///< Inform the K02 we want to reboot into recovery
  I2SPI_SHUTDOWN,   ///< Inform the K02 we want to shut down
  I2SPI_RESUME,     ///< Attempt to resume a paused connection without resyncing
} I2SpiMode;

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
bool i2spiQueueMessage(uint8_t* msgData, int msgLen);

/** Check if the I2SPI message queue is empty
 * @return True if there are no clad messages waiting to be sent
 */
bool i2spiMessageQueueIsEmpty(void);

/** Switch the operating mode of the I2SPI interface
 * I2SPI_NORMAL is the default mode
 * @param mode Which mode to transition insertion
 */
void i2spiSwitchMode(const I2SpiMode mode);

/** Check the status of the RTIP bootloader
 */
int16_t i2spiGetRtipBootloaderState(void);

/** Check the status of the body Bootloader
 */
uint32_t i2spiGetBodyBootloaderCode(void);

/** Push a chunk of firmware to the RTIP
 * @param chunk a Pointer to data to be sent
 */
bool i2spiBootloaderPushChunk(FirmwareBlock* chunk);

/** Push a bootload command done message to the RTIP
 */
bool i2spiBootloaderCommandDone(void);

/// Count how many tx underruns we've had
extern uint32_t i2spiTxUnderflowCount;
/// Count how many tx overruns we've had
extern uint32_t i2spiTxOverflowCount;
/// Count how many RX overruns we've had
extern uint32_t i2spiRxOverflowCount;
/// Count how many times the drop phase has jumped more than we expected it to
extern uint32_t i2spiPhaseErrorCount;
/// Count the integral drift in the I2SPI system
extern int32_t i2spiIntegralDrift;


#endif
