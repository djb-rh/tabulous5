#include "apu2A03.h"
#include "bus.h"
#include "cpu6502.h"

#ifdef COMPOSITE_VIDEO
void cv_audio_write_16(const uint16_t* s, int len, int channels);
bool cv_audio_buffer_full(int buffer_size);
#endif

DMA_ATTR uint16_t Apu2A03::audio_buffer[AUDIO_BUFFER_SIZE * 2];
Apu2A03::AudioCallback Apu2A03::audio_callback = nullptr;

Apu2A03::Apu2A03()
{
    memset(audio_buffer, 0, sizeof(audio_buffer));
}

Apu2A03::~Apu2A03()
{
}

void Apu2A03::reset()
{
    pulse1_enable = false;
    pulse2_enable = false;
    triangle_enable = false;
    noise_enable = false;
    DMC_enable = false;
    IRQ = false;

    pulse1.len_counter.timer = 0;
    pulse2.len_counter.timer = 0;
    triangle.len_counter.timer = 0;
    noise.len_counter.timer = 0;

    DMC.output_unit.output_level = 0;
    DMC.output_unit.remaining_bits = 0;
    DMC.output_unit.shift_register = 0;
    DMC.memory_reader.address = 0;
    DMC.memory_reader.remaining_bytes = 0;
    DMC.timer = 0;
    DMC.sample_address = 0;
    DMC.sample_buffer = 0;
    DMC.sample_length = 0;
    DMC.output_unit.silence_flag = true;
}

void Apu2A03::cpuWrite(uint16_t addr, uint8_t data)
{
    switch (addr)
    {
    case 0x4000:
        pulse1.seq.duty_cycle = ((data & 0xC0) >> 6);

        pulse1.env.loop = ((data >> 5) & 0x01);
        pulse1.len_counter.halt = ((data >> 5) & 0x01);
        pulse1.env.constant_volume = ((data >> 4) & 0x01);
        pulse1.env.volume = (data & 0x0F);
        break;

    case 0x4001:
        pulse1.sweep.enable = (data >> 7);
        pulse1.sweep.reload = ((data >> 4) & 0x07) + 1;
        pulse1.sweep.negate = ((data >> 3) & 0x01);
        pulse1.sweep.shift_count = data & 0x07;
        pulse1.sweep.reload_flag = true;
        break;

    case 0x4002: pulse1.seq.reload = (pulse1.seq.reload & 0xFF00) | data; break;

    case 0x4003:
        pulse1.seq.cycle_position = 0;
        pulse1.seq.reload = (pulse1.seq.reload & 0x00FF) | (uint16_t)((data & 0x07) << 8);
        pulse1.seq.timer = pulse1.seq.reload;
        pulse1.env.start_flag = true;

        if (pulse1_enable) pulse1.len_counter.timer = length_counter_lookup[data >> 3] + 1;

        // Restart envelope
        pulse1.env.timer = pulse1.env.volume;
        pulse1.env.decay_level_counter = 15;
        break;

    case 0x4004:
        pulse2.seq.duty_cycle = ((data & 0xC0) >> 6);

        pulse2.env.loop = ((data >> 5) & 0x01);
        pulse2.len_counter.halt = pulse2.env.loop;
        pulse2.env.constant_volume = ((data >> 4) & 0x01);
        pulse2.env.volume = (data & 0x0F);
        break;

    case 0x4005:
        pulse2.sweep.enable = (data >> 7);
        pulse2.sweep.reload = ((data >> 4) & 0x07) + 1;
        pulse2.sweep.negate = ((data >> 3) & 0x01);
        pulse2.sweep.shift_count = data & 0x07;
        pulse2.sweep.reload_flag = true;
        break;

    case 0x4006: pulse2.seq.reload = (pulse2.seq.reload & 0xFF00) | data; break;

    case 0x4007:
        pulse2.seq.cycle_position = 0;
        pulse2.seq.reload = (pulse2.seq.reload & 0x00FF) | (uint16_t)((data & 0x07) << 8);
        pulse2.seq.timer = pulse2.seq.reload;
        pulse2.env.start_flag = true;

        if (pulse2_enable) pulse2.len_counter.timer = length_counter_lookup[data >> 3] + 1;

        // Restart envelope
        pulse2.env.timer = pulse2.env.volume;
        pulse2.env.decay_level_counter = 15;
        break;

    case 0x4008:
        triangle.lin_counter.reload = data & 0x7F;
        triangle.len_counter.halt = data >> 7;
        triangle.lin_counter.control = data >> 7;
        break;

    case 0x400A: triangle.seq.reload = (triangle.seq.reload & 0xFF00) | data; break;

    case 0x400B:
        triangle.seq.reload = ((triangle.seq.reload & 0x00FF) | (uint16_t)((data & 0x07)) << 8) + 1;
        triangle.seq.timer = triangle.seq.reload;

        if (triangle_enable) triangle.len_counter.timer = length_counter_lookup[data >> 3] + 1;
        triangle.lin_counter.reload_flag = true;
        break;

    case 0x400C:
        // Bit 5 is BOTH the length-counter halt and the envelope loop flag,
        // exactly as on the pulse channels above. Upstream set only the halt
        // for noise, so a looping noise envelope decayed once and went silent.
        noise.len_counter.halt = (data >> 5) & 0x01;
        noise.env.loop = noise.len_counter.halt;
        noise.env.constant_volume = (data >> 4) & 0x01;
        noise.env.volume = data & 0x0F;
        break;

    case 0x400E:
        noise.mode = data >> 7;
        noise.reload = noise_period_lookup[data & 0x0F] / 2;
        break;

    case 0x400F:
        noise.env.start_flag = true;

        if (noise_enable) noise.len_counter.timer = length_counter_lookup[data >> 3] + 1;
        break;

    case 0x4010:
        DMC.IRQ_flag = data >> 7;
        DMC.loop_flag = (data & 0x40) == 0x40;
        DMC.reload = (DMC_rate_lookup[data & 0x0F] / 2) - 1;
        DMC.timer = DMC.reload;
        break;

    case 0x4011: DMC.output_unit.output_level = data & 0x7F; break;

    case 0x4012:
        DMC.sample_address = 0xC000 | ((uint32_t)data << 6);
        DMC.memory_reader.address = DMC.sample_address;
        break;

    case 0x4013:
        DMC.sample_length = (data << 4) | 0x0001;
        DMC.memory_reader.remaining_bytes = DMC.sample_length;
        break;

    case 0x4015:
        IRQ = false;
        // Pulse 1 enable
        if (data & 0x01) { pulse1_enable = true; }
        else
        {
            pulse1_enable = false;
            pulse1.len_counter.timer = 0;
        }

        // Pulse 2 enable
        if ((data >> 1) & 0x01) { pulse2_enable = true; }
        else
        {
            pulse2_enable = false;
            pulse2.len_counter.timer = 0;
        }

        // Triangle enable
        if ((data >> 2) & 0x01) { triangle_enable = true; }
        else
        {
            triangle_enable = false;
            triangle.len_counter.timer = 0;
        }

        // Noise enable
        if ((data >> 3) & 0x01) { noise_enable = true; }
        else
        {
            noise_enable = false;
            noise.len_counter.timer = 0;
        }

        // DMC enable
        if ((data >> 4) & 0x01)
        {
            DMC_enable = true;
            if (DMC.sample_buffer_empty)
            {
                setDMCBuffer();
                cpu->cycles += 3;
            }
        }
        else { DMC_enable = false; }
        break;

    case 0x4017:
        four_step_sequence_mode = ((data >> 7) == 0) ? true : false;

        if (((data >> 6) & 0x01) == 1)
        {
            IRQ = false;
            interrupt_inhibit = true;
        }
        else interrupt_inhibit = false;
        break;

    default: return;
    }
}

uint8_t Apu2A03::cpuRead(uint16_t addr)
{
    uint8_t data = 0x00;
    if (addr == 0x4015) { IRQ = false; }
    return data;
}

void Apu2A03::setVolume(uint8_t vol)
{
    volume = vol;
}

void Apu2A03::clock()
{
    // Clock all sound channels
    pulseChannelClock(pulse1.seq, pulse1_enable);
    pulseChannelClock(pulse2.seq, pulse2_enable);
    noiseChannelClock(noise, noise_enable);
    DMCChannelClock(DMC, DMC_enable);
    triangleChannelClock(triangle, triangle_enable);

    switch (clock_counter)
    {
    case 3728:
        soundChannelEnvelopeClock(pulse1.env);
        soundChannelEnvelopeClock(pulse2.env);
        soundChannelEnvelopeClock(noise.env);
        linearCounterClock(triangle.lin_counter);
        break;

    case 7456:
        soundChannelEnvelopeClock(pulse1.env);
        soundChannelEnvelopeClock(pulse2.env);
        soundChannelEnvelopeClock(noise.env);
        linearCounterClock(triangle.lin_counter);

        soundChannelSweeperClock(pulse1);
        soundChannelLengthCounterClock(pulse1.len_counter);

        soundChannelSweeperClock(pulse2);
        soundChannelLengthCounterClock(pulse2.len_counter);

        soundChannelLengthCounterClock(triangle.len_counter);
        soundChannelLengthCounterClock(noise.len_counter);
        break;

    case 11185:
        soundChannelEnvelopeClock(pulse1.env);
        soundChannelEnvelopeClock(pulse2.env);
        soundChannelEnvelopeClock(noise.env);
        linearCounterClock(triangle.lin_counter);
        break;

    case 14914:
        if (four_step_sequence_mode)
        {
            if (!interrupt_inhibit) IRQ = true;
            soundChannelEnvelopeClock(pulse1.env);
            soundChannelEnvelopeClock(pulse2.env);
            soundChannelEnvelopeClock(noise.env);
            linearCounterClock(triangle.lin_counter);

            soundChannelSweeperClock(pulse1);
            soundChannelLengthCounterClock(pulse1.len_counter);

            soundChannelSweeperClock(pulse2);
            soundChannelLengthCounterClock(pulse2.len_counter);

            soundChannelLengthCounterClock(triangle.len_counter);
            soundChannelLengthCounterClock(noise.len_counter);
            clock_counter = 0;
        }
        break;

    case 18640:
        if (!four_step_sequence_mode)
        {
            soundChannelEnvelopeClock(pulse1.env);
            soundChannelEnvelopeClock(pulse2.env);
            soundChannelEnvelopeClock(noise.env);
            linearCounterClock(triangle.lin_counter);

            soundChannelSweeperClock(pulse1);
            soundChannelLengthCounterClock(pulse1.len_counter);

            soundChannelSweeperClock(pulse2);
            soundChannelLengthCounterClock(pulse2.len_counter);

            soundChannelLengthCounterClock(triangle.len_counter);
            soundChannelLengthCounterClock(noise.len_counter);
            clock_counter = 0;
        }
        break;

    default: break;
    }

    // Mix at EVERY clock and average across the ~20.3 clocks that make up
    // one output sample, instead of grabbing a single instantaneous value.
    //
    // Upstream sampled-and-held. A pulse channel at period 9 runs at ~11 kHz
    // and its harmonics at 33 and 55 kHz then fold straight back into the
    // audible band when taken at 44.1 kHz — inharmonic hash that appears
    // exactly where a sweep reaches the top of its range, i.e. the tail of
    // every jump. Averaging across the whole inter-sample interval is a box
    // decimation filter: the area under the curve, which is what the real
    // console's RC output stage approximates. See ../CHANGES.md.
    //
    // Muting is applied per clock too; at sample time only, a muted channel
    // would keep contributing between samples.
    {
        const uint8_t p1 = (pulse1.sweep.mute || pulse1.seq.reload < 8 ||
                            pulse1.len_counter.timer == 0 || !pulse1.seq.output)
                               ? 0 : pulse1.env.output;
        const uint8_t p2 = (pulse2.sweep.mute || pulse2.seq.reload < 8 ||
                            pulse2.len_counter.timer == 0 || !pulse2.seq.output)
                               ? 0 : pulse2.env.output;
        const uint8_t tri = triangle.seq.output;
        const uint8_t noi = (!(noise.shift_register & 0x01) && noise.len_counter.timer > 0)
                                ? noise.env.output : 0;
        const uint8_t dmc = DMC.output_unit.output_level;
        const int32_t m = (int32_t)mixTables(p1 + p2, 3 * tri + 2 * noi + dmc);

        // Anti-alias BEFORE decimation: three cascaded one-poles at ~14 kHz,
        // running at the full 895 kHz clock. A box average alone is a weak
        // filter — it left ~34% of the worst alias through, and the hiss was
        // reported unchanged. Three poles give ~-24 dB at 32 kHz on their own
        // and ~-33 dB combined with the averaging that follows.
        //   a = 1 - exp(-2*pi*14000/894886) = 0.0937 -> 384/4096
        lp1 += ((m - lp1) * 384) >> 12;
        lp2 += ((lp1 - lp2) * 384) >> 12;
        lp3 += ((lp2 - lp3) * 384) >> 12;
        mix_acc += (uint32_t)lp3;
        mix_n++;
    }

    // Generate a sample every 20.29221088 clocks: (1.789773 MHz / 2) / 44100 Hz
    pulse_hz += SAMPLE_RATE;
    if (pulse_hz > 894886)
    {
        generateSample();
        pulse_hz -= 894886;
    }
    clock_counter++;
}

// The NES's non-linear mixer (NESdev formulas), tabulated on first use.
inline uint32_t Apu2A03::mixTables(uint8_t pulse_idx, uint8_t tnd_idx)
{
    static uint16_t pulse_table[31];
    static uint16_t tnd_table[203];
    static bool ready = false;
    if (!ready)
    {
        pulse_table[0] = 0;
        for (int i = 1; i < 31; i++)
            pulse_table[i] = (uint16_t)((95.88 / (8128.0 / i + 100.0)) * 32767.0);
        tnd_table[0] = 0;
        for (int i = 1; i < 203; i++)
            tnd_table[i] = (uint16_t)((163.67 / (24329.0 / i + 100.0)) * 32767.0);
        ready = true;
    }
    return (uint32_t)pulse_table[pulse_idx] + tnd_table[tnd_idx];
}

inline void Apu2A03::generateSample()
{
    uint16_t index = (buffer_index << 1);

    // The average of everything mixed since the last sample. See clock().
    uint32_t mixed = mix_n ? mix_acc / mix_n : 0;
    mix_acc = 0;
    mix_n = 0;
    mixed = mixed * volume / 100;
    if (mixed > 65535) mixed = 65535;

    uint16_t sample = (uint16_t)((mixed + prev_sample) >> 1);
    prev_sample = sample;
    audio_buffer[index] = sample;
    audio_buffer[index + 1] = sample;

    // Reset audio buffer index once filled
    buffer_index++;
    if (buffer_index >= AUDIO_BUFFER_SIZE)
    {
        buffer_index = 0;
        writeBuffer();
    }
}

inline void Apu2A03::writeBuffer()
{
#ifndef COMPOSITE_VIDEO
    if (audio_callback) audio_callback(audio_buffer, sizeof(audio_buffer));
#else
    while (cv_audio_buffer_full(AUDIO_BUFFER_SIZE)) vTaskDelay(1);
    cv_audio_write_16((const uint16_t*)audio_buffer, AUDIO_BUFFER_SIZE, 2);
#endif
}

inline void Apu2A03::pulseChannelClock(sequencerUnit& seq, bool enable)
{
    if (!enable) return;

    seq.timer--;
    if (seq.timer == 0xFFFF)
    {
        seq.timer = seq.reload;
        // Shift duty cycle with wrapping
        seq.output = duty_sequences[seq.duty_cycle][seq.cycle_position];
        seq.cycle_position = (seq.cycle_position + 1) & 7;
    }
}

inline void Apu2A03::triangleChannelClock(triangleChannel& triangle, bool enable)
{
    if (!enable) return; // Temp

    for (int i = 0; i < 2; i++)
    {
        triangle.seq.timer--;
        if (triangle.seq.timer == 0)
        {
            triangle.seq.timer = triangle.seq.reload;
            if (!(triangle.len_counter.timer > 0 && triangle.lin_counter.counter > 0)) return;

            if (triangle.seq.reload >= 2)
            {
                triangle.seq.output = triangle_sequence[triangle.seq.duty_cycle];
                triangle.seq.duty_cycle = (triangle.seq.duty_cycle + 1) & 31;
            }
        }
    }
}

inline void Apu2A03::noiseChannelClock(noiseChannel& noise, bool enable)
{
    if (!enable) return; // Temp

    noise.timer--;
    if (noise.timer == 0xFFFF)
    {
        noise.timer = noise.reload;
        uint8_t temp =
            noise.mode ? (noise.shift_register >> 6) & 0x01 : (noise.shift_register >> 1) & 0x01;
        noise.output = (noise.shift_register & 0x01) ^ (temp);
        noise.shift_register >>= 1;
        noise.shift_register |= noise.output << 14;
    }
}

inline void Apu2A03::DMCChannelClock(DMCChannel& DMC, bool enable)
{
    if (!enable) return;

    DMC.timer--;
    if (DMC.timer == 0xFFFF)
    {
        DMC.timer = DMC.reload + 1;
        if (DMC.output_unit.silence_flag == false)
        {
            if (DMC.output_unit.shift_register & 0x01)
            {
                if (DMC.output_unit.output_level <= 125) DMC.output_unit.output_level += 2;
            }
            else
            {
                if (DMC.output_unit.output_level >= 2) DMC.output_unit.output_level -= 2;
            }

            DMC.output_unit.shift_register >>= 1;
        }

        // Update Bits remaining counter
        DMC.output_unit.remaining_bits--;
        if (DMC.output_unit.remaining_bits <= 0)
        {
            DMC.output_unit.remaining_bits = 8;

            if (DMC.sample_buffer_empty) { DMC.output_unit.silence_flag = true; }
            else
            {
                DMC.output_unit.silence_flag = false;
                DMC.output_unit.shift_register = DMC.sample_buffer;
                DMC.sample_buffer_empty = true;
                setDMCBuffer();
                cpu->cycles += 4;
            }
        }
    }
}

inline void Apu2A03::soundChannelEnvelopeClock(envelopeUnit& envelope)
{
    if (envelope.start_flag)
    {
        envelope.start_flag = false;
        envelope.decay_level_counter = 15;
        envelope.timer = envelope.volume + 1;
    }
    else
    {
        envelope.timer--;
        if (envelope.timer == 0)
        {
            envelope.timer = envelope.volume + 1;
            if (envelope.decay_level_counter > 0) envelope.decay_level_counter--;
            else if (envelope.loop) envelope.decay_level_counter = 15;
        }
    }

    if (envelope.constant_volume) envelope.output = envelope.volume;
    else envelope.output = envelope.decay_level_counter;
}

inline void Apu2A03::soundChannelSweeperClock(pulseChannel& channel)
{
    // Calculate the target period
    channel.sweep.change = (int16_t)(channel.seq.reload >> channel.sweep.shift_count);
    // Negate change if negate flag is true
    // Pulse 1 adds one's complement = -c - 1
    // Pulse 2 adds two's complement = -c
    if (channel.sweep.negate && channel.sweep.pulse_channel_number == 1)
        channel.sweep.change = (int16_t)(-channel.sweep.change - 1);
    else if (channel.sweep.negate && channel.sweep.pulse_channel_number == 2)
        channel.sweep.change = (int16_t)(-channel.sweep.change);

    channel.sweep.target_period = (int16_t)(channel.seq.reload + channel.sweep.change);
    if (channel.sweep.target_period < 0) channel.sweep.target_period = 0;

    // Check if channel should be muted
    if (channel.seq.reload < 8) channel.sweep.mute = true;
    else if (channel.sweep.target_period > 0x7FF) channel.sweep.mute = true;
    else channel.sweep.mute = false;

    channel.sweep.timer--;
    if (channel.sweep.enable && channel.sweep.timer == 0 && channel.sweep.shift_count != 0)
    {
        if (!channel.sweep.mute) channel.seq.reload = channel.sweep.target_period;
    }
    if (channel.sweep.timer == 0 || channel.sweep.reload_flag)
    {
        channel.sweep.timer = channel.sweep.reload;
        channel.sweep.reload_flag = false;
    }
}

inline void Apu2A03::soundChannelLengthCounterClock(length_counter& len_counter)
{
    if (!len_counter.halt && len_counter.timer > 0) len_counter.timer--;
}

inline void Apu2A03::linearCounterClock(linear_counter& lin_counter)
{
    if (lin_counter.reload_flag) lin_counter.counter = lin_counter.reload;
    else if (lin_counter.counter > 0) lin_counter.counter--;

    if (lin_counter.control == 0) lin_counter.reload_flag = false;
}

inline void Apu2A03::setDMCBuffer()
{
    uint8_t value = bus->cpuRead(DMC.memory_reader.address);
    if (DMC.memory_reader.remaining_bytes == 0) return;

    DMC.sample_buffer = value;
    DMC.sample_buffer_empty = false;

    DMC.memory_reader.address++;
    if (DMC.memory_reader.address == 0x0000) DMC.memory_reader.address = 0x8000;

    DMC.memory_reader.remaining_bytes--;
    if (DMC.memory_reader.remaining_bytes == 0)
    {
        if (DMC.loop_flag)
        {
            // Restart sample
            DMC.memory_reader.address = DMC.sample_address;
            DMC.memory_reader.remaining_bytes = DMC.sample_length;
        }
        else if (DMC.IRQ_flag) IRQ = true;
    }
}
