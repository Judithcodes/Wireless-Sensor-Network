// To configure:

static const uint16_t C_MEASUREMENT_EVERY_X_MIN = 5; // Every how many minutes a measurement is taken from the sensor module
static const uint16_t C_SD_XBEE_EVERY_X = 1; // Every how many measurements a XBEE transmit and SD card write is made

static const float LOW_BATTERY_WARNING_LEVEL = 3.4; // V

// Coordinator XBee Address:
#define UPLINK_SH 0x13A200

//#define UPLINK_SL 0x  // C0
//#define UPLINK_SL 0x41046BD5  // C100
//#define UPLINK_SL 0x41628F9C  // C200
//#define UPLINK_SL 0x415ABDD9  // C300
#define UPLINK_SL 0x41046775  // C400
//#define UPLINK_SL 0x41631484  // C500
//#define UPLINK_SL 0x41628FF0  // C600
//#define UPLINK_SL 0x41628FA9  // C700
//#define UPLINK_SL 0x41628FFD  // C800



// Network ID (XBee PAN ID of Corrdinator): unsigned int 32 bit
//static const uint32_t PAN_ID = 0x01;   // C0
//static const uint32_t PAN_ID = 0x100;  // C100
//static const uint32_t PAN_ID = 0x200;  // C200
//static const uint32_t PAN_ID = 0x300;  // C300
static const uint32_t PAN_ID = 0x400;  // C400
//static const uint32_t PAN_ID = 0x500;  // C500
//static const uint32_t PAN_ID = 0x600;  // C600
//static const uint32_t PAN_ID = 0x700;  // C700
//static const uint32_t PAN_ID = 0x800;  // C800

// This communication module's ID. Allowed range: [0,65535] (unsigned 16 bit integer)
static const uint16_t THIS_CM_ID = 417;





/********************************
   END OF CONFIGURATION
 ********************************/

uint16_t wakeup_counter = 0;
uint8_t measurement_counter = 0;

static const uint8_t FRAMESTART_BYTE = 0xAA; // ANYTHING OTHER THAN 0xBB
uint8_t* total_xbee_payload;
uint8_t total_xbee_payload_size;
uint8_t SMtype = 0; // Will be set once the sensor module tells us
uint8_t sizeofSMdata = 0; // Will be set to number of bytes of the SM payload once the sensor module
uint16_t MEASUREMENT_EVERY_X_MIN = 1;
uint16_t SD_XBEE_EVERY_X = 1;


static const uint8_t xbee_header_bytes = 4; // uint16_t NodeID, uint8_t n_measurements, uint8_t SM_type

// Pin numbers
static const uint8_t P_L1 = 14; // LED 1
static const uint8_t P_L2 = 15; // LED 2
static const uint8_t P_L3 = 16;// LED 3
static const uint8_t P_DBG_ENABLE = 17; // Input, Debug Slide Switch
static const uint8_t P_SDA = 18; // SDA pin for I2C
static const uint8_t P_SCL = 19; // SDL pin for I2C
static const uint8_t P_VBAT_MEASURE = 20; // Input, pin for measureing battery voltage over voltage divider
static const uint8_t P_A7 = 21; // Unused
static const uint8_t P_MCU_RXI = 0;
static const uint8_t P_MCU_TXO = 1;
static const uint8_t P_RTC_WAKE = 2; // Interrupt from RTC
static const uint8_t P_SM_WAKE = 3; // Interrupt from interrupting sensor module
static const uint8_t P_SLP_XBEE = 4;  // Output, put XBee module to sleep
static const uint8_t P_SM_VCC_EN = 5; // Output, enable controlled supply voltage to SM
static const uint8_t P_CTS_XBEE = 6; // Input, XBee's clear to send pin
static const uint8_t P_SD_CD = 7; // Input, detect whether SD card is inserted
static const uint8_t P_SD_SS = 8; // Output, slave select pin for SD card SPI
static const uint8_t P_XBEE_RXI = 9; // Serial communication RXI pin for Xbee
static const uint8_t P_XBEE_TXO = 10; // Serial communication TXO pin for Xbee
static const uint8_t P_MOSI = 11; // SPI MOSI pin
static const uint8_t P_MISO = 12; // SPI MISO pin
static const uint8_t P_SCK = 13; // SPI SCK pin


#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/io.h>

#include "Wire.h"
#define DS3231_I2C_ADDRESS 0x68

#include <SD.h>
#include <SPI.h>
File root;
File myFile;

#include <XBee.h>

XBee xbee = XBee();
XBeeAddress64 addr64 = XBeeAddress64(UPLINK_SH, UPLINK_SL); // SH + SL Address of receiving XBee
ZBTxRequest zbTx;
ZBTxStatusResponse txStatus = ZBTxStatusResponse();

#include <SoftwareSerial.h>
SoftwareSerial xbeeSoftSerial(P_XBEE_TXO, P_XBEE_RXI);

volatile bool wakeUpInterrupt_flag_RTC = false;
volatile bool wakeUpInterrupt_flag_SM = false;
volatile bool sd_card_is_initialized = false;
volatile bool ignore_rtc_interrupt = false;
volatile bool ignore_sm_interrupt = false;

bool debug_mode_enabled = false;
bool debug_mode_has_been_switched_off  = false;
bool low_battery_level_detected = false;
bool first_time = true;


void setup()
{
  pinMode(P_SM_VCC_EN, OUTPUT);
  digitalWrite(P_SM_VCC_EN, LOW);

  pinMode(P_L1, OUTPUT);
  digitalWrite(P_L1, LOW);
  pinMode(P_L2, OUTPUT);
  digitalWrite(P_L2, LOW);
  pinMode(P_L3, OUTPUT);
  digitalWrite(P_L3, LOW);
  pinMode(P_DBG_ENABLE, INPUT);
  pinMode(P_SD_CD, INPUT);

  // Initialize RTC 
  Wire.begin();
  setupDS3231(true);
  setDS3231time(01, 49, 23, 7, 01, 01, 10); // DS3231 seconds, minutes, hours, day, date, month, year
  pinMode(P_RTC_WAKE, INPUT_PULLUP); // Pin for Wakeup from RTC

  digitalWrite(P_L1, HIGH); // Indicate RTC initialized ok


  // For sleep mode: Shut down things we do not need:
  ACSR = (1 << ACD); //Disable the analog comparator

  // Sensor Module setup
  setupSerialToSM();
  pinMode(P_SM_WAKE, INPUT_PULLUP);

  // Start XBee
  pinMode(P_CTS_XBEE, INPUT);
  pinMode(P_SLP_XBEE, OUTPUT);
  digitalWrite(P_SLP_XBEE, LOW);

  xbeeSoftSerial.begin(9600);
  xbee.setSerial(xbeeSoftSerial);

  // Wait until XBee is ready after starting up:
  while (digitalRead(P_CTS_XBEE) == 1) {}

  // XBee configuration
  uint8_t SM_value = 0x01;
  setATCommandToValue('S', 'M', &SM_value, sizeof(SM_value));
  sendATCommand('W', 'R');
  sendATCommand('A', 'C');
  // The SM configuration does not work

  // Convert PAN ID from little endian (what arudino uses) to big endian (what XBee wants)
  uint32_t b0, b1, b2, b3;
  b0 = (PAN_ID & 0x000000FF) << 24u;
  b1 = (PAN_ID & 0x0000FF00) << 8u;
  b2 = (PAN_ID & 0x00FF0000) >> 8u;
  b3 = (PAN_ID & 0xFF000000) >> 24u;

  uint32_t big_endian_pan_id = b0 | b1 | b2 | b3;

  setATCommandToValue('I', 'D', (uint8_t*)&big_endian_pan_id, sizeof(big_endian_pan_id));

  sendATCommand('W', 'R');
  sendATCommand('A', 'C');

  // Wait until XBee network joined
  uint8_t AI_value = 0xFF;
  while (AI_value != 0x00)
  {
    // "AI" AT command returns network status. 0x00 means successfully joined.
    AI_value = getSingleByteATCmdValue('A', 'I');

    digitalWrite(P_L2, HIGH); // blinking led indicates trying to join XBee network
    delay(150);
    digitalWrite(P_L2, LOW);
    delay(350);
  }

  digitalWrite(P_L2, HIGH); // Indicate Xbee network joined ok

  // Initialize SD card with slave-select pin
  sdSetup();
  digitalWrite(P_L3, HIGH); // Indicate SD initialized ok


  delay(2000); // Time to let the user see the status LEDs.

  // If debug mode is enable we measure and send more often
  if (digitalRead(P_DBG_ENABLE) == LOW) // Switch on east side = LOW
  {
    debug_mode_enabled = true;
    MEASUREMENT_EVERY_X_MIN = 1;
    SD_XBEE_EVERY_X = 1;
  }
  else
  {
    debug_mode_enabled = false;
    MEASUREMENT_EVERY_X_MIN = C_MEASUREMENT_EVERY_X_MIN;
    SD_XBEE_EVERY_X = C_SD_XBEE_EVERY_X;
  }

  digitalWrite(P_L1, LOW);
  digitalWrite(P_L2, LOW);
  digitalWrite(P_L3, LOW);

  // Make first RTC interruppt occur shortly afterwards seconds
  setDS3231time(59, 49, 23, 7, 01, 01, 10); // DS3231 seconds, minutes, hours, day, date, month, year
  // Clear interrupt flag in RTC IC: Clear A2F and A1F bits by setting them to 0
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x0F); // write to register (0Fh)
  Wire.write(0b10001000);
  Wire.endTransmission();


  wakeUpInterrupt_flag_RTC = false;
  wakeUpInterrupt_flag_SM = false;
}

void loop()
{
  // START EXTENDED SETUP
  // This first_time part is not in the setup() because it somehow did not work then.
  if (first_time == true && wakeUpInterrupt_flag_RTC  == true)
  {
    // The first time the RTC interrupt happens, we are still kind of in the setup phase (not programmed very nicely...)
    // Meaning that only now do we learn whether the sensor module is a periodic one or an interrupting one
    first_time = false;

    digitalWrite(P_L1, HIGH); // Indicate start acquiring sensor measurement

    // Take sensor module measurement
    turnOnSensorModule();
    delay(100); // Give some time for SM to start if it was not turned on
    Serial.write(0xBB); // If it is an interrupting sensor module it will respond to this and if it is a periodic one it will respond with its measurement, which will begin with the FRAMESTART_BYTE

    bool checksum_ok = readSensorModuleData();
    turnOffSensorModule();

    // From ignore_rtc_interrupt we can now tell what kind of sensor module we have
    if (ignore_rtc_interrupt == false)
    {
      // LED 3 indicates periodic sensor module
      digitalWrite(P_L3, HIGH);
      delay(1000);
      digitalWrite(P_L3, LOW);
    }
    else
    {
      // Sensor module is one which will interrupt the communication module
      digitalWrite(P_L2, HIGH);
      delay(1000);
      digitalWrite(P_L2, LOW);
    }
    digitalWrite(P_L1, LOW);

    wakeup_counter = 0;
    measurement_counter = 0;

    // Disable interrupt flag in RTC IC: Clear A2F and A1F bits by setting them to 0
    Wire.beginTransmission(DS3231_I2C_ADDRESS);
    Wire.write(0x0F); // write to register (0Fh)
    Wire.write(0b10001000);
    Wire.endTransmission();

    // Make next RTC interruppt occur in 5 seconds afterwards seconds
    setDS3231time(55, 49, 23, 7, 01, 01, 10); // DS3231 seconds, minutes, hours, day, date, month, year
    // Clear interrupt flag in RTC IC: Clear A2F and A1F bits by setting them to 0
    Wire.beginTransmission(DS3231_I2C_ADDRESS);
    Wire.write(0x0F); // write to register (0Fh)
    Wire.write(0b10001000);
    Wire.endTransmission();


    wakeUpInterrupt_flag_RTC = false;
  }
  // END OF EXTENDED SETUP


  // The usual RTC interrupt when using a periodic sensor module will trigger this:
  if (wakeUpInterrupt_flag_RTC == true && ignore_rtc_interrupt == false)
  {
    if (debug_mode_enabled == true)
    {
      // First check if slide switch still selects debug mode
      if (digitalRead(P_DBG_ENABLE) == HIGH)
      {
        // Debug has been disabled by the user
        debug_mode_enabled = false;
        debug_mode_has_been_switched_off = true;
        MEASUREMENT_EVERY_X_MIN = C_MEASUREMENT_EVERY_X_MIN;
        SD_XBEE_EVERY_X = C_SD_XBEE_EVERY_X;
      }
      else
      {
        digitalWrite(P_L1, HIGH);
      }
    }


    wakeup_counter++;

    // Take measurement if it is time
    if (wakeup_counter >= MEASUREMENT_EVERY_X_MIN)
    {
      // Check battery level
      float currentBatteryVoltage = measureVBat();
      if (low_battery_level_detected == false && currentBatteryVoltage < LOW_BATTERY_WARNING_LEVEL && currentBatteryVoltage > 1) // > 1 check because if it is supplied through the power jack, then VBat is zero. So I use 1V here, because with 1V at VBat the system would not be running.
      {
        digitalWrite(P_SLP_XBEE, LOW); // Wake XBee up
        // Wait until XBee is ready after waking up:
        while (digitalRead(P_CTS_XBEE) == 1) {}
        xbee_transmit_lowbatterywarning(currentBatteryVoltage);
        digitalWrite(P_SLP_XBEE, HIGH); // Xbee sleep
        low_battery_level_detected = true; // So that only the first time will the warning be transmitted
        ignore_rtc_interrupt = true; // Do not wake up ever again to safe the battery
        ignore_sm_interrupt = true;
      }

      // Take sensor module measurement
      turnOnSensorModule();

      bool checksum_ok = readSensorModuleData();
      //      if (checksum_ok)

      turnOffSensorModule();

      if (debug_mode_enabled == true)
      {
        digitalWrite(P_L2, HIGH);
      }

      wakeup_counter = 0;
      measurement_counter++;
    }

    // Transmit and SD write accumulated measurements if it is time
    if (measurement_counter >= SD_XBEE_EVERY_X)
    {
      // Do SD write and XBEE transmit

      digitalWrite(P_SLP_XBEE, LOW); // Wake XBee up
      // Wait until XBee is ready after waking up:
      while (digitalRead(P_CTS_XBEE) == 1) {}

      xbee_transmit_data();

      digitalWrite(P_SLP_XBEE, HIGH); // Xbee sleep


      if (debug_mode_enabled == true)
      {
        digitalWrite(P_L3, HIGH);
      }


      sdWrite();

      measurement_counter = 0;
    }

    if (debug_mode_enabled == true)
    {
      delay(2000);
      digitalWrite(P_L1, LOW);
      digitalWrite(P_L2, LOW);
      digitalWrite(P_L3, LOW);
    }

    if (debug_mode_enabled == true)
    {
      // If we are in debug mode we want an interrupt every 10 seconds instead of every minute.
      setDS3231time(50, 49, 23, 7, 01, 01, 10); // DS3231 seconds, minutes, hours, day, date, month, year
    }

    // Disable interrupt flag in RTC IC: Clear A2F and A1F bits by setting them to 0
    Wire.beginTransmission(DS3231_I2C_ADDRESS);
    Wire.write(0x0F); // write to register (0Fh)
    Wire.write(0b10001000);
    Wire.endTransmission();


    wakeUpInterrupt_flag_RTC = false;
  }


  // The usual interrupt from the sensor module when using an interrupting sensor module will trigger this:
  if (wakeUpInterrupt_flag_SM == true && ignore_sm_interrupt == false)
  {
    // transmit at every event
    SD_XBEE_EVERY_X = 1;
    startSerialToSM();

    // Check battery level
    float currentBatteryVoltage = measureVBat();
    if (low_battery_level_detected == false && currentBatteryVoltage < LOW_BATTERY_WARNING_LEVEL && currentBatteryVoltage > 1)
    {
      digitalWrite(P_SLP_XBEE, LOW); // Wake XBee up
      // Wait until XBee is ready after waking up:
      while (digitalRead(P_CTS_XBEE) == 1) {}
      xbee_transmit_lowbatterywarning(currentBatteryVoltage);
      digitalWrite(P_SLP_XBEE, HIGH); // Xbee sleep
      low_battery_level_detected = true; // So that only the first time will the warning be transmitted
      ignore_rtc_interrupt = true; // Do not wake up ever again to safe the battery
      ignore_sm_interrupt = true;
    }


    if (debug_mode_enabled == true)
    {
      // First check if slide switch still selects debug mode
      if (digitalRead(P_DBG_ENABLE) == HIGH)
      {
        debug_mode_enabled = false;
        debug_mode_has_been_switched_off = true;
      }
      else
      {
        digitalWrite(P_L1, HIGH);
      }
    }

    turnOnSensorModule();
    startSerialToSM();
    readSensorModuleData();
    turnOffSensorModule();   // Addition by Mario to code from Marc
    
    if (debug_mode_enabled == true)
    {
      digitalWrite(P_L2, HIGH);
    }

    digitalWrite(P_SLP_XBEE, LOW); // Wake XBee up
    // Wait until XBee is ready after waking up:
    while (digitalRead(P_CTS_XBEE) == 1) {}

    xbee_transmit_data();
    digitalWrite(P_SLP_XBEE, HIGH); // Xbee sleep

    if (debug_mode_enabled == true)
    {
      digitalWrite(P_L3, HIGH);
    }


    sdWrite();


    if (debug_mode_enabled == true)
    {
      delay(2000);
      digitalWrite(P_L1, LOW);
      digitalWrite(P_L2, LOW);
      digitalWrite(P_L3, LOW);
    }


    wakeUpInterrupt_flag_SM = false;
  }


  goToSleep();
}







/*****************************
   Sensor Module related functions
*/

void turnOnSensorModule()
{
  startSerialToSM();

  delay(3); // Small delay necessary somehow

  // Enable voltage to sensor module
  digitalWrite(P_SM_VCC_EN, HIGH);
}

void turnOffSensorModule()
{
  // Disable voltage to sensor module
  digitalWrite(P_SM_VCC_EN, LOW);

  endSerialToSM();
}

// return: true if checksum ok, false if checksum wrong
bool readSensorModuleData()
{

  uint8_t sumForChecksum = 0x00;


  // Wait for frame start byte
  uint8_t readByte = 0x00;
  while (readByte != FRAMESTART_BYTE)
  {
    while (!Serial.available()) {}
    readByte = Serial.read();

    // The sensor module is an interrupt based one and not one that we use periodically, so return. WWe will note this and not try to ask it with the RTC interrupt anymore.
    if (readByte == 0xBB)
    {
      ignore_rtc_interrupt = true;
      return false;
    }
  }


  sumForChecksum += FRAMESTART_BYTE;

  // Next byte indicates type of sensormodule
  while (!Serial.available()) {}
  uint8_t SMtypebyte = Serial.read();
  sumForChecksum += SMtypebyte;

  // Next byte indicates length of data
  while (!Serial.available()) {}
  uint8_t SMsizebyte = Serial.read();
  sumForChecksum += SMsizebyte;


  // If sensor module type has changed, or if it is the first time we read from sensor module. Or if the debug mode was disabled we need to cahnge the size of the memory for the payload too
  if (SMtypebyte != SMtype || debug_mode_has_been_switched_off == true)
  {
    debug_mode_has_been_switched_off = false;

    if (SMtype != 0 || debug_mode_has_been_switched_off == true)
    {
      // The sensor module was actually changed or debug mode has been switched off
      // Throw out data measured but not yet transmitted of old SM for sake of simplicity
      free(total_xbee_payload);
      wakeup_counter = 0;
      measurement_counter = 0;
    }

    sizeofSMdata = SMsizebyte;
    SMtype = SMtypebyte;

    // Allocate space for accumulating required number of measurements before transmit is done
    total_xbee_payload_size = (xbee_header_bytes + sizeofSMdata * SD_XBEE_EVERY_X);
    total_xbee_payload = (uint8_t*) malloc(total_xbee_payload_size);


    // Fill in the header
    total_xbee_payload[0] = 0x00FF & THIS_CM_ID;
    total_xbee_payload[1] = (0xFF00 & THIS_CM_ID)>>8;  // >>8 is a fix from original code (Mario/Marc)
    total_xbee_payload[2] = SD_XBEE_EVERY_X; // How many measurements are in this packet
    total_xbee_payload[3] = SMtype;
  }


  // Wait until payload arrived plus checksum byte
  while (Serial.available() < sizeofSMdata + 1) {}

  // Read the payload
  for (int k = 0; k < sizeofSMdata; k++)
  {
    uint8_t inByte = (uint8_t) Serial.read();
    sumForChecksum += inByte;
    (total_xbee_payload + xbee_header_bytes + measurement_counter * SMsizebyte)[k] = inByte; // " xbee_header_bytes + (measurement_counter-1)*SMsizebyte" provides offset, for second, third etc measurements
  }


  // Read checksum byte
  uint8_t checksumbyte = Serial.read();


  // Confirm that checksum is ok
  if (uint8_t (checksumbyte + sumForChecksum) == 0xFF)
  {
    return true;
  }
  else
  {
    return false;
  }
}

void setupSerialToSM()
{
  pinMode(P_MCU_TXO, OUTPUT);
  digitalWrite(P_MCU_TXO, LOW);
  pinMode(P_MCU_RXI, OUTPUT);
  digitalWrite(P_MCU_RXI, LOW);
}

void startSerialToSM()
{
  Serial.begin(9600);
}

void endSerialToSM()
{
  // End serial connection to sensor module
  // If Serial.end() is not called, the MCU_TXO pin stays at high voltage level
  Serial.end();

  // Pins low
  pinMode(P_MCU_TXO, OUTPUT);
  digitalWrite(P_MCU_TXO, LOW);
  pinMode(P_MCU_RXI, OUTPUT);
  digitalWrite(P_MCU_RXI, LOW);
}

void serialFlush()
{
  while (Serial.available() > 0) {
    char t = Serial.read();
  }
}

/*****************************
   XBee
*/
void xbee_transmit_data()
{
  zbTx = ZBTxRequest(addr64, total_xbee_payload, total_xbee_payload_size);


  //  uint8_t dummy[3] = {10,5,1};
  //  zbTx = ZBTxRequest(addr64, dummy, 3);

  bool message_acked = false;
  while (message_acked == false)
  {
    message_acked = true; // TODO
    xbee.send(zbTx);

    // flash TX indicator
    //Serial.println("SENT.");
    //digitalWrite(13, HIGH);
    //delay(20);
    //digitalWrite(13, LOW);

    // after sending a tx request, we expect a status response
    // wait up to half second for the status response
    if (xbee.readPacket(500)) {
      // got a response!

      // should be a znet tx status
      if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
        xbee.getResponse().getZBTxStatusResponse(txStatus);

        // get the delivery status, the fifth byte
        if (txStatus.getDeliveryStatus() == SUCCESS) {
          // success.  time to celebrate
          //Serial.println("SUCCESS.");
          //flashLed(statusLed, 5, 50);
          message_acked = true;

        } else {
          // the remote XBee did not receive our packet. is it powered on?
          //Serial.println("FAILURE - RECEIVER DID NOT RECEIVE IT");
          //flashLed(errorLed, 3, 500);
          message_acked = false;

        }
      }
    } else if (xbee.getResponse().isError()) {
      //nss.print("Error reading packet.  Error code: ");
      //nss.println(xbee.getResponse().getErrorCode());
    } else {
      // local XBee did not provide a timely TX Status Response -- should not happen
      //Serial.println("FAILURE - FROM OUR MODULE.");
      //flashLed(errorLed, 2, 50);
    }
  }
}

void xbee_transmit_lowbatterywarning(float currentBatteryVoltage)
{
  struct
  {
    uint32_t marker_this_xbee_payload_is_low_battery_warning;
    uint16_t this_node_id;
    float voltage;
  } lowbatterypayload;

  lowbatterypayload.marker_this_xbee_payload_is_low_battery_warning = 0xFFFFFFFF;
  lowbatterypayload.this_node_id = THIS_CM_ID;
  lowbatterypayload.voltage = currentBatteryVoltage;

  zbTx = ZBTxRequest(addr64, (uint8_t*) &lowbatterypayload, sizeof(lowbatterypayload));

  bool message_acked = false;
  while (message_acked == false)
  {
    message_acked = true; // TODO
    xbee.send(zbTx);

    // flash TX indicator
    //Serial.println("SENT.");
    //digitalWrite(13, HIGH);
    //delay(20);
    //digitalWrite(13, LOW);

    // after sending a tx request, we expect a status response
    // wait up to half second for the status response
    if (xbee.readPacket(500)) {
      // got a response!

      // should be a znet tx status
      if (xbee.getResponse().getApiId() == ZB_TX_STATUS_RESPONSE) {
        xbee.getResponse().getZBTxStatusResponse(txStatus);

        // get the delivery status, the fifth byte
        if (txStatus.getDeliveryStatus() == SUCCESS) {
          // success.  time to celebrate
          //Serial.println("SUCCESS.");
          //flashLed(statusLed, 5, 50);
          message_acked = true;

        } else {
          // the remote XBee did not receive our packet. is it powered on?
          //Serial.println("FAILURE - RECEIVER DID NOT RECEIVE IT");
          //flashLed(errorLed, 3, 500);
          message_acked = false;

        }
      }
    } else if (xbee.getResponse().isError()) {
      //nss.print("Error reading packet.  Error code: ");
      //nss.println(xbee.getResponse().getErrorCode());
    } else {
      // local XBee did not provide a timely TX Status Response -- should not happen
      //Serial.println("FAILURE - FROM OUR MODULE.");
      //flashLed(errorLed, 2, 50);
    }
  }
}

void setATCommandToValue(uint8_t firstChar, uint8_t secondChar, uint8_t* value, uint8_t valueLength)
{
  uint8_t at_command_container[2];
  at_command_container[0] = firstChar;
  at_command_container[1] = secondChar;

  AtCommandRequest atRequest = AtCommandRequest(at_command_container, value, valueLength);
  AtCommandResponse atResponse = AtCommandResponse();

  //Serial.println("Sending command to the XBee");


  // send the command
  xbee.send(atRequest);



  // wait up to xxx milliseconds for the status response
  if (xbee.readPacket(10000)) {
    // got a response!

    // should be an AT command response
    if (xbee.getResponse().getApiId() == AT_COMMAND_RESPONSE) {
      xbee.getResponse().getAtCommandResponse(atResponse);

      //      if (atResponse.isOk()) {
      //        Serial.print("Command [");
      //        Serial.print(atResponse.getCommand()[0]);
      //        Serial.print(atResponse.getCommand()[1]);
      //        Serial.println("] was successful!");
      //
      //        if (atResponse.getValueLength() > 0) {
      //          Serial.print("Command value length is ");
      //          Serial.println(atResponse.getValueLength(), DEC);
      //
      //          Serial.print("Command value: ");
      //
      //          for (int i = 0; i < atResponse.getValueLength(); i++) {
      //            Serial.print(atResponse.getValue()[i], HEX);
      //            Serial.print(" ");
      //          }
      //
      //          Serial.println("");
      //        }
      //      }
      //      else {
      //        Serial.print("Command return error code: ");
      //        Serial.println(atResponse.getStatus(), HEX);
      //      }
      //    } else {
      //      Serial.print("Expected AT response but got ");
      //      Serial.print(xbee.getResponse().getApiId(), HEX);
      //    }
      //  } else {
      //    // at command failed
      //    if (xbee.getResponse().isError()) {
      //      Serial.print("Error reading packet.  Error code: ");
      //      Serial.println(xbee.getResponse().getErrorCode());
      //    }
      //    else {
      //      Serial.print("No response from radio");
      //    }
      //  }
    }
  }
}

// For commands that don't need a value but are executed like AC, FR, etc
void sendATCommand(uint8_t firstChar, uint8_t secondChar)
{
  uint8_t at_command_container[2];
  at_command_container[0] = firstChar;
  at_command_container[1] = secondChar;

  AtCommandRequest atRequest = AtCommandRequest(at_command_container);
  AtCommandResponse atResponse = AtCommandResponse();

  //Serial.println("Sending command to the XBee");

  // send the command
  xbee.send(atRequest);

  // wait up to __ seconds for the status response
  if (xbee.readPacket(10000)) {
    // got a response!

    // should be an AT command response
    if (xbee.getResponse().getApiId() == AT_COMMAND_RESPONSE) {
      xbee.getResponse().getAtCommandResponse(atResponse);

      //      if (atResponse.isOk()) {
      //        Serial.print("Command [");
      //        Serial.print(atResponse.getCommand()[0]);
      //        Serial.print(atResponse.getCommand()[1]);
      //        Serial.println("] was successful!");
      //
      //        if (atResponse.getValueLength() > 0) {
      //          Serial.print("Command value length is ");
      //          Serial.println(atResponse.getValueLength(), DEC);
      //
      //          Serial.print("Command value: ");
      //
      //          for (int i = 0; i < atResponse.getValueLength(); i++) {
      //            Serial.print(atResponse.getValue()[i], HEX);
      //            Serial.print(" ");
      //          }
      //
      //          Serial.println("");
      //        }
      //      }
      //      else {
      //        Serial.print("Command return error code: ");
      //        Serial.println(atResponse.getStatus(), HEX);
      //      }
      //    } else {
      //      Serial.print("Expected AT response but got ");
      //      Serial.print(xbee.getResponse().getApiId(), HEX);
    }
  }
  //  else {
  //    // at command failed
  //    if (xbee.getResponse().isError()) {
  //      Serial.print("Error reading packet.  Error code: ");
  //      Serial.println(xbee.getResponse().getErrorCode());
  //    }
  //    else {
  //      Serial.print("No response from radio");
  //    }
  //  }
}


uint8_t getSingleByteATCmdValue(uint8_t firstChar, uint8_t secondChar)
{
  uint8_t at_command_container[2];
  at_command_container[0] = firstChar;
  at_command_container[1] = secondChar;

  AtCommandRequest atRequest = AtCommandRequest(at_command_container);
  AtCommandResponse atResponse = AtCommandResponse();

  uint8_t responseValue = 0xBA; // Some default value

  //Serial.println("Sending command to the XBee");

  // send the command
  xbee.send(atRequest);

  // wait up to __ milliseconds for the status response
  if (xbee.readPacket(10000)) {
    // got a response!

    // should be an AT command response
    if (xbee.getResponse().getApiId() == AT_COMMAND_RESPONSE) {
      xbee.getResponse().getAtCommandResponse(atResponse);

      if (atResponse.isOk()) {
        if (atResponse.getValueLength() > 0) {
          responseValue = atResponse.getValue()[0];
        }
      }
      //      else {
      //        Serial.print("Command return error code: ");
      //        Serial.println(atResponse.getStatus(), HEX);
      //      }
      //    } else {
      //      Serial.print("Expected AT response but got ");
      //      Serial.print(xbee.getResponse().getApiId(), HEX);
      //    }
      //  } else {
      //    // at command failed
      //    if (xbee.getResponse().isError()) {
      //      Serial.print("Error reading packet.  Error code: ");
      //      Serial.println(xbee.getResponse().getErrorCode());
      //    }
      //    else {
      //      Serial.print("No response from radio");
      //    }
    }
  }
  else
  {
    // No response
    return 0xFF;
  }
  return responseValue;
}

/*****************************
   Functions for SD Card
*/

void sdSetup()
{
  if (digitalRead(P_SD_CD) == LOW)
  {
    // SD card not inserted
    sd_card_is_initialized = false;
    return;
  }

  // Initialize SD card with slave-select pin
  if (!SD.begin(P_SD_SS))
  {
    // SD card is not inserted
    sd_card_is_initialized = false;
  }
  else
  {
    // SD card is inserted
    sdDummyFileWrite();
    sd_card_is_initialized = true;

    // Create the measurement file if it does not exist
    if (!SD.exists("m.bin"))
    {
      myFile = SD.open("m.bin");
      myFile.close();
    }

  }


}

void sdDummyFileWrite()
{
  // Create and close dummy file to prevent high power usage initially
  if (SD.exists("init.xyz"))
  {
    SD.remove("init.xyz");
  }
  // SD.open creates new file if it doesnt exist
  myFile = SD.open("init.xyz", O_CREAT | O_WRITE);
  myFile.close();
}

void sdWrite()
{
  if (digitalRead(P_SD_CD) == LOW)
  {
    // SD Card is not inserted, return from this function to simply skip it
    sd_card_is_initialized = false;
    return;
  }
  else if (sd_card_is_initialized == false)
  {
    // SD card is inserted because P_SD_CD read was high, but card is not initialized yet
    sdSetup();

    if (sd_card_is_initialized == false)
    {
      // If for some reason initialized failed, we dont write this time
      return;
    }
  }

  // open the file and append
  myFile = SD.open("m.bin", O_CREAT | O_APPEND | O_WRITE);

  // if the file opened okay, write to it:
  if (myFile)
  {
    myFile.write(total_xbee_payload, total_xbee_payload_size);
    //myFile.flush();
    myFile.close();
  } else {
    // if the file didn't open, just ignore it...
  }
}



// Commented out, not because it doesnt work but because we currently never read anything from the card
//void sdRead()
//{
//  // re-open the file for reading:
//  myFile = SD.open("test_2.txt");
//  if (myFile) {
//    Serial.println("test_2.txt content:");
//
//    // read from the file until there's nothing else in it:
//    while (myFile.available()) {
//      Serial.write(myFile.read());
//    }
//    // close the file:
//    myFile.close();
//  } else {
//    // if the file didn't open, print an error:
//    Serial.println("error opening test_2.txt");
//  }
//
//  // Somehow if I dont do a dummy filewrite the current consumption staays high afterwards
//  sdDummyFileWrite();
//}


/*****************************
   Measure Battery Voltage in Volts
*/
float measureVBat()
{
  uint16_t sensorValue = analogRead(P_VBAT_MEASURE);
  return ((float)sensorValue * 1000 * 3.3 * 3 / 2 / 1023 / 1000); // ok for 1:2
}


/*****************************
   Functions for sleep mode
*/

// Interrupt from RTC
void pin2_isr(void)
{
  detachInterrupt(0);
  ADCSRA |= (1 << ADEN); //Enable ADC
  wakeUpInterrupt_flag_RTC = true;
}


// Interrupt from sensor module
void pin3_isr(void)
{
  detachInterrupt(1);
  ADCSRA |= (1 << ADEN); //Enable ADC
  wakeUpInterrupt_flag_SM = true;
}


void goToSleep()
{
  cli(); // Disable interrupts to prevent an interrupt from disturbing this sequence
  digitalWrite(P_SLP_XBEE, HIGH); // Set XXBEe to sleep
  ADCSRA &= ~(1 << ADEN); //Disable ADC
  sleep_enable();
  if (ignore_rtc_interrupt == false)
  {
    attachInterrupt(0, pin2_isr, LOW);
  }
  if (ignore_sm_interrupt == false)
  {
    //attachInterrupt(1, pin3_isr, LOW);  // Original line
    attachInterrupt(1, pin3_isr, RISING); // Adjustment in order to make the windows opening sensor node work (Mario/Marc)
  }
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  power_twi_disable(); // Two Wire
  power_spi_disable(); // SPI
  power_usart0_disable();
  power_timer0_disable();
  power_timer1_disable();
  power_timer2_disable();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  /* wake up here */
  sleep_disable();

  power_twi_enable(); // Two Wire
  power_spi_enable(); // SPI
  power_usart0_enable();
  power_timer0_enable();
  power_timer1_enable();
  power_timer2_enable();
}



/*****************************
   Functions for DS3231 Real Time Clock.
*/

// Convert normal decimal numbers to binary coded decimal
byte decToBcd(byte val)
{
  return ( (val / 10 * 16) + (val % 10) );
}

// Convert binary coded decimal to normal decimal numbers
byte bcdToDec(byte val)
{
  return ( (val / 16 * 10) + (val % 16) );
}


// This function enables or disables the two alarm functions
//void setupDS3231(bool useAlarm1, bool useAlarm2)
void setupDS3231(bool useAlarm1)
{
  //  while(Serial.available() == 0){}
  //  // Check content:
  //  byte content;
  //  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  //  Wire.write(0x0E); // set DS3231 register pointer to 00h
  //  Wire.endTransmission();
  //  Wire.requestFrom(DS3231_I2C_ADDRESS, 1);
  //  // request seven bytes of data from DS3231 starting from register 00h
  //  content = Wire.read();
  //  Serial.print("Content before: ");
  //  Serial.print(content, DEC);
  //  Serial.print("\n");

  byte dataToWrite = 0b00011100;
  if (useAlarm1 == true)
  {
    dataToWrite |= (1 << 0);
  }
  //  if (useAlarm2 == true)
  //  {
  //    dataToWrite |= (1 << 1);
  //  }

  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0x0E); // set next input to start at (0Eh) register
  Wire.write(dataToWrite); // set seconds
  Wire.endTransmission();

  // Configure time of alarm
  if (useAlarm1 == true)
  {
    // We use alarm 1 as once per minute
    Wire.beginTransmission(DS3231_I2C_ADDRESS);
    Wire.write(0x07); // set next input to start at (0Eh) register

    Wire.write(1); // A1M1 is 0 and second values is zero
    Wire.write( (1 << 7) ); // A1M2 is 1 and we dont care about minute value we write here since it wont be checked, so we set them to 0
    Wire.write( (1 << 7) ); // A1M3 is 1 and we dont care about hoour value we write here since it wont be checked, so we set them to 0
    Wire.write( (1 << 7) ); // A1M4 is 1 and we dont care about day value we write here since it wont be checked, so we set them to 0

    Wire.endTransmission();
  }
  //  if (useAlarm2 == true)
  //  {
  //    // We use alarm 1 as once per hour
  //  }
}



void setDS3231time(byte second, byte minute, byte hour, byte dayOfWeek, byte dayOfMonth, byte month, byte year)
{
  // sets time and date data to DS3231
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0); // set next input to start at the seconds register
  Wire.write(decToBcd(second)); // set seconds
  Wire.write(decToBcd(minute)); // set minutes
  Wire.write(decToBcd(hour)); // set hours
  Wire.write(decToBcd(dayOfWeek)); // set day of week (1=Sunday, 7=Saturday)
  Wire.write(decToBcd(dayOfMonth)); // set date (1 to 31)
  Wire.write(decToBcd(month)); // set month
  Wire.write(decToBcd(year)); // set year (0 to 99)
  Wire.endTransmission();
}

void readDS3231time(byte *second, byte *minute, byte *hour, byte *dayOfWeek, byte *dayOfMonth, byte *month, byte *year)
{
  Wire.beginTransmission(DS3231_I2C_ADDRESS);
  Wire.write(0); // set DS3231 register pointer to 00h
  Wire.endTransmission();
  Wire.requestFrom(DS3231_I2C_ADDRESS, 7);
  // request seven bytes of data from DS3231 starting from register 00h
  *second = bcdToDec(Wire.read() & 0x7f);
  *minute = bcdToDec(Wire.read());
  *hour = bcdToDec(Wire.read() & 0x3f);
  *dayOfWeek = bcdToDec(Wire.read());
  *dayOfMonth = bcdToDec(Wire.read());
  *month = bcdToDec(Wire.read());
  *year = bcdToDec(Wire.read());
}

//void displayTime()
//{
//  byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
//  // retrieve data from DS3231
//  readDS3231time(&second, &minute, &hour, &dayOfWeek, &dayOfMonth, &month, &year);
//  // send it to the serial monitor
//  Serial.print(hour, DEC);
//  // convert the byte variable to a decimal number when displayed
//  Serial.print(":");
//  if (minute < 10)
//  {
//    Serial.print("0");
//  }
//  Serial.print(minute, DEC);
//  Serial.print(":");
//  if (second < 10)
//  {
//    Serial.print("0");
//  }
//  Serial.print(second, DEC);
//  Serial.print(" ");
//  Serial.print(dayOfMonth, DEC);
//  Serial.print("/");
//  Serial.print(month, DEC);
//  Serial.print("/");
//  Serial.print(year, DEC);
//  Serial.print(" Day of week: ");
//  switch (dayOfWeek) {
//    case 1:
//      Serial.println("Sunday");
//      break;
//    case 2:
//      Serial.println("Monday");
//      break;
//    case 3:
//      Serial.println("Tuesday");
//      break;
//    case 4:
//      Serial.println("Wednesday");
//      break;
//    case 5:
//      Serial.println("Thursday");
//      break;
//    case 6:
//      Serial.println("Friday");
//      break;
//    case 7:
//      Serial.println("Saturday");
//      break;
//  }
//}
