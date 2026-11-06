#include "serial.h"       // 已带 stm32f1xx_hal.h(含 uart 模块)
#include "string.h"        // strlen：求字符串长度

/* ========== UART句柄的"定义" ==========
 * HAL_UART_Init / HAL_UART_Transmit 都需要一个 UART_HandleTypeDef 结构体
 * 来保存外设配置和运行状态，这个结构体必须真正分配一块内存。
 *
 * 注意区分"声明"和"定义"：
 *   - 这里(serial.c) 写的是   UART_HandleTypeDef huart2;          → 定义，分配内存
 *   - serial.h 里写的是 extern UART_HandleTypeDef huart2;         → 声明，只告诉编译器
 *     "别的文件里有这么一个变量"，不分配内存。
 * 有 extern 没定义 → 链接时报 undefined reference；两处都定义 → 报重复定义。
 * 规矩：定义放 .c(只能一处)，声明放 .h。
 */
UART_HandleTypeDef huart2;

/**
 * @brief  UART初始化：异步全双工串口，PA2=TX、PA3=RX，波特率115200，8N1
 * @note   硬件过程：CPU把数据写进DR数据寄存器 → USART把数据搬进移位寄存器
 *         → 移位寄存器按波特率一位一位从TX引脚移出去。
 *          注：usart是《《低位先行》》，所以发送0x12时，先发0x02再发0x01。
 *         配置流程(Init/MspInit 分工)：
 *         1. HAL_UART_MspInit()：RCC开 GPIOA/USART2 时钟，PA2 配复用推挽输出(TX)、
 *            PA3 配上拉输入(RX)。MCU 层支撑，由 HAL_UART_Init 自动回调；
 *         2. Serial_Init()：只配置 USART 工作模式(全双工、波特率115200、8N1)；
 *         3. HAL_UART_Init()：按上面参数初始化外设。
 *
 *         注意：PA2/PA3 是 USART2 的引脚，USART1 的引脚是 PA9/PA10。
 *         引脚、Instance、时钟三者必须对应同一个外设，否则串口完全不工作。
 *
 *         异步串口含义：收发双方约定好波特率，不需要额外时钟线，
 *         靠起始位/停止位对时，是最常用的串口通信方式。
 * @retval None
 */
void Serial_Init(void)
{
    // ==========只配置 USART 外设自身的参数==========
    // 引脚(PA2/PA3)和时钟在 HAL_UART_MspInit() 里配，
    // HAL_UART_Init() 内部会自动回调它。这就是 Init / MspInit 的分工：
    //   Init    → 外设自己的工作参数(波特率、数据位、停止位…)
    //   MspInit → MCU 层面的支撑(GPIO 引脚、时钟使能)
    huart2.Instance = USART2;                  // 指定USART实例为USART2
    huart2.Init.Mode = UART_MODE_TX_RX;       // 全双工：既能发也能收
    huart2.Init.BaudRate = 115200;             // 波特率：每秒115200个位(PC端串口脚本默认也是115200)
    huart2.Init.WordLength = UART_WORDLENGTH_8B; // 数据位：8位
    huart2.Init.StopBits = UART_STOPBITS_1;   // 停止位：1位
    huart2.Init.Parity = UART_PARITY_NONE;    // 校验：无(合起来就是常见的 8N1)
    HAL_UART_Init(&huart2);                   // 内部会回调 HAL_UART_MspInit
}

/**
 * @brief  阻塞发送一段数据(任意字节)
 * @param  pTxData：要发送的数据缓冲区首地址
 * @param  Size：要发送的字节数
 * @param  Timeout：超时时间(ms)，超过还没发完就返回HAL_TIMEOUT
 * @note   HAL_UART_Transmit是阻塞函数：内部会自动"等TXE标志置1→写DR→等TC标志"，
 *         直到所有字节发完(或超时)才返回，所以调用后不用再手动等标志。
 * @retval None
 */
void Serial_SendData(const uint8_t *pTxData, uint16_t Size, uint32_t Timeout)
{
    HAL_UART_Transmit(&huart2, pTxData, Size, Timeout);
}

/**
 * @brief  阻塞发送一个字符串(结尾'\0'之前的所有字符)
 * @param  pTxData：以'\0'结尾的字符串
 * @param  Timeout：超时时间(ms)
 * @note   先用strlen算出字符串长度(不含结尾'\0')，再交给Serial_SendData发送。
 *         注意：结尾的'\0'不会被发出去，这是串口发送字符串的标准做法。
 * @retval None
 */
void Serial_SendString(const char *pTxData, uint32_t Timeout)
{
    uint16_t Size = strlen(pTxData);                      // 求字符串实际长度(不含'\0')
    HAL_UART_Transmit(&huart2, (uint8_t *)pTxData, Size, Timeout);
}

/**
 * @brief  阻塞接收指定字节数的数据
 * @param  pRxData：接收缓冲区首地址(由调用者提供)
 * @param  Size：要接收的字节数
 * @param  Timeout：超时时间(ms)
 * @note   把 HAL_UART_Receive 的返回值原样传出去，调用者才能知道到底收全了没有。
 *         返回 HAL_OK     ：Size个字节全部收到，数据可用；
 *         返回 HAL_TIMEOUT：超时了，没收满，pRxData 里可能是残缺数据。
 *         调用者如果不管返回值，超时的时候就会拿着旧数据/垃圾数据当有效数据用。
 * @retval HAL_OK / HAL_TIMEOUT / HAL_ERROR / HAL_BUSY
 */
HAL_StatusTypeDef Serial_Receive(uint8_t *pRxData, uint16_t Size, uint32_t Timeout){
    return HAL_UART_Receive(&huart2, pRxData, Size, Timeout);
}

/**
 * @brief  按自定义帧格式接收一帧hex数据(阻塞式)
 * @param  pData：输出，接收到的数据区首地址(由调用者提供缓冲)
 * @param  pLen：输出，实际收到的数据字节数
 * @param  Timeout：超时时间(ms)
 * @note   帧格式：[0xAA][0x55][长度][数据...][校验和]
 *         - 帧头0xAA 0x55：用来"对齐"数据流，分辨一帧从哪开始；
 *         - 长度：数据区的字节数；
 *         - 数据：真正的hex原始数据；
 *         - 校验和：长度+数据所有字节累加后取低8位，用来判断传输有没有出错。
 *
 *         接收流程(逐字节状态机)：
 *         1. 一直等0xAA(不是就丢弃，重新等)；
 *         2. 等到0xAA后，下一个字节必须是0x55，否则回到第1步；
 *         3. 读长度字节；
 *         4. 按长度读数据，同时累加校验和；
 *         5. 读校验和字节，跟本地算的比对，一致才算成功。
 *
 *         为什么帧头要两个字节：单个0xAA容易在数据里"误撞"上，
 *         连续两个特定字节同时出现的概率极低，帧头更可靠。
 * @retval 1：成功解析一帧；0：超时/帧头不对/校验错误
 */
uint8_t Serial_ReceiveFrame(uint8_t *pData, uint8_t *pLen, uint32_t Timeout)
{
    uint8_t byte = 0;       // 临时存放每次收到的1个字节
    uint8_t len  = 0;       // 帧里的长度字段
    uint8_t sum  = 0;       // 本地累加的校验和

    // ==========第1步：等帧头0xAA==========
    while (1)
    {
        if (HAL_UART_Receive(&huart2, &byte, 1, Timeout) != HAL_OK)
        {
            return 0;                        // 超时没收到数据
        }
        if (byte == FRAME_HEAD1)
        {
            break;                           // 等到0xAA，跳出
        }
        // 不是0xAA就丢弃，继续等下一个字节
    }

    // ==========第2步：下一个字节必须是0x55==========
    if (HAL_UART_Receive(&huart2, &byte, 1, Timeout) != HAL_OK)
    {
        return 0;
    }
    if (byte != FRAME_HEAD2)
    {
        return 0;                            // 帧头不完整，作废
    }

    // ==========第3步：读长度字段==========
    if (HAL_UART_Receive(&huart2, &byte, 1, Timeout) != HAL_OK)
    {
        return 0;
    }
    len = byte;
    if (len > FRAME_MAX_LEN)
    {
        return 0;                            // 长度超上限，判定错帧
    }

    // ==========第4步：读数据区，并累加校验和==========
    for (uint8_t i = 0; i < len; i++)
    {
        if (HAL_UART_Receive(&huart2, &byte, 1, Timeout) != HAL_OK)
        {
            return 0;
        }
        pData[i] = byte;                     // 存下数据
        sum += byte;                         // 累加校验和(数据部分)
    }
    sum += len;                              // 校验和把长度也加进去

    // ==========第5步：读校验和字节，比对==========
    if (HAL_UART_Receive(&huart2, &byte, 1, Timeout) != HAL_OK)
    {
        return 0;
    }
    if (byte != sum)
    {
        return 0;                            // 校验和不一致，数据传错了
    }

    *pLen = len;                             // 把实际长度传出去
    return 1;                                // 成功解析一帧
}

/**
 * @brief  按帧格式打包并发送一帧hex数据(阻塞式)
 * @param  pData：要发送的数据区首地址
 * @param  Len：数据字节数(上限FRAME_MAX_LEN)
 * @param  Timeout：超时时间(ms)
 * @note   打包成 [0xAA][0x55][长度][数据...][校验和] 后一次性发出，
 *         和Serial_ReceiveFrame配套使用(校验和算法必须一致)。
 *         校验和 = 长度 + 所有数据字节，累加取低8位。
 * @retval None
 */
void Serial_SendFrame(const uint8_t *pData, uint8_t Len, uint32_t Timeout)
{
    uint8_t txbuf[FRAME_MAX_LEN + 4] = {0};  // 帧缓冲：2帧头+1长度+数据+1校验
    uint8_t sum = 0;

    // 组帧：帧头、长度、数据、校验和
    txbuf[0] = FRAME_HEAD1;                  // 帧头第1字节
    txbuf[1] = FRAME_HEAD2;                  // 帧头第2字节
    txbuf[2] = Len;                          // 长度字段
    for (uint8_t i = 0; i < Len; i++)
    {
        txbuf[3 + i] = pData[i];             // 拷贝数据区
        sum += pData[i];                     // 累加数据部分校验和
    }
    sum += Len;                              // 校验和把长度也加进去
    txbuf[3 + Len] = sum;                    // 最后一个字节=校验和

    // 一次性发出整帧(总长 = 3个固定字节 + 数据长度 + 1个校验字节)
    HAL_UART_Transmit(&huart2, txbuf, (uint16_t)(Len + 4), Timeout);
}