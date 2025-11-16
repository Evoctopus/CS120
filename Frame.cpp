#include "Frame.h"
#include <chrono>
#include <vector>

// 基类Frame构造函数
// 初始化帧类型、目的地址、源地址、索引、数据载荷和重传系数
Frame::Frame(int type, int destination, int source, int index, const std::vector<int>& data)
    : frameType(type),
    destinationAddr(destination),
    sourceAddr(source),
    frameIndex(index),
    data(data),
    resendCoefficient(1.0f)  // 初始重传系数为1.0
{
}

// 获取目的地址
int Frame::getDestination() const {
    return destinationAddr;
}

// 获取源地址
int Frame::getSource() const {
    return sourceAddr;
}

// 获取帧索引
int Frame::getIndex() const {
    return frameIndex;
}

// 获取帧类型
int Frame::getType() const {
    return frameType;
}

// 设置帧索引（用于更新序号）
void Frame::setIndex(int index) {
    frameIndex = index;
}

// 数据帧构造函数
// 继承自Frame，额外初始化发送时间戳
DataFrame::DataFrame(int type,
    int destination,
    int source,
    int index,
    const std::vector<int>& data,
    std::chrono::time_point<std::chrono::steady_clock> sendTime)
    : Frame(type, destination, source, index, data),
    sendTime(sendTime)
{
}

// 更新发送时间戳（用于重传时刷新计时）
void DataFrame::setSendTime(std::chrono::time_point<std::chrono::steady_clock> newTime) {
    sendTime = newTime;
}

// 获取发送时间戳（用于超时检测）
std::chrono::time_point<std::chrono::steady_clock> DataFrame::getSendTime() const {
    return sendTime;
}

// 确认帧构造函数
// 继承自Frame，数据载荷为空向量（无需携带数据）
AckFrame::AckFrame(int type, int destination, int source, int index)
    : Frame(type, destination, source, index, std::vector<int>())  // 空数据向量
{
}