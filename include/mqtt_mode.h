#ifndef MQTT_MODE_H
#define MQTT_MODE_H

// Compile-time MQTT transport selector.
// 0: Local Mosquitto (username/password, port from mqtt_secrets.h)
// 1: AWS IoT Core (TLS + ECC certificate, port 8883)
#ifndef BLOOM_MQTT_USE_AWS
#define BLOOM_MQTT_USE_AWS 0
#endif

#endif
