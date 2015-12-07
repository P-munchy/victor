#include "anki/cozmo/robot/hal.h"
#include "hal/portable.h"
#include "hal/i2c.h"
#include "hal/imu.h"
#include "MK02F12810.h"

struct {
  int16_t gyro[3];
  int16_t acc[3];
} IMUData;

void Anki::Cozmo::HAL::IMUInit(void) {
  ReadIMUID();
  IMUManage();
}

const uint8_t DATA_8[] = { ADDR_IMU << 1, 0x0C };
const uint8_t read_imu = (ADDR_IMU << 1) + 1;

void Anki::Cozmo::HAL::IMUManage(void) {
  /*
  I2CCmd(I2C_DIR_WRITE | I2C_SEND_START | I2C_SEND_STOP, (uint8_t*)DATA_8, sizeof(DATA_8), NULL);
  I2CCmd(I2C_DIR_WRITE | I2C_SEND_START, (uint8_t*)&read_imu, sizeof(read_imu), NULL);
  I2CCmd( I2C_DIR_READ |  I2C_SEND_STOP, (uint8_t*)&IMUData, sizeof(IMUData), NULL);
  */
}

uint8_t Anki::Cozmo::HAL::ReadIMUID(void) {
  return I2CReadReg(ADDR_IMU, 0);
}
