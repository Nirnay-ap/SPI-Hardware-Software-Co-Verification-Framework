#include "hw_model.h"
#include <string.h>
#include <stdlib.h>

static inline void reg_bit_set(volatile uint32_t* reg, uint8_t bit) {
    *reg |= (1U << bit);
}

static inline void reg_bit_clear(volatile uint32_t* reg, uint8_t bit) {
    *reg &= ~(1U << bit);
}

static inline bool reg_bit_is_set(uint32_t reg, uint8_t bit) {
    return (reg & (1U << bit)) != 0;
}

static void record_transition(SPI_HW_Model* model, SPI_State new_state) {
    if (model->current_state != new_state) {
        model->tracker.transitions[model->current_state][new_state]++;
        model->current_state = new_state;
        model->tracker.visit_count[new_state]++;
        if (model->on_state_change) {
            model->on_state_change(model->callback_context, model->current_state, new_state);
        }
    }
}

void spi_hw_init(SPI_HW_Model* model, uint32_t base_addr) {
    if (!model) return;
    memset(model, 0, sizeof(SPI_HW_Model));
    model->regs.CR1 = 0x0000;
    model->regs.CR2 = 0x0700;
    model->regs.SR = 0x0002;
    model->current_state = SPI_STATE_IDLE;
    model->clock_cycle = 0;
    model->baud_rate = 1000000;
    model->tx_ptr = model->rx_ptr = model->tx_level = model->rx_level = 0;
    model->simulation_mode = true;
    printf("[HW_MODEL] SPI initialized at 0x%08X\n", base_addr);
}

void spi_hw_clock_cycle(SPI_HW_Model* model) {
    if (!model) return;
    model->clock_cycle++;
    if (!reg_bit_is_set(model->regs.CR1, 6)) {
        if (model->current_state != SPI_STATE_IDLE) record_transition(model, SPI_STATE_IDLE);
        return;
    }

    switch (model->current_state) {
        case SPI_STATE_IDLE:
            if (model->tx_level > 0 || reg_bit_is_set(model->regs.SR, 1))
                record_transition(model, SPI_STATE_TX_ACTIVE);
            break;
        case SPI_STATE_TX_ACTIVE:
            if (model->tx_level > 0) {
                uint8_t data = model->tx_fifo[model->tx_ptr];
                model->tx_ptr = (model->tx_ptr + 1) % 16;
                model->tx_level--;
                if (model->mosi_callback) model->mosi_callback(data);
                uint8_t rx_data = data ^ 0xFF;
                if (model->rx_level < 16) {
                    model->rx_fifo[(model->rx_ptr + model->rx_level) % 16] = rx_data;
                    model->rx_level++;
                }
                model->bytes_transmitted++;
            }
            if (model->tx_level == 0 && !reg_bit_is_set(model->regs.SR, 1)) {
                record_transition(model, model->rx_level > 0 ? SPI_STATE_RX_ACTIVE : SPI_STATE_IDLE);
            }
            break;
        case SPI_STATE_RX_ACTIVE:
            if (model->rx_level > 0) reg_bit_set(&model->regs.SR, 0);
            if (model->rx_level == 0) {
                reg_bit_clear(&model->regs.SR, 0);
                record_transition(model, SPI_STATE_IDLE);
            }
            break;
        case SPI_STATE_ERROR:
            if (!reg_bit_is_set(model->regs.SR, 4) && !reg_bit_is_set(model->regs.SR, 5) && !reg_bit_is_set(model->regs.SR, 6))
                record_transition(model, SPI_STATE_RECOVERY);
            break;
        case SPI_STATE_RECOVERY:
            if (model->clock_cycle % 10 == 0)
                record_transition(model, SPI_STATE_IDLE);
            break;
        default: break;
    }

    if (model->tx_level < 16) reg_bit_set(&model->regs.SR, 1);
    else reg_bit_clear(&model->regs.SR, 1);
}

void spi_hw_write_reg(SPI_HW_Model* model, uint32_t offset, uint32_t value) {
    if (!model) return;
    volatile uint32_t* reg = NULL;
    switch (offset) {
        case 0x00: reg = &model->regs.CR1; break;
        case 0x04: reg = &model->regs.CR2; break;
        case 0x08: reg = &model->regs.SR; break;
        case 0x0C:
            reg = &model->regs.DR;
            if (model->tx_level < 16) {
                model->tx_fifo[(model->tx_ptr + model->tx_level) % 16] = (uint8_t)value;
                model->tx_level++;
                model->bytes_transmitted++;
            }
            break;
        default: return;
    }
    if (reg) {
        *reg = value;
        for (int i = 0; i < 32; i++) {
            if (value & (1U << i))
                model->reg_write_coverage[i / 8] |= (1 << (i % 8));
        }
    }
}

uint32_t spi_hw_read_reg(SPI_HW_Model* model, uint32_t offset) {
    if (!model) return 0;
    uint32_t value = 0;
    switch (offset) {
        case 0x00: value = model->regs.CR1; break;
        case 0x04: value = model->regs.CR2; break;
        case 0x08: value = model->regs.SR; break;
        case 0x0C:
            if (model->rx_level > 0) {
                value = model->rx_fifo[model->rx_ptr];
                model->rx_ptr = (model->rx_ptr + 1) % 16;
                model->rx_level--;
                model->bytes_received++;
                if (model->rx_level == 0) reg_bit_clear(&model->regs.SR, 0);
            }
            break;
    }
    for (int i = 0; i < 32; i++) {
        if (value & (1U << i))
            model->reg_read_coverage[i / 8] |= (1 << (i % 8));
    }
    return value;
}

float spi_calculate_state_coverage(SPI_HW_Model* model) {
    if (!model) return 0.0f;
    uint32_t visited = 0;
    for (int i = 0; i < SPI_STATE_COUNT; i++) if (model->tracker.visit_count[i] > 0) visited++;
    return (float)visited / SPI_STATE_COUNT * 100.0f;
}

void spi_print_state_analysis(SPI_HW_Model* model) {
    if (!model) return;

    printf("\n=== SPI State Space Analysis ===\n");
    printf("Current State: %d\n", model->current_state);
    printf("Clock Cycles: %I64u\n", model->clock_cycle);
    printf("State Coverage: %.1f%%\n", spi_calculate_state_coverage(model));

    const char* state_names[] = {
        "IDLE", "TX_ACTIVE", "RX_ACTIVE", "TXRX_ACTIVE", "ERROR", "RECOVERY"
    };

    printf("\nState Visit Count:\n");
    for (int i = 0; i < SPI_STATE_COUNT; i++) {
        printf("  %-12s: %u\n", state_names[i], model->tracker.visit_count[i]);
    }

    printf("\nTransition Matrix:\n");
    printf("     ");
    for (int j = 0; j < SPI_STATE_COUNT; j++) {
        printf("%8s ", state_names[j]);
    }
    printf("\n");

    for (int i = 0; i < SPI_STATE_COUNT; i++) {
        printf("%-5s", state_names[i]);
        for (int j = 0; j < SPI_STATE_COUNT; j++) {
            printf("%8u ", model->tracker.transitions[i][j]);
        }
        printf("\n");
    }

    printf("\nStatistics:\n");
    printf("  Bytes Transmitted: %u\n", model->bytes_transmitted);
    printf("  Bytes Received:    %u\n", model->bytes_received);
    printf("  Errors:            %u\n", model->error_count);
}

void spi_hw_reset(SPI_HW_Model* model) {
    if (!model) return;
    spi_hw_init(model, 0);
    printf("[HW_MODEL] SPI hardware reset\n");
}

void spi_hw_print_registers(SPI_HW_Model* model) {
    if (!model) return;
    printf("\n=== SPI Registers ===\n");
    printf("CR1: 0x%08X\n", model->regs.CR1);
    printf("CR2: 0x%08X\n", model->regs.CR2);
    printf("SR:  0x%08X\n", model->regs.SR);
    printf("DR:  0x%08X\n", model->regs.DR);
}

void spi_hw_dump_fifo(SPI_HW_Model* model) {
    if (!model) return;
    printf("\n=== SPI FIFOs ===\n");
    printf("TX level: %u, RX level: %u\n", model->tx_level, model->rx_level);
}