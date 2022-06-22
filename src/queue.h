// Copyright (C) 2022 DEV47APPS, github.com/dev47apps
#pragma once

#include <vector>
#include <mutex>

struct DataPacket {
    uint8_t *data;
    size_t size;
    size_t used;
    uint64_t pts;

    DataPacket(size_t new_size) {
        size = 0;
        data = 0;
        resize(new_size);
    }

    ~DataPacket(void) {
        if (data) bfree(data);
    }

    void resize(size_t new_size) {
        if (size < new_size){
            data = (uint8_t*) brealloc(data, new_size);
            size = new_size;
        }
    }
};

struct DataQueue {
    std::vector<DataPacket*> readyQueue;
    std::vector<DataPacket*> emptyQueue;
    size_t alloc_count;
    std::mutex mutex;

    inline void lock() { mutex.lock(); }
    inline void unlock() { mutex.unlock(); }

    DataQueue(void) {
        alloc_count = 0;
    }

    void clear() {
        DataPacket* packet;
        while ((packet = pull_ready_packet()) != NULL) {
            delete packet;
            alloc_count --;
        }
        while ((packet = pull_empty_packet(0)) != NULL){
            delete packet;
            alloc_count --;
        }
    }

    ~DataQueue(void) {
        clear();
        ilog("~alloc_count=%lu", alloc_count);
    }

    inline DataPacket* pull_ready_packet(void) {
        DataPacket* packet;
        if (readyQueue.size()) {
            packet = readyQueue.front();
            readyQueue.erase(readyQueue.begin());
        }
        else {
            packet = NULL;
        }
        return packet;
    }

    DataPacket* pull_empty_packet(size_t size) {
        DataPacket* packet;
        if (emptyQueue.size()) {
            packet = emptyQueue.front();
            emptyQueue.erase(emptyQueue.begin());
            packet->resize(size);
        }
        else if (size) {
            packet = new DataPacket(size);
            ilog("alloc: size=%ld", size);
            alloc_count ++;
        }
        else {
            return NULL;
        }
        packet->used = 0;
        return packet;
    }

    inline void push_empty_packet(DataPacket* packet) {
        emptyQueue.push_back(packet);
    }

    inline void push_ready_packet(DataPacket* packet) {
        readyQueue.push_back(packet);
    }
};
