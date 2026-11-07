/** Interface file for Renogy DC-DC Charger, model RBC40D1U-CA
 * This charger includes an RS-485 serial connection port with Modbus interface.
 * This software running on a device such as an ESP8266 can 
 * directly interact with the charger over a wired link to the RS-485 port.
 * 
 * by Bjarne Hansen
 *  last edits: 2026-10-31
 */

#include "renogy_charger.h"
#include <sstream>
#include <utility>
#include <vector>
#include <algorithm>

/**
 * Used to retrieve a human-readable interpretation of the bits
 * in Status Registers 2 - 4
 */
struct BitfieldStatus
{   uint8_t     bit;                //bit position, 0 = LSBit
    std::string description;  
};

/**
 * @brief  Fault Status bits found in StatusReg2. A set bit indicates fault.
 * Only bit1 (Low Input Voltage) behaviour has been confirmed.
 */
BitfieldStatus bitFieldStatus2[] =         
{   {   0,  "Reserved" },
    {   1,  "ALT input low voltage protection (set on 11.0V falling, clear on 12.7V rising)" },
    {   2,  "ALT input current limiting protection" },
    {   3,  "Reserved" },
    {   4,  "Controller temperature overheating" },
    {   5,  "Generator charging overcurrent" },
    {   6,  "Reserved" },
    {   7,  "Reserved" },
    {   8,  "Generator overvoltage protection" },
    {   9,  "Starting battery reverse connection" },
    {   10, "BMS overcharge protection" },
    {   11, "Backup battery low temperature protection stop charging (Lithium 0°C,Lead-acid -35°C)" },
    {   12, "Reserved" },
    {   13, "Reserved" },
    {   14, "Reserved" },
    {   15, "Reserved" },
};

/**
 * @brief  Fault Status bits found in StatusReg3. A set bit indicates fault.  From specs; none confirmed.
 */
BitfieldStatus bitFieldStatus3[] =          
{   {   0,  "Backup battery over-discharge" },
    {   1,  "Backup battery overvoltage" },
    {   2,  "Backup battery undervoltage warning" },
    {   3,  "Backup battery reverse connection" },
    {   4,  "Reserved" },
    {   5,  "Controller internal overheating" },
    {   6,  "Backup battery temperature overheating" },
    {   7,  "PV input power too large" },
    {   8,  "Fan alarm - likely unused" },
    {   9,  "PV input terminal overvoltage" },
    {   10, "Load overcurrent protection" },
    {   11, "Load short circuit protection" },
    {   12, "Solar panel reverse connection" },
    {   13, "Reserved" },
    {   14, "Reserved" },
    {   15, "Reserved" },
};

/**
 * @brief  Charging Status bits found in StatusReg4.
 * Only observed behaviour is bit0: clear when charger is idle, and set when charging.
 */
BitfieldStatus bitFieldStatus4[] =           
{   {   0,  "0 = Standby, 1 = Charging" },
    {   1,  "ALT input charging BAT output" },
    {   2,  "Living battery reverse charging starting battery" },
    {   3,  "Solar only forward charging living battery" },
    {   4,  "Solar and starting battery jointly forward charging living battery" },
    {   5,  "Solar reverse charging starting battery" },
    {   6,  "not defined" },
    {   7,  "not defined" },
    {   8,  "not defined" },
    {   9,  "not defined" },
    {   10, "not defined" },
    {   11, "not defined" },
    {   12, "not defined" },
    {   13, "not defined" },
    {   14, "not defined" },
    {   15, "not defined" },
};

const std::vector <BatteryTypeDict> batteryTypeDictionary = 
{   { BatteryType::USER_DEFINED,    "User (Wht)" },
    { BatteryType::FLOODED,         "Flooded (Red)" },
    { BatteryType::AGM,             "AGM (Grn)" },
    { BatteryType::GEL,             "Gel (Grn/Yel)" },
    { BatteryType::LITHIUM,         "Lithium (Blu)" },  
};

const std::vector <UnitsDictionary> units =
{   { Units::AMPERES,   "A" },
    { Units::AMPHOURS,  "Ahr" },
    { Units::DAYS,      "days" },
    { Units::DEGREES_C, "°C" },
    { Units::MINS,      "minutes" },
    { Units::MVC,       "mv/°C" },
    { Units::NONE,      "" },
    { Units::PERCENT,   "%" },
    { Units::SECS,      "s" },
    { Units::VOLTS,     "V" },
    { Units::WATTHOURS, "Whr" },
    { Units::WATTS,     "W" },
};

const std::vector <RenogyChargerDictionary> renogyChargerDictionary = 
{   { CID::SOC,      "Battery State of Charge",      0x0100, CIDType::READ_ONLY, InterpretAs::UINT8,             Units::PERCENT },
    { CID::BATTV,    "Battery Voltage",              0x0101, CIDType::READ_ONLY, InterpretAs::UINT16_TENTHS,     Units::VOLTS },
    { CID::BATTTEMP, "Battery Temperature",          0x0103, CIDType::READ_ONLY, InterpretAs::INT8,             Units::DEGREES_C },
    { CID::INPUTV,   "Input Voltage",                0x0104, CIDType::READ_ONLY, InterpretAs::UINT16_TENTHS,     Units::VOLTS },
    { CID::INPUTI,   "Input Current",                0x0105, CIDType::READ_ONLY, InterpretAs::UINT16_HUNDREDTHS, Units::AMPERES },
    { CID::INPUTP,   "Input Power",                  0x0106, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::WATTS },
    { CID::SOLARP,   "Solar Power",                  0x0109, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::WATTS },  //don't know if input or output
    { CID::MINDAYV,  "Minimum Battery V Today",      0x010b, CIDType::READ_ONLY, InterpretAs::UINT16_TENTHS,     Units::VOLTS },
    { CID::MAXDAYV,  "Maximum Battery V Today",      0x010c, CIDType::READ_ONLY, InterpretAs::UINT16_TENTHS,     Units::VOLTS },
    { CID::MAXDAYI,  "Maximum Charging I Today",     0x010d, CIDType::READ_ONLY, InterpretAs::UINT16_HUNDREDTHS, Units::AMPERES },
    { CID::MAXDAYP,  "Maximum Charging P Today",     0x010f, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::WATTS },
    { CID::MINDAYP,  "Minimum Charging P Today",     0x0110, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::WATTS },
    { CID::DAYAHR,   "Total Charging Ahr Today",     0x0111, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::AMPHOURS },
    { CID::DAYWHR,   "Total Charging Whr Today",     0x0113, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::WATTHOURS },
    { CID::DAYSON,   "Total Days of Operation",      0x0115, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::DAYS },
    { CID::DISCHGCNT,"Over-discharges of Battery",   0x0116, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::NONE },
    { CID::CHGCNT,   "Full charges of Battery",      0x0117, CIDType::READ_ONLY, InterpretAs::UINT16,            Units::NONE },
    { CID::CUMAH_H,  "Cumulative AHr from Charger",  0x0118, CIDType::READ_ONLY, InterpretAs::UINT32_HIGH,       Units::AMPHOURS },
    { CID::CUMAH_L,  "Cumulative AHr from Charger",  0x0119, CIDType::READ_ONLY, InterpretAs::UINT32_LOW,        Units::AMPHOURS },
    { CID::CUMWH_H,  "Cumulative WHr from Charger",  0x011c, CIDType::READ_ONLY, InterpretAs::UINT32_HIGH,       Units::WATTHOURS },
    { CID::CUMWH_L,  "Cumulative WHr from Charger",  0x011d, CIDType::READ_ONLY, InterpretAs::UINT32_LOW,        Units::WATTHOURS },
    { CID::STATUS_1, "Charging Status bitfield 1",   0x0120, CIDType::READ_ONLY, InterpretAs::BITFIELD,          Units::NONE },
    { CID::STATUS_2, "Fault Status bitfield 1",      0x0121, CIDType::READ_ONLY, InterpretAs::BITFIELD,          Units::NONE },
    { CID::STATUS_3, "Fault Status bitfield 2",      0x0122, CIDType::READ_ONLY, InterpretAs::BITFIELD,          Units::NONE },
    { CID::STATUS_4, "Charging Status bitfield 2",   0x0125, CIDType::READ_ONLY, InterpretAs::BITFIELD,          Units::NONE },
    { CID::IGNSTATE, "IGN input: 1 if at 12V",       0x0126, CIDType::READ_ONLY, InterpretAs::FLAG,              Units::NONE },
    { CID::BATTTYPE, "Battery Type (also see LED)",  0xe004, CIDType::READ_WRITE,InterpretAs::LIST,              Units::NONE,    0 },
    { CID::OVT,      "Overvoltage threshold",        0xe005, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 160 }, //read-only unless BattType = 0
    { CID::EQV,      "Equalization voltage",         0xe007, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 144 }, //read-only unless BattType = 0
    { CID::BULKV,    "Bulk Charge voltage",          0xe008, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 144 }, //read-only unless BattType = 0
    { CID::FLOATV,   "Float voltage",                0xe009, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 144 }, //read-only unless BattType = 0
    { CID::BRV,      "Bulk return voltage",          0xe00a, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 130 }, //read-only unless BattType = 0
    { CID::ODRV,     "Overdischarge Return Voltage", 0xe00b, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 120 }, //read-only unless BattType = 0
    { CID::UVWV,     "Undervoltage Warning Voltage", 0xe00c, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 123 }, //read-only unless BattType = 0
    { CID::ODV,      "Overdischarge Voltage",        0xe00d, CIDType::READ_WRITE,InterpretAs::UINT16_TENTHS,     Units::VOLTS, 118 }, //read-only unless BattType = 0
    { CID::ODDLY,    "Overdischarge Limit Delay",    0xe010, CIDType::READ_WRITE,InterpretAs::UINT16,            Units::SECS,    5 }, 
    { CID::EQDUR,    "Equalization Duration",        0xe011, CIDType::READ_WRITE,InterpretAs::UINT16,            Units::MINS,    0 }, //read-only unless BattType = 0
    { CID::BLKDUR,   "Bulk Charge Duration",         0xe012, CIDType::READ_WRITE,InterpretAs::UINT16,            Units::MINS,    0 }, //read-only unless BattType = 0
    { CID::EQINT,    "Equalization Interval",        0xe013, CIDType::READ_WRITE,InterpretAs::UINT16,            Units::DAYS,    0 }, // 0 disables Equalization
    { CID::TCOMP,    "Temperature Comp Coeff",       0xe014, CIDType::READ_WRITE,InterpretAs::UINT16,            Units::MVC,     0  }, 
    { CID::MODEL1,   "Model",                        0x000c, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::MODEL2,   "Model",                        0x000d, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::MODEL3,   "Model",                        0x000e, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::MODEL4,   "Model",                        0x000f, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::MODEL5,   "Model",                        0x0010, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::MODEL6,   "Model",                        0x0011, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::MODEL7,   "Model",                        0x0012, CIDType::READ_ONLY, InterpretAs::CHRx2,             Units::NONE }, 
    { CID::ADDR,     "Modbus device address",        0x001a, CIDType::READ_WRITE,InterpretAs::UINT8,             Units::NONE, _RENOGY_RBC40D1U_DEFAULT_ADDR }, 
    { CID::RATEDV,   "Rated Voltage",                0x000a, CIDType::READ_ONLY, InterpretAs::UINT8_HIGHBYTE,    Units::VOLTS }, 
    { CID::RATEDI,   "Rated Current",                0x000a, CIDType::READ_ONLY, InterpretAs::UINT8,             Units::AMPERES }, 
    { CID::SYSTEMV,  "System Voltage",               0xe003, CIDType::READ_ONLY, InterpretAs::UINT8_HIGHBYTE,    Units::VOLTS }, 
    { CID::DETECTEDV,"Detected Voltage",             0xe003, CIDType::READ_ONLY, InterpretAs::UINT8,             Units::VOLTS }, 
    { CID::ILIMIT,   "Current Limit",                0xe001, CIDType::READ_WRITE,InterpretAs::UINT16_HUNDREDTHS, Units::AMPERES, 1200 }, 

};

/**
 * @brief Constructor
 */
RenogyCharger::RenogyCharger()
{   chargerAddr_ = _RENOGY_RBC40D1U_DEFAULT_ADDR; //start communications with device at address 0x01 (can be changed via Modbus msg)   
    isReplyToBeProcessed_ = false;  
    chargerState_ = ChargerState::OFFLINE;
    nextChargerState_ = ChargerState::OFFLINE;
    revertChargerState_ = ChargerState::OFFLINE;
    timestampLastCommunication_ = 0;
    chargingCurrentLimit_ = FindInDictionary(CID::ILIMIT)->defaultVal / 100.0;
    isChargingCurrentLimitUploaded_ = false;
    chargerBatteryType_ = static_cast<BatteryType>(FindInDictionary(CID::BATTTYPE)->defaultVal); 
    isBatteryTypeUploaded_ = false; 
    mostRecentSentDictIt_ = renogyChargerDictionary.end();
    messagesAndAlertsString_ = "This variable holds messages generated by RenogyCharger, fetchable using GetCurrentAlert() and optional output by main()";
    isAlertNew_ = false;    //indicate that Alert string set above is not worthy of notice (it's just a placeholder)
};

/**
 * @brief assigns the string passed in as the current alert or message
 *  To avoid RenogyCharger library calling Serial.print() or otherwise dictating an output method,
 * assign any alerts/messages to member variable messagesAndAlertsString_. The calling program or main()
 * can fetch the current alert using GetCurrentAlert().  The current alert can be added to using
 * AddToCurrentAlert(). When SetCurrentAlert() is called, any previous alert message is lost and the
 * heap memory used to store the previous message is re-used for the new message. The heap 
 * allocation is re-sized if needed, though apparently is is not usually shrunk.
 * @param alert The string to assign to the current alert variable
 */
void RenogyCharger::SetCurrentAlert( std::string_view alert )
{   messagesAndAlertsString_ = alert;
    isAlertNew_ = true;
}
/**
 * @brief Add a string to the current alert message, without deleting the existing message
 * @param alert The string to be added. Specified as a std::string_view to protect the source against modification
 */
void RenogyCharger::AddToCurrentAlert( std::string_view alert )
{   messagesAndAlertsString_ += alert;
    isAlertNew_ = true;
}
/** @brief Return the current alert message
 * @returns a pointer to a non-modifiable string (std::string_view, as opposed to a std::string)
 */
std::string_view RenogyCharger::GetCurrentAlert( void )
{   AddToCurrentAlert( "\n" );    //tack a linefeed on
    isAlertNew_ = false;    //flag fact that current alert will shortly be seen
    return messagesAndAlertsString_;
}
bool RenogyCharger::IsAlertNew()
{   return isAlertNew_;
}

/**
 *  @brief Returns the contents of the next Modbus query
 * Based on how old information from the charger is, and what state it is in, will select the appropriate parameter
 * to update, generate the Modbus message contents to fetch that data, and return the contents.
 * A separate method checks the returned Modbus data, updates the internal variables and charger state.
 * This method also acts as the charger's state machine, indicating the next state on successful Modbus transaction
 * and the state to revert to if the Modbus transaction fails. 
 * @param chargerID Modbus address of target device
 * @param startReg address of first register to read or write
 * @param numRegs  number of registers to read/write
 * @param function whether to read or write
 * @param value if writing a register, the value to write
 * @returns true if request is provided, else false
 */
bool RenogyCharger::GetNextModbusQuery( uint8_t *chargerID, uint16_t *startReg, uint16_t *numRegs, FunctionCodes *function, uint16_t *value )
{   
    if( millis() - timestampLastCommunication_ >= INTERVAL_BEFORE_LOSS_COMMS_MS )
    {   chargerState_ = ChargerState::OFFLINE;
        SetCurrentAlert( "Modbus Comm timeout.");
        //Serial.print("Modbus Comm timeout.");
    }
    //chargerState_ is normally updated after receiving appropriate responses

    std::vector<RenogyChargerDictionary>::const_iterator it = renogyChargerDictionary.end();
    mostRecentSentDictIt_ = it;

    switch( chargerState_ )
    {   case ChargerState::OFFLINE:
            //attempt to connect and read bus address register
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::ADDR);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::PRESENT;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::OFFLINE;  //move to this state if comms fail
            break;
        case ChargerState::PRESENT:
            //attempt to read model name of charger
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::MODEL1);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  //TODO - can we read multiple registers and get the whole name?
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::VERIFIED1;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::OFFLINE;  //move to this state if comms fail
            break;    
        case ChargerState::VERIFIED1:
            //configure battery type
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BATTTYPE);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;
            *function = FunctionCodes::WRITE_REG;
            *value = static_cast<uint16_t>(chargerBatteryType_);  
            nextChargerState_ = ChargerState::VERIFIED1b;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
        case ChargerState::VERIFIED1b:
            //check battery type
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BATTTYPE);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::VERIFIED2;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
        case ChargerState::VERIFIED2:
            //configure max charging current
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::ILIMIT);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = static_cast<uint16_t>(chargingCurrentLimit_ * 100.0);  //register stores current in hundredths of  1 Amp.
            nextChargerState_ = ChargerState::VERIFIED3;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
        case ChargerState::VERIFIED3:
            //configure Equalization voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::EQV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED4;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED4:
            //configure Bulk Charging voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BULKV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED5;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED5:
            //configure Float voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::FLOATV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED6;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
        case ChargerState::VERIFIED6:
            //configure Bulk Return voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BRV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED7;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
        case ChargerState::VERIFIED7:
            //configure Overdischarge Return voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::ODRV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED8;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED8:
            //configure Undervoltage Warning voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::UVWV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED9;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED9:
            //configure Overdischarge voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::ODV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED10;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED10:
            //configure Overdischarge Limit Delay
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::ODDLY);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED11;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED11:
            //configure Equalization Duration
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::EQDUR);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED12;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED12:
            //configure Bulk Charge Duration
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BLKDUR);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED13;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED13:
            //configure Equalization Interval
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::EQINT);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::VERIFIED14;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
         case ChargerState::VERIFIED14:
            //configure Temperature Compensation Coeff
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::TCOMP);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::WRITE_REG;
            *value = it->defaultVal;
            nextChargerState_ = ChargerState::CONFIGURED1;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;    
        case ChargerState::CONFIGURED1:
            //check status bits to see what state charger is in
            //since there are several registers containing status,cycle between multiple check states
             *chargerID = chargerAddr_;
            it = FindInDictionary(CID::STATUS_1);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED2;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED2:
            //check status bits to see what state charger is in
            //since there are several registers containing status,cycle between multiple check states
             *chargerID = chargerAddr_;
            it = FindInDictionary(CID::STATUS_2);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED3;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED3:
            //check status bits to see what state charger is in
            //since there are several registers containing status,cycle between multiple check states
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::STATUS_3);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED4;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED4:
            //check status bits to see what state charger is in
            //since there are several registers containing status,cycle between multiple check states
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::STATUS_4);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED5;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED5:
            //fetch Battery Voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BATTV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED6;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED6:
            //fetch Input Voltage
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::INPUTV);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED7;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED7:
            //fetch Input Current
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::INPUTI);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED8;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED8:
            //fetch high word of cumulative Wh
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::CUMWH_H);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED9;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED9:
            //fetch low word in cumulative Wh
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::CUMWH_L);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED10;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED10:
            //check Battery Temp
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::BATTTEMP);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED11;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED11:
            //check IGN input
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::IGNSTATE);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED12;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        case ChargerState::CONFIGURED12:
            //check current limit setting
            *chargerID = chargerAddr_;
            it = FindInDictionary(CID::ILIMIT);
            if( it == renogyChargerDictionary.end()) 
            {   return false;   //couldn't find dictionary entry
            }   
            *startReg = it->reg_addr;
            *numRegs = 1;  
            *function = FunctionCodes::READ_REG;
            nextChargerState_ = ChargerState::CONFIGURED1;    //move to this state if comms succeeds
            revertChargerState_ = ChargerState::VERIFIED1;  //move to this state if comms fail
            break;               
        default:    
            return false;
            break;
    }
    mostRecentSentDictIt_ = it;

   return true;
}

std::vector<RenogyChargerDictionary>::const_iterator RenogyCharger::FindInDictionary( CID desiredCID )
{
    //Search for particular CID in the dictionary
    return std::find_if(renogyChargerDictionary.begin(), renogyChargerDictionary.end(), [desiredCID](const RenogyChargerDictionary& dictEntry) 
    {   return dictEntry.cid == desiredCID;
    });
}

/**
 *  @brief Given the CID for a piece of information, provides Modbus parameters to read or write that info. 
 * Does not actually post the message - that is left to the calling function.
 * Keeps chargerState_ unaltered on receipt of Modbus reply.
 * @param isread true to request a read operation; False to request a write.
 * @param function returns the function code to use in the Modbus message. Choices are FunctionCodes::READ_REG, and READ_WRITE
 * @returns true if Modbus parameters are available, else false
 */
bool RenogyCharger::ModbusParmsForSpecificInfo( CID cidMsg, bool isRead, uint8_t *chargerID, uint16_t *startReg, uint16_t *numRegs, FunctionCodes *function )
{
    //look up the relevant message parameters in the dictionary
    auto it = std::find_if(renogyChargerDictionary.begin(), renogyChargerDictionary.end(), [&](const RenogyChargerDictionary& dictEntry) 
    {   return dictEntry.cid == cidMsg;
    });
    if (it == renogyChargerDictionary.end()) 
    {   std::ostringstream msgstream;
        msgstream << "CID " << static_cast<uint8_t>(cidMsg) << " not found in the dictionary.";
        SetCurrentAlert( msgstream.str() );
        //uint8_t badCID = static_cast<uint8_t>(cidMsg);
        //Serial.printf( "CID %d not found in the dictionary",badCID );
        return false;  //didn't find the requested CID 
    }
    //ensure we have a READ or READ/WRITE type CID
    if( ((it->reg_type == CIDType::READ_ONLY) && !isRead) ||  //catch if trying to write to Read-only register
        ((it->reg_type != CIDType::READ_WRITE) && (it->reg_type != CIDType::READ_ONLY)) )// or register is neither R/W nor R
    {   return false;
    }
    //Serial.printf( "Requesting %s", (it->description).c_str() );
    *chargerID = chargerAddr_;
    *startReg = it->reg_addr;
    *numRegs = 1;
    if( isRead )
    {   *function = FunctionCodes::READ_REG;
    }else
    {   *function = FunctionCodes::WRITE_REG;
    }
    nextChargerState_ = chargerState_;    //on reply receipt, don't change state
    mostRecentSentDictIt_ = it;
    return true;
}//end RequestInfo()

/**
 *  @brief Processes a Modbus message reply received from the server.
 * Updates member variables with info received as appropriate.
 * Timeouts are handled at higher level by calling process.
 * In the case of responses to Register Writes, this method is still used
 * but numDataWords = 0, and the data pointer is NULL.  Errors in writes
 * are handled at a higher level (the callback), so when this function is
 * called we already know the write was successful.
 * The chargerState_ state is updated here
 * based on the success / failure of the Modbus message
 * @param success whether the Modbus message (read or write) succeeded
 * @param data pointer to payload: one or more uint16_t words 
 * @param numDataWords number of words received in the ModBus message payload
 * @returns true if reply received and processed, else false
 */
bool RenogyCharger::ProcessInfoReceived( bool success, uint16_t *data, uint16_t numDataWords )
{   
    static bool isFirstTimeReadingWh = true;    //for calculating session Wh

    if( success )
    {   //change to the next state 
        chargerState_ = nextChargerState_;
        timestampLastCommunication_ = millis();
    }else
    {   chargerState_ = revertChargerState_;     //the charger state, if existing Modbus message fails.
        SetCurrentAlert( "Reverting." );
    }

    //look up expected reply parameters
    if (mostRecentSentDictIt_ == renogyChargerDictionary.end()) 
    {   return false;
    }

    if (NULL != data)
    {   switch (mostRecentSentDictIt_->cid)
        {   case CID::STATUS_2: //interpret the bits in this fault register
            case CID::STATUS_3:
            case CID::STATUS_4:
            case CID::IGNSTATE:
            {   //Serial.printf("Reply %04x to %s: \n", data[0], (mostRecentSentDictIt_->description).c_str());
                break;
            }
            default:
                break;
        }
    }else
    {   std::ostringstream msgstream;
        msgstream << "Reply OK to write: " << (mostRecentSentDictIt_->description).c_str();
        SetCurrentAlert( msgstream.str() );
        //Serial.printf("Reply OK to write: %s\n", (mostRecentSentDictIt_->description).c_str());
    }

    switch( mostRecentSentDictIt_->cid )
    {   case CID::BATTV:
        {   if( numDataWords == 1 )
            {   float voltage = (float)data[0] / 10.0;
                batteryVoltage_ = voltage;
            }
            break;
        }
        case CID::BATTTEMP:
        {   if( numDataWords == 1 )
            {   int16_t temperature = (int16_t)(data[0]);   //docs say signed 7-bit value, but we see values like 1219decimal
                temperature_ = temperature;
            }
            break;
        }
        case CID::INPUTV:
        {   if( numDataWords == 1 )
            {   float voltage = (float)data[0] / 10.0;
                inputVoltage_ = voltage;
            }
            break;    
        }
        case CID::INPUTI:
        {   if( numDataWords == 1 )
            {   float current = (float)data[0] / 100.0;
                if( (current >= 0.0) && (current < 100.0) )
                {   chargingCurrent_ = current;
                }
            }
            break;    
        }
        case CID::CUMWH_H:
        {   if( numDataWords == 1 )
            {   //change the high word of accumulated WHr to reflect new value.
                cumulativeWh_ = ((uint32_t)(data[0]) << 16) | (cumulativeWh_ & 0x00001111);
            }
            break;
        }
        case CID::CUMWH_L:
        {   if( numDataWords == 1 )
            {   //change the low word of accumulated WHr to reflect new value.
                cumulativeWh_ = ((uint32_t)(data[0])) | (cumulativeWh_ & 0x11110000);
                //the first time we read the Cumulative Wh low word, store it to later calc session Wh
                if( isFirstTimeReadingWh )
                {   cumulativeWhAtSessionStart_ = cumulativeWh_;
                    isFirstTimeReadingWh = false;
                }
            }
            break;
        }
        case CID::STATUS_1:
        {   if( numDataWords == 1 )
            {   statusRegister1_ = data[0];
            }
            break;
        }
        case CID::STATUS_2:
        {   if( numDataWords == 1 )
            {   statusRegister2_ = data[0];
                if( statusRegister2_ != 0 ) //unless fault condition occurs, expect statusRegister2 to have no set bits
                {   std::ostringstream msgstream;
                    msgstream << "Register2 shows fault: ";
                    for( int i = 0; i < 16; i++ )
                    {   if( (statusRegister2_ & (0x01 << i)) != 0 )
                        {   msgstream << bitFieldStatus2[i].description << " ";   
                        }
                    }
                    SetCurrentAlert( msgstream.str() );                     
                }
            }
            break;
        }
        case CID::STATUS_3:
        {   if( numDataWords == 1 )
            {   statusRegister3_ = data[0];
                if( statusRegister3_ != 0 ) //unless fault condition occurs, expect statusRegister3 to have no set bits
                {   std::ostringstream msgstream;
                    msgstream << "Register3 shows fault: ";
                    for( int i = 0; i < 16; i++ )
                    {   if( (statusRegister3_ & (0x01 << i)) != 0 )
                        {   msgstream << bitFieldStatus3[i].description << " ";   
                        }
                    }
                    SetCurrentAlert( msgstream.str() );                     
                }
            }
            break;
        }
        case CID::STATUS_4:
        {   if( numDataWords == 1 )
            {   statusRegister4_ = data[0];
                if( (statusRegister4_ != 0) && (statusRegister4_ != 1) ) 
                    //unless fault condition occurs, expect statusRegister4 to have no set bits, except bit 0 which indicates charging or not.
                    //Since that is redundant with statusRegister1, ignore bit 0 but report all other set bits.
                {   std::ostringstream msgstream;
                    msgstream << "Register4 shows fault: ";
                    for( int i = 1; i < 16; i++ )
                    {   if( (statusRegister4_ & (0x01 << i)) != 0 )
                        {   msgstream << bitFieldStatus4[i].description << " ";   
                        }
                    }
                    SetCurrentAlert( msgstream.str() );                     
                }
            }
            break;
        }
        case CID::IGNSTATE:
        {   if( numDataWords == 1 )
            {   if( data[0] != 0 )
                {   if( isIGNinputHigh_ != true )
                    {   isIGNinputHigh_ = true;
                        SetCurrentAlert( "IGN input went High." ); 
                    }
                }else
                {   if( isIGNinputHigh_ != false )
                    {   isIGNinputHigh_ = false;
                        SetCurrentAlert( "IGN input went Low." ); 
                    }
                }
            }
            break;
        }
        case CID::BATTTYPE:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                chargerBatteryType_ = static_cast<BatteryType>(data[0]);
                //Serial.printf( "Read battery type. data[0] = %d and cBT_=%d", data[0], static_cast<uint16_t>(chargerBatteryType_) );
            }
            isBatteryTypeUploaded_ = true;  //a read or a write will sync the local variable and the charger register
            break;
        }
        case CID::OVT:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                overvoltageThresholdV_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::EQV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                equalizationVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::BULKV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                bulkChargeVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::FLOATV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                floatChargeVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::BRV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                bulkReturnVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::ODRV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                overdischargeReturnVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::UVWV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                undervoltageWarningVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::ODV:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                overdischargeVoltage_ = (float)data[0] / 10.0;
            }
            break;
        }
        case CID::ODDLY:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                overdischargeDelayS_ = data[0];
            }
            break;
        }
        case CID::EQDUR:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                equalizationDuration_ = data[0];
            }
            break;
        }
        case CID::BLKDUR:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                bulkChargeDuration_ = data[0];
            }
            break;
        }
        case CID::EQINT:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                equalizationInterval_ = data[0];
            }
            break;
        }
        case CID::TCOMP:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                tempCompCoefficient_ = data[0];
            }
            break;
        }
        case CID::MODEL1:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                modelName_.asInt[0] = data[0];
            }
            break;
        }
        case CID::ADDR:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write - no action needed
            }else if( numDataWords == 1 )
            {   //this was a successful read
                chargerAddr_ = (uint8_t)(data[0]);
            }
            break;
        }
        case CID::RATEDV:
        case CID::RATEDI:
        {   if( numDataWords == 1 )
            {   //this was a successful read
                ratedVoltage_ = (data[0] & 0xff00) >> 8;
                ratedCurrent_ = data[0] & 0x00ff;
            }
            break;
        }
        case CID::SYSTEMV:
        case CID::DETECTEDV:
        {   if( numDataWords == 1 )
            {   //this was a successful read
                systemVoltage_ = (data[0] & 0xff00) >> 8;
                detectedVoltage_ = data[0] & 0x00ff;
            }
            break;
        }
        case CID::ILIMIT:
        {   //distinguish between register reads and write by numDataWords in response
            if( numDataWords == 0 )
            {   //this was a write
            }else if( numDataWords == 1 )
            {   //this was a successful read
                chargingCurrentLimit_ = (float)(data[0]) / 100.0;  //register stores current in 1/100 Amp units.              
            }
            isChargingCurrentLimitUploaded_ = true; //a Read or Write synchronizes chargingCurrentLimit_  with the register's stored value
            break;
        }
        default:
            break;
    }

    isReplyToBeProcessed_ = false;
    return true;

}//end IsInfoReceived()

/**
 * following are a bunch of Update____() functions that higher-level program calls. These start a sequence
 * of actions: formulate and send ModBus request for desired parameter; monitor for response; parse
 * the response; update the variable storing the parameter.  A series of Get____() functions allow the
 * higher level program to see the updates parameter values.
 */
/*bool RenogyCharger::UpdateBatteryVoltage()
{   return RequestInfo( CID::BATTV );
}
bool RenogyCharger::UpdateChargingCurrent()
{   return RequestInfo( CID::INPUTI );
}
bool RenogyCharger::UpdateModelNumber()
{   return RequestInfo( CID::MODEL1 );
}
*/

/**
 * Following are Get___() functions to fetch values of parameters received from charger
 * 
 */
float RenogyCharger::GetBatteryVoltage( void )
{   return batteryVoltage_;
};
float RenogyCharger::GetInputVoltage(void)
{   return inputVoltage_;
}
float RenogyCharger::GetChargingCurrent( void )
{   return chargingCurrent_;
}
bool RenogyCharger::IsIGNHigh( void )
{   return isIGNinputHigh_;
}
float RenogyCharger::GetCurrentLimit(void)
{   return chargingCurrentLimit_;
}
void RenogyCharger::SetCurrentLimit(float newLimit)
{   chargingCurrentLimit_ = newLimit;
    isChargingCurrentLimitUploaded_ = false;    //set flag to have new value uploaded to charger
}
bool RenogyCharger::IsCurrentLimitUploaded(void)
{   return isChargingCurrentLimitUploaded_;
}
int8_t RenogyCharger::GetTemperature(void)
{   return temperature_;
}
uint32_t RenogyCharger::GetTotalWattHours(void)
{   return cumulativeWh_;
}
uint16_t RenogyCharger::GetSessionWattHours(void)
{   sessionWh_ = cumulativeWh_ - cumulativeWhAtSessionStart_;
    return sessionWh_;
}
StatusReg1 RenogyCharger::GetChargingStatusAsReg(void)
{   if( chargerState_ == ChargerState::OFFLINE)
    {   return StatusReg1::OFFLINE;
    }
    return static_cast<StatusReg1>(statusRegister1_);
}
String RenogyCharger::GetChargingStatusAsString(void)
{   if( chargerState_ == ChargerState::OFFLINE)
    {   return "Offline";
    }
    switch( statusRegister1_)
    {   case static_cast<uint16_t>(StatusReg1::NOT_CHARGING):
            return "Idle";
            break;
        case static_cast<uint16_t>(StatusReg1::SENSING):
            return "Sensing...";
            break;
        case static_cast<uint16_t>(StatusReg1::BULK_CHARGING):
            return "Bulk Mode";
            break;
        case static_cast<uint16_t>(StatusReg1::CURRENT_LIMITED_CHARGING):
            return "Constant I";
            break;
        case static_cast<uint16_t>(StatusReg1::EQUALIZATION):
            return "Equalize";
            break;
        case static_cast<uint16_t>(StatusReg1::FLOAT_CHARGING):
            return "Floating";
            break;
        case static_cast<uint16_t>(StatusReg1::OFFLINE):    //StatusReg1 would never be read as this value
            return "Offline";
            break;
        default:
            return "Undefined";
            break;
    }
    return "Error in GetC...()";
}//end GetChargingStatusAsString()
void RenogyCharger::SetBatteryType(BatteryType type )
{   chargerBatteryType_ = type;
    isBatteryTypeUploaded_ = false;    //set flag to have new value uploaded to charger
}
bool RenogyCharger::IsBatteryTypeUploaded(void)
{   return isBatteryTypeUploaded_;
}
BatteryType RenogyCharger::GetBatteryType()
{   return chargerBatteryType_;
}
String RenogyCharger::GetBatteryTypeAsString()
{   switch( chargerBatteryType_ )
    {   case BatteryType::USER_DEFINED:
            return "Custom";
            break;
        case BatteryType::LITHIUM:
            return "Lithium";
            break;
        case BatteryType::AGM:
            return "AGM";
            break;
        case BatteryType::FLOODED:
            return "Flooded";
            break;
        case BatteryType::GEL:
            return "Gel";
            break;
        default:
            return "Unknown";
            break;
    }
    return "Error in GetB...()";
}//end GetBatteryTypeAsString()

