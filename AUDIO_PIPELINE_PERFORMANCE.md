# Audio pipeline performance on the RP2350B

Measured on hardware, not inferred. Every number here comes from the Cortex-M33
cycle counter with interrupts masked, at the production clock (300 MHz), buffer
size (256 frames) and sample rate (48 kHz).

**Deadline: 1,600,000 cycles = 5333 µs per buffer.** The audio path is
double-buffered, so a render that overruns immediately starves the PIO FIFO and
is audible. What matters is therefore the *worst* buffer, never the average.

## How to reproduce

```sh
source ~/.platformio/penv/bin/activate

# Micro/component benchmarks, isolated, one op at a time
tools/run_bench.py bench_2350b          # built like production (-Os)
tools/run_bench.py bench_2350b_o2       # -O3 -fno-math-errno comparison

# The real firmware, under real conditions, swept over voice count
tools/profile_voices.py prod_ram_2350b
```

Both scripts build, load into **SRAM** via `picotool load -x` and read the report
back over USB. Nothing touches flash, so a long profiling session causes no wear.
`prod_ram_2350b` is the shipped firmware with `PICO_DEFAULT_BINARY_TYPE=no_flash`.

The production firmware reports its own per-buffer cost continuously
(`ENABLE_CYCLE_PROFILE` in `src/main_rp2350.cpp`), as JSON lines:

```json
{"type":"render","buffers":100,"minPct10":283,"avgPct10":310,"maxPct10":332,
 "minCycles":465000,"maxCycles":531512,"overBudget":0,"starved":0}
```

`overBudget` counts buffers whose render exceeded the deadline; `starved` counts
buffers where the DMA had already finished before we came back to swap, i.e. the
output actually ran dry. Percentages are ×10 because this SDK build's `printf`
has no `%f` support (see *Gotchas*).

## Headline findings

### 1. Core 0's telemetry `printf` was breaking real-time audio

This is the cause of "misses deadlines even with a single voice". The keyboard
scanner emits an nlohmann-serialised JSON telemetry line per scan (~60/s) from
core 0. That alone turned a comfortable single voice into a starving one:

| voices held | telemetry on: min / avg / **max** | telemetry off: min / avg / **max** |
|---|---|---|
| 0 | 14.3% / 14.9% / **22.8%** | 14.1% / 14.1% / **14.4%** |
| 1 | 28.7% / 36.2% / **144.9%** — 12 starved | 28.3% / 31.0% / **33.2%** — 0 starved |
| 2 | 43.0% / 49.9% / **165.9%** — 4 starved | — |
| 4 | 72.3% / 99.8% / **204.5%** — 110 starved | 71.5% / 74.7% / **77.8%** — 0 starved |
| 6 | 99.5% / 139.5% / **236.5%** — 400 starved | — |
| 8 | 129.9% / 179.2% / **267.1%** — 300 starved | 128.8% / 132.9% / **137.6%** |

The *minimum* barely moves. Only the spread changes, from ~1.15× to ~5×. Averages
completely hide this, which is why it went unnoticed for so long.

**The scanning itself is not the problem.** Parking core 0 entirely (no scan, no
DMA, no telemetry) is indistinguishable from merely silencing the telemetry:

| voices | telemetry off, still scanning | core 0 fully parked |
|---|---|---|
| 1 | 28.3% / 31.0% / 33.2% | 28.2% / 30.7% / 33.5% |
| 4 | 71.5% / 74.7% / 77.8% | 69.8% / 74.9% / 78.8% |

So the capacitive scanner and its DMA cost the audio core essentially nothing.
The damage comes from JSON serialisation plus the USB stdio write path — heavy
allocation and memory traffic on core 0, contending with core 1 for SRAM.

### 2. Throughput ceiling is about 6 voices, not 8

Even with a quiet core 0, 8 voices need 129–140% of the deadline. `NUM_VOICES = 8`
has never actually been achievable. 6 voices sit right at 99.5% minimum — no
margin.

### 3. The shipped build is compiled `-Os`

The framework appends `-Os` *after* `build_flags`, so it silently wins over any
`-O2`/`-O3` in `build_flags`; it has to be removed with `build_unflags`. Moving
the audio path to `-O3 -fno-math-errno` is a large free win:

| | -Os | -O3 + -fno-math-errno |
|---|---|---|
| `WavetableSynth::nextSample` (active) | 852 cycles | **663** |
| `AdsrEnvelope::nextSample` | 21 | **1.7** |
| `Oscillator::nextSample` | 145 | **60** |
| `BiquadFilter::processSample` | 32 | **13** |
| `BiquadFilter::setCutoff` (skipped) | 50 | **25** |
| inactive voice (early return) | 39 | **2** |
| 8 voices, whole buffer | 2,047,187 (128%) | **1,742,019 (109%)** |
| `sqrtf` | 44.9 | **12.0** (becomes the VSQRT instruction) |

`-fno-math-errno` is what lets GCC use hardware instructions instead of calling
libm wrappers that must set `errno`.

### 4. Cost of a single voice, per sample (-Os / -O3)

| component | -Os | -O3 | notes |
|---|---|---|---|
| 2 × `Lfo::nextSample` | 239 | 194 | **runs even at depth 0** — one `sinf` each |
| `Oscillator::nextSample` | 145 | 60 | PolyBLEP; evaluates all three shapes always |
| `setCutoff` guard, skipped path | 50 | 25 | paid every sample just to decide not to work |
| `BiquadFilter::processSample` | 32 | 13 | |
| coefficient recompute, amortised | 15 | 13 | 352 (295) cycles × 11/256 samples |
| 2 × `AdsrEnvelope::nextSample` | 42 | 3 | |
| **measured total** | **852** | **663** | remainder is in-function load/store traffic |

`Lfo::nextSample` costs the same whether depth is 0.5 or 0 — it always calls
`sinf`. Vibrato and tremolo depth are 0 in the default program, so **~30% of every
voice's time is spent computing modulation that is then multiplied by zero.**

### 5. Filter coefficient recomputation was *not* the bottleneck

The long-standing hypothesis (see the old `TODO_ESP_OPTIMIZATION.md`, and the
comments in `biquad_filter.hpp`) was that `setCutoff` recalculates coefficients
every sample. Measured with a recompute counter (`-DBIQUAD_PROFILE_UPDATES`):

| phase | recomputes per 256 samples |
|---|---|
| attack/decay, just triggered | **11** |
| sustain, held 1 s | **0** |

The change threshold works as designed. Setting `filterEnvAmount = 0` — removing
all cutoff modulation — changes voice cost from 852 to 841 cycles, i.e. nothing.
Chasing this further would be wasted effort.

### 6. Individual operation costs (300 MHz, -Os)

| op | cycles | | op | cycles |
|---|---|---|---|---|
| float multiply | 2.9 | | `sinf` | 82 |
| float add | 11 | | `cosf` | 96 |
| float divide | 13.9 | | `expf` | 123 |
| `sqrtf` (-O3) | 12 | | `tanhf` | 194 |
| | | | `fmodf` | 75 |
| | | | **`powf` / `exp2f`** | **450** |

`powf(2, x)` at 450 cycles is 7% of a whole sample's budget for all eight voices.
`exp2f` is no cheaper — newlib routes it through the same code — so a fast
`2^x` needs a hand-written implementation. It is currently well hidden by the
0.01-semitone pitch cache in `nextSample`, but it is a trap for anything new.

`std::pow(2.0f, x)` correctly resolves to `powf` (447 vs 450), and
`std::sin(float)` to `sinf` (82). No accidental double promotion anywhere.

### 7. Whole-buffer overheads outside the voices

| item | cycles/buffer | % deadline |
|---|---|---|
| `OutputProcessor` (tanh + post biquad) | 57,153 | 3.5% |
| ... of which `tanhf` | 41,488 | 2.6% |
| `std::function` per frame in the mix loop | 62,468 | 3.9% |
| 8 idle voices called anyway | ~80,000 | 5.0% |
| mono → stereo duplication | 2,565 | 0.2% |
| float → int32 conversion | ~0 | 0% |

Silence costs 216,000 cycles (13.5%) before a single note is played.

The mix loop in `SynthApplication::renderAudio` builds a `std::function` **per
frame** and reaches every voice through it, which also blocks inlining of
`nextSample`. Measured alternatives for a 256-frame buffer:

| structure | 1 voice | 8 voices |
|---|---|---|
| `forEachVoice` (`std::function`, current) | 397,985 | 2,047,187 |
| direct array, frame-major | 335,517 | 1,984,796 |
| voice-major, skipping idle voices | **249,407** | 1,988,209 |

Voice-major with idle skipping is 37% faster with one voice held and identical
when all eight sound — exactly the right shape, since real playing rarely holds
eight notes.

## Gotchas found the hard way

- **DWT is per-core.** `CycleCounter::enable()` must be called on core 1 as well
  as core 0; enabling only on core 0 makes core 1's `CYCCNT` read a constant 0.
- **`printf("%f")` prints nothing** in this SDK configuration (newlib-nano). All
  profiling output is integer-formatted, with fixed-point values carried as
  tenths and split by hand.
- **`LapTimer` cannot measure the per-sample path.** Its `nextSpan()` costs 103
  cycles (-Os) — a timer read plus a linear span lookup. The old instrumentation
  put 8 of those *inside* `nextSample`, adding ~800 cycles to a 852-cycle
  function. Any conclusion drawn from it was measuring the instrument. The
  per-buffer profiler used now reads the counter twice per 1.6 M cycles instead.
- `-Os` is appended by the framework after `build_flags`; use `build_unflags`.

## What was changed

Ordered by measured payoff.

1. **Telemetry no longer runs at the scan rate, and no longer allocates.**
   `MidiKeyboardController` throttles reports to every 6th scan (~10 Hz,
   `DEFAULT_TELEMETRY_INTERVAL`), and `KeyScanStats::writeJson` serialises into a
   fixed buffer instead of building five `std::vector`s and a `std::string`.
   `Rp2350TelemetrySink` picks that path up automatically via the `HasDirectJson`
   trait, falling back to nlohmann for payload types that don't offer it.
   Verified equivalent to the nlohmann output by `test/test_telemetry_json/`.
2. **The audio path is compiled `-O3 -fno-math-errno`** (`build_unflags = -Os` in
   the `waveshare_2350b` environment). No code or behaviour change.
3. **The LFOs skip their `sinf` at zero depth** (`Lfo::nextSample`). Phase still
   advances, so nothing jumps if depth comes back mid-note. 122 cycles → 2.
4. **The mix loop is voice-major over indexed access and skips silent voices**
   (`SynthApplication::renderAudio`, `PolyphonicSynthTarget::voiceAt`). No more
   `std::function` per frame, and `nextSample` can inline.
5. **The waveshaper uses `synth::fastTanh`**, a Padé approximant within ~0.2% of
   `tanh` over |x| < 3. `OutputProcessor` per buffer: 57,153 cycles → 14,106.
6. **`BiquadFilter::setCutoff` early-outs on a precomputed tolerance band**
   instead of recomputing a relative delta. The overwhelmingly common no-op path
   went from 50 cycles to 5, with identical behaviour — the band is exactly the
   region the old threshold test would have rejected.

Component costs after the change (-O3):

| | before (-Os) | after |
|---|---|---|
| `Lfo::nextSample`, depth 0 | 121.6 | **2.0** |
| `BiquadFilter::setCutoff`, skipped | 50.0 | **5.0** |
| `WavetableSynth::nextSample`, default program | 852 | **427** |
| `WavetableSynth::nextSample`, all modulation on | 907 | **760** |
| `OutputProcessor`, whole buffer | 57,153 | **14,106** |

## Results

Measured by `tools/profile_voices.py prod_ram_2350b`, ~4000 buffers per data
point, both cores doing their normal work with telemetry enabled:

| voices held | before: min / avg / **max** | starved | after: min / avg / **max** | starved |
|---|---|---|---|---|
| 0 | 14.3% / 14.9% / **22.8%** | 0 | 2.0% / 2.0% / **3.0%** | 0 |
| 1 | 28.7% / 36.2% / **144.9%** | 12 | 9.5% / 9.7% / **17.1%** | **0** |
| 2 | 43.0% / 49.9% / **165.9%** | 4 | 17.1% / 17.4% / **29.2%** | **0** |
| 4 | 72.3% / 99.8% / **204.5%** | 110 | 32.2% / 32.6% / **52.1%** | **0** |
| 6 | 99.5% / 139.5% / **236.5%** | 400 | 47.3% / 47.8% / **67.1%** | **0** |
| 8 | 129.9% / 179.2% / **267.1%** | 300 | 62.4% / 63.2% / **82.0%** | **0** |

All eight voices now fit, worst case, in 82% of the deadline, and no
configuration starves the output. Per voice per sample: ~900 cycles → ~475.
Silence went from 216,000 cycles per buffer to 32,000.

The remaining 1.3× spread between min and max is the USB write for the telemetry
line, which is contention rather than a fixed cost — it slows the render in
proportion to how much work the render is doing. At 19% headroom it no longer
threatens the deadline.

## Deliberately not done

**Triple buffering.** It would give a whole extra buffer period of slack, and the
comment in `core1_audio_loop` already (incorrectly) claims the sink is triple
buffered — it has two buffers, so a late render starves the output immediately.
It was left alone for two reasons: the third buffer adds 5.3 ms of output latency
to an instrument whose whole point is pressure-sensitive expressive playing, and
after the changes above there is 19% headroom with zero starvation, so the slack
is not currently needed. If polyphony grows past eight voices, or a heavier
effect lands in `OutputProcessor`, this is the next lever — but the latency cost
should be a deliberate choice, not a side effect. The misleading comment has been
corrected.

## Ideas not yet explored

- `powf`/`exp2f` at 450 cycles: a small polynomial `2^x` would make pitch
  modulation (vibrato, aftertouch bend) much cheaper. Currently masked by the
  0.01-semitone cache, so it only bites when pitch is actually being swept —
  which is exactly when aftertouch is in use.
- `Oscillator::nextSample` evaluates saw, triangle and square unconditionally and
  applies two PolyBLEP corrections, then multiplies two of the three by zero for
  any non-morphed waveform. 60 cycles, so worth ~25% of a voice.
- Flush-to-zero on the FPU (`FPSCR.FZ`): filter tails decaying into denormals are
  a classic source of unexplained cost. Unmeasured here.
- `NUM_VOICES` could now plausibly go above 8; the ceiling is roughly 12 voices
  at the measured ~475 cycles/sample/voice.
