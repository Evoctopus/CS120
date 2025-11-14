#pragma once

#include <vector>
#include "Mutex_FIFO.h"

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

void push_vector(std::deque<float>& q, const std::vector<float>& v) {
    for (const auto& item : v) {
        q.push_back(item);
    }
}


class Modulator {

private:
	Mutex_FIFO<float>& sending_fifo;
    
    std::vector<float> chirp;
    std::vector<float> carrier1;
    std::vector<float> carrier2;

public:
    Modulator(Mutex_FIFO<float>& Sending_FIFO) : 
		sending_fifo(Sending_FIFO) {
        chirp = generateChirp();
        carrier1 = generateCarrierWave(FREQUENCY1);
		carrier2 = generateCarrierWave(FREQUENCY2);
        //std::vector<float> silence = generateSilence(SILENCE_LENGTH);
    }
    
    void modulate(const std::deque<bool>& frame) {

        std::deque<float> output_track;
        push_vector(output_track, chirp);

		std::deque<bool> frame_copy = frame;
        //print_deque(frame, "Original Frame");
        int length = frame_copy.size();
		encode_header(length, frame_copy, LENGTH_BITS);

        /*frame = hammingEncode(frame);
        length = frame.size();

        std::vector<bool> length_bits = dec2bin(length, LENGTH_BITS);
        frame.insert(frame.begin(), length_bits.begin(), length_bits.end());*/

        length += LENGTH_BITS;
        int i = 0;
        while (i < length)
        {
            float symbol1 = frame_copy[i++] ? 1.0f : -1.0f;
            float symbol2 = 10.0f;
            if (i < length) symbol2 = frame_copy[i++] ? 1.0f : -1.0f;
            for (int k = 0; k < SAMPLES_PER_BIT; ++k) {
                float modulated_carrier = symbol1 * carrier1[k];
                if (symbol2 < 5.0f)
					modulated_carrier += symbol2 * carrier2[k];
                output_track.push_back(modulated_carrier);
            }
        }
        //std::cout << std::endl;

		sending_fifo.push_batch(std::move(output_track));
        return;
    }
};








