/**
 * RP2350 I2S Audio Sink
 *
 * PIO-based I2S transmitter with double buffering for stable audio output.
 * Uses DMA to feed samples to the PIO state machine.
 *
 * On RP2350B, PIO instances can only address 32 GPIOs at a time. The SDK
 * function pio_claim_free_sm_and_add_program_for_gpio_range automatically
 * selects a PIO instance and sets its GPIOBASE so the requested pins are
 * reachable. GPIOBASE must be a multiple of 16 (0, 16, or 32).
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "i2s.pio.h"

namespace rp2350 {

/**
 * @brief Double-buffered I2S audio output using PIO + DMA
 *
 * Feeds mono 32-bit samples to the I2S PIO program, which outputs them as
 * stereo I2S (left and right channels carry the same sample).
 *
 * The clock divider is set so that each 32-bit sample occupies exactly 64 PIO
 * cycles (32 bits × 2 clocks per bit), yielding the requested sample rate.
 *
 * @tparam BUFFER_SIZE Number of samples per buffer
 */
template<size_t BUFFER_SIZE>
class Rp2350AudioSink {
public:
    /**
     * @brief Construct the audio sink and initialise PIO hardware.
     *
     * @param sampleRate Sample rate in Hz
     * @param firstPin   First of three consecutive GPIO pins: DATA, BCLK, LRCLK
     */
    Rp2350AudioSink(uint32_t sampleRate, uint8_t firstPin)
        : sampleRate_(sampleRate)
        , dataPin_(firstPin)
        , bclkPin_(firstPin + 1)
        , lrclkPin_(firstPin + 2)
        , pio_(nullptr)
        , sm_(-1)
        , dmaChannel_(-1)
        , currentBuffer_(0)
    {
        memset(buffers_[0], 0, sizeof(buffers_[0]));
        memset(buffers_[1], 0, sizeof(buffers_[1]));
        initialize();
    }

    ~Rp2350AudioSink() {
        if (dmaChannel_ >= 0) {
            dma_channel_abort(dmaChannel_);
            dma_channel_unclaim(dmaChannel_);
        }
        if (pio_ && sm_ >= 0) {
            pio_sm_set_enabled(pio_, sm_, false);
            pio_sm_unclaim(pio_, sm_);
        }
    }

    /**
     * @brief Start audio output.
     *
     * Enables the state machine and starts the first DMA transfer.
     * Call after populating the inactive buffer via getInactiveBuffer().
     */
    void start() {
        currentBuffer_ = 0;
        startDmaTransfer(0);
        pio_sm_set_enabled(pio_, sm_, true);
    }

    /**
     * @brief Stop audio output.
     */
    void stop() {
        pio_sm_set_enabled(pio_, sm_, false);
        if (dmaChannel_ >= 0) {
            dma_channel_abort(dmaChannel_);
        }
    }

    /**
     * @brief Get a pointer to the buffer that is not currently being transmitted.
     *
     * Safe to write while a DMA transfer is in progress on the other buffer.
     */
    int32_t* getInactiveBuffer() {
        return buffers_[1 - currentBuffer_];
    }

    /** @brief Number of samples per buffer. */
    constexpr size_t getBufferSize() const { return BUFFER_SIZE; }

    /** @brief Sample rate in Hz. */
    uint32_t getSampleRate() const { return sampleRate_; }

    /**
     * @brief Returns true when the current DMA transfer has finished and
     *        swapBuffers() can be called without stalling.
     */
    bool isTransferComplete() const {
        return dmaChannel_ >= 0 && !dma_channel_is_busy(dmaChannel_);
    }

    /**
     * @brief Promote the inactive buffer to active and begin transmitting it.
     *
     * Call only after isTransferComplete() returns true.
     */
    void swapBuffers() {
        currentBuffer_ = 1 - currentBuffer_;
        startDmaTransfer(currentBuffer_);
    }

private:
    void initialize() {
        uint programOffset;

        // Find a PIO instance whose GPIOBASE can be set to reach all three
        // pins, claim a state machine on it, load the program, and set
        // GPIOBASE.  On RP2350B this is essential for pins >= 32.
        if (!pio_claim_free_sm_and_add_program_for_gpio_range(
                &PioI2S_out_program, &pio_, &sm_, &programOffset,
                dataPin_, lrclkPin_ - dataPin_ + 1, true)) {
            printf("ERROR: No PIO instance can reach GPIO %d-%d. "
                   "Pins must fall within a single 32-pin window "
                   "aligned to a multiple of 16.\n",
                   dataPin_, lrclkPin_);
            return;
        }

        printf("I2S audio sink: PIO%d SM%d GPIOBASE=%lu offset=%u "
               "DATA=%d BCLK=%d LRCLK=%d rate=%luHz buf=%zu\n",
               pio_get_index(pio_), sm_,
               (unsigned long)pio_->gpiobase, programOffset,
               dataPin_, bclkPin_, lrclkPin_,
               (unsigned long)sampleRate_, BUFFER_SIZE);

        pio_sm_config config = PioI2S_out_program_get_default_config(programOffset);

        // sm_config_set_* functions accept real GPIO numbers 0-47.
        // When PICO_PIO_USE_GPIO_BASE is set (as it is for RP2350B boards),
        // the SDK stores the full pin number in the sm_config struct;
        // pio_sm_init then maps them relative to GPIOBASE into the hardware
        // pinctrl register.
        sm_config_set_out_pins(&config, dataPin_, 1);
        sm_config_set_sideset_pins(&config, bclkPin_);

        // Autopull: shift MSB-first, pull a new word every 32 bits.
        sm_config_set_out_shift(&config, /*shift_right=*/false,
                                         /*autopull=*/false,
                                         /*threshold=*/0);

        // PIO cycle count per stereo sample (see i2s.pio):
        // Left:  pull+mov (2) + set[1] (2) + 31 iterations × 4 cycles (124) = 128
        // Right: mov[1] (2) + set[1] (2) + 31 iterations × 4 cycles (124) = 128
        // Total: 256 PIO cycles per mono sample (stereo-expanded)
        float clockDiv = (float)clock_get_hz(clk_sys) / (sampleRate_ * 256.0f);
        sm_config_set_clkdiv(&config, clockDiv);

        // Hand GPIO ownership to the PIO block and configure as outputs.
        pio_gpio_init(pio_, dataPin_);
        pio_gpio_init(pio_, bclkPin_);
        pio_gpio_init(pio_, lrclkPin_);
        pio_sm_set_consecutive_pindirs(pio_, sm_, dataPin_, 3, true);

        // Apply config. Returns PICO_ERROR_BAD_ALIGNMENT if GPIOBASE cannot
        // satisfy the pin mapping — should not happen given the range check
        // above, but we catch it to aid debugging.
        int rc = pio_sm_init(pio_, sm_, programOffset, &config);
        if (rc != PICO_OK) {
            printf("ERROR: pio_sm_init failed (rc=%d) — "
                   "GPIOBASE=%lu cannot map DATA=%d BCLK=%d LRCLK=%d.\n",
                   rc, (unsigned long)pio_->gpiobase,
                   dataPin_, bclkPin_, lrclkPin_);
            return;
        }

        // Set up DMA to stream buffers to the PIO TX FIFO.
        dmaChannel_ = dma_claim_unused_channel(true);

        dma_channel_config dmaConfig = dma_channel_get_default_config(dmaChannel_);
        channel_config_set_transfer_data_size(&dmaConfig, DMA_SIZE_32);
        channel_config_set_read_increment(&dmaConfig, true);
        channel_config_set_write_increment(&dmaConfig, false);
        // DREQ paces DMA to the TX FIFO vacancy rate, preventing overrun.
        channel_config_set_dreq(&dmaConfig, pio_get_dreq(pio_, sm_, true));

        // Configure the channel but don't start it yet; start() does that.
        dma_channel_configure(
            dmaChannel_,
            &dmaConfig,
            &pio_->txf[sm_],  // destination: PIO TX FIFO
            nullptr,           // source: set per-transfer in startDmaTransfer()
            0,                 // count: set per-transfer in startDmaTransfer()
            false              // don't start yet
        );

        printf("I2S audio sink: DMA channel %d ready\n", dmaChannel_);
    }

    void startDmaTransfer(uint8_t bufferIndex) {
        dma_channel_set_read_addr(dmaChannel_, buffers_[bufferIndex], false);
        dma_channel_set_trans_count(dmaChannel_, BUFFER_SIZE, true);
    }

    uint32_t sampleRate_;
    uint8_t  dataPin_;
    uint8_t  bclkPin_;
    uint8_t  lrclkPin_;
    PIO      pio_;
    uint     sm_;
    int      dmaChannel_;
    uint8_t  currentBuffer_;
    int32_t  buffers_[2][BUFFER_SIZE];
};

} // namespace rp2350
