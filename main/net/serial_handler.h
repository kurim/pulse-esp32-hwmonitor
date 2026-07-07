#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Startet den Empfang von Hardwaredaten per USB - entweder ueber UART0
// (externer Bridgechip, z.B. CH340/CP2102) oder natives USB-Serial/JTAG
// (bridgechip-lose Boards wie der ESP32-C3 Super Mini), siehe
// app_config.serial_iface und serial_handler.c. Alternative zu
// mqtt_handler_begin(), siehe app_config.hw_source.
void serial_handler_begin(void);

#ifdef __cplusplus
}
#endif
