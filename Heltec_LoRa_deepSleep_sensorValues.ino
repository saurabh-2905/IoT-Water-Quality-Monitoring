#include "Arduino.h"
#include "LoRaWan_APP.h"

#include "LoRaWan_APP.h"
#include <OneWire.h>
#include <DallasTemperature.h> // DS18B20 library
#include "DFRobot_EC.h"
#include <EEPROM.h>
#include <Arduino.h>

// -- Temperature
#define ONE_WIRE_BUS 7 // data wire is plugged to GPIO7
#define T 273.15               // degrees Kelvin
#define Alpha 0.05916          // alpha
// -- pH Value
#define Offset 0.10 
#define LED 13
// -- TDS Value
#define tdsPin 5
// -- EC
#define EC_PIN 4
// -- DO
#define DO_PIN 3
#define VREF 5000    //VREF (mv)
#define ADC_RES 4096 //ADC Resolution
#define TWO_POINT_CALIBRATION 1
//Single point calibration needs to be filled CAL1_V and CAL1_T
#define CAL1_V (670) //mv
#define CAL1_T (8)   //℃
//Two-point calibration needs to be filled CAL2_V and CAL2_T
//CAL1 High temperature point, CAL2 Low temperature point
#define CAL2_V (1310) //mv
#define CAL2_T (24)   //℃
// -- DEEP SLEEP
#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  30        /* Time ESP32 will go to sleep (in seconds) */

const uint16_t DO_Table[41] = {
    14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
    11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
    9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
    7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410};

uint8_t Temperaturet;
uint16_t ADC_Raw;
uint16_t ADC_Voltage;
uint16_t DO;

int16_t readDO(uint32_t voltage_mv, uint8_t temperature_c)
{
#if TWO_POINT_CALIBRATION == 0
  uint16_t V_saturation = (uint32_t)CAL1_V + (uint32_t)35 * temperature_c - (uint32_t)CAL1_T * 35;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#else
  uint16_t V_saturation = (int16_t)((int8_t)temperature_c - CAL2_T) * ((uint16_t)CAL1_V - CAL2_V) / ((uint8_t)CAL1_T - CAL2_T) + CAL2_V;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#endif
}

/* OTAA para*/
uint8_t devEui[] = {  };
uint8_t appEui[] = {  }; // joinEUI
uint8_t appKey[] = {  };

/* ABP para*/
uint8_t nwkSKey[] = {  };
uint8_t appSKey[] = {  };
uint32_t devAddr =  ( uint32_t );

/*LoraWan channelsmask, default channels 0-7*/ 
uint16_t userChannelsMask[6]={  };

/*LoraWan region, select in arduino IDE tools*/
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/*LoraWan Class, Class A and Class C are supported*/
DeviceClass_t  loraWanClass = CLASS_A;

/*the application data transmission duty cycle.  value in [ms].*/
uint32_t appTxDutyCycle = 60000; // 60,000 = 1 minute; 300,000 = 5 minutes; 1,800,000 = 30 minutes

/*OTAA or ABP*/
bool overTheAirActivation = true;

/*ADR enable*/
bool loraWanAdr = true;

/* Indicates if the node is sending confirmed or unconfirmed messages */
bool isTxConfirmed = true;

/* Application port */
uint8_t appPort = 2;
/*!
* Number of trials to transmit the frame, if the LoRaMAC layer did not
* receive an acknowledgment. The MAC performs a datarate adaptation,
* according to the LoRaWAN Specification V1.0.2, chapter 18.4, according
* to the following table:
*
* Transmission nb | Data Rate
* ----------------|-----------
* 1 (first)       | DR
* 2               | DR
* 3               | max(DR-1,0)
* 4               | max(DR-1,0)
* 5               | max(DR-2,0)
* 6               | max(DR-2,0)
* 7               | max(DR-3,0)
* 8               | max(DR-3,0)
*
* Note, that if NbTrials is set to 1 or 2, the MAC will not decrease
* the datarate, in case the LoRaMAC layer did not receive an acknowledgment
*/
uint8_t confirmedNbTrials = 4;

// -- Temperature
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// -- pH Value
// int pHArray[ArrayLenth];   //Store the average value of the sensor feedback
// int pHArrayIndex=0;
const int phPin=6; // reading the analog value via pin GPIO6
float ph;
float Value=0;
// define params for temperature-based pH correction
float pHvoltage, pHcorr;

// -- TDS Value
int tdsSensorValue = 0;
float tdsValue = 0;
float tdsVoltage = 0;

// -- EC
float ecVoltage,ecValue;
DFRobot_EC ec;

/* Prepares the payload of the frame */
static void prepareTxFrame( uint8_t port )
{
  /*appData size is LORAWAN_APP_DATA_MAX_SIZE which is defined in "commissioning.h".
  *appDataSize max value is LORAWAN_APP_DATA_MAX_SIZE.
  *if enabled AT, don't modify LORAWAN_APP_DATA_MAX_SIZE, it may cause system hanging or failure.
  *if disabled AT, LORAWAN_APP_DATA_MAX_SIZE can be modified, the max value is reference to lorawan region and SF.
  *for example, if use REGION_CN470, 
  *the max value for different DR can be found in MaxPayloadOfDatarateCN470 refer to DataratesCN470 and BandwidthsCN470 in "RegionCN470.h".
  */
  // TEMPERATURE
  sensors.begin(); // start up the sensor library
  sensors.requestTemperatures(); // Send the command to get temperatures
  float tempC = sensors.getTempCByIndex(0); // there's only one temperature sensor so we can index to it

  // PH VALUE
  pinMode(phPin,INPUT); // get the pH output
  static float pHValue,voltage;

  Value = analogRead(phPin);
  voltage = Value * (5.0/6144.0); // 6306.0; 4095.0
  //pHValue = 3.3*voltage+Offset;
  pHValue = 3.5*voltage;
  pHvoltage = voltage / 327.68;
  pHcorr = pHValue - Alpha * (T + tempC) * pHvoltage; 

  // TDS VALUE
  tdsSensorValue = analogRead(tdsPin);
  tdsVoltage = tdsSensorValue*(5.0/4096.0); //Convert analog reading to Voltage
  float compensationCoefficient=1.0+0.02*(tempC-25.0);    //temperature compensation formula: fFinalResult(25^C) = fFinalResult(current)/(1.0+0.02*(fTP-25.0));
  float compVoltage=tdsVoltage/compensationCoefficient;
  tdsValue=(133.42/compVoltage*compVoltage*compVoltage - 255.86*compVoltage*compVoltage + 857.39*compVoltage)*0.5;

  // EC VALUE
  ec.begin();
  ecVoltage = analogRead(EC_PIN)/1024.0*5000;
  ecValue =  ec.readEC(ecVoltage,tempC);
  // Serial.println(ecValue,2);

  // DO VALUE
  Temperaturet = tempC;
  ADC_Raw = analogRead(DO_PIN);
  ADC_Voltage = uint32_t(VREF) * ADC_Raw / ADC_RES;
  int do_raw = readDO(ADC_Voltage, Temperaturet);
  float DO = do_raw / 1000.0; // convert DO unit to mg/L

  // PRINT OUT
  // Serial.print("Temp: ");
  // Serial.print(tempC);
  // Serial.print(" | pH: ");
  // Serial.print(pHcorr);
  // Serial.print(" | TDS: ");
  // Serial.print(tdsValue);
  // Serial.print(" | EC: ");
  // Serial.print(ecValue);
  // Serial.print(" | DO: ");
  // Serial.println(DO);

  // INTEGER CONVERSIONS
  int int_temp = tempC * 100; //remove comma
  int int_ph = pHcorr * 100;
  int int_tds = tdsValue * 100;
  int int_ec = ecValue * 100;
  int int_do = DO * 100;

  // DATA SIZES
  appDataSize = 10;
  appData[0] = int_temp >> 8;
  appData[1] = int_temp;
  appData[2] = int_ph >> 8;
  appData[3] = int_ph;
  appData[4] = int_tds >> 8;
  appData[5] = int_tds;
  appData[6] = int_ec >> 8;
  appData[7] = int_ec;
  appData[8] = int_do >> 8;
  appData[9] = int_do;
}

void lora_init(void)
{
  Mcu.begin();
  static RadioEvents_t RadioEvents;

  RadioEvents.TxDone = NULL;
  RadioEvents.TxTimeout = NULL;
  RadioEvents.RxDone = NULL;

  Radio.Init( &RadioEvents );

}


void VextON(void)
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, LOW);
  
}

void VextOFF(void) //Vext default OFF
{
  pinMode(Vext,OUTPUT);
  digitalWrite(Vext, HIGH);
}
void setup()
{
  Serial.begin(115200);
  Mcu.begin();
  deviceState = DEVICE_STATE_INIT;
  delay(100);
  lora_init();
  delay(1000); 
}


void loop()
{
  VextOFF();
  Radio.Sleep();
  SPI.end();
  pinMode(RADIO_DIO_1,ANALOG);
  pinMode(RADIO_NSS,ANALOG);
  pinMode(RADIO_RESET,ANALOG);
  pinMode(RADIO_BUSY,ANALOG);
  pinMode(LORA_CLK,ANALOG);
  pinMode(LORA_MISO,ANALOG);
  pinMode(LORA_MOSI,ANALOG);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // I assume that here is where I have to put code for something to happen.
  switch( deviceState )
  {
    case DEVICE_STATE_INIT:
    {
#if(LORAWAN_DEVEUI_AUTO)
      LoRaWAN.generateDeveuiByChipID();
#endif
      LoRaWAN.init(loraWanClass,loraWanRegion);
      break;
    }
    case DEVICE_STATE_JOIN:
    {
      LoRaWAN.join();
      break;
    }
    case DEVICE_STATE_SEND:
    {
      prepareTxFrame( appPort );
      LoRaWAN.send();
      deviceState = DEVICE_STATE_CYCLE;
      break;
    }
    case DEVICE_STATE_CYCLE:
    {
      // Schedule next packet transmission
      txDutyCycleTime = appTxDutyCycle + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
      LoRaWAN.cycle(txDutyCycleTime);
      deviceState = DEVICE_STATE_SLEEP;
      break;
    }
    case DEVICE_STATE_SLEEP:
    {
      LoRaWAN.sleep(loraWanClass);
      break;
    }
    default:
    {
      deviceState = DEVICE_STATE_INIT;
      break;
    }
  }

  esp_deep_sleep_start();
}
