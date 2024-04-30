// This configuration is for a generic ESP32C3 board with 4MiB (or more) of flash.

#define MICROPY_HW_BOARD_NAME               "Farm monitor communication board"
#define MICROPY_HW_MCU_NAME                 "ESP32C3"

#define MICROPY_HW_ENABLE_SDCARD            (0)
#define MICROPY_PY_MACHINE_I2S              (0)
#define MICROPY_TASK_STACK_SIZE             (24 * 1024)
#define MICROPY_GC_INITIAL_HEAP_SIZE        (56 * 1024)

#define MICROPY_PY_SYS_EXC_INFO             (0)
// #define MICROPY_MALLOC_USES_ALLOCATED_SIZE  (1)
// #define MICROPY_MEM_STATS                   (1)
// #define MICROPY_PY_SYS_SETTRACE             (1)

// Enable UART REPL for modules that have an external USB-UART and don't use native USB.
#define MICROPY_HW_ENABLE_UART_REPL         (0)
#define MICROPY_PY_NETWORK_HOSTNAME_DEFAULT ("farm-monitor-cm")
//#define MICROPY_PY_MACHINE_ADC (0)