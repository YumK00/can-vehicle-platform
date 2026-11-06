#include "can.h"     // 已带 stm32f1xx_hal.h(含 can 模块)
#include "main.h"    // Error_Handler()

/* CAN1 句柄：can.h 里 extern 声明，这里真正分配内存 */
CAN_HandleTypeDef hcan1;

/* 接收队列句柄：can.h 里 extern 声明。
 * 真正的创建放在 MX_FREERTOS_Init() 里 —— 因为 CAN_Init() 在
 * osKernelInitialize() 之前就被调用，那会儿 RTOS 还没初始化好。 */
osMessageQueueId_t canRxQueue;

/**
 * @brief  CAN1 初始化：正常(Normal)模式，250kbps
 * @note   波特率：CAN1 挂在 APB1=36MHz，位时间 = (1+BS1+BS2) tq
 *         = (1+13+2)=16tq，分频9 → 36M/(9×16) = 250kbps。
 *         采样点 = (1+BS1)/(1+BS1+BS2) = 14/16 = 87.5%。
 *
 *         引脚/时钟在 HAL_CAN_MspInit() 里配(PA11=RX、PA12=TX、CAN1/GPIOA 时钟)，
 *         HAL_CAN_Init() 内部自动回调它，这里只配 CAN 自身参数。
 *
 *         正常模式：收发都走外部引脚。必须外接收发器(TJA1050 等)接 PA11/PA12，
 *         总线两端各一个 120Ω 终端电阻。少了这两样，总线会一直报错、收不到帧。
 * @retval None
 */
void CAN_Init(void)
{
    hcan1.Instance = CAN1;
    hcan1.Init.Mode = CAN_MODE_NORMAL;        // 正常模式：收发都经过外部引脚
    hcan1.Init.Prescaler = 9;                 // 分频：36MHz/9 = 4MHz 的 tq 时钟
    hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;        // 时间段1 =13 tq
    hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;        // 时间段2 = 2 tq
    hcan1.Init.SyncJumpWidth = CAN_SJW_2TQ;   // 同步跳变宽度 = 2 tq

    hcan1.Init.TimeTriggeredMode = DISABLE;   // 不用时间触发
    /* AutoBusOff(ABOM)：掉到 BUS-OFF 之后能不能自己爬回来。
     *   ENABLE  = 硬件在总线上数到 128 次"连续 11 位隐性"就自动恢复收发 —— 常规配置
     *   DISABLE = 硬件置起 INIT 就不管了，【软件不清 INIT 就永远醒不过来】，
     *             而本项目没有任何地方清 INIT ⇒ 一旦掉线只能手动复位。
     * 注意它管的是【恢复】，不是"要不要掉线"：TEC>255 时掉线是硬件强制的，拦不住。 */
    hcan1.Init.AutoBusOff = ENABLE;
    hcan1.Init.AutoWakeUp = DISABLE;          // 不自动唤醒(只在 Sleep 模式下有意义)
    /* AutoRetransmission(NART)：发失败要不要硬件自动重发。
     *   ENABLE  = 一直重发到成功为止(或掉到 BUS-OFF) —— 常规配置，不丢帧
     *   DISABLE = 只发一次，失败就丢，错误计数 +8，邮箱释放
     * 排物理层故障时可以临时改成 DISABLE：那时 ENABLE 会疯狂重发、刷一堆错误帧
     * 把总线搅得更乱、更快掉线。物理层正常时就用 ENABLE。 */
    hcan1.Init.AutoRetransmission = ENABLE;
    hcan1.Init.ReceiveFifoLocked = DISABLE;   // 接收FIFO满时新帧覆盖旧帧
    hcan1.Init.TransmitFifoPriority = DISABLE;// 发送邮箱按ID优先级决定
    HAL_CAN_Init(&hcan1);                     // 内部回调 HAL_CAN_MspInit


    CAN_FilterTypeDef sFilterConfig = {0};
    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;   // 掩码模式
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;  // 32位：标准/扩展ID通用
    sFilterConfig.FilterIdHigh = 0x0000;
    sFilterConfig.FilterIdLow = 0x0000;
    sFilterConfig.FilterMaskIdHigh = 0x0000;
    sFilterConfig.FilterMaskIdLow = 0x0000;
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;  // 收到的帧进FIFO0
    sFilterConfig.FilterActivation = ENABLE;            // 启用该过滤器
    sFilterConfig.SlaveStartFilterBank = 14;            // F1无主从，此字段无实际作用

    if (HAL_CAN_ConfigFilter(&hcan1, &sFilterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_CAN_Start(&hcan1) != HAL_OK)
    {
        Error_Handler();
    }

    /* 使能 RX FIFO0 的"有新消息"中断。
     * 这样收到一帧后硬件会进 USB_LP_CAN1_RX0_IRQHandler，
     * 不用在主循环里轮询 FIFO 有没有数据。 */
    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief  发送一帧标准数据帧
 * @param  id：11位标准ID(0~0x7FF)
 * @param  data：数据缓冲区首地址
 * @param  length：数据字节数(1~8)
 * @note   一帧CAN数据最多8字节，超过直接返回失败。
 *         发送失败(比如3个发送邮箱都满)是正常情况，不卡死，
 *         直接丢这一帧返回 0，调用方可自行决定要不要重发。
 * @retval 1 = 发送成功；0 = 长度非法 / 无空闲邮箱 / 发送失败
 */
uint8_t CAN_Send(uint32_t id, uint8_t *data, uint8_t length)
{
    if (length > 8)
    {
        return 0;                     // CAN 一帧最多 8 字节
    }

    /* 没有空闲发送邮箱(3个都在忙)就直接丢帧返回，不能 Error_Handler 卡死 */
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0)
    {
        return 0;
    }

    CAN_TxHeaderTypeDef TxHeader = {0};
    TxHeader.StdId = id;              // 标准ID
    TxHeader.IDE = CAN_ID_STD;        // 标准帧(11位ID)
    TxHeader.RTR = CAN_RTR_DATA;      // 数据帧(不是远程帧)
    TxHeader.DLC = length;            // 数据长度

    uint8_t TxData[8] = {0};
    for (uint8_t i = 0; i < length; i++)
    {
        TxData[i] = data[i];          // 拷贝到发送缓冲
    }

    uint32_t TxMailbox = 0;           // 输出：用的哪个邮箱，这里不关心
    return (HAL_CAN_AddTxMessage(&hcan1, &TxHeader, TxData, &TxMailbox) == HAL_OK) ? 1 : 0;
}

/**
 * @brief  CAN1 RX FIFO0 中断服务函数(名字必须和向量表一致)
 * @note   F103 上 CAN1 的 RX0 中断和 USB 低优先级共用一个中断向量，
 *         所以向量名是 USB_LP_CAN1_RX0_IRQHandler(= CAN1_RX0_IRQHandler 宏别名)。
 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    HAL_CAN_IRQHandler(&hcan1);   // HAL 判断是哪种 CAN 中断，再回调对应回调函数
}

/**
 * @brief  CAN RX FIFO0 收到新消息的回调(HAL 弱函数，重写它做自己的处理)
 * @note   运行在"中断上下文"里，所以要快：把整帧拷进结构体、丢进队列就返回，
 *         重活(解析/显示)留给 canRxTask。
 */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef RxHeader;
    CanFrame frame;
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &RxHeader, frame.data) != HAL_OK)
    {
        return;
    }

    frame.id  = RxHeader.StdId;
    frame.len = RxHeader.DLC;

    /* 超时必须给 0：中断里不能阻塞等队列。
     * 队列满(接收任务没跟上)时返回失败 —— 显示用途丢一帧无所谓，
     * 比在中断里卡住总线强。 */
    (void)osMessageQueuePut(canRxQueue, &frame, 0U, 0U);
}
