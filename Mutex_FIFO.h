#pragma once

#include "Utils.h"


template <typename T>
class Mutex_FIFO {
private:
    mutable std::mutex mtx_;
    std::deque<T> data_;

public:
    Mutex_FIFO() = default;
    ~Mutex_FIFO() = default;

    // 禁用拷贝和移动，确保线程安全的简单性
    Mutex_FIFO(const Mutex_FIFO& other) = delete;
    Mutex_FIFO& operator=(const Mutex_FIFO& other) = delete;
    Mutex_FIFO(Mutex_FIFO&& other) = delete;
    Mutex_FIFO& operator=(Mutex_FIFO&& other) = delete;

    // --- 基础操作 ---

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.clear();
    }

    void push(T value) {
        std::lock_guard<std::mutex> lock(mtx_);
        data_.push_back(value);
    }

    bool pop(T& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.empty()) {
            return false;
        }
        value = data_.front();
        data_.pop_front();
        return true;
    }

    // --- 批量推入操作 ---

    void push_batch(const std::vector<T>& source) {
        if (source.empty()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& item : source) {
            data_.push_back(item);
		}
        //std::copy(source.begin(), source.end(), std::back_inserter(data_));
    }

    void push_batch(const T* const source, size_t len) {
        if (len == 0 || source == nullptr) return;

        std::lock_guard<std::mutex> lock(mtx_);

        for (size_t i = 0; i < len; ++i) {
			data_.push_back(source[i]);
        }
        return;
    }

    void push_batch(std::vector<T>&& source) {
        if (source.empty()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        std::move(source.begin(), source.end(), std::back_inserter(data_));
    }

    void push_batch(const std::queue<T>& source) {
        if (source.empty()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        // 由于 queue 不能直接迭代，我们需要复制一份来操作
        std::queue<T> temp_q = source;
        while (!temp_q.empty()) {
            data_.push_back(temp_q.front());
            temp_q.pop();
        }
    }

    void push_batch(std::queue<T>&& source) {
        if (source.empty()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        while (!source.empty()) {
            data_.push_back(std::move(source.front())); // 对 T 来说 move 和 copy 一样快
            source.pop();
        }
    }

    // --- 批量弹出操作 ---

    size_t pop_batch(std::vector<T>& dest, size_t max_count) {
        if (max_count == 0) return 0;

        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.empty()) return 0;

        size_t actual_count = std::min(max_count, data_.size());
        dest.reserve(dest.size() + actual_count);

        for (size_t i = 0; i < actual_count; ++i) {
            dest.push_back(data_.front());
            data_.pop_front();
        }

        return actual_count;
    }

    size_t pop_batch(T* const dest, size_t max_count) {
        if (max_count == 0 || dest == nullptr) return 0;

        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.empty()) return 0;

        size_t actual_count = std::min(max_count, data_.size());

        for (size_t i = 0; i < actual_count; ++i) {
            dest[i] = data_.front();
            data_.pop_front();
        }

        return actual_count;
    }

    size_t pop_batch(std::queue<T>& dest, size_t max_count) {
        if (max_count == 0) return 0;

        std::lock_guard<std::mutex> lock(mtx_);
        if (data_.empty()) return 0;

        size_t actual_count = std::min(max_count, data_.size());

        for (size_t i = 0; i < actual_count; ++i) {
            dest.push(data_.front());
            data_.pop_front();
        }

        return actual_count;
    }

    // 调试用：打印队列内容
    void print_queue() const {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << "Queue contents: ";
        for (T val : data_) {
            std::cout << val << " ";
        }
        std::cout << "(size: " << data_.size() << ")" << std::endl;
    }

    std::vector<T> vectorize() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return std::vector<T>(data_.begin(), data_.end());
	}

    std::deque<T> dequeize() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return data_;
	}
};




bool checkQueue(std::deque<float> queue, std::queue<float> queue2) {

    if (queue.size() != queue2.size()) {
		std::cout << "Size mismatch: FIFO size = " << queue.size() << ", Queue size = " << queue2.size() << std::endl;
        return false;
	}
    while (!queue2.empty()) {
        float val1, val2;
		val1 = queue.front();
		queue.pop_front();
        val2 = queue2.front();
        queue2.pop();
        if (val1 != val2) {
            return false;
        }
    }
    return true;
}