/* Конфигурация ядра OpenThread для JN5169 (RCP, 32 КБ RAM) */
#ifndef OPENTHREAD_CORE_JN5169_CONFIG_H_
#define OPENTHREAD_CORE_JN5169_CONFIG_H_

#define OPENTHREAD_CONFIG_PLATFORM_INFO "JN5169-DGNWG05LM"

/* RCP: Spinel поверх HDLC/UART */
#define OPENTHREAD_CONFIG_NCP_HDLC_ENABLE 1
/* RADIO-ONLY: без этого otMacFrameProcessTransmitAesCcm = пустышка (FTD=MTD=0) */
#define OPENTHREAD_CONFIG_MAC_SOFTWARE_TX_SECURITY_ENABLE 1

/* Экономия RAM: минимум буферов сообщений (в RCP они почти не используются) */
#define OPENTHREAD_CONFIG_NUM_MESSAGE_BUFFERS 12
#define OPENTHREAD_CONFIG_MESSAGE_BUFFER_SIZE 128

/* Возможности радио: CSMA делает MMAC, остальное — хост */
#define OPENTHREAD_CONFIG_MAC_CSL_TRANSMITTER_ENABLE 0
#define OPENTHREAD_CONFIG_MAC_CSL_RECEIVER_ENABLE 0

/* Без файловой системы: настройки в RAM (для RCP этого достаточно) */
#define OPENTHREAD_SETTINGS_RAM 1

/* Логи по умолчанию выключены (UART занят Spinel) */
#ifndef OPENTHREAD_CONFIG_LOG_OUTPUT
#define OPENTHREAD_CONFIG_LOG_OUTPUT OPENTHREAD_CONFIG_LOG_OUTPUT_NONE
#endif

#endif /* OPENTHREAD_CORE_JN5169_CONFIG_H_ */
