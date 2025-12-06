#pragma once

#include "Mutex_FIFO.h"
#include "CRC.h"

#define CRC_BITS 8

using namespace juce;

std::vector<float> generateChirp() {
    float cycle = 1.0 / float(SAMPLE_RATE);
    std::vector<float> Preamble(PREAMBLE_LENGTH, 0.0f);
    std::vector<float> f_p(PREAMBLE_LENGTH, 0.0f);
    std::vector<float> omega(PREAMBLE_LENGTH, 0.0f);

	double f0 = 2000.0;
    double f1 = 10000.0;
    float freq_step = (float(f1 - f0) / float(PREAMBLE_LENGTH)) * 2.0;
    f_p[0] = f0;
    f_p[PREAMBLE_LENGTH / 2] = f1;
    for (int i = 1; i < PREAMBLE_LENGTH / 2; i++)
        f_p[i] = f_p[i - 1] + freq_step;
    for (int i = PREAMBLE_LENGTH / 2 + 1; i < PREAMBLE_LENGTH; i++)
        f_p[i] = f_p[i - 1] - freq_step;
    for (int i = 1; i < PREAMBLE_LENGTH; i++)
    {
        omega[i] = omega[i - 1] + ((f_p[i] + f_p[i - 1]) / 2.0) * cycle;
    }
    for (int i = 0; i < PREAMBLE_LENGTH; i++)
        Preamble[i] = sin(2 * PI * omega[i]);
	return Preamble;
}

std::vector<float> generateSilence(int length) {
    return std::vector<float>(length, 0.0f);
}

std::vector<float> generateCarrierWave(int frequency) {
    std::vector<float> carrier(SAMPLES_PER_BIT);
    double omega = 2.0 * PI * frequency;
    for (int i = 0; i < SAMPLES_PER_BIT; ++i) {
        double t = static_cast<double>(i) / SAMPLE_RATE;
        carrier[i] = static_cast<float>(sin(omega * t));
    }
    return carrier;
}


class Modulator {

private:

	Mutex_FIFO<float>& sending_fifo;
    CRC8 crc_handler;

    std::vector<float> chirp;
    std::vector<float> carrier1;
    std::vector<float> carrier2;

    void encode_crc(std::deque<bool>& payload) const {
        int crc = crc_handler.calculate(payload);

        encode_header(crc, payload, CRC_BITS);
        return;
    }

public:
    Modulator(Mutex_FIFO<float>& Sending_FIFO) : 
		sending_fifo(Sending_FIFO) {
        chirp = generateChirp();
        carrier1 = generateCarrierWave(FREQUENCY1);
		carrier2 = generateCarrierWave(FREQUENCY2);
        //std::vector<float> silence = generateSilence(SILENCE_LENGTH);
    }

    std::vector<float> PSK(const std::deque<bool>& data) {
        int i = 0;
        std::vector<float> output_track;
        size_t length = data.size();
        while (i < length)
        {
            float symbol1 = data[i++] ? 1.0f : -1.0f;
            float symbol2 = 10.0f;
            if (i < length) symbol2 = data[i++] ? 1.0f : -1.0f;
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                float modulated_carrier = symbol1 * carrier1[k];
                if (symbol2 < 5.0f)
                    modulated_carrier += symbol2 * carrier2[k];
                output_track.push_back(modulated_carrier);
            }
        }
        return output_track;
    }

    std::vector<float> Line_Coding(const std::deque<bool>& data) {

        std::vector<float> output_track;
        for (bool bit : data)
        {   
            float symbol1 = bit ? 1.0f : -1.0f;
            float symbol2 = bit ? -1.0f : 1.0f;
            
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                if (k < SAMPLES_PER_BIT / 2)
                    output_track.push_back(symbol1);
                else 
                    output_track.push_back(symbol2);               
            }
        }
        return output_track;
    }
    
    void modulate(std::deque<bool> frame) {

        encode_crc(frame);
        int length = frame.size();
		encode_header(length, frame, LENGTH_BITS);

        /*frame = hammingEncode(frame);
        length = frame.size();

        std::vector<bool> length_bits = dec2bin(length, LENGTH_BITS);
        frame.insert(frame.begin(), length_bits.begin(), length_bits.end());*/

        std::deque<float> output_track;
        output_track.insert(output_track.end(), chirp.begin(), chirp.end());
        std::vector<float> encoded_signal = Line_Coding(frame);
        output_track.insert(output_track.end(), encoded_signal.begin(), encoded_signal.end());

		sending_fifo.push_batch(std::move(output_track));
        return;
    }
};








