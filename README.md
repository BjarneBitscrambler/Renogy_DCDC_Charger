# Renogy_DCDC_Charger

Remote interface to a Renogy DC-DC Charger using its RS-485 Modbus port. Control and monitoring logic is implemented on an ESP8266.

## Background

Some Renogy DC-DC Chargers include a remote **RS-485 interface port**, by which one can control and monitor the charger via *Modbus* data packets. Renogy sells a Bluetooth dongle that plugs into this port, and communicates with an app on a device such as a smart phone.  **This project provides an alternative to the Bluetooth dongle**: using an inexpensive microcontroller such as the ESP8266 and an RS-485 interface module, one can directly control the charger without using Renogy's dongle or their app.

Renogy published in 2025 an API for their Chargers, called [DCC Charger Controller Modbus Protocol v1.0](https://renogy-website.oss-us-east-1.aliyuncs.com/DeveloperPlatform/DCC_Charger_Controller_Modbus_Protocol_V1.0_EN.pdf "Renogy Modbus Protocol v1.0")  This document is fairly general, and not entirely accurate when dealing with specific models or DC-DC charger. By testing the various register settings listed in their API, I have reverse-engineered the actual functions of many of the registers.

## Hardware
- Renogy DC-DC Charger 12V 40A, model RBC40D1U  (I suspect the 20A model would have identical register behaviour)
- ESP8266 module, such as the [WeMos LOLIN D1-mini](https://www.wemos.cc/en/latest/d1/d1_mini.html)
- RS-485 to 3.3V logic-level Converter, such as this one: (https://www.aliexpress.com/item/1005010594926432.html)
- Display, such as the SSD1306 OLED 128x64 monochrome display with I2C interface, available from [Adafruit](https://www.adafruit.com/product/938) among others 

I chose an isolated RS-485 to TTL converter, which provides electrical isolation between the two connected devices (Charger and ESP8266).  This reduces the chance of noise interfering with the communications.  One can also, especially for short wire runs, employ a non-isolated RS-485 converter.

I chose an ESP8266 module because they are quite common and inexpensive. For only a slightly higher cost, one can easily port the software to work with an ESP32 module, but you don't really need the added computing power and I/O the ESP32 has, if you are just controlling the charger and displaying its parameters.

## Software
The software is written as a C++ header and source file (`renogy_charger.cpp` and `renogy_charger.h`) that put all the registers I confirmed into a class called `RenogyCharger`
