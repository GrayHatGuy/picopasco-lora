/* picopasco 
* -environmental sensor scan of tds, ec, temp, rh, CO2, VOC, soil moisture.
* -automated nutrient mixing based upon user specified recipe and part ratios/concentrations. 
* -relays trigger pump on at differential times to replicate mix recipe. 
* additional libraries required for a successfull build: https://github.com/Seeed-Studio/SGP30_Gas_Sensor https://www.arduinolibraries.info/libraries/sensirion-i2-c-sht4x  *  #include "pins_arduino.h" * mailto://GrayHatGuy@GrayHatGuy.com 
* 
* TODO: #$ ADD PID_BU.ino as pid() use picopasco-main-V4.3597447222071-alpha-hartree-pid-dev (previous build fails loop/led as ref)
*/
#include <Arduino.h>
#include <SensirionI2CSht4x.h>
#include <Wire.h>
#include "pins_arduino.h" //custom header for seeed grove pico breakout
#include <SoftwareSerial.h>
#include "sensirion_common.h"
#include "sgp30.h"

// pid --->
#include <PID_v1.h>

#define PIN_INPUT 0  //set pin input to 
#define RELAY_PIN 6 //update output pins

double Setpoint, Input, Output, Kp=2, Ki=5, Kd=1;
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);
int WindowSize = 5000;
unsigned long windowStartTime = millis();
// pid ----^

/*                             **user inputs**                       */
#define pHPin A0                                     //pH pin added for rawdogRob
#define TdsSensorPin A1                             //TDS sensor pin
#define moisturePin A2                             //moisture sensor pin
#define VREF 5.0                                  //TDS ADC ref. alt VCC 3.3v
#define SCOUNT 30                                //TDS voltage samples - *currently not used* default 30

const int mixIntSP = 5000;                    //while wait auto mix interval [ms]
const int n =4;                               //number of pump relays or mixes
int ledPin = LED_BUILTIN;                    //led pin
int t_loop = 97;                            //while wait interval for loop()
int mixprint = 20;                         //number of status lines to print during mix dwell
int looplast = millis();                  //last loop time stamp millis()
int ledState = LOW;                      //init state of led
int ctl[n] = {6,7,20,21};               //relay GPIO control trigger pins
int trigger = LOW;                     //init state of relay
int tmixSP[n] = {500,1000,500,1000};  //mix time for pump on [ms] 
int mix = 0;                         //0 no mix 1 mix request.
int pumptotal;                      //cumulative pump time since boot
/*                             **user inputs**                                              */

//          ** sensor() globals**
unsigned long previousMillis = millis(); //led do while last time stamp
float mixIntPV = mixIntSP + millis(); //start time for next auto mix
int analogBuffer[SCOUNT], analogBufferTemp[SCOUNT], analogBufferIndex = 0,copyIndex = 0, moistureValue = 0; 
float averageVoltage = 0,tdsValue = 0,temperaturetds = 25, temperature, humidity; 
short pHValue = 0.00;
uint32_t serialNumber; 
uint16_t error; char errorMessage[256]; //error return SHT4x
int errecho = 13; //setup error echo print repeat
int mixcnt = 1;  //mixes since reboot
s16 err = 0;  //error return SPG30
u32 ah = 0; //absolute hydrogen *not used* except in formulae
u16 scaled_ethanol_signal, scaled_h2_signal; //init SGP return
SensirionI2CSht4x sht4x;  //init SHT4x return

void setup() { 
  Serial.begin(115200);  
  Wire.setSDA(PIN_WIRE0_SDA);                 //i2c_0 init
  Wire.setSCL(PIN_WIRE0_SCL); 
  Wire.begin(); 
  for(int i=0; i < n; i++) {                //set state of relay
    pinMode(ctl[i], OUTPUT);  
    digitalWrite(ctl[i], LOW);  
  } 
  sht4x.begin(Wire);                        //SHT4x sensor setup /init
  error = sht4x.serialNumber(serialNumber); 

  while (sgp_probe() != STATUS_OK) {        //SGP30 sensor setup /init
    while (errecho) { 
      Serial.println("SGP30 init failed!"); 
    }
  } 
  err = sgp_measure_signals_blocking_read(&scaled_ethanol_signal,&scaled_h2_signal); 
  if (err == STATUS_OK) { 
    Serial.println("requesting ram signal! - Please hold..."); 
  } else { 
    while (errecho) {
      Serial.println("error reading SGP30 signals "); 
    }
  } 
  sgp_set_absolute_humidity(13000);           //SGP30 calibration
  err = sgp_iaq_init();  
  if (error) {                                //error handle SHT4X init
    while (errecho) {
      Serial.print("SHT4x init failed - Verify I2C0 connection with SGP30/SHT4x sensors to GPIO 8(SDA0) GPIO 9(SCL0) VCC and GND. Error trying to execute serialNumber(): "); 
      errorToString(error, errorMessage, 256); Serial.println(errorMessage); 
    }
 } else { 
    Serial.print("Serial Number: "); 
    Serial.println(serialNumber); 
    Serial.println(); 
 } 
} 

void loop() { 
  while ((millis()-looplast) > t_loop) {      //loop while wait 
    if (millis() > mixIntPV){                 //Set mix flag at interval
      mix = 1; 
      Serial.println(String(millis())+"> SCANNING: trigger mix = 1 - starting mix now!"); 
    } 
              //toggle led
    if (mix == 1){
      stiritup();             //start mix
    }else {
      sensors();                //call sensor scan
      blink(-1);  
    } 
    looplast = millis();
  }
} 

void blink(long led_int) { 
  pinMode(ledPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
  unsigned long currentMillis = millis(); 
  if ((currentMillis - previousMillis >= led_int) and (led_int > 0) ) {   //led_int input in ms -1 toggle rn
    previousMillis = currentMillis; 
    if (ledState == LOW) { 
      ledState = HIGH; 
      } else { 
        ledState = LOW; 
      } 
      digitalWrite(ledPin, ledState); 
    } 
  if (led_int == -1){             //bypass time interval with -1 toggle rn
    if (ledState == LOW) { 
      ledState = HIGH; 
    } else { 
      ledState = LOW; } ; 
    digitalWrite(ledPin, ledState);
  }
}
void sensors() { 
  String printStr = "";  
  error = sht4x.measureHighPrecision(temperature, humidity);        //read sht4x
  printStr = String(millis())+"> SCANNING: t," + temperature + ",rh," + humidity; u16 tvoc_ppb, co2_eq_ppm; 
  err = sgp_measure_iaq_blocking_read(&tvoc_ppb, &co2_eq_ppm);      //read sgp30
  if (err == STATUS_OK) { 
    printStr = printStr + ",voc," + tvoc_ppb + ",co2," + co2_eq_ppm; 
  } 
  averageVoltage =  analogRead(TdsSensorPin) * (float)VREF/ 1024.0; float compensationCoefficient=1.0+0.02*(temperaturetds-25.0); //TDS calculations *add loop count for averaging SCOUNT*
  float compensationVoltage=averageVoltage/compensationCoefficient; 
  tdsValue=(133.42*compensationVoltage*compensationVoltage*compensationVoltage - 255.86*compensationVoltage*compensationVoltage + 857.39*compensationVoltage)*0.5; //TDS and ec formula
  moistureValue = analogRead(moisturePin); //read moisture sensor 
  pHValue = analogRead(pHPin); //read pH  
  printStr = printStr + ",ec," + compensationVoltage + ",tds," + tdsValue + ",h20," + moistureValue +",pH," +(pHValue/100.00); 
  String prntrelay =""; 
  for (int k=0; k<n;k++){ 
    prntrelay = prntrelay + ",S" + (k+1) + "," +String(tmixSP[k]) ; 
  }
  if (mixIntPV > millis()){
      Serial.println(String(printStr+prntrelay+"," + String(mixIntPV - millis()))+","+mix); 
  } else {
    Serial.println(String(printStr+prntrelay)+","+mix); 
  }
} 
  
void stiritup(){ 
    for (int m =0; m<n; m++){ 
    sensors();
    int pumpon = millis() + tmixSP[m];            //capture pump end time
    digitalWrite(ctl[m], HIGH);                   //trigger relay high
    Serial.println (String(millis())+"> MIXING: Pump #"+String(m+1)+" ON! for "+tmixSP[m]+" at "+millis()+" ms ends at "+pumpon+ " ms");
      int cnt = 0;
    while (pumpon >= millis()) {                  //while pumping print countdown to end at fixed interval
      blink(-1);                                  //blink toggle on demand (-1) else (n>=0 is blink interva)
      Serial.println(String(millis())+"> MIXING: Pump #"+String(m+1)+" "+(pumpon - millis())+" ms of pump time remaining " + (mixprint-cnt) + "/" + mixprint ); cnt = cnt +1;
      delay(tmixSP[m]/mixprint);                  //report status of the remaining pump time setting print interval at 1/19 of the dwell time.
    } 
    digitalWrite(ctl[m], LOW);
    pumptotal = pumptotal + tmixSP[m];
    Serial.println(String(millis())+"> MIXING: Pump #" + String(m+1) + " done! - " + millis() +" ms "); 
  } 
  mix = 0; //reset mix for return to scan loop
  mixIntPV =  mixIntSP + millis(); 
  Serial.println(String (millis())+"> MIXING: Completed "+String(mixcnt )+ " mixes with a total pump on time of " + pumptotal+" ms since boot.. reset mix=" + String(mix) + " next mix at " + String(mixIntPV)+" ms"); mixcnt = mixcnt +1;
}

double pid_relay(int sensorPID){
  //if sensorPID case switch
 { 
   //RELAY_PIN = 2 relays for mix; 2 relays for pH;  1 relay for moisture sensor irrigation
   //int PIDsensor 0 tds 1 ec 2 pH 3 soil moisture 
  // add sensorpin TdsSensorPin/pHPin/moisture pin to case
  // add Setpoint to case
  // setup()
  Setpoint = 100; //add as input variable to function() by sensor
  //tell the PID to range between 0 and the full window size
  myPID.SetOutputLimits(0, WindowSize);
  //turn the PID on
  myPID.SetMode(AUTOMATIC);
  // end setup()
}
  Input = analogRead(TdsSensorPin); //variable switch case 
  myPID.Compute();
  /************************************************
   * turn the output pin on/off based on pid output
   ************************************************/
  if (millis() - windowStartTime > WindowSize){ //time to shift the Relay Window
    windowStartTime += WindowSize;
  }
  if (Output < millis() - windowStartTime) {
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PIN, LOW);
  }
  return Output; //if return error could use as turn turn off pid flag at tol
}