#ifndef HPMA115_H
#define HPMA115_H

#include "application.h"

#define HPMA115_BAUD 9600

#define HPMA115_HEAD                  0x68
#define HPMA115_START_MEASUREMENT     0x01
#define HPMA115_STOP_MEASUREMENT      0x02
#define HPMA115_START_AUTO_SEND_CMD   0x40
#define HPMA115_STOP_AUTO_SEND_CMD    0x20

#define HPMA115_SUCCESS        0
#define HPMA115_NO_DATA_AVAIL  2

typedef enum {
  READY,
  DATA_AVAILABLE,
  DATA_READ,
  DISABLED
} hpma115_state_t;

typedef struct {
  uint16_t pm25;
  uint16_t pm10;
} hpma115_data_t;

typedef void (*hpma115_cb)(hpma115_data_t * p_data);

typedef struct {
  hpma115_cb callback;
  uint8_t enable_pin;
} hpma115_init_t;

class HPMA115 {
  public:
    HPMA115(void);
    uint32_t setup(hpma115_init_t *p_init);
    uint32_t enable();
    uint32_t disable();
    void process();
    void int_handler(void);
  protected:
    hpma115_cb callback;
    hpma115_data_t data;
    bool    data_ready;
    volatile hpma115_state_t state;
    uint8_t enable_pin;
    char rx_buf[32];
};

#endif //HPMA115_H