/**
 * Display and Controller for Renogy 12/12/40 DCDC Charger
 * Uses OLED 128x64 monochrome display with I2C interface.
 * Power plus two pins (SDA, SCL) connect to display.
 * Power plus two pins (Serial In, Serial Out) connect to RS-485 level shifter.
 * Gnd plus two pins (GPInput, AnalogIn) connect to keypad. 
 * 
 * TODO:  
 * try reading multiple registers in one transaction. Would speed up certain operations.
  *
 */
#include <Arduino.h>
#include <stdio.h>
#include <Wire.h>
#include <ModbusRTU.h>
#include <SoftwareSerial.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "renogy_charger.h"

// Defaults and pin assignments for Modbus connection to charger, using Lolin D1-Mini
#define MAX_MODBUS_REG_COUNT    16  //Max Number of 16-bit registers that can be requested in a single read
#define PIN_MODBUS_RX           12  //Modbus Rx, data coming into ESP8266 from RS485 controller 
#define PIN_MODBUS_TX           13  //Modbus Tx, data going out to RS485 controller
#define PIN_MODBUS_TX_ENABLE    -1  //Unneeded for chosen RS485 controller. Can be used if controller needs an RE/DE signal.

// Pin assignments for optional keypad (for changing parameters like max charging current)
#define PIN_KEY_MENU    14  // Digital input pulled to GND when Menu key is pressed. Uses internal pullup.
#define PIN_KEY_MUX     17  // Analog input (A0) is pulled to 3V3 via 4k75. It has two keys attached, one via a 4k75 R. Both keys pull to GND. 


// Create instance of RenogyCharger
RenogyCharger renogyCharger;


// --------------------------------- Keypad ------------------------------------------
enum class MenuMode
{   None = 0,
    ILimit,
    BatteryType,
};
// Variables marked 'volatile' because they are modified inside the ISRs
volatile MenuMode menuMode = MenuMode::None;
// Debounce and timeout variables (in milliseconds)
const uint32_t      debounceDelay = 250;
const uint32_t      menuTimeoutDelay = 4000;
uint32_t            lastKeyRepeatTime = 0;
const uint32_t      keyRepeatInterval = 1000;
volatile uint32_t   lastMenuDebounceTime = 0;

/**
 * @brief Interrupt Service Routine (ISR) for MenuButton
 * Must be marked IRAM_ATTR for ESP8266 compatibility
 * Called when Menu button pushed, which pulls digital input low.
 * Debounce delay prevents re-activation of button within 250ms of first falling edge.
 */
void IRAM_ATTR handleMenuButton() 
{
    uint32_t currentTime = millis();
    if (currentTime - lastMenuDebounceTime > debounceDelay)
    {   //each time Menu button is pushed (after debounce has elapsed), cycle to next Menu mode
        switch( menuMode )
        {   case MenuMode::None:
                menuMode = MenuMode::ILimit;
                break;
            case MenuMode::ILimit:
                menuMode = MenuMode::BatteryType;
                break;
            case MenuMode::BatteryType:
                menuMode = MenuMode::None;
                break;
            default:
                menuMode = MenuMode::None;
                break;
        }
        lastMenuDebounceTime = currentTime;
        Serial.println("Menu pushed.");
    }
}

// --------------------------SSD1306 Display methods and defines ------------------------------------------
#define PIN_I2C_SDA     4
#define PIN_I2C_SCL     5
#define SCREEN_WIDTH    128     // OLED display width, in pixels
#define SCREEN_HEIGHT   64      // OLED display height, in pixels
#define SCREEN_RESET    -1      // no dedicated reset pin
#define SCREEN_ADDRESS  0x3D    ///< See datasheet for Address. Can be changed using jumper: 0x3C / 0x3A

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET);

void SplashScreen(void) {
  display.clearDisplay();

  display.setTextSize(2);             // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 7);
  display.print(F("DingBat")); 
  display.setCursor(15, 25);
  display.print(F("Charger")); 
  display.setTextSize(1);             
  display.setCursor(35, 50);
  display.print(F("v1.0.0")); 

  display.display();
}

#define POS_CHARGE_STATUS_X 0
#define POS_CHARGE_STATUS_Y 0
#define POS_MIDTOP1_X       0
#define POS_MIDTOP2_X       65
#define POS_MIDTOP_Y        20
#define POS_MIDBOT1_X       0
#define POS_MIDBOT2_X       65
#define POS_MIDBOT_Y        39
#define POS_MENU_X          0
#define POS_MENU_Y          48
#define POS_BOTTOM_X        0
#define POS_BOTTOM_Y        56
#define ROLL_TEXT_INTERVAL_MS   2000    //interval between changing the status text field shown
#define ROLL_TEXT_COUNT     4   //total number of status text fields to alternate between

/**
 * @brief Formats and sends Charger's present data to display 
 */
void DisplayReadings()
{   display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    //top section shows charging status
    display.setCursor(POS_CHARGE_STATUS_X, POS_CHARGE_STATUS_Y);
    display.setTextSize(2);
    display.print(renogyCharger.GetChargingStatusAsString());
 
    //middle-top section shows charging current and voltage and session Wh when charging,
    //and supply voltage, temperature, and cumulative Wh when Idle
    if( renogyCharger.GetChargingStatusAsReg() == StatusReg1::OFFLINE )
    {   display.setTextSize(2);
        display.setCursor(POS_MIDTOP1_X, POS_MIDTOP_Y);
        display.print( "----" );
        display.setCursor(POS_MIDTOP2_X, POS_MIDTOP_Y);
        display.print( "----" );
        display.setTextSize(1);
    }else if( renogyCharger.GetChargingStatusAsReg() == StatusReg1::NOT_CHARGING )
    {   display.setTextSize(2);
        display.setCursor(POS_MIDTOP1_X, POS_MIDTOP_Y);
        display.printf( "%0.1f", renogyCharger.GetInputVoltage() );
        display.setCursor(POS_MIDTOP2_X, POS_MIDTOP_Y);
        display.printf( "%0.1f", renogyCharger.GetCurrentLimit() );
        display.setTextSize(1);
        display.setCursor(POS_MIDBOT1_X, POS_MIDBOT_Y);
        display.print( "input V" );
        display.setCursor(POS_MIDBOT2_X, POS_MIDBOT_Y);
        display.print( "I limit" );
    }else   //some form of charging is happening
    {   display.setTextSize(2);
        display.setCursor(POS_MIDTOP1_X, POS_MIDTOP_Y);
        display.printf( "%0.1f", renogyCharger.GetBatteryVoltage() );
        display.setCursor(POS_MIDTOP2_X, POS_MIDTOP_Y);
        display.printf( "%0.1f", renogyCharger.GetChargingCurrent() );
        display.setTextSize(1);
        display.setCursor(POS_MIDBOT1_X, POS_MIDBOT_Y);
        display.print( "battery V" );
        display.setCursor(POS_MIDBOT2_X, POS_MIDBOT_Y);
        display.print( "charging A" );
    }
    //bottom section shows various statuses, rotating
    //if buttons are pressed to change parameters, show the changing parameter
 
    if( menuMode == MenuMode::ILimit )
    {   display.setTextSize(2);
        display.setCursor(POS_MENU_X, POS_MENU_Y);
        display.printf( "maxI:%0.1fA", renogyCharger.GetCurrentLimit() );
    }else if( menuMode == MenuMode::BatteryType )
    {   display.setTextSize(2);
        display.setCursor(POS_MENU_X, POS_MENU_Y);
        display.printf( "Bat:%s", renogyCharger.GetBatteryTypeAsString().substring(0,6).c_str() );
    }else
    {   display.setTextSize(1);
        display.setCursor(POS_BOTTOM_X, POS_BOTTOM_Y);
        uint32_t roll_field = (millis() / ROLL_TEXT_INTERVAL_MS) % ROLL_TEXT_COUNT;
        switch( roll_field )
        {   case 0:
                display.printf( "Temperature %2d C", renogyCharger.GetTemperature());
                break;
            case 1:
                if( millis() < 15000 )  //for first little while, show the model name
                {   display.print( renogyCharger.GetModelNameAsString() );
                }else
                {   if( renogyCharger.IsIGNHigh() )
                    {   display.print( "IGN Input High" );
                    }else
                    {   display.print( "IGN Input Low" );
                    }
                }
                break;
            case 2:
                display.print( "Batt type: " );
                display.print( renogyCharger.GetBatteryTypeAsString() );
                break;
            case 3:
                if( renogyCharger.GetChargingStatusAsReg() == StatusReg1::NOT_CHARGING )  
                {   display.printf( "Total %08d Wh", renogyCharger.GetTotalWattHours() );
                }else
                {   display.printf( "Session %4d Wh", renogyCharger.GetSessionWattHours() );
                }
                //display.print("This space for rent");   //TODO display any error statuses etc
                break;
            default:        //need case entries up to (ROLL_TEXT_COUNT - 1)
                break;
        }
    }
    display.display();

}//end DisplayReadings()

// --------------------------------- Modbus ------------------------------------------

// Create software serial port (no hardware UART avail): RX = D6 (GPIO12), TX = D7 (GPIO13)
SoftwareSerial modbusSerial(PIN_MODBUS_RX, PIN_MODBUS_TX); 

// Create a ModbusRTU client instance
ModbusRTU   modbus;
uint16_t    masterBuffer[MAX_MODBUS_REG_COUNT]; //holds values updated by Modbus library when a query succeeds. The callback's void* points to this array.
bool        isModbusReplyRecd = false;

/**
 * @brief Modbus Register-Read Callback Function
 * This function executes automatically as soon as the server responds or errors/times out.
 * @param data pointer to global data - but can't use as it seems the callback doesn't know contect
 * @param event Result code from the call
 * @param transactionId 
 * 
 */
bool modbusReadCallback(Modbus::ResultCode event, uint16_t transactionId, void *data)
{

    if (event == Modbus::EX_SUCCESS)
    {   renogyCharger.ProcessInfoReceived( true, masterBuffer, 1 );
        //Serial.printf( "Received %d\n", masterBuffer[0]);
    }else if (event == Modbus::EX_TIMEOUT)
    {   Serial.println("No reply from Modbus server.");
        renogyCharger.ProcessInfoReceived( false, NULL, 0 );
    }else
    {   //log the Modbus exception code
        Serial.printf("--> Modbus Error Occurred. Code: 0x%02X\n", event);
        renogyCharger.ProcessInfoReceived( false, NULL, 0 );
    }

//    if( renogyCharger.IsAlertNew() )
//    {   Serial.write( renogyCharger.GetCurrentAlert().data(), renogyCharger.GetCurrentAlert().size() );
//    }

    isModbusReplyRecd = true;
    return true;
}

/**
 * @brief Modbus Register-Write Callback Function
 * This function executes automatically as soon as the server responds or errors/times out.
 * @param data Will be a null pointer after a write
 * @param event Result code from the call
 * @param transactionId 
 * 
 */
static bool modbusWriteCallback(Modbus::ResultCode event, uint16_t transactionId, void *data)
{
    if (event == Modbus::EX_SUCCESS)
    {   // Server echoed the payload successfully, indicating the write took effect.
        //Serial.println("Modbus register write succeeded.");
        renogyCharger.ProcessInfoReceived( true, NULL, 0 );  //record that write succeeded
    }
    else
    { // If the server rejected it, it sends an exception code (e.g., 0x03 Illegal Data Value)
        Serial.printf("Modbus Server rejected write or timed out. Code: 0x%02X\n", event);
        renogyCharger.ProcessInfoReceived( false, NULL, 0 );
    }
    
    isModbusReplyRecd = true;
    return true;
}

// --------------------------------- Setup() -------------------------------------------
void setup() 
{
    delay(2000);           // to give time to open a serial monitor.  Not needed except for debugging
    Serial.begin(115200);  // UART0, connected to USB port as console I/O                     
    modbusSerial.begin(9600, SWSERIAL_8N1); // Initialize software-driven serial port to communicate with external RS-485 IC.                              // to give time to open a serial monitor.  Not needed except for debugging
    Serial.println("Started serial ports.");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);  // SDA -> pin GPIO4; SCL -> GPIO5.  Don't spec speed for ESP8266 Wire() - it wants address.
    Wire.setClock(400000);  //SSD1306 display runs I2C at up to 400kHz
    Serial.println("Started I2C");

    Serial.println("Scanning for I2C devices...");
    byte error, address;
    int nDevices;
    nDevices = 0;
    bool foundDisplay = false;
    for(address = 1; address < 127; address++ ) 
    {   Serial.print(".");
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) 
        {   Serial.print("I2C device found at address 0x");
            if (address<16)
            {   Serial.print("0");
            }
            Serial.println(address,HEX);
            if( address == SCREEN_ADDRESS )
            {   foundDisplay = true;
            }
            nDevices++;
        }
        else if (error==4)
        {   Serial.print("Unknow error at address 0x");
            if (address<16)
            {   Serial.print("0");
            }
            Serial.println(address,HEX);
        }    
    }
    if (nDevices == 0) 
    {   Serial.println("\nNo I2C devices found");
    }else if( foundDisplay )
    {   Serial.println("\nDisplay found at expected address.");
    }else
    {   Serial.println("\nDisplay doesn't seem to be at expected address.");
    }

    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
    }

    Serial.println(F("Display initted"));

    SplashScreen();

    // Attach Modbus to the Serial port
    modbus.begin(&modbusSerial, PIN_MODBUS_TX_ENABLE); // if we pass TX_ENABLE_PIN = -1, then it won't be used.

    // Define this ESP8266's role as the Client
    modbus.master();

    Serial.println("Modbus RTU Client initialized.");

    //configure digital input pin to read keypad. Keypress pulls pin to GND. Other two keys are attached to analog input.
    pinMode(PIN_KEY_MENU, INPUT_PULLUP);    // GPIO14: Safe to use internal pull-up 

    // Attach interrupts to look for signal dropping to GND (FALLING edge)
    attachInterrupt(digitalPinToInterrupt(PIN_KEY_MENU), handleMenuButton, FALLING);
  
    Serial.println("Keypress Interrupt configured.");

    delay(2000);
}//end setup()

// --------------------------------- loop() ------------------------------------------

void loop() 
{
    static uint32_t     lastModbusMsgTime = 0;
    const uint32_t      intervalBetweenUpdates = 500;
    static uint32_t     lastDisplayUpdate = 0;
    static bool         buttonPlus = false;
    static bool         buttonMinus = false;       
    static bool         notOutput = true;
    
    //periodically update display
    if( millis() - lastDisplayUpdate >= intervalBetweenUpdates )
    {   DisplayReadings();
        lastDisplayUpdate = millis();
    }

    if( millis() > 10000 && notOutput )
    {   Serial.println( renogyCharger.GetModelNameAsString() );
        notOutput = false;
    }
    //check whether we are in menu mode, and once menuTimeoutDelay elapses between button pushes, exit the menu
    if( (menuMode != MenuMode::None) && (millis() - lastMenuDebounceTime > menuTimeoutDelay) )
    {   menuMode = MenuMode::None;
        buttonMinus = false;
        buttonPlus = false;
        Serial.println("Menu timeout.");
    }
    //once in Menu mode, keep checking for other buttons
    if( menuMode != MenuMode::None ) 
    {   /** Check for buttons pressed.  Acting on button presses happens later, and only if Modbus is idle.
        * Read raw ADC value (between 0 and 1023) at pin A0. 
        * Two buttons are attached that pull A0 toward GND - one of them via 4k75. A0 itself is pulled to Vcc via 4k75. So A0
        * should read either 0, 512, or 1023 (give/take), corresponding to Button1, Button2, or no buttons. Both buttons give 0.
        */
        int rawValue = analogRead(PIN_KEY_MUX);

        if( rawValue > 750 )
        {   buttonMinus = false;
            buttonPlus = false;
            //Serial.printf( "No button %d\n", rawValue);
        }else if( rawValue > 250 )
        {   buttonMinus = false;
            buttonPlus = true;
            lastMenuDebounceTime = millis();
            //Serial.printf( "Plus button %d\n", rawValue);
        }else
        {   buttonMinus = true;
            buttonPlus = false;
            lastMenuDebounceTime = millis();
            //Serial.printf( "Minus button %d\n", rawValue);
        }
    }

    //at keyRepeatInterval, act on any buttons pushed
    if( ( buttonPlus || buttonMinus ) && (millis() - lastKeyRepeatTime > keyRepeatInterval) )
    {   switch( menuMode )
        {   case MenuMode::ILimit:
            {   float targetCurrentLimit = renogyCharger.GetCurrentLimit();    //retrieve value of current current limit;
                if (buttonPlus)
                {   targetCurrentLimit *= 1.2; // increase current
                }
                else if (buttonMinus)
                {   targetCurrentLimit *= 0.8; // decrease current
                }
                if (targetCurrentLimit > _RENOGY_RBC40D1U_MAX_CURRENT_A)
                {   targetCurrentLimit = _RENOGY_RBC40D1U_MAX_CURRENT_A;
                }
                else if (targetCurrentLimit < 1.5)
                {   targetCurrentLimit = 1.5; // note that charging current regulation becomes unstable at values < 1.5 A, by testing.
                }
                Serial.printf("Changing current to %f\n", targetCurrentLimit);
                renogyCharger.SetCurrentLimit(targetCurrentLimit); // this sets flag to ensure eventual write to charger
                break;
            }
            case MenuMode::BatteryType:
            {   BatteryType targetBatteryType = renogyCharger.GetBatteryType();    //retrieve value of current current limit;
                using BatteryInt = std::underlying_type_t<BatteryType>;
                if (buttonPlus)
                {   targetBatteryType = static_cast<BatteryType>(static_cast<BatteryInt>(targetBatteryType) + 1);
                    if(targetBatteryType == BatteryType::UNDEFINED)
                    {   targetBatteryType = static_cast<BatteryType>(0);    //wrap around to start of list
                    }
                }
                else if (buttonMinus)
                {   if( static_cast<BatteryInt>(targetBatteryType) == 0 )
                    {   targetBatteryType = BatteryType::UNDEFINED; //start over from top of the list
                    }
                    targetBatteryType = static_cast<BatteryType>(static_cast<BatteryInt>(targetBatteryType) - 1);                   
                }
                Serial.printf("Changing battery type to %s\n", renogyCharger.GetBatteryTypeAsString().c_str() );
                renogyCharger.SetBatteryType(targetBatteryType); // this sets flag to ensure eventual write to charger
                break;
            }
            case MenuMode::None:
                break;
        }
        buttonMinus = false; // reset buttons here. They will be reasserted at keyRepeatInterval, if still pressed.
        buttonPlus = false;
        lastKeyRepeatTime = millis();       
    }

    modbus.task();  //perform queued reads/writes

    if( !modbus.slave() )   //if no active Modbus transaction happening
    {   //Check for received message to process
        if( isModbusReplyRecd ) //take post-transaction actions, if any
        {   //callback functions call this method:  renogyCharger.ProcessInfoReceived( true, masterBuffer, 1 );
            isModbusReplyRecd = false;  //reset the flag that the receiving callback sets
        } 
        
        if( (menuMode == MenuMode::None) && !renogyCharger.IsCurrentLimitUploaded() )
        {   //we have exited menu mode, and latest current limit has not been uploaded to charger
            uint8_t         chargerID;
            uint16_t        chargerRegister;
            uint16_t        chargerNumRegs;
            uint16_t        registerValue;
            FunctionCodes   registerOperation;
            if( renogyCharger.ModbusParmsForSpecificInfo( CID::ILIMIT, false, &chargerID, &chargerRegister, &chargerNumRegs, &registerOperation ) )
            {   registerValue = static_cast<uint16_t>(renogyCharger.GetCurrentLimit() * 100);    //register is in units of hundredths of an Amp
                modbus.writeHreg(chargerID, chargerRegister, registerValue, modbusWriteCallback );
            }
            //done adjusting max current. If no parm changes, proceed through state machine
        }else if( (menuMode == MenuMode::None) && !renogyCharger.IsBatteryTypeUploaded() )
        {   //we have exited menu mode, and latest bettery type has not been uploaded to charger
            uint8_t         chargerID;
            uint16_t        chargerRegister;
            uint16_t        chargerNumRegs;
            uint16_t        registerValue;
            FunctionCodes   registerOperation;
            if( renogyCharger.ModbusParmsForSpecificInfo( CID::BATTTYPE, false, &chargerID, &chargerRegister, &chargerNumRegs, &registerOperation ) )
            {   registerValue = static_cast<uint16_t>(renogyCharger.GetBatteryType());
                modbus.writeHreg(chargerID, chargerRegister, registerValue, modbusWriteCallback );
            }
            //done adjusting battery type. If no parm changes, proceed through state machine as follows 
        }else if( (menuMode == MenuMode::None) && (millis() - lastModbusMsgTime > 80) ) //with ~12 states, rotate through them in less than 1 second
        //periodically move on to the next message transaction / state, updating sequentially local data like battery voltage 
        //Skip running through the states whilst in Menu mode, as otherwise the register reads will overwrite potential local changes.      
        {   uint8_t chargerID;
            uint16_t chargerRegister;
            uint16_t chargerNumRegs;
            uint16_t registerValue;
            FunctionCodes registerOperation;
             if( renogyCharger.GetNextModbusQuery( &chargerID, &chargerRegister, &chargerNumRegs, &registerOperation, &registerValue ) )
            {   if( renogyCharger.IsAlertNew() )
                {   Serial.write( renogyCharger.GetCurrentAlert().data(), renogyCharger.GetCurrentAlert().size() );
                }
                if( registerOperation == FunctionCodes::READ_REG )// Function Code 0x03: Read Holding Registers
                {   // Parameters: Slave ID, Start Register, Data Target Buffer, Quantity, Callback Function
                    modbus.readHreg(chargerID, chargerRegister, masterBuffer, chargerNumRegs, modbusReadCallback);
                }else if( registerOperation == FunctionCodes::WRITE_REG ) // Function Code 0x06: Write Holding Register
                {   // Parameters: Slave ID, Start Register, Word to write, Callback Function
                    modbus.writeHreg(chargerID,chargerRegister, registerValue, modbusWriteCallback );
                }
                lastModbusMsgTime = millis();
            }   
        }
    }

    yield(); // Keep any ESP8266 background Wi-Fi/system tasks running

}//end loop()
