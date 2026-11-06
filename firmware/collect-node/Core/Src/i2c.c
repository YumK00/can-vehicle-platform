// #include "i2c.h"
// #include "stm32f1xx_hal_gpio.h"

// // I2C2句柄：全局变量，保存I2C2配置与运行状态
// I2C_HandleTypeDef hi2c2;

// /**
//  * @brief  I2C2初始化：PB10=SCL、PB11=SDA，标准模式100kHz
//  * @note   I2C引脚必须配成复用开漏(GPIO_MODE_AF_OD)：
//  *         开漏才能实现"线与"，多设备共用总线时靠外部上拉电阻把线拉高。
//  *         所以PB10/PB11外部要接4.7k上拉电阻到3.3V。
//  * @retval None
//  */
// void I2C_Init(void)
// {
//     // ==========开启GPIOB、I2C2时钟==========
//     __HAL_RCC_GPIOB_CLK_ENABLE();   // PB10/PB11所在端口
//     __HAL_RCC_I2C2_CLK_ENABLE();    // I2C2外设，挂在APB1(36MHz)上

//     // ==========GPIO配置：PB10=SCL、PB11=SDA 复用开漏==========
//     GPIO_InitTypeDef gpio = {0};
//     gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;   // PB10=SCL, PB11=SDA
//     gpio.Mode = GPIO_MODE_AF_OD;            // I2C必须开漏(配合外部上拉做线与)
//     gpio.Pull = GPIO_NOPULL;                // 需要外部4.7k上拉电阻
//     gpio.Speed = GPIO_SPEED_FREQ_HIGH;
//     HAL_GPIO_Init(GPIOB, &gpio);

//     // ==========I2C2工作模式配置==========
//     hi2c2.Instance = I2C2;                          // 指定实例为I2C2
//     hi2c2.Init.ClockSpeed = 100000;                 // 100kHz标准模式
//     hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; // 7位地址
//     //当配置为高速模式(100kHz~400kHz)时才需要配置占空比
//     hi2c2.Init.DutyCycle = I2C_DUTYCYCLE_2;         // 高速模式占空比，100k时忽略
//     //当配置为从机10位地址时才需要配置以下参数
//     hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE; // 不用双地址
//     hi2c2.Init.OwnAddress1 = 0;                     // 自身地址：主模式用不到，填0
//     hi2c2.Init.OwnAddress2 = 0;                     // 第二个自身地址(已禁用)
//     hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; // 不响应广播
//     hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;     // 允许时钟拉伸
//     HAL_I2C_Init(&hi2c2);
// }

// /**
//  * @brief  写MPU6050单个寄存器
//  * @param  reg：寄存器地址(如0x6B=电源管理)
//  * @param  value：要写入的值(1字节)
//  * @note   HAL_I2C_Mem_Write参数顺序：
//  *         (句柄, 器件地址, 寄存器地址, 寄存器地址长度, 数据, 数据长度, 超时)
//  * @retval None
//  */
// void MPU6050_WriteReg(uint8_t reg, uint8_t value)
// {
//     uint8_t data = value;
//     HAL_I2C_Mem_Write(&hi2c2, MPU6050_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &data, 1, 1000);
// }

// /**
//  * @brief  MPU6050初始化：退出睡眠、配置采样率和量程
//  * @note   上电后MPU6050默认在睡眠模式(PWR_MGMT_1的SLEEP位=1)，
//  *         必须先写PWR_MGMT_1=0x01退出睡眠，否则读不到数据。
//  * @retval None
//  */
// void MPU6050_Init(void)
// {
//     I2C_Init();

//     MPU6050_WriteReg(MPU6050_PWR_MGMT_1,  0x01);  // 退出睡眠，时钟选X轴陀螺
//     MPU6050_WriteReg(MPU6050_PWR_MGMT_2,  0x00);  // 所有传感器使能
//     MPU6050_WriteReg(MPU6050_SMPLRT_DIV,  0x09);  // 采样率分频：1000/(1+9)=100Hz
//     MPU6050_WriteReg(MPU6050_CONFIG,      0x06);  // 数字低通滤波约5Hz
//     MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x18);  // 陀螺仪量程±2000°/s
//     MPU6050_WriteReg(MPU6050_ACCEL_CONFIG,0x18);  // 加速度计量程±16g
// }

// /**
//  * @brief  连续写多个寄存器
//  * @param  MemAddress：起始寄存器地址
//  * @param  MemAddSize：寄存器地址长度(MPU6050用I2C_MEMADD_SIZE_8BIT=1字节)
//  * @param  pData：数据缓冲区
//  * @param  Size：字节数
//  * @retval None
//  */
// void MPU6050_WriteRegs(uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size)
// {
//     HAL_I2C_Mem_Write(&hi2c2, MPU6050_ADDR, MemAddress, MemAddSize, pData, Size, 1000);
// }

// /**
//  * @brief  连续读多个寄存器
//  * @param  MemAddress：起始寄存器地址
//  * @param  MemAddSize：寄存器地址长度(1字节)
//  * @param  pData：读到的数据存放缓冲区
//  * @param  Size：字节数
//  * @retval None
//  */
// void MPU6050_ReadRegs(uint16_t MemAddress, uint16_t MemAddSize, uint8_t *pData, uint16_t Size)
// {
//     HAL_I2C_Mem_Read(&hi2c2, MPU6050_ADDR, MemAddress, MemAddSize, pData, Size, 1000);
// }

// /**
//  * @brief  一次性读取加速度+角速度(共14字节)，解析成有符号数据
//  * @param  data：输出，存放解析后的6轴数据
//  * @note   从0x3B开始连续读14字节，寄存器排列：
//  *         AccX(0x3B,0x3C) AccY(0x3D,0x3E) AccZ(0x3F,0x40)
//  *         温度(0x41,0x42) 
//  *         GyroX(0x43,0x44) GyroY(0x45,0x46) GyroZ(0x47,0x48)
//  *         每个轴16位，高字节在前(大端)，所以拼成 (高<<8)|低。
//  *         中间温度两个字节(buffer[6..7])跳过不用。
//  * @retval None
//  */
// void MPU6050_GetData(MPU6050Data *data)
// {
//     uint8_t buffer[14];
//     MPU6050_ReadRegs(MPU6050_ACCEL_XOUT_H, I2C_MEMADD_SIZE_8BIT, buffer, 14);

//     data->AccX = (int16_t)((buffer[0] << 8) | buffer[1]);
//     data->AccY = (int16_t)((buffer[2] << 8) | buffer[3]);
//     data->AccZ = (int16_t)((buffer[4] << 8) | buffer[5]);

//     data->GyroX = (int16_t)((buffer[8]  << 8) | buffer[9]);
//     data->GyroY = (int16_t)((buffer[10] << 8) | buffer[11]);
//     data->GyroZ = (int16_t)((buffer[12] << 8) | buffer[13]);
// }
