#include <JuceHeader.h>
#include <chrono>
#include <vector>

#ifndef FRAME_H
#define FRAME_H


class Frame {
public:

    Frame(int type, int destination, int source, int index, const std::vector<int>& data);

    // 获取目的地址
    int getDestination() const;

    // 获取源地址
    int getSource() const;

    // 获取帧索引
    int getIndex() const;

    // 获取帧类型
    int getType() const;

    // 设置帧索引
    void setIndex(int index);

    // 数据载荷
    std::vector<int> data;

    // 重传系数
    float resendCoefficient;

protected:
    int frameType;       // 帧类型标识
    int destinationAddr; // 目的地址
    int sourceAddr;      // 源地址
    int frameIndex;      // 帧序号索引
};


class DataFrame : public Frame {
public:

    DataFrame(int type,
        int destination,
        int source,
        int index,
        const std::vector<int>& data,
        std::chrono::time_point<std::chrono::steady_clock> sendTime);

    // 获取发送时间戳
    std::chrono::time_point<std::chrono::steady_clock> getSendTime() const;

    // 更新发送时间戳
    void setSendTime(std::chrono::time_point<std::chrono::steady_clock> newTime);

private:
    // 发送时间戳
    std::chrono::time_point<std::chrono::steady_clock> sendTime;
};


class AckFrame : public Frame {
public:

    AckFrame(int type, int destination, int source, int index);
};

#endif // FRAME_H