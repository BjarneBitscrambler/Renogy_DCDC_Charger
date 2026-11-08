# Renogy_DCDC_Charger

Remote interface to a Renogy DC-DC Charger using its RS-485 Modbus port. Control and monitoring logic is implemented on an ESP8266.

<img width="600" alt="Renogy DC-DC Charger wired to this project's components" src="https://github.com/user-attachments/assets/b7e93e0a-4cb1-4ec1-ad46-3be43f8dc0d2" />


## Background

Some Renogy DC-DC Chargers include a remote **RS-485 interface port**, by which one can control and monitor the charger via *Modbus* data packets. Renogy sells a Bluetooth dongle that plugs into this port, and communicates with an app on a device such as a smart phone.  **This project provides an alternative to the Bluetooth dongle**: using an inexpensive microcontroller such as the ESP8266 and an RS-485 interface module, one can directly control the charger without using Renogy's dongle or their app.

Renogy published in 2025 an API for their Chargers, called [DCC Charger Controller Modbus Protocol v1.0](https://renogy-website.oss-us-east-1.aliyuncs.com/DeveloperPlatform/DCC_Charger_Controller_Modbus_Protocol_V1.0_EN.pdf "Renogy Modbus Protocol v1.0")  This document is fairly general, and not entirely accurate when dealing with specific models or DC-DC charger. By testing the various register settings listed in their API, I have reverse-engineered the actual functions of many of the registers.

## Hardware
- Renogy DC-DC Charger 12V 40A, model RBC40D1U  (I suspect the 20A model would have identical register behaviour)
- ESP8266 module, such as the [WeMos LOLIN D1-mini](https://www.wemos.cc/en/latest/d1/d1_mini.html)
- RS-485 to 3.3V logic-level Converter, such as this one: (https://www.aliexpress.com/item/1005010594926432.html)
- Display (optional), such as the SSD1306 OLED 128x64 monochrome display with I2C interface, available from [Adafruit](https://www.adafruit.com/product/938) among others
- Keypad (optional), for interactively changing charging parameters. Alternatively, this can be done via a serial connection to the ESP8266.

<img width="600" alt="Components of this project" src="https://github.com/user-attachments/assets/e32302dd-e7ae-450c-a8a6-f55769299b30" />

I chose an isolated RS-485 to TTL converter, which provides electrical isolation between the two connected devices (Charger and ESP8266).  This reduces the chance of noise interfering with the communications.  One can also, especially for short wire runs, employ a non-isolated RS-485 converter.

I chose an ESP8266 module because they are quite common and inexpensive. For only a slightly higher cost, one can easily port the software to work with an ESP32 module, but you don't really need the added computing power and I/O the ESP32 has, if you are just controlling the charger and displaying its parameters.

## Software
The software is written as a C++ header and source file (`renogy_charger.cpp` and `renogy_charger.h`) that create a class called `RenogyCharger`.  Methods of reading and writing to all the registers I confirmed are provided, by passing back to a calling routine the contents of the specific Modbus message that accomplishes the desired read or write.  

A `main.cpp` example program demonstrates the use of the RenogyCharger class, by configuring a Modbus client (using an ESP8266 Modbus library) and then communicating with an attached Renogy charger. Visual output is provided on a small OLED display, but is optional if you have some other way of interacting with the program. For example, a serial connection, which in the example is only used for console output, could also be used to control the main program.

The demonstration program is written for the ESP8266, but it can be modified to run on an ESP32 or other microcontrollers. The RenogyCharger class itself would not need altering: the only changes needed would be in adapting to the particular Modbus library used, and in the display, if a different display is used.

Libraries Used:
- [emelianov's Modbus-esp8266](https://github.com/emelianov/modbus-esp8266.git)
- [Adafruit's SSD1306 Display](https://github.com/adafruit/Adafruit_SSD1306.git)
- [Adafruit's GFX Graphics](https://github.com/adafruit/Adafruit_SSD1306.git)

## Features
The sample program successfully selects the "Custom' battery type, sets the various charging voltages and times, changes the maximum charging current, and monitors the charging progress. Registers containing the status of the IGN input on the charger and containing the temperature of the external probe have been tested and are read correctly. 

Note that the Renogy App appears to only allow a limited selection of discrete charging current limits (e.g. 10A, 20A, 30A, 40A), whereas testing demonstrates that the max current register actually accepts intermediate values, giving a very fine control over the max charging current. Testing on the RBC40D1U model (40A max) charger shows that the current limit can be adjusted down to about 1.5A before the internal current-control hardware of the charger becomes unstable. Furthermore, changes written to the register take effect almost immediately, opening the possibility of having real-time control over the charging current based on an outside parameter such as incoming solar current.


