/*
 * Project Particle Squared
 * Description: Particle powered PM2.5 and air quality sensor
 * Author: Jared Wolff
 * Date: 2/26/2019
 * License: GNU GPLv3
 */

#include "si7021.h"
#include "ccs811.h"
#include "hpma115.h"

// SYSTEM_MODE(SEMI_AUTOMATIC);
// SYSTEM_THREAD(ENABLED);

#define I2C_SDA_PIN     D0
#define I2C_SCL_PIN     D1

#define CCS811_WAKE_PIN D6
#define CCS811_INT_PIN  D8
#define CCS811_RST_PIN  D7

#define HPMA1150_EN_PIN D5

// Address for CCS811, it is setable in hardware so should be defined here
#define CCS811_ADDRESS  0x5a

// Watchdog timeout period
#define WATCHDOG_TIMEOUT_MS 30000

// Delay and timing related contsants
#define MEASUREMENT_DELAY_MS 120000
#define MIN_MEASUREMENT_DELAY_MS 10000

// I2C Related constants
#define I2C_CLK_SPEED 100000

// Timer handler
void timer_handler();

// Reading delay ms
static uint32_t m_reading_period = MEASUREMENT_DELAY_MS;

// Set up timer
Timer timer(m_reading_period, timer_handler);

// Watchdog
ApplicationWatchdog wd(WATCHDOG_TIMEOUT_MS, System.reset);

// Static objects
static Si7021  si7021 = Si7021();
static CCS811  ccs811 = CCS811();
static HPMA115 hpma115 = HPMA115();
static si7021_data_t si7021_data, si7021_data_last;
static ccs811_data_t ccs811_data;
static hpma115_data_t hpma115_data;

// Data check bool
static bool data_check = false;

// Data state ready
static bool hpma115_data_ready = false;
static bool si7021_data_ready = false;
static bool ccs811_data_ready = false;

// Definition of timer handler
void timer_handler() {
  data_check = true;
}

// ccs811_pin_interrupt() forwards pin interrupt on to the specific handler
void ccs811_pin_interrupt() {
  ccs811.int_handler();
}

// forwards serial data interrupt to HPMA driver
void serialEvent1() {
  hpma115.int_handler();
}

// Async publish event
void hpma_evt_handler(hpma115_data_t *p_data) {
  hpma115_data = *p_data;

  // Serial.printf("pm25 %dμg/m3 pm10 %dμg/m3\n", hpma115_data.pm25, hpma115_data.pm10);

  hpma115_data_ready = true;

  Serial.println("hpma rdy");
}

int set_reading_period( String period ) {

  uint32_t temp_period = (uint32_t)period.toInt();

  if( temp_period != m_reading_period && temp_period > MIN_MEASUREMENT_DELAY_MS ) {
    Serial.printf("update reading period %d\n",temp_period);
    m_reading_period = temp_period;

    // Change period if variable is updated
    timer.changePeriod(m_reading_period);

    return 1;

  }

  return -1;

}

// setup() runs once, when the device is first turned on.
void setup() {

  // Set up PC based UART (for debugging)
  Serial.blockOnOverrun(false);
  Serial.begin();

  // Set up I2C
  Wire.setSpeed(I2C_CLK_SPEED);
  Wire.begin();

  // Set up Si7021;
  si7021_data_last.humidity = 0;
  si7021_data_last.temperature = 0;

  uint32_t err_code = si7021.setup();
  if( err_code != 0 ) {
    Serial.printf("si7021 setup err %d\n", err_code);
    Serial.flush();
    System.reset();
  }

  // Setup CC8012
  ccs811_init_t ccs811_init;
  ccs811_init.int_pin = CCS811_INT_PIN;
  ccs811_init.address = CCS811_ADDRESS;
  ccs811_init.pin_interrupt = ccs811_pin_interrupt;
  ccs811_init.rst_pin = CCS811_RST_PIN;
  ccs811_init.wake_pin = CCS811_WAKE_PIN;

  // Init the TVOC & C02 sensor
  err_code = ccs811.setup(&ccs811_init);
  if( err_code != 0 ) {
    Serial.printf("ccs811 setup err %d\n", err_code);
    Serial.flush();
    System.reset();
  }

  // Start VOC measurement
  // This is an async reading.
  err_code = ccs811.enable();
  if( err_code != 0 ) {
    Serial.printf("ccs811 enable err %d\n", err_code);
    Serial.flush();
    System.reset();
  }

  // Setup HPMA115
  hpma115_init_t hpma115_init;
  hpma115_init.callback = hpma_evt_handler;
  hpma115_init.enable_pin = HPMA1150_EN_PIN;

  // Init HPM115 sensor
  err_code = hpma115.setup(&hpma115_init);
  if (err_code != HPMA115_SUCCESS) {
    Serial.printf("hpma115 enable err %d\n", err_code);
    Serial.flush();
    System.reset();
  }

  // Start the timer
  timer.start();

  // Set up cloud variable
  Particle.function("set_period", set_reading_period);


}

// loop() runs over and over again, as quickly as it can execute.
void loop() {

  // Connect if not connected..
  if (Mesh.ready() == false) {
    Serial.println("Not connected..");
    Mesh.connect();
  }

  // If all the data is ready, send it as one data blob
  if (ccs811_data_ready && si7021_data_ready && hpma115_data_ready ) {
    Serial.println("data send");

    String out = String::format("{\"temperature\":%.2f,\"humidity\":%.2f,\"pm25\":%d,\"pm10\":%d,\"tvoc\":%d,\"c02\":%d}",si7021_data.temperature,si7021_data.humidity,hpma115_data.pm25,hpma115_data.pm10,ccs811_data.tvoc,ccs811_data.c02);
    Particle.publish("blob", out , PRIVATE, WITH_ACK);

    // Reset to false
    ccs811_data_ready = false;
    si7021_data_ready = false;
    hpma115_data_ready = false;
  }

  // If we're greater than or equal to the measurement delay
  // start taking measurements!
  if( data_check ) {

    // Set state variable to false
    data_check = false;

    // Read temp and humiity
    uint32_t err_code = si7021.read(&si7021_data);

    if( err_code == SI7021_SUCCESS ) {
      // Set env data in the CCS811

      int temp_abs = abs(si7021_data.temperature - si7021_data_last.temperature);
      int hum_abs = abs(si7021_data.humidity - si7021_data_last.humidity);

      // Change on absolute value > 1
      if( temp_abs > 1 || hum_abs >  1 ) {
          Serial.println("change");

          // Set "last" value
          si7021_data_last.temperature = si7021_data.temperature;
          si7021_data_last.humidity = si7021_data.humidity;

          // Update the CCS811 environmental params
      ccs811.set_env(si7021_data.temperature,si7021_data.humidity);
      }

      si7021_data_ready = true;
      Serial.println("temp rdy");
    } else {
      Serial.println("temp err");
    }

    // Process CCS811
    err_code = ccs811.read(&ccs811_data);

    if ( err_code == CCS811_SUCCESS ) {
      ccs811_data_ready = true;

      Serial.println("tvoc rdy");
    } else {
      Serial.println("tvoc err");
    }

    // Process PM2.5 and PM10 results
    // This is slightly different from the other readings
    // due to the fact that it should be shut off when not taking a reading
    // (extends the life of the device)
    hpma115.enable();

  }

  // Processes any avilable serial data
  hpma115.process();

  // Send updates/communicate with service when connected
  if( Particle.connected() ) {
    Particle.process();
  }

  // Checking with WD
  wd.checkin();

}