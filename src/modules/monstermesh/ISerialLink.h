#pragma once
#include <Arduino.h>

class ISerialLink {
public:
    virtual ~ISerialLink() = default;
    virtual void onSerialTx(uint8_t byte) = 0;
    virtual bool onSerialRx(uint8_t &out) = 0;
};
