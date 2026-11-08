#include <Arduino.h> 
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_ADS1X15.h>
#include <RunningAverage.h>
#include <EEPROM.h>
#include <EasyButton.h>
#include "DFRobot_MultiGasSensor.h"

// For ESP32-C3 Super Mini
#define WIRE_SDA_PIN 8
#define WIRE_SCL_PIN 9

// Gas constants
#define fO2_21 0.207
#define fO2_O2 0.98

// Calibration cell millivolt limits
#define airCalMvMin 7
#define airCalMvMax 13
#define o2CalMvMin 44

// Calibration variables
#define EEPROM_ADDRESS_MV 0
#define EEPROM_ADDRESS_AIRCAL 1
#define EEPROM_ADDRESS_STATE 2
double calMv = 0;
bool isAirCal;

// Calibration Settings
#define CALIBRATION_BUTTON_PIN 2          
#define CALIBRATION_MESSAGE_DELAY 3000    

// Analogue/Digital Converter
#define ANALYSE_DELAY 100       // Duration to delay in loop
#define ADS_MULTIPLIER 0.0625F  // Multiplier for 16-Bit resolution

// Read count for cell read averaging
#define RA_SIZE 40
RunningAverage RA(RA_SIZE);

Adafruit_ADS1115 ads;
U8G2_SH1107_SEEED_128X128_F_HW_I2C disp(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
DFRobot_GAS_I2C gas(&Wire ,0x74);
EasyButton button(CALIBRATION_BUTTON_PIN);

int i; // Progress counter

struct Result {
  double fO2;
  double mv;
};

// Read ADS value from cell
double read_sensor() {  

  RA.clear();
   
  // Loop for multipe reads
  for(int cx=0; cx<= RA_SIZE; cx++) {
    RA.addValue(ads.readADC_Differential_0_1());
  }

  // Return smoothed average
  return abs(RA.getAverage()) * ADS_MULTIPLIER;
}

// Format centre text
int getCenterX(String s) {
  return (128 - disp.getUTF8Width(s.c_str())) / 2;
}

// Format right text
int getRightX(String s) {
  return 126 - disp.getUTF8Width(s.c_str());
}

// Read cell value and cal fO2
Result analyseO2() {

  Result r;
  
  // Get millivolts
  r.mv = read_sensor();  

  // Determine FO2 based on calibration gas
  r.fO2 = ((isAirCal) ? fO2_21 :  fO2_O2) * (r.mv / calMv);

  /* 
  // Debug code
  Serial.print("mv: ;");
  Serial.print(mv);      
  Serial.print("; FO2: ");
  Serial.print(result);    
  Serial.print(" CalMv: ");
  Serial.println(calMv);  
  */
 
  return r;
}

// Save calbration state to EEPROM
void saveCalibrationState(double calMv, bool isO2) {  

  EEPROM.put(EEPROM_ADDRESS_STATE, 1);
  EEPROM.put(EEPROM_ADDRESS_MV, calMv);
  EEPROM.put(EEPROM_ADDRESS_AIRCAL, isO2);
  EEPROM.commit();
  isAirCal = isO2;
}

// Draw titles that don't refresh 
void drawTitles() {
  
    // Draw display titles 
    disp.clearBuffer();
    disp.setFont(u8g2_font_helvB10_tr );	
    disp.setCursor(0,0); 
    disp.drawStr(0, 12, "O2 %");
    disp.drawStr(0,67 + 14, "CO ppm");
    disp.drawHLine(0, 65, 128);
    disp.sendBuffer();					// transfer internal memory to the display
}

// Calibration function
void calibrate() 
{   
  calMv = read_sensor();
  
  disp.clearBuffer();
  disp.sendBuffer();
  disp.setFont(u8g2_font_helvB10_tr);	
  
  String s = String(calMv);
  s.concat("mv");
  
  // Check for air calibration mv range
  if (calMv > airCalMvMin && calMv < airCalMvMax)
  {
    // Write cal values to EEPROM
    saveCalibrationState(calMv, true);
   
    disp.drawStr(getCenterX("Calibrated AIR"), 30, "Calibrated AIR"); 
    disp.drawStr(getCenterX(s.c_str()), 80, s.c_str()); 
    disp.sendBuffer();
    delay(CALIBRATION_MESSAGE_DELAY);   
    drawTitles(); 
    return;
  }

  // Check for o2 calibration mv range
  if (calMv > o2CalMvMin)
  {
    // Write cal values to EEPROM
    saveCalibrationState(calMv, false);
    
    isAirCal = false;

    disp.drawStr(getCenterX("Calibrated O2"), 30, "Calibrated O2"); 
    disp.drawStr(getCenterX(s.c_str()), 80, s.c_str()); 
    disp.sendBuffer();
    delay(CALIBRATION_MESSAGE_DELAY);    
    drawTitles();

    return;
  }

  Serial.println("Cal Error");  
    
  disp.drawStr(getCenterX("CAL ERROR"), 30, "CAL ERROR"); 
  disp.sendBuffer();

  // Forve infinite loop due to cal error
  while(true) { }
}

// Callback function to be called when the button is pressed.
void onCalbirationPressed() {
  
  Serial.println("onCalbirationPressed()");
  calibrate();
}
 
void setup() {
  
  // Set I2C pins for ESP32
  Wire.begin(WIRE_SDA_PIN, WIRE_SCL_PIN);

  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Serial initiatised...");
  
  // Draw initialisation sequence
  disp.begin(); 
  disp.clearBuffer();
  disp.sendBuffer();
  disp.setFont(u8g2_font_helvB10_tr);	
  disp.drawStr(0, 20, "Initialising:");
  disp.setFont(u8g2_font_helvR08_tr);	
  disp.drawStr(0, 30, "   - Serial Output...OK");
  disp.drawStr(0, 40, "   - Display...OK");
  disp.sendBuffer();
  delay(200);

  // Start CO sensor
  while(!gas.begin())
  {
    Serial.println("NO Deivces !");
    delay(1000);
  }
  
  disp.drawStr(0, 50, "   - CO Cell...OK");
  disp.sendBuffer();
  delay(200);

  gas.changeAcquireMode(gas.PASSIVITY);
  gas.setTempCompensation(gas.OFF); 

  // Start ADC
  ads.setGain(GAIN_TWO); // 16-Bit
  ads.begin(); 
  
  disp.drawStr(0, 60, "   - O2 Cell...OK");
  disp.sendBuffer();
  disp.setFont(u8g2_font_helvB10_tr);	
  disp.drawStr(0, 80, "Setup complete!");
  disp.sendBuffer();
  delay(1000); 
  
  disp.clearBuffer();
  disp.sendBuffer();
  
  // Start EEPROM
  EEPROM.begin(255);

  // Read calibration values from EEPROM if present
  if (EEPROM.read(EEPROM_ADDRESS_STATE) != 255) {
     EEPROM.get(EEPROM_ADDRESS_MV, calMv);
     EEPROM.get(EEPROM_ADDRESS_AIRCAL, isAirCal);
  }
  else {    
    calibrate(); // Should be first flash of board
  }
 
  // Initialize the calibration button pin for input
  pinMode(CALIBRATION_BUTTON_PIN, INPUT_PULLUP);

  // Setup button for long press callback
  button.begin();
  button.onPressed(onCalbirationPressed);
    
  // Draw titles that don't refresh
  drawTitles();
}

// Loop function that analysers and displays to screen
void loop() {

  // Get gas O2 test result
  Result r = analyseO2();
  
  // Get gas CO ppm test result
  int p = gas.readGasConcentrationPPM();
  String ppm(p);

  // Clear old values
  disp.setDrawColor(0);
  disp.drawBox(0, 20, 128, 34);       // O2
  disp.drawBox(58, 0, 128-58, 16);    // O2 mv
  disp.drawBox(0, 84, 128, 40);       // CO ppm
  disp.drawBox(58, 69, 128-58, 16);   // CO Temp

  // Set white display text
  disp.setDrawColor(1);
  
  String o2 = String(r.fO2 * 100, 1);
  disp.setFont(u8g2_font_helvB24_tr );	
  disp.drawStr(getCenterX(o2), 50, o2.c_str());

  String mv = String(r.mv);
  mv.concat("mv");    
  disp.setFont(u8g2_font_helvB10_tr );	
  disp.drawStr(getRightX(mv), 12, mv.c_str());  

  disp.setFont(u8g2_font_helvB24_tr );	
  disp.drawStr(getCenterX(ppm), 116, ppm.c_str());

  disp.setFont(u8g2_font_helvB10_tr );	
  float t = gas.readTempC(); 
  String temp(t);
  temp.concat("C");

  disp.drawStr(getRightX(temp), 67 + 14, temp.c_str());  

  // Draw progress bar
  if (i > 128) {
    i = 0;
    disp.setDrawColor(0);
    disp.drawHLine(0, 127, 128);
  }
  else{
    disp.setDrawColor(1);
    disp.drawHLine(0, 127, i);
  }

  // Output display buffer 
  disp.sendBuffer();	

  // Increment progress bar
  i++;

  // Read button status
  button.read();

  // DEBUG PROGRESS
  Serial.print("."); // Send text to the serial monitor
}