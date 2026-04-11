#pragma once

#include <key_scanner.hpp>
#include <cstdio>
#include <cstdlib>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/structs/padsbank0.h"
#include "capacitive_touch.pio.h"

namespace rp2350 {

/**
 * @brief RP2350 capacitive key scanner using PIO and DMA
 * 
 * Uses PIO peripheral to measure capacitive discharge time with DMA for automated scanning.
 * Each key is scanned sequentially by dynamically reconfiguring the PIO state machine
 * via DMA control blocks. The entire scan sequence runs autonomously without CPU intervention.
 * 
 * HARDWARE REQUIREMENTS:
 * - GPIO 0-31 connected to capacitive touch sensors
 * - Internal pull-up resistors enabled dynamically during scan
 * - External electrodes (copper pads, conductive fabric, etc.)
 */
class PioCapacitiveScanner : public midi::KeyScanner {
public:
    static constexpr uint8_t NUM_KEYS = 32;
    static constexpr uint8_t FIRST_KEY_PIN = 0;
    static constexpr float PIO_CLOCK_DIV = 5.0f;  // Tuned for capacitive response time
    
    /**
     * @brief DMA control block structure for scripted operations
     */
    struct DmaControlBlock {
        void* read_addr;
        void* write_addr;
        uint32_t transfer_count;
        uint32_t ctrl;
    };
    
    /**
     * @brief Construct and initialize the PIO-based capacitive scanner
     */
    PioCapacitiveScanner() 
        : pio_(pio0)
        , sm_(0)
        , dmaWorkerChan_(0)
        , dmaControlChan_(0)
        , controlBlocks_(nullptr) {
        
        // Add PIO program to PIO
        pioOffset_ = pio_add_program(pio_, &capacitive_touch_program);
        
        // Claim a state machine
        sm_ = pio_claim_unused_sm(pio_, true);  // Will panic if unavailable
        
        printf("PIO capacitive scanner: PIO%d SM%d, program offset %d\n", 
               pio_get_index(pio_), sm_, pioOffset_);
        
        // One-time GPIO setup
        setupGpios();
        
        // Initialize state machine
        pio_sm_config config = capacitive_touch_program_get_default_config(pioOffset_);
        sm_config_set_clkdiv(&config, PIO_CLOCK_DIV);
        sm_config_set_set_pins(&config, FIRST_KEY_PIN, 1);
        sm_config_set_jmp_pin(&config, FIRST_KEY_PIN);
        
        // Start at wait_for_restart label
        pio_sm_init(pio_, sm_, pioOffset_ + capacitive_touch_offset_wait_for_restart, &config);
        pio_sm_set_consecutive_pindirs(pio_, sm_, FIRST_KEY_PIN, 1, false);
        pio_sm_set_enabled(pio_, sm_, true);
        
        // Claim DMA channels
        dmaWorkerChan_ = dma_claim_unused_channel(true);
        dmaControlChan_ = dma_claim_unused_channel(true);
        
        printf("PIO scanner: Worker DMA chan %u, Control DMA chan %u\n", 
               dmaWorkerChan_, dmaControlChan_);
        
        // Build DMA control blocks
        controlBlocks_ = setupDmaControlBlocks();
        
        if (!controlBlocks_) {
            printf("ERROR: Failed to allocate DMA control blocks!\n");
        }
        
        printf("PIO capacitive scanner initialized with %d keys\n", NUM_KEYS);
    }
    
    ~PioCapacitiveScanner() {
        // Stop DMA
        if (dmaWorkerChan_ != 0) {
            dma_channel_abort(dmaWorkerChan_);
            dma_channel_unclaim(dmaWorkerChan_);
        }
        if (dmaControlChan_ != 0) {
            dma_channel_abort(dmaControlChan_);
            dma_channel_unclaim(dmaControlChan_);
        }
        
        // Stop PIO
        if (pio_ && sm_ != 0) {
            pio_sm_set_enabled(pio_, sm_, false);
            pio_sm_unclaim(pio_, sm_);
        }
        
        // Free control blocks
        if (controlBlocks_) {
            free(controlBlocks_);
            controlBlocks_ = nullptr;
        }
        
        // Reset GPIOs to safe state
        for (uint i = 0; i < NUM_KEYS; i++) {
            uint pin = FIRST_KEY_PIN + i;
            gpio_set_pulls(pin, false, false);
            gpio_set_dir(pin, GPIO_IN);
        }
    }
    
    /**
     * @brief Trigger a new scan sequence
     * 
     * Starts the DMA control chain that will autonomously scan all keys.
     * Non-blocking - use isScanComplete() to check when done.
     */
    void startScan() {
        // Clear any previous IRQ
        dma_hw->ints0 = 1u << dmaWorkerChan_;
        
        // Reset control DMA to beginning of control block array
        dma_channel_set_read_addr(dmaControlChan_, controlBlocks_, false);
        
        // Start the scan
        dma_channel_start(dmaControlChan_);
    }
    
    /**
     * @brief Check if the current scan is complete
     * @return true if scan finished, false if still running
     */
    bool isScanComplete() const {
        return (dma_hw->intr & (1u << dmaWorkerChan_)) != 0;
    }
    
    /**
     * @brief Wait for current scan to complete (blocking)
     */
    void waitForScanComplete() {
        while (!isScanComplete()) {
            tight_loop_contents();
        }
        // Clear IRQ flag
        dma_hw->ints0 = 1u << dmaWorkerChan_;
    }
    
    /**
     * @brief Get scan readings (implements KeyScanner interface)
     * 
     * Returns raw PIO counter values. Lower values = more capacitance (longer charge time).
     * Convert to uint16_t for interface compatibility.
     */
    const uint16_t* getScanReadings() const override {
        // Convert from 32-bit raw readings to 16-bit for interface
        // Take upper 16 bits for better resolution
        for (uint8_t i = 0; i < NUM_KEYS; i++) {
            readings16_[i] = static_cast<uint16_t>((rawReadings_[i] >> 16) & 0xFFFF);
        }
        return readings16_;
    }
    
    /**
     * @brief Get raw 32-bit readings (full resolution)
     * @return Pointer to array of 32-bit raw counter values
     */
    const uint32_t* getRawReadings() const {
        return rawReadings_;
    }
    
    uint8_t getKeyCount() const override {
        return NUM_KEYS;
    }
    
private:
    PIO pio_;
    uint sm_;
    uint pioOffset_;
    uint dmaWorkerChan_;
    uint dmaControlChan_;
    DmaControlBlock* controlBlocks_;
    
    uint32_t rawReadings_[NUM_KEYS] = {0};
    mutable uint16_t readings16_[NUM_KEYS] = {0};  // Mutable for lazy conversion in const getter
    
    // Persistent storage for DMA control block references
    uint32_t execctrls_[NUM_KEYS];
    uint32_t pinctrls_[NUM_KEYS];
    uint32_t pueEnableBit_ = PADS_BANK0_GPIO0_PUE_BITS;
    uint32_t restartJmp_;
    
    /**
     * @brief One-time GPIO setup
     */
    void setupGpios() {
        for (uint i = 0; i < NUM_KEYS; i++) {
            uint pin = FIRST_KEY_PIN + i;
            gpio_set_pulls(pin, false, false);  // Start with pulls disabled
            pio_gpio_init(pio_, pin);
        }
    }
    
    /**
     * @brief Build DMA control blocks for autonomous scanning
     * @return Pointer to control blocks array (allocated on heap)
     */
    DmaControlBlock* setupDmaControlBlocks() {
        // 6 operations per key (2 GPIO + 2 PIO config + 1 restart + 1 read) + 1 null block
        const uint numBlocks = NUM_KEYS * 6 + 1;
        DmaControlBlock* blocks = static_cast<DmaControlBlock*>(
            malloc(numBlocks * sizeof(DmaControlBlock)));
        
        if (!blocks) {
            return nullptr;
        }
        
        // Build execctrl and pinctrl for each key
        pio_sm_config config = capacitive_touch_program_get_default_config(pioOffset_);
        
        for (uint i = 0; i < NUM_KEYS; i++) {
            uint pin = FIRST_KEY_PIN + i;
            sm_config_set_set_pins(&config, pin, 1);
            sm_config_set_jmp_pin(&config, pin);
            
            execctrls_[i] = config.execctrl;
            pinctrls_[i] = config.pinctrl;
        }
        
        // Prepare restart instruction
        restartJmp_ = pio_encode_jmp(pioOffset_);
        
        // Get worker DMA register base
        volatile uint32_t* workerRegs = (volatile uint32_t*)&dma_hw->ch[dmaWorkerChan_];
        
        // Configure base DMA configs
        dma_channel_config pioWriteCfg = dma_channel_get_default_config(dmaWorkerChan_);
        channel_config_set_transfer_data_size(&pioWriteCfg, DMA_SIZE_32);
        channel_config_set_read_increment(&pioWriteCfg, false);
        channel_config_set_write_increment(&pioWriteCfg, false);
        channel_config_set_chain_to(&pioWriteCfg, dmaControlChan_);
        channel_config_set_irq_quiet(&pioWriteCfg, true);
        
        dma_channel_config pioReadCfg = dma_channel_get_default_config(dmaWorkerChan_);
        channel_config_set_transfer_data_size(&pioReadCfg, DMA_SIZE_32);
        channel_config_set_read_increment(&pioReadCfg, false);
        channel_config_set_write_increment(&pioReadCfg, false);
        channel_config_set_dreq(&pioReadCfg, pio_get_dreq(pio_, sm_, false));
        channel_config_set_chain_to(&pioReadCfg, dmaControlChan_);
        channel_config_set_irq_quiet(&pioReadCfg, true);
        
        // Build control blocks for each key
        for (uint i = 0; i < NUM_KEYS; i++) {
            uint blockBase = i * 6;
            uint currentPin = FIRST_KEY_PIN + i;
            uint prevPin = FIRST_KEY_PIN + ((i + NUM_KEYS - 1) % NUM_KEYS);
            
            // 1. Disable pull-up on previous key
            blocks[blockBase + 0].read_addr = &pueEnableBit_;
            blocks[blockBase + 0].write_addr = (void*)&padsbank0_hw->io[prevPin] + REG_ALIAS_CLR_BITS;
            blocks[blockBase + 0].transfer_count = 1;
            blocks[blockBase + 0].ctrl = pioWriteCfg.ctrl;
            
            // 2. Enable pull-up on current key
            blocks[blockBase + 1].read_addr = &pueEnableBit_;
            blocks[blockBase + 1].write_addr = (void*)&padsbank0_hw->io[currentPin] + REG_ALIAS_SET_BITS;
            blocks[blockBase + 1].transfer_count = 1;
            blocks[blockBase + 1].ctrl = pioWriteCfg.ctrl;
            
            // 3. Write execctrl register
            blocks[blockBase + 2].read_addr = &execctrls_[i];
            blocks[blockBase + 2].write_addr = (void*)&pio_->sm[sm_].execctrl;
            blocks[blockBase + 2].transfer_count = 1;
            blocks[blockBase + 2].ctrl = pioWriteCfg.ctrl;
            
            // 4. Write pinctrl register
            blocks[blockBase + 3].read_addr = &pinctrls_[i];
            blocks[blockBase + 3].write_addr = (void*)&pio_->sm[sm_].pinctrl;
            blocks[blockBase + 3].transfer_count = 1;
            blocks[blockBase + 3].ctrl = pioWriteCfg.ctrl;
            
            // 5. Write restart instruction
            blocks[blockBase + 4].read_addr = &restartJmp_;
            blocks[blockBase + 4].write_addr = (void*)&pio_->sm[sm_].instr;
            blocks[blockBase + 4].transfer_count = 1;
            blocks[blockBase + 4].ctrl = pioWriteCfg.ctrl;
            
            // 6. Read result from RX FIFO
            blocks[blockBase + 5].read_addr = (void*)&pio_->rxf[sm_];
            blocks[blockBase + 5].write_addr = &rawReadings_[i];
            blocks[blockBase + 5].transfer_count = 1;
            blocks[blockBase + 5].ctrl = pioReadCfg.ctrl;
        }
        
        // Null control block at end (triggers IRQ)
        blocks[NUM_KEYS * 6].read_addr = nullptr;
        blocks[NUM_KEYS * 6].write_addr = nullptr;
        blocks[NUM_KEYS * 6].transfer_count = 0;
        blocks[NUM_KEYS * 6].ctrl = 0;
        
        // Configure control DMA
        dma_channel_config controlCfg = dma_channel_get_default_config(dmaControlChan_);
        channel_config_set_transfer_data_size(&controlCfg, DMA_SIZE_32);
        channel_config_set_read_increment(&controlCfg, true);
        channel_config_set_write_increment(&controlCfg, true);
        channel_config_set_ring(&controlCfg, true, 4);  // Ring on write: 4 registers
        
        dma_channel_configure(dmaControlChan_, &controlCfg,
                             workerRegs,
                             blocks,
                             4,      // Transfer 4 words per control block
                             false); // Don't start yet
        
        printf("Created %u DMA control blocks for %u keys\n", numBlocks, NUM_KEYS);
        
        return blocks;
    }
};

} // namespace rp2350
