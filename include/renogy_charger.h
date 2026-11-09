#ifndef _RENOGY_RBC40D1U_H
#define _RENOGY_RBC40D1U_H

/** Interface file for Renogy DC-DC Charger, model RBC40D1U-CA
 * This charger includes an RS-485 serial connection port with Modbus interface.
 * This software running on a device such as an ESP8266 can 
 * directly interact with the charger over a wired link to the RS-485 port.
 * 
 * ESP8266 pin assignments used for testing and development:
 *   
 * 
 * by Bjarne Hansen
 *  last edits: 2026-10-31
 */

#include <stdint.h>
#include <string>
#include <ModbusRTU.h>
//#include "mbcontroller.h"

#define INTERVAL_BEFORE_LOSS_COMMS_MS   2000    //when this time has elapsed without successful comms from Charger, declare it offline
#define _RENOGY_RBC40D1U_DEFAULT_ADDR   0x01    //starting address on Modbus to look for charger.
#define _RENOGY_RBC40D1U_MAX_CURRENT_A  40.0    //maximum charging current for this model charger

union ModelName
{   char        asChars[16];
    uint16_t    asInt[8];
};

// Enumeration of valid functions for any given register
enum class CIDType
{   READ_ONLY  = 0,
    READ_WRITE,
    WRITE_ONLY,
};

/** 
 * Enums for the bits in Status Register 1. Only the LSByte is shown - the MSByte seems to always read 0. 
 */
enum class StatusReg1
{      //the Renogy specs don't agree with the following, which are actual observed values
    NOT_CHARGING =              0b0000'0000,    //seen when no battery, and after battery connected but its V is above charging threshold
    SENSING =                   0b0000'0010,    //starts about 10s after connecting a battery with V below Bulk Return Voltage. Current ramps up over ~10s
    EQUALIZATION =              0b0000'0011,    //not tested - based solely on specs
    BULK_CHARGING =             0b0000'0100,    //initial charging for period equal to Bulk Charging Duration, at current needed to maintain Bulk Charging Voltage
    FLOAT_CHARGING =            0b0000'0101,    //after Bulk Charging Duration, current changes to that needed to maintain Float Voltage
    CURRENT_LIMITED_CHARGING =  0b0000'0110,    //in any charging mode if target voltage cannot be attained at currents less than this maximum
    OFFLINE =                   0b1111'1111,    //special case used for switch() logic - can't read register when offline
};

// Enumeration of valid function codes
enum class FunctionCodes
{   READ_REG   = 0x03,
    WRITE_REG  = 0x06,
};

/**
 * @brief Register values corresponding to defined battery types.
 * The values are taken from Renogy's specs and are verified. Note
 * that this list needs to end with an Undefined type if one is 
 * iterating through the list as in the example main().  Note also
 * that the list needs to be contiguous ints starting with 0 for the
 * same reason.  If not iterating through the list, then these limitations
 * do not apply.
 */
enum class BatteryType
{   USER_DEFINED = 0,
    FLOODED = 1,
    AGM = 2,
    GEL = 3,
    LITHIUM = 4,
    UNDEFINED = 5, 
};

struct BatteryTypeDict
{   BatteryType     value;     //integer value indicating battery type
    std::string     description;  
};


// Enumeration of how to interpret 2-byte response data 
enum class InterpretAs
{   CHRx2 = 0,           //each byte is an ASCII character, forming a 2-character string          
    FLAG,                //1 if set, 0 if clear
    LIST,                //indicates one of several choices from list
    UINT8,
    UINT8_HIGHBYTE,      //most significant byte only, interpreted as unsigned int
    INTS7,               //least-significant 7 bits represent the value. 8th bit if set indicates negative. Not 2s-complement!
    UINT16,
    UINT16_TENTHS,       //as a 16-bit unsigned int, expressed in 1/10s of the appropriate units
    UINT16_HUNDREDTHS,
    UINT16_THOUSANDTHS,
    UINT32_HIGH,         //high-order 2 bytes of a 4-byte value
    UINT32_LOW,          //low-order 2 bytes of a 4-byte value
    BITFIELD,
};

// Enumeration of units
enum class Units
{   VOLTS = 0,
    AMPERES,
    DEGREES_C,
    WATTS,
    AMPHOURS,    
    WATTHOURS,
    PERCENT,
    MVC,
    SECS,
    MINS,
    DAYS,
    NONE,
};

struct UnitsDictionary
{   Units       unitsId;
    std::string unitString;
};


/**
 * @brief Enumeration of all supported CIDs for Renogy DC-DC Charger 12V 40A model RBC40D1U
 * These were taken from Renogy's "DCC Charger Controller Modbus Protocol V1.0" document, dated 2025.06.04
 * which covers other devices as well. Each of the document's listed PDUs (registers) was tested, and if the
 * result seemed sane, then that register was included in the following list. PDU registers that did not
 * seem to make sense for the RBC40D1U are not included in the following.  No attempt was made to read/write
 * registers *not* listed in the Renogy document.
 * Descriptions of the registers' functions/contents have been modified to reflect actual observed behaviour.
 */
enum class CID 
{   SOC = 0,    //Battery State of Charge, likely based on voltage
    BATTV,      //Voltage at Battery Output Terminals
    BATTTEMP,   //Temperature of remote sensor. Reports 25C if not connected.
    INPUTV,     //Input Volage at ALT terminals
    INPUTI,     //Input Current at ALT terminals
    INPUTP,     //Input Power at ALT terminals
    SOLARP,     //Solar Power - likely not used
    MINDAYV,    //Minimum Voltage during current day
    MAXDAYV,    //Maximum Voltage during current day 
    MAXDAYI,    //Maximum Current during current day
    MAXDAYP,    //Maximum Power during current day
    MINDAYP,    //Minimum Power during current day
    DAYAHR,     //Amp-hours supplied during current day
    DAYWHR,     //Watt-hours supplied during current day
    DAYSON,     //Cumulative Days of operation
    DISCHGCNT,  //Total number of full discharges detected by charger - likely not used.
    CHGCNT,     //Total number of full charges supplied by charger
    CUMAH_H,    //Total Amp-hours output by charger: Two MSBytes of 32-bit integer
    CUMAH_L,    //Total Amp-hours output by charger: Two LSBytes of 32-bit integer
    CUMWH_H,    //Total Watt-hours output by charger: Two MSBytes of 32-bit integer
    CUMWH_L,    //Total Watt-hours output by charger: Two LSBytes of 32-bit integer
    STATUS_1,   //Status register 1 of 4
    STATUS_2,   //Status register 2 of 4
    STATUS_3,   //Status register 3 of 4
    STATUS_4,   //Status register 4 of 4
    IGNSTATE,   //State of the IGM input. Reports 1 when 12V is applied, 0 when no input or grounded.
    BATTTYPE,   //Type of Battery (User-defined, Flooded Lead-acid, Gel, AGM, Lithium)
    //    Most of the configurable voltage values (following) have 1/10 volt resolution and a valid range of [7 - 17] V.
    // Charging voltages are specific to each battery type.
    // Toggling the front-panel battery type selector switch resets voltage values for that type.
    // Values for "User" type carry over from the most-recently-selected battery type (i.e. need to reprogram them after changing types).
    OVT,        // Threshold at which an Overvoltage condition is reported.
    EQV,        //Equalization Voltage
    BULKV,      //Bulk Charging Voltage - the target voltage for initial stage of charging
    FLOATV,     //Float Voltage - after Bulk Charging Duration has elapsed, the target voltage is the Float Voltage
    BRV,        //Falling voltage threshold at which Bulk charging is resumed
    ODRV,       //Rising voltage threshold at which an Overdischarge condition is cleared
    UVWV,       //Minimum settable value = 11.2. Ask for less and get 11.3
    ODV,        //Falling voltage threshold for Overdischarge condition
    ODDLY,      //Delay before an Overdischarge condition is declared
    EQDUR,      //Duration of Equalization charging
    BLKDUR,     //Duration of Bulk charging stage before moving to Float Charging stage
    EQINT,      //Equalization interval. Value of 0 indicates equalization is disabled
    TCOMP,      //Temperature Compensation on voltages, in mV/degreeC. Stored as positive number but charger algorithm treats as negative
    MODEL1,     //First two characters of charger model number. Subsequent 6 registers store remaining characters. 
    MODEL2,     
    MODEL3,     
    MODEL4,     
    MODEL5,     
    MODEL6,     
    MODEL7, 
    ADDR,       //Charger's address on the Modbus. Defaults to 0x01, but can be changed. 
    RATEDV,     //Rated voltage of charger
    RATEDI,     //Rated max current of charger
    SYSTEMV,    //System Voltage (likely based on detected voltage) 
    DETECTEDV,  //Detected voltage
    ILIMIT,     //Current limit settings below about 0.5A are unstable. Values up to device limit (40A) accepted.
};

// Struct containing pertinent details for each valid/useful Modbus register on charger
struct RenogyChargerDictionary
{   CID                 cid;
    std::string         description;
    uint16_t            reg_addr;
    CIDType             reg_type;
    InterpretAs         interpretas;
    Units               units;
    uint16_t            defaultVal; //for Writable registers, provides a suggested value to use
};

/**
 * @brief States used to control the sequence of communications with the charger. 
 * Each corresponds with one particular register read or write. GetNextModbusQuery() implements 
 * the sequencing logic; based on the current state, that method specifies the next state to 
 * enter if the register operation succeeds, as well as the state to revert to if the 
 * communication fails.
 */
enum class ChargerState
{   OFFLINE = 0,    //have not had communications with charger yet, or for long while
    PRESENT,        //successfully communicated with something that responds like charger
    MODEL1,         //Reading the model name, one register at a time (limitation of Modbus lib)
    MODEL2,
    MODEL3,
    MODEL4,
    MODEL5,
    MODEL6,
    VERIFIED1,      //read the charger model
    VERIFIED1b,     //battery type 'Custom' has been written
    VERIFIED2,      //confirmed 'Custom' battery type
    VERIFIED3,      //Charging Current Limit written
    VERIFIED4,      //Equalization Voltage written
    VERIFIED5,      //Bulk Charging Voltage written
    VERIFIED6,      //Float Voltage written
    VERIFIED7,      //Bulk Return Voltage written
    VERIFIED8,      //Overdischarge Return Voltage written
    VERIFIED9,      //Undervoltage Warning Voltage written
    VERIFIED10,     //Overdischarge Voltage written
    VERIFIED11,     //Overdischarge Limit Delay written
    VERIFIED12,     //Equalization Duration written
    VERIFIED13,     //Bulk Charge Duration written
    VERIFIED14,     //Equalization Interval written
    CONFIGURED1,    //Temperature Comp written.  All other charging parameters written.
    CONFIGURED2,    //Status Register 1 read
    CONFIGURED3,    //Status Register 2 read
    CONFIGURED4,    //Status Register 3 read
    CONFIGURED5,    //Status Register 4 read
    CONFIGURED6,    //Battery Voltage read
    CONFIGURED7,    //Input Voltage read
    CONFIGURED8,    //Input Current read
    CONFIGURED9,    //Cumulative Wh high word read
    CONFIGURED10,   //Cumulative Wh low word read
    CONFIGURED11,   //Battery Temp read
    CONFIGURED12,   //IGN Input read. 
    CONFIGURED13,   //Current Limit read.  Battery type to read, and if successful, loop back to CONFIGURED1.
};


class RenogyCharger 
{
    public:
                            RenogyCharger();
        bool                GetNextModbusQuery(uint8_t *chargerID, uint16_t *startReg, uint16_t *numRegs, FunctionCodes *function, uint16_t *value);
        bool                ModbusParmsForSpecificInfo( CID cidMsg, bool isRead, uint8_t *chargerID, uint16_t *startReg, uint16_t *numRegs, FunctionCodes *function );
        bool                ProcessInfoReceived( bool success, uint16_t *data, uint16_t numDataWords );
        bool                IsIGNHigh(void);
        float               GetBatteryVoltage(void);
        float               GetInputVoltage(void);
        float               GetChargingCurrent(void);
        float               GetCurrentLimit(void);
        void                SetCurrentLimit(float newLimit);
        bool                IsCurrentLimitUploaded(void);
        int8_t              GetTemperature(void);
        uint32_t            GetTotalWattHours(void);
        uint16_t            GetSessionWattHours(void);
        StatusReg1          GetChargingStatusAsReg(void);
        String              GetChargingStatusAsString(void);
        String              GetModelNameAsString(void);
        BatteryType         GetBatteryType(void);
        void                SetBatteryType(BatteryType type );
        bool                IsBatteryTypeUploaded(void);
        String              GetBatteryTypeAsString(void);
        std::string_view    GetCurrentAlert(void);
        void                SetCurrentAlert(std::string_view alert);
        void                AddToCurrentAlert(std::string_view alert);
        bool                IsAlertNew(void);

    private:
        std::vector<RenogyChargerDictionary>::const_iterator FindInDictionary( CID desiredCID );

        std::vector<RenogyChargerDictionary>::const_iterator              mostRecentSentDictIt_;
        std::string     messagesAndAlertsString_;
        bool            isAlertNew_;
        uint32_t        timestampLastCommunication_;
        bool            isReplyToBeProcessed_;  //is Modbus message currently awaiting processing
        uint8_t         chargerAddr_;
        BatteryType     chargerBatteryType_;
        bool            isBatteryTypeUploaded_;
        float           batteryVoltage_;
        float           chargingCurrent_;
        float           chargingCurrentLimit_;
        bool            isChargingCurrentLimitUploaded_;
        float           inputVoltage_;
        int16_t         temperature_;
        uint32_t        cumulativeWh_;
        uint32_t        cumulativeWhAtSessionStart_;
        uint16_t        sessionWh_;
        uint16_t        statusRegister1_;
        uint16_t        statusRegister2_;
        uint16_t        statusRegister3_;
        uint16_t        statusRegister4_;
        bool            isIGNinputHigh_;
        float           overvoltageThresholdV_;
        float           equalizationVoltage_;
        float           bulkChargeVoltage_;
        float           floatChargeVoltage_;
        float           bulkReturnVoltage_;
        float           overdischargeReturnVoltage_;
        float           undervoltageWarningVoltage_;
        float           overdischargeVoltage_;
        uint16_t        overdischargeDelayS_;
        uint16_t        equalizationDuration_;
        uint16_t        bulkChargeDuration_;
        uint16_t        equalizationInterval_;
        uint16_t        tempCompCoefficient_;
        uint8_t         ratedVoltage_;
        uint8_t         ratedCurrent_;
        uint8_t         systemVoltage_;
        uint8_t         detectedVoltage_;
        ModelName       modelName_;
        ChargerState    chargerState_;
        ChargerState    nextChargerState_;     //next charger state if existing Modbus message succeeds.
        ChargerState    revertChargerState_;   //charger state to revert to if existing Modbus message fails.

};
#endif //_RENOGY_RBC40D1U_H 

