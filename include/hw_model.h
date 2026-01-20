#ifndef HW_MODEL_H
#define HW_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// SPI Register Map (simulating STM32-like SPI)
typedef struct {
    volatile uint32_t CR1;      // Control register 1
    volatile uint32_t CR2;      // Control register 2
    volatile uint32_t SR;       // Status register
    volatile uint32_t DR;       // Data register
    volatile uint32_t CRCPR;    // CRC polynomial
    volatile uint32_t RXCRCR;   // RX CRC register
    volatile uint32_t TXCRCR;   // TX CRC register
} SPI_Registers;

// SPI Hardware Model States
typedef enum {
    SPI_STATE_IDLE = 0,
    SPI_STATE_TX_ACTIVE,
    SPI_STATE_RX_ACTIVE,
    SPI_STATE_TXRX_ACTIVE,
    SPI_STATE_ERROR,
    SPI_STATE_RECOVERY,
    SPI_STATE_COUNT  // Total number of states
} SPI_State;

// State Transition Matrix (for complexity analysis)
typedef struct {
    uint32_t transitions[SPI_STATE_COUNT][SPI_STATE_COUNT];
    uint32_t visit_count[SPI_STATE_COUNT];
} State_Tracker;

// SPI Hardware Model
typedef struct {
    // Registers
    SPI_Registers regs;
    
    // Internal state
    SPI_State current_state;
    State_Tracker tracker;
    
    // Simulation
    uint64_t clock_cycle;
    uint32_t baud_rate;
    bool simulation_mode;
    
    // FIFOs (simulating hardware buffers)
    uint8_t tx_fifo[16];
    uint8_t rx_fifo[16];
    uint8_t tx_ptr;
    uint8_t rx_ptr;
    uint8_t tx_level;
    uint8_t rx_level;
    
    // External connections (for co-simulation)
    void (*mosi_callback)(uint8_t data);
    void (*miso_callback)(uint8_t data);
    void (*ss_callback)(bool active);
    
    // Verification hooks
    void (*on_state_change)(void* ctx, SPI_State old, SPI_State new);
    void* callback_context;
    
    // Statistics
    uint32_t bytes_transmitted;
    uint32_t bytes_received;
    uint32_t error_count;
    
    // Coverage tracking
    uint8_t reg_write_coverage[32];  // Which bits written
    uint8_t reg_read_coverage[32];   // Which bits read
} SPI_HW_Model;

// Public API
void spi_hw_init(SPI_HW_Model* model, uint32_t base_addr);
void spi_hw_clock_cycle(SPI_HW_Model* model);
void spi_hw_reset(SPI_HW_Model* model);
void spi_hw_write_reg(SPI_HW_Model* model, uint32_t offset, uint32_t value);
uint32_t spi_hw_read_reg(SPI_HW_Model* model, uint32_t offset);

// State space analysis
void spi_print_state_analysis(SPI_HW_Model* model);
float spi_calculate_state_coverage(SPI_HW_Model* model);

// Debug
void spi_hw_print_registers(SPI_HW_Model* model);
void spi_hw_dump_fifo(SPI_HW_Model* model);

#endif // HW_MODEL_H