#pragma once

#include <unordered_map>
#include <stdexcept> // 用于异常处理
#include <utility>   // std::pair

template <typename K, typename V>
class BiMap {
private:
    // 两个哈希表维护双向映射
    std::unordered_map<K, V> k2v;
    std::unordered_map<V, K> v2k;

public:
    // 插入键值对（保证双射：Key和Value都唯一）
    void insert(const K& key, const V& val) {
        // 检查Key或Value是否已存在，避免冲突
        if (k2v.count(key) || v2k.count(val)) {
            throw std::invalid_argument("Key or Value already exists (bijection violated)");
        }
        k2v[key] = val;
        v2k[val] = key;
    }

    // 从Key查Value
    V get_by_key(const K& key) const {
        auto it = k2v.find(key);
        if (it == k2v.end()) {
            throw std::out_of_range("Key not found");
        }
        return it->second;
    }

    // 从Value查Key
    K get_by_val(const V& val) const {
        auto it = v2k.find(val);
        if (it == v2k.end()) {
            throw std::out_of_range("Value not found");
        }
        return it->second;
    }

    // 删除（按Key）
    void erase_by_key(const K& key) {
        auto it = k2v.find(key);
        if (it != k2v.end()) {
            v2k.erase(it->second); // 同步删除Value→Key的映射
            k2v.erase(it);
        }
    }

    // 删除（按Value）
    void erase_by_val(const V& val) {
        auto it = v2k.find(val);
        if (it != v2k.end()) {
            k2v.erase(it->second); // 同步删除Key→Value的映射
            v2k.erase(it);
        }
    }

    // 判空/清空
    bool empty() const { return k2v.empty(); }
    void clear() {
        k2v.clear();
        v2k.clear();
    }
};