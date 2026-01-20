#ifndef SPI_DRIVER_H
#define SPI_DRIVER_H

#include "hw_model.h"
#include <stdbool.h>
#include <stdint.h>

// Driver error codes
typedef enum {
    SPI_OK = 0,
    SPI_ERR_INVALID_ARG,
    SPI_ERR_TIMEOUT,
    SPI_ERR_BUSY,
    SPI_ERR_MODE,
    SPI_ERR_HW
} SPI_Error;

// SPI Configuration
typedef struct {
    uint32_t baud_rate;
    uint8_t data_size;
    uint8_t clock_polarity;
    uint8_t clock_phase;
    uint8_t bit_order;
    bool software_slave_management;
    bool master_mode;
} SPI_Config;

// External declaration of default config (defined in spi_driver.c)
extern const SPI_Config default_config;

// SPI Driver Instance
typedef struct {
    SPI_HW_Model* hw_model;
    SPI_Config config;
    bool initialized;
    bool transfer_in_progress;
    uint32_t total_transfers;
    uint32_t total_bytes;
    uint64_t total_latency_cycles;
    uint32_t error_count;
    void (*pre_transfer_hook)(void* ctx);
    void (*post_transfer_hook)(void* ctx, SPI_Error result);
    void* hook_context;
} SPI_Driver;

// Public API
SPI_Error spi_driver_init(SPI_Driver* driver, uint32_t base_addr, SPI_Config* config);
SPI_Error spi_driver_deinit(SPI_Driver* driver);
SPI_Error spi_driver_transfer(SPI_Driver* driver, uint8_t* tx_data, 
                              uint8_t* rx_data, uint32_t length, uint32_t timeout_ms);
SPI_Error spi_driver_transfer_dma(SPI_Driver* driver, uint8_t* tx_data,
                                  uint8_t* rx_data, uint32_t length);
SPI_Error spi_driver_set_baudrate(SPI_Driver* driver, uint32_t baud_rate);
SPI_Error spi_driver_get_status(SPI_Driver* driver);
void spi_driver_print_stats(SPI_Driver* driver);
float spi_driver_get_efficiency(SPI_Driver* driver);

#endif // SPI_DRIVER_H