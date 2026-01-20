#include "spi_driver.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

void test_basic_transfer(void) {
    printf("\n=== Test 1: Basic Transfer ===\n");
    SPI_Driver driver;
    SPI_Config config = default_config;
    config.baud_rate = 500000;
    SPI_Error err = spi_driver_init(&driver, 0x40013000, &config);
    assert(err == SPI_OK);

    uint8_t tx_data[8] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0x55, 0xFF, 0x00};
    uint8_t rx_data[8] = {0};

    err = spi_driver_transfer(&driver, tx_data, rx_data, 8, 100);
    assert(err == SPI_OK);

    // Suppress the false-positive sign-compare warning only for this safe comparison
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
    for (int i = 0; i < 8; i++) {
        assert(rx_data[i] == (uint8_t)(tx_data[i] ^ 0xFFU));
    }
#pragma GCC diagnostic pop

    spi_driver_print_stats(&driver);
    spi_print_state_analysis(driver.hw_model);
    printf("✓ Basic transfer test PASSED\n");
}

void test_error_conditions(void) {
    printf("\n=== Test 2: Error Conditions ===\n");
    SPI_Driver driver;
    spi_driver_init(&driver, 0x40013000, NULL);

    SPI_Error err = spi_driver_transfer(&driver, NULL, NULL, 8, 100);
    assert(err == SPI_ERR_INVALID_ARG);

    uint8_t data[1] = {0xAA};
    err = spi_driver_transfer(&driver, data, NULL, 0, 100);
    assert(err == SPI_ERR_INVALID_ARG);

    driver.hw_model->regs.SR &= ~(1U << 1);
    err = spi_driver_transfer(&driver, data, NULL, 1, 1);
    assert(err == SPI_ERR_TIMEOUT);

    printf("✓ Error condition test PASSED\n");
}

void test_state_space_coverage(void) {
    printf("\n=== Test 3: State Space Coverage ===\n");
    SPI_Driver driver;
    spi_driver_init(&driver, 0x40013000, NULL);

    uint8_t tx_data[32], rx_data[32];
    for (int i = 0; i < 32; i++) tx_data[i] = i;

    SPI_Error err = spi_driver_transfer(&driver, tx_data, rx_data, 32, 100);
    assert(err == SPI_OK);

    driver.hw_model->regs.SR |= (1U << 4);
    for (int i = 0; i < 100; i++) spi_hw_clock_cycle(driver.hw_model);
    driver.hw_model->regs.SR &= ~(1U << 4);
    for (int i = 0; i < 100; i++) spi_hw_clock_cycle(driver.hw_model);

    spi_print_state_analysis(driver.hw_model);
    float coverage = spi_calculate_state_coverage(driver.hw_model);
    printf("State Coverage Achieved: %.1f%%\n", coverage);

    if (coverage >= 95.0f) {
        printf("✓ State space coverage PASSED (>= 95%%)\n");
    } else {
        printf("✗ State space coverage FAILED (%.1f%% < 95%%)\n", coverage);
    }
}

void test_performance_benchmark(void) {
    printf("\n=== Test 4: Performance Benchmark ===\n");
    SPI_Driver driver;
    SPI_Config config = default_config;
    config.baud_rate = 1000000;
    spi_driver_init(&driver, 0x40013000, &config);

    uint32_t sizes[] = {1, 10, 100, 1000};
    for (int s = 0; s < 4; s++) {
        uint32_t size = sizes[s];
        uint8_t* tx_data = (uint8_t*)malloc(size);
        uint8_t* rx_data = (uint8_t*)malloc(size);
        for (uint32_t i = 0; i < size; i++) tx_data[i] = i & 0xFF;

        uint64_t start = driver.hw_model->clock_cycle;
        SPI_Error err = spi_driver_transfer(&driver, tx_data, rx_data, size, 1000);
        assert(err == SPI_OK);

        uint64_t cycles = driver.hw_model->clock_cycle - start;
        float bps = (float)size * 1000000.0f / (cycles ? cycles : 1);
        float eff = (bps * 8.0f) / config.baud_rate * 100.0f;

        printf("  Size: %4u bytes, Cycles: %6I64u, Throughput: %6.1f KB/s, Efficiency: %5.1f%%\n",
               size, cycles, bps / 1000.0f, eff);

        free(tx_data);
        free(rx_data);
    }

    printf("\nOverall Efficiency: %.1f%%\n", spi_driver_get_efficiency(&driver));
    if (spi_driver_get_efficiency(&driver) > 80.0f) {
        printf("✓ Performance benchmark PASSED\n");
    } else {
        printf("✗ Performance benchmark FAILED\n");
    }
}

void test_concurrent_access(void) {
    printf("\n=== Test 5: Concurrent Access Simulation ===\n");
    SPI_Driver driver;
    spi_driver_init(&driver, 0x40013000, NULL);
    uint8_t race = 0;

    for (int i = 0; i < 10; i++) {
        uint8_t tx[4] = {1,2,3,4};
        uint8_t rx[4];
        spi_driver_transfer(&driver, tx, rx, 4, 100);
        if (driver.transfer_in_progress) race = 1;
        driver.config.baud_rate = 2000000;
        for (int j = 0; j < 50; j++) spi_hw_clock_cycle(driver.hw_model);
    }

    if (!race) printf("✓ No data races detected\n");
    else printf("✗ Potential data race detected!\n");
    printf("Concurrent access test completed\n");
}