/*********************************************************************************
 * ESP-Now-Burst-Serial-Bridge
 *
 * ESP32 based serial bridge for transmitting serial databursts between two boards
 *
 * The primary purpose of this sketch was to enable an RTCM3 serial connection,
 *   which can have packets as large as 1023 bytes (much bigger than the 250
 *   byte limit of ESP-Now).  It can be used in any application where burst
 *   transmissions exceed ESP-Now's 250 byte limit but overall packet size can be
 *   transmitted in 250 byte chunks between data bursts.  Do not use this sketch
 *   to transmit continuous data streams.
 *   
 * Testing was done up to 460800 baud, and since the ESP-Now protocol is baud rate
 *   agnostic, each endpoint can have a different serial baud rate, so long as
 *   there's enough time between databursts to relay the serial messages.
 *
 * If packet size is always expected to be less than 250 bytes, use my other
 *   sketch, ESP-Now-Serial-Bridge, rather than this one for more efficient
 *   and likely more reliable communication.
 *
 * Range is easily better than regular WiFi, however an external antenna may be
 *   required for truly long range messaging, to combat obstacles/walls, and/or
 *   to achieve success in an area saturated with 2.4GHz traffic.
 *
 * I made heavy use of compiler macros to keep the sketch compact/efficient.
 *
 * To find the MAC address of each board, uncomment the #define DEBUG line, 
 *   and monitor serial output on boot.  Set the OPPOSITE board's MAC address
 *   as the value for RECVR_MAC in the macros at the top of the sketch.
 *
 * The BLINK_ON_* macros should be somewhat self-explanatory.  If your board has a built-in
 *   LED (or you choose to wire an external one), it can indicate ESP-Now activity as
 *   defined by the macros you choose to enable.
 *
 * When uploading the sketch, be sure to define BOARD1 or BOARD2 as appropriate
 *   before compiling.
 *
 * -- Yuri - Sep 2021
 * 
 * Blog post here: https://discuss.ardupilot.org/t/simple-esp-telemetry-serial-bridges/75536
 *
 * Based this example: https://randomnerdtutorials.com/esp-now-two-way-communication-esp32/
 * 
 * For a similar bridge using LoRa, see: https://github.com/ktrussell/Serial_to_LoRa
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files.
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
*********************************************************************************/

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

/********************** CHANGE THIS FOR EACH BOARD'S UPLOAD **********************/
#define BOARD1 // BOARD1 or BOARD2
/*********************************************************************************/

#ifdef BOARD1
#define RECVR_MAC {0x94, 0xB9, 0x7E, 0xD9, 0xDD, 0xD4}  // replace with your board's address
//#define BLINK_ON_SEND         // blink during message send (not an indication of success)
//#define BLINK_ON_SEND_SUCCESS // blink on receiver acknowledgement of sent message
//#define BLINK_ON_RECV         // blink during message receipt
#define BLINK_ON_RECV_BURST 100 // blink on databurst of at least this many bytes
#else
#define RECVR_MAC {0x94, 0xB9, 0x7E, 0xE4, 0x8D, 0xFC}  // replace with your board's address
//#define BLINK_ON_SEND
#define BLINK_ON_SEND_SUCCESS
//#define BLINK_ON_RECV
//#define BLINK_ON_RECV_BURST 100
#endif

#if defined(BLINK_ON_SEND_SUCCESS) || defined(BLINK_ON_RECV_BURST)
#define BLINK_DURATION 100    // (ms) LED stays lit for this long on activity
uint32_t blink_timeout = 0;
#endif

#define WIFI_CHAN  13         // 12-13 only legal in US in lower power mode, do not use 14
#define BAUD_RATE  460800
#define TX_PIN     1          // default UART0 is pin 1 (shared by USB)
#define RX_PIN     3          // default UART0 is pin 3 (shared by USB)
#define SER_PARAMS SERIAL_8N1 // SERIAL_8N1: start/stop bits, no parity

#define BUFFER_SIZE      250  // max of 250 bytes
#define MAX_BURST_SIZE  1024  // setting this too large will result in data loss
#define RECV_TIMEOUT_MS   50  // if we haven't received a transmission in this long, assume we have a fully formed databurst message
//#define DEBUG               // for additional serial messages (may interfere with other messages)

#ifndef LED_BUILTIN
#define LED_BUILTIN 2         // some boards don't have an LED or have it connected elsewhere
#endif

const uint8_t broadcastAddress[] = RECVR_MAC;
// wait for double the time between bytes at this serial baud rate before sending a packet
// this *should* allow for complete packet forming when using packetized serial comms
const uint32_t send_timeout_us = (int)(1.0 / BAUD_RATE * 1E6) * 20;

uint8_t  buf_recv[MAX_BURST_SIZE];
uint8_t  buf_send[BUFFER_SIZE];
uint8_t  send_buf_size = 0;
uint16_t recv_buf_size = 0;
uint32_t send_timeout = 0;
uint32_t recv_timeout = 0;

#if defined(DEBUG) || defined(BLINK_ON_SEND_SUCCESS)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  #ifdef DEBUG
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Send success");
  } else {
  Serial.println("Send failed");
  }
  #endif

  #ifdef BLINK_ON_SEND_SUCCESS
  if (status == ESP_NOW_SEND_SUCCESS) {
    digitalWrite(LED_BUILTIN, HIGH);
    blink_timeout = millis() + BLINK_DURATION;
    return;
  }
  // turn off the LED if send fails
  digitalWrite(LED_BUILTIN, LOW);
  blink_timeout = 0;
  #endif
}
#endif

void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  recv_timeout = millis() + RECV_TIMEOUT_MS;
  #ifdef BLINK_ON_RECV
  digitalWrite(LED_BUILTIN, HIGH);
  #endif
  uint8_t bytes_received[BUFFER_SIZE];
  memcpy(&bytes_received, incomingData, sizeof(bytes_received));
  for (uint16_t i = 0; (i < len && i < MAX_BURST_SIZE); i++){  // this WILL cause data loss if packets exceed max buffer size
    buf_recv[recv_buf_size] = bytes_received[i];
    recv_buf_size++;
  }
  #ifdef BLINK_ON_RECV
  digitalWrite(LED_BUILTIN, LOW);
  #endif
  #ifdef DEBUG
  Serial.print("\nBytes received: ");
  Serial.println(len);
  #endif
}

void write_recv_buffer() {
  Serial.write(buf_recv, recv_buf_size);
  #ifdef DEBUG
  Serial.print("\nTotal databurst size: ");
  Serial.println(recv_buf_size);
  #endif
  #ifdef BLINK_ON_RECV_BURST
  if (recv_buf_size >= BLINK_ON_RECV_BURST) {
    digitalWrite(LED_BUILTIN, HIGH);
    blink_timeout = millis() + BLINK_DURATION;
  }
  #endif
  recv_buf_size = 0;
  recv_timeout = 0;
}
 
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(BAUD_RATE, SER_PARAMS, RX_PIN, TX_PIN);
  Serial.println(send_timeout);
  WiFi.mode(WIFI_STA);

  #ifdef DEBUG
  Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
  #endif
  
  if (esp_wifi_set_channel(WIFI_CHAN, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    #ifdef DEBUG
    Serial.println("Error changing WiFi channel");
    #endif
    return;
  }

  if (esp_now_init() != ESP_OK) {
    #ifdef DEBUG
    Serial.println("Error initializing ESP-NOW");
    #endif
    return;
  }

  #if defined(DEBUG) || defined(BLINK_ON_SEND_SUCCESS)
  esp_now_register_send_cb(OnDataSent);
  #endif
  
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHAN;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    #ifdef DEBUG
    Serial.println("Failed to add peer");
    #endif
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
}

void loop() {

  // read up to BUFFER_SIZE from serial port
  if (Serial.available()) {
    while (Serial.available() && send_buf_size < BUFFER_SIZE) {
      buf_send[send_buf_size] = Serial.read();
      send_timeout = micros() + send_timeout_us;
      send_buf_size++;
    }
  }

  // send buffer contents when full or timeout has elapsed
  if (send_buf_size == BUFFER_SIZE || (send_buf_size > 0 && micros() >= send_timeout)) {
    #ifdef BLINK_ON_SEND
    digitalWrite(LED_BUILTIN, HIGH);
    #endif
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &buf_send, send_buf_size);
    send_buf_size = 0;
    #ifdef DEBUG
    if (result == ESP_OK) {
      Serial.println("Sent!");
    }
    else {
      Serial.println("Send error");
    }
    #endif
    #ifdef BLINK_ON_SEND
    digitalWrite(LED_BUILTIN, LOW);
    #endif
  }

  uint32_t time_now_ms = millis();

  if (recv_buf_size > 0 && time_now_ms > recv_timeout) {
    write_recv_buffer();  // write whole databurst at once to serial output
  }

  #if defined(BLINK_ON_SEND_SUCCESS) || defined(BLINK_ON_RECV_BURST)
  if (blink_timeout > 0 && time_now_ms > blink_timeout) {
    digitalWrite(LED_BUILTIN, LOW);
    blink_timeout = 0;
  }
  #endif

}
