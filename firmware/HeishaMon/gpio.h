#include <PubSubClient.h>
#include <WiFi.h>

#define NUMGPIO 1

struct gpioSettingsStruct {
  unsigned int gpioPin[NUMGPIO] = {39};
  unsigned int gpioMode[NUMGPIO] = {INPUT};
};

void setupGPIO(gpioSettingsStruct gpioSettings);
