#include <OneWire.h> // used by temperature sensors
#include <DallasTemperature.h> // used by temperature sensors
#include <Wire.h> // used by lps pressure and lsm303 sensors
#include <SoftwareSerial.h> // used by display
#include <LPS_Pressure.h>
#include <LSM303_Magnet.h>

// in milliseconds
#define TEMP_REFRESH_INTERVAL       3000ul
#define COMPASS_REFRESH_INTERVAL     600ul
#define ALTIMETER_REFRESH_INTERVAL  5000ul
#define ANIMATION_REFRESH_INTERVAL   500ul
#define LOOP_WAIT_INTERVAL            50ul

// temperature sensors OneWire signal pin
#define TEMP_SENSORS_PIN 12

// temperature sensors resolution (9 to 12 inclusive)
#define TEMP_RESOLUTION 9

// screen TX and RX pins (TX pin is needed and yet not used)
#define SCREEN_TX_PIN 11
#define SCREEN_RX_PIN 10
#define SCREEN_BAUDRATE 9600
#define SCREEN_BRIGHTNESS 140

// AltIMU sensor timeouts
#define PRESSURE_READ_TIMEOUT         200
#define MAGNETIC_SENSOR_READ_TIMEOUT  200

// will turn on led on pin 13 while reading sensors
#define LOOP_DELAY_LED_PIN 13

// temperature chip i/o
OneWire oneWire(TEMP_SENSORS_PIN);
DallasTemperature tempSensors(&oneWire);

//DS18S20 hardware addresses
DeviceAddress InTemp =  { 0x28, 0x4E, 0x76, 0x74, 0x06, 0x00, 0x00, 0xFE };
DeviceAddress OutTemp = { 0x28, 0xFF, 0x7E, 0x74, 0x06, 0x00, 0x00, 0xF0 };

// screen dim button pin
//const int ScreenDimBtnPin = 7;

// screen dim values
//const int ScreenDimOn = 140;
//const int ScreenDimOff = 150;

// AltIMU specific settings
LSM303::vector<int16_t> CompassMin = LSM303::vector<int16_t> {  +846,   +400,  -2451};
LSM303::vector<int16_t> CompassMax = LSM303::vector<int16_t> {  +869,   +423,  -2409};
// LSM303::vector<int16_t> CompassMin = (LSM303::vector<int16_t>){-32767, -32767, -32767};
// LSM303::vector<int16_t> CompassMax = (LSM303::vector<int16_t>){+32767, +32767, +32767};

// AltIMU sensors
LPS pressureSensor = LPS();
LSM303 magneticSensor = LSM303();

// AltIMU sensors detection
boolean pressureSensorDetected = false;
boolean magneticSensorDetected = false;

// last screen symbol on the second line
char animationChars[] = "^<^>";
int loadingIndex = 0;

// arrow showed on no AltIMU response
char arrow[] = "->";

// screen output
SoftwareSerial screenSerial(SCREEN_TX_PIN, SCREEN_RX_PIN);

// async update milliseconds
unsigned long lastTemperatureMillis = millis();
unsigned long lastCompassMillis = millis();
unsigned long lastAltimeterMillis = millis();
unsigned long lastAnimationMillis = millis();

void setup(void) {
  // for debugging purposes
//  Serial.begin(9600);

  // setting up screen
  screenSerial.begin(SCREEN_BAUDRATE);
  clearScreen();
  changeScreenBrightness(SCREEN_BRIGHTNESS);

  // setting up Wire instance for pressure and magnetic
  // sensor
  Wire.begin();

  setupTemperatureSensors();
  setupAltIMUSensors();

  pinMode(LOOP_DELAY_LED_PIN, OUTPUT);
  digitalWrite(LOOP_DELAY_LED_PIN, LOW);
}

void setupTemperatureSensors() {
  // setting up temperature sensors
  tempSensors.begin();

  // setting the resolution of the temperature sensors
  tempSensors.setResolution(InTemp, TEMP_RESOLUTION);
  tempSensors.setResolution(OutTemp, TEMP_RESOLUTION);
}

void setupAltIMUSensors() {
  // setting up atmospheric pressure sensor
  pressureSensor.setTimeout(PRESSURE_READ_TIMEOUT);
  if (pressureSensor.init()) {
    pressureSensorDetected = true;
    pressureSensor.enableDefault();
  }

  // setting up compass
  magneticSensor.setTimeout(MAGNETIC_SENSOR_READ_TIMEOUT);
  if (magneticSensor.init()) {
    magneticSensorDetected = true;
    magneticSensor.enableDefault();

    magneticSensor.m_min = CompassMin;
    magneticSensor.m_max = CompassMax;
  }
}

void loop(void) {
  checkIfShouldUpdateAnimation(ANIMATION_REFRESH_INTERVAL);
  
  if (!checkIfShouldUpdateTemperatureValues(TEMP_REFRESH_INTERVAL)) {
    checkIfShouldUpdateCompassValues(COMPASS_REFRESH_INTERVAL);
    checkIfShouldUpdateAltimeterValues(ALTIMETER_REFRESH_INTERVAL);
  }
  
  digitalWrite(LOOP_DELAY_LED_PIN, LOW);
  delay(LOOP_WAIT_INTERVAL);
  digitalWrite(LOOP_DELAY_LED_PIN, HIGH);
}

// all methods below will return true if their screen values
// have been updated
boolean checkIfShouldUpdateTemperatureValues(unsigned long interval) {
  unsigned long currentMillis = millis();

  if (currentMillis - lastTemperatureMillis >= interval) {
//    Serial.println("will update temperatures");
    lastTemperatureMillis = currentMillis;

    // get temperature values from sensors
    tempSensors.requestTemperatures();

    // temperature is in Celsius
    float temperatureIn = tempSensors.getTempC(InTemp);
    float temperatureOut = tempSensors.getTempC(OutTemp);

    refreshDisplayFirstLine(temperatureIn, temperatureOut);

    return true;
  }

  return false;
}

boolean checkIfShouldUpdateCompassValues(unsigned long interval) {
  unsigned long currentMillis = millis();

  if (currentMillis - lastCompassMillis >= interval) {
//    Serial.println("will update compass");
    lastCompassMillis = currentMillis;

    // heading is in degrees
    float heading = -1.0;
    if (magneticSensorDetected) {
      magneticSensor.read();

      if (!magneticSensor.timeoutOccurred()) {
        heading = magneticSensor.heading();
      } else {
        magneticSensorDetected = false;
      }
    } else {
//      Serial.println("must setup from compass");
      setupAltIMUSensors();
    }

    char *direction;
    getHeadingAsString(heading, &direction);

    refreshDisplaySecondLineCompass(direction, heading);

    return true;
  }

  return false;
}

boolean checkIfShouldUpdateAltimeterValues(unsigned long interval) {
  unsigned long currentMillis = millis();

  if (currentMillis - lastAltimeterMillis >= interval) {
//    Serial.println("will update altimeter");
    lastAltimeterMillis = currentMillis;

    // altitude is in meters
    int altitude = -1;
    if (pressureSensorDetected) {
      float pressure = pressureSensor.readPressureMillibars();
      float altitudeAsFloat = pressureSensor.pressureToAltitudeMeters(pressure);

      if (!pressureSensor.timeoutOccurred()) {
        altitude = (int)altitudeAsFloat;
      } else {
        pressureSensorDetected = false;
      }
    } else {
      setupAltIMUSensors();
    }

    refreshDisplaySecondLineAltitude(altitude);

    return true;
  }

  return false;
}

boolean checkIfShouldUpdateAnimation(unsigned long interval) {
  unsigned long currentMillis = millis();

  if (currentMillis - lastAnimationMillis >= interval) {
//    Serial.println("will update animation");
    lastAnimationMillis = currentMillis;

    updateAnimation();

    return true;
  }

  return false;
}
