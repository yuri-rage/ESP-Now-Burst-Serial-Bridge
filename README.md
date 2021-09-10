# ESP-Now-Burst-Serial-Bridge

ESP32 based serial bridge for transmitting serial databursts between two boards

The primary purpose of this sketch was to enable an RTCM3 serial connection, which can have packets as large as 1023 bytes (much bigger than the 250 byte limit of ESP-Now).  It can be used in any application where burst transmissions exceed ESP-Now's 250 byte limit but overall packet size can be transmitted in 250 byte chunks between data bursts.  Do not use this sketch to transmit continuous data streams.

Testing was done up to 460800 baud, and since the ESP-Now protocol is baud rate agnostic, each endpoint can a different serial baud rate, so long as there's enough time between databursts to relay the serial messages.  If packet size is always expected to be less than 250 bytes, use my other sketch, ESP-Now-Serial-Bridge, rather than this one for more efficient and likely more reliable communication.

Range is easily better than regular WiFi, however an external antenna may be required for truly long range messaging, to combat obstacles/walls, and/or to achieve success in an area saturated with 2.4GHz traffic.

To find the MAC address of each board, uncomment the `#define DEBUG` line, and monitor serial output on boot.  Set the OPPOSITE board's MAC address as the value for RECVR_MAC in the macros at the top of the sketch.

When uploading the sketch, be sure to define `BOARD1` or `BOARD2` as appropriate before compiling.

-- Yuri - Sep 2021

Blog post here: https://discuss.ardupilot.org/t/simple-esp-telemetry-serial-bridges/75536

Based this example: https://randomnerdtutorials.com/esp-now-two-way-communication-esp32/

For a similar bridge using LoRa, see: https://github.com/ktrussell/Serial_to_LoRa

### License

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files.

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
