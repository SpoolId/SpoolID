#include <Arduino.h>

#include "config.h"
#include "logger.h"
#include "material_db.h"
#include "rfid_writer.h"
#include "serial_proto.h"
#include "web_server.h"
#include "wifi_net.h"

// Optional active buzzer (set to a free GPIO, or -1 to disable). D2 = GPIO4.
constexpr int BUZZER_PIN = -1;

void setup() {
  config::begin();
  Config &c = config::get();
  logger::begin(c.baud, static_cast<LogLevel>(c.logLevel));
  LOG_I("SpoolID booting");

  rfid::begin(BUZZER_PIN);
  matdb::begin();
  net::begin();
  web::begin();

  randomSeed(esp_random());
  LOG_I("ready (serial JSON protocol active)");
}

void loop() {
  net::loop();
  serialproto::poll();
  rfid::poll();
  delay(20);
}
