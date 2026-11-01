#include "io_probe.h"

#include "FreeRTOS.h"
#include "task.h"

static can_probe_t can_last;
static rs485_probe_t rs485_last;

/* A pin that reads high with the internal pull-down (or low with the pull-up) is being
 * driven by something stronger than ~40 kOhm, i.e. an external chip's output. */
line_state_t io_line_state(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef gpio = {.Pin = pin, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_PULLDOWN,
                             .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(port, &gpio);
    vTaskDelay(pdMS_TO_TICKS(2));
    bool high_with_pulldown = HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(port, &gpio);
    vTaskDelay(pdMS_TO_TICKS(2));
    bool low_with_pullup = HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(port, &gpio);
    if (high_with_pulldown) return LINE_DRIVEN_HIGH;
    if (low_with_pullup) return LINE_DRIVEN_LOW;
    return LINE_FLOATING;
}

const char *io_line_str(line_state_t s)
{
    switch (s) {
    case LINE_DRIVEN_HIGH: return "driven high";
    case LINE_DRIVEN_LOW:  return "driven low";
    default:               return "floating";
    }
}

can_probe_t can_probe(void)
{
    can_probe_t r = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    r.pb8 = io_line_state(GPIOB, GPIO_PIN_8);
    r.pb9 = io_line_state(GPIOB, GPIO_PIN_9);

    /* CAN1 on PB8 (RX) / PB9 (TX), AF9, 500 kbit/s: 42 MHz / 6 = 7 MHz, 1 + 11 + 2 = 14 tq */
    __HAL_RCC_CAN1_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {.Pin = GPIO_PIN_8 | GPIO_PIN_9, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_PULLUP,
                             .Speed = GPIO_SPEED_FREQ_HIGH, .Alternate = GPIO_AF9_CAN1};
    HAL_GPIO_Init(GPIOB, &gpio);

    CAN_HandleTypeDef hcan = {0};
    hcan.Instance = CAN1;
    hcan.Init.Prescaler = 6;
    hcan.Init.Mode = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1 = CAN_BS1_11TQ;
    hcan.Init.TimeSeg2 = CAN_BS2_2TQ;
    hcan.Init.AutoRetransmission = DISABLE; /* one-shot: a single frame, no bus flooding */
    if (HAL_CAN_Init(&hcan) == HAL_OK && HAL_CAN_Start(&hcan) == HAL_OK) {
        CAN_TxHeaderTypeDef h = {.StdId = 0x7E5, .IDE = CAN_ID_STD, .RTR = CAN_RTR_DATA, .DLC = 2};
        uint8_t data[2] = {0x4B, 0x53};
        uint32_t mailbox;
        r.tx_attempted = HAL_CAN_AddTxMessage(&hcan, &h, data, &mailbox) == HAL_OK;
        vTaskDelay(pdMS_TO_TICKS(10));
        r.tx_ok = (CAN1->TSR & CAN_TSR_TXOK0) != 0;
        r.lec = (uint8_t)((CAN1->ESR & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos);
        HAL_CAN_Stop(&hcan);
    }
    HAL_CAN_DeInit(&hcan);
    __HAL_RCC_CAN1_CLK_DISABLE();
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio);

    /* LEC 3 = ACK error: our own bits came back through a transceiver, nobody ACKed.
     * LEC 4/5 = bit error: RX does not follow TX, so no transceiver on these pins. */
    if (!r.tx_attempted) r.verdict = "CAN controller did not start (RX held dominant?)";
    else if (r.tx_ok) r.verdict = "transceiver present, another node ACKed";
    else if (r.lec == 3) r.verdict = "transceiver echoes TX; no other node on the bus";
    else if (r.lec == 4 || r.lec == 5) r.verdict = "no transceiver echo on PB8/PB9";
    else r.verdict = "inconclusive";
    can_last = r;
    return r;
}

rs485_probe_t rs485_probe(void)
{
    rs485_probe_t r = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /* PG8 = DE/RE (BSP RS485_RTS): low = receive, so the transceiver drives RO */
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_8, GPIO_PIN_RESET);
    GPIO_InitTypeDef gpio = {.Pin = GPIO_PIN_8, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                             .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(GPIOG, &gpio);
    vTaskDelay(pdMS_TO_TICKS(2));
    r.pc7 = io_line_state(GPIOC, GPIO_PIN_7);
    r.pg9 = io_line_state(GPIOG, GPIO_PIN_9);

    if (r.pc7 != LINE_FLOATING && r.pg9 == LINE_FLOATING) r.verdict = "transceiver RO on PC7 (USART6_RX)";
    else if (r.pg9 != LINE_FLOATING && r.pc7 == LINE_FLOATING) r.verdict = "transceiver RO on PG9 (USART6_RX)";
    else if (r.pc7 == LINE_FLOATING && r.pg9 == LINE_FLOATING) r.verdict = "no driven RX line: not fitted?";
    else r.verdict = "both candidates driven: inconclusive";
    rs485_last = r;
    return r;
}

const can_probe_t *can_last_probe(void) { return &can_last; }
const rs485_probe_t *rs485_last_probe(void) { return &rs485_last; }
