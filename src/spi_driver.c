#include "spi_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>  // Added for malloc/free

// Default configuration - now visible to other files
const SPI_Config default_config = {
    .baud_rate = 1000000,
    .data_size = 8,
    .clock_polarity = 0,
    .clock_phase = 0,
    .bit_order = 0,
    .software_slave_management = true,
    .master_mode = true
};

// Rest of the file unchanged except for minor cleanups (same as previous version)
SPI_Error spi_driver_init(SPI_Driver* driver, uint32_t base_addr, SPI_Config* config) {
    if (!driver) return SPI_ERR_INVALID_ARG;
    driver->hw_model = (SPI_HW_Model*)malloc(sizeof(SPI_HW_Model));
    if (!driver->hw_model) return SPI_ERR_HW;
    spi_hw_init(driver->hw_model, base_addr);
    driver->config = config ? *config : default_config;

    uint32_t cr1_value = 0;
    if (driver->config.baud_rate <= 1000000) cr1_value |= (0b000 << 3);
    else if (driver->config.baud_rate <= 2000000) cr1_value |= (0b001 << 3);
    else cr1_value |= (0b010 << 3);
    if (driver->config.clock_polarity) cr1_value |= (1 << 1);
    if (driver->config.clock_phase) cr1_value |= (1 << 0);
    if (driver->config.master_mode) cr1_value |= (1 << 2);
    if (driver->config.data_size == 16) cr1_value |= (1 << 11);
    spi_hw_write_reg(driver->hw_model, 0x00, cr1_value);

    uint32_t cr2_value = 0;
    if (driver->config.software_slave_management) cr2_value |= (1 << 2);
    spi_hw_write_reg(driver->hw_model, 0x04, cr2_value);

    cr1_value |= (1 << 6);
    spi_hw_write_reg(driver->hw_model, 0x00, cr1_value);

    driver->initialized = true;
    driver->transfer_in_progress = false;
    driver->total_bytes = driver->total_transfers = 0;
    driver->total_latency_cycles = 0;
    driver->error_count = 0;

    printf("[DRIVER] SPI driver initialized at 0x%08X\n", base_addr);
    printf("         Baud: %u, Mode: %d%d, %s\n", 
           driver->config.baud_rate,
           driver->config.clock_polarity,
           driver->config.clock_phase,
           driver->config.master_mode ? "Master" : "Slave");
    return SPI_OK;
}

// spi_driver_transfer, spi_driver_print_stats, spi_driver_get_efficiency unchanged (same as before)

SPI_Error spi_driver_deinit(SPI_Driver* driver) {
    if (!driver || !driver->initialized) return SPI_ERR_INVALID_ARG;
    if (driver->hw_model) {
        free(driver->hw_model);
        driver->hw_model = NULL;
    }
    driver->initialized = false;
    printf("[DRIVER] SPI driver deinitialized\n");
    return SPI_OK;
}

SPI_Error spi_driver_transfer_dma(SPI_Driver* driver, uint8_t* tx_data,
                                  uint8_t* rx_data, uint32_t length) {
    (void)driver; (void)tx_data; (void)rx_data; (void)length;
    printf("[DRIVER] DMA transfer not supported in simulation mode\n");
    return SPI_ERR_MODE;
}

SPI_Error spi_driver_set_baudrate(SPI_Driver* driver, uint32_t baud_rate) {
    if (!driver || !driver->initialized) return SPI_ERR_INVALID_ARG;
    driver->config.baud_rate = baud_rate;
    driver->hw_model->baud_rate = baud_rate;
    uint32_t cr1 = driver->hw_model->regs.CR1 & ~(0x7 << 3);
    if (baud_rate <= 1000000) cr1 |= (0b000 << 3);
    else if (baud_rate <= 2000000) cr1 |= (0b001 << 3);
    else cr1 |= (0b010 << 3);
    spi_hw_write_reg(driver->hw_model, 0x00, cr1);
    return SPI_OK;
}

SPI_Error spi_driver_get_status(SPI_Driver* driver) {
    if (!driver || !driver->initialized) return SPI_ERR_INVALID_ARG;
    return driver->transfer_in_progress ? SPI_ERR_BUSY : SPI_OK;
}

void spi_driver_print_stats(SPI_Driver* driver) {
    if (!driver || !driver->initialized) return;
    printf("\n=== SPI Driver Statistics ===\n");
    printf("Total Transfers:    %u\n", driver->total_transfers);
    printf("Total Bytes:        %u\n", driver->total_bytes);
    printf("Error Count:        %u\n", driver->error_count);
    if (driver->total_transfers > 0) {
        printf("Avg Latency:        %.2f cycles/byte\n", 
               (float)driver->total_latency_cycles / driver->total_bytes);
        printf("Theoretical Eff:    %.1f%%\n", spi_driver_get_efficiency(driver));
    }
}

float spi_driver_get_efficiency(SPI_Driver* driver) {
    if (!driver || driver->total_bytes == 0) return 0.0f;
    float ideal = driver->total_bytes * 10.0f;
    return (ideal / (float)driver->total_latency_cycles) * 100.0f;
}

// spi_driver_transfer function remains the same as previous corrected version
SPI_Error spi_driver_transfer(SPI_Driver* driver, uint8_t* tx_data, 
                              uint8_t* rx_data, uint32_t length, uint32_t timeout_ms) {
    // ... (same as before, unchanged)
    if (!driver || !driver->initialized || !tx_data || length == 0) return SPI_ERR_INVALID_ARG;
    if (driver->transfer_in_progress) return SPI_ERR_BUSY;
    driver->transfer_in_progress = true;
    if (driver->pre_transfer_hook) driver->pre_transfer_hook(driver->hook_context);
    uint64_t start_cycle = driver->hw_model->clock_cycle;
    SPI_Error result = SPI_OK;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t cnt = 0;
        while (!(spi_hw_read_reg(driver->hw_model, 0x08) & (1 << 1))) {
            spi_hw_clock_cycle(driver->hw_model);
            if (++cnt > timeout_ms * 1000) { result = SPI_ERR_TIMEOUT; break; }
        }
        if (result != SPI_OK) break;
        spi_hw_write_reg(driver->hw_model, 0x0C, tx_data[i]);
        cnt = 0;
        while (!(spi_hw_read_reg(driver->hw_model, 0x08) & (1 << 0))) {
            spi_hw_clock_cycle(driver->hw_model);
            if (++cnt > timeout_ms * 1000) { result = SPI_ERR_TIMEOUT; break; }
        }
        if (result != SPI_OK) break;
        if (rx_data) rx_data[i] = (uint8_t)spi_hw_read_reg(driver->hw_model, 0x0C);
        else spi_hw_read_reg(driver->hw_model, 0x0C);
        for (int j = 0; j < 100; j++) spi_hw_clock_cycle(driver->hw_model);
    }
    uint32_t cnt = 0;
    while (spi_hw_read_reg(driver->hw_model, 0x08) & (1 << 7)) {
        spi_hw_clock_cycle(driver->hw_model);
        if (++cnt > timeout_ms * 1000) { result = SPI_ERR_TIMEOUT; break; }
    }
    uint64_t end_cycle = driver->hw_model->clock_cycle;
    driver->total_latency_cycles += (end_cycle - start_cycle);
    driver->total_transfers++;
    driver->total_bytes += length;
    if (result != SPI_OK) driver->error_count++;
    driver->transfer_in_progress = false;
    if (driver->post_transfer_hook) driver->post_transfer_hook(driver->hook_context, result);
    return result;
}