/*
   Simple simulator for Fossasat-1 groundstations.

   This example transmits packets using SX1278 LoRa radio module.

   Reply is provided for 4 commands.
   
   0x00 send ping frame
   0x01 request satellite info
   0x03 send message to be retransmitted
   0x04 request last packet info

   0x02 is not implemented
*/
#define DEBUG

#include <SPI.h>

// include the library
#include <RadioLib.h>

// Change to your setup here

// My SX1278 has the following connections:
// NSS pin:   16
// DIO0 pin:  15
// DIO1 pin:  15
SX1278 lora = new Module(16, 15, 15);  // hallard values, frequency is set to 868.7 (as I have no spare 436.7 sx)
//SX1278 lora = new Module(10, 2, 3);  // arduino values
#define LORA_CARRIER_FREQUENCY                          868.7f //  For FOSSASAT use 436.7f  // MHz

//#define LORA_CARRIER_FREQUENCY                          436.7f  // MHz
#define LORA_BANDWIDTH                                  125.0f  // kHz dual sideband
#define LORA_SPREADING_FACTOR                           11
#define LORA_SPREADING_FACTOR_ALT                       10
#define LORA_CODING_RATE                                8       // 4/8, Extended Hamming
#define LORA_OUTPUT_POWER                               17      // dBm
#define LORA_CURRENT_LIMIT                              120     // mA

// flag to indicate that a packet was received
volatile bool receivedFlag = false;

// disable interrupt when it's not needed
volatile bool enableInterrupt = true;

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
// ICACHE_RAM_ATTR is required for Wemos D1
// https://community.blynk.cc/t/error-isr-not-in-iram/37426/20

ICACHE_RAM_ATTR
void setFlag(void) {
  // check if the interrupt is enabled
  if(!enableInterrupt) {
    Serial.println("interrupt ignored");
    return;
  }

  // we got a packet, set the flag
  Serial.println("got a package");
  receivedFlag = true;
}

uint8_t downlink[64];
uint8_t uplinked[64];
uint8_t downlinklen = 0;

int state;
float lastRSSI = 0.0;
float lastSNR = 0.0;
String callsign = "FOSSASAT-1";

int flag1 = 0;

void setup() {
  Serial.begin(115200);
  
  // use the buildin led to see if there is life
  pinMode(LED_BUILTIN, OUTPUT);

  // initialize SX1278 with default settings
  Serial.println();
  Serial.println(F("Fossasat-1 Simulator"));
  
  Serial.print(F("[SX1278] Initializing ... "));

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The SYNC_WORD discussion.
// Fossasat-1 is hardcoded 0x0F0F and it uses an sx1268 lora chip
// This SYNC_WORD will sync with another sx1268 with the same SYNC_WORD
// The sx1278 does not sync with that value even when set to 0xFF
// 
// Accepted sync words for sx1268 are PRIVATE_SYNCWORD = 0x1424 and PUBLIC_SYNCWORD 0x3444.
// for the sx1278 to sync it must have PRIVATE_SYNCWORD = 0x12 and PUBLIC_SYNCWORD 0x34.
// The way it matches is that the top 8 bits of sx1268 first integer in the SYNC_WORD 0x14 provides  sx1278 SYNC_WORD with the top 8 bit (1)
// and the top 8 bit of sx1268 second integer in the SYNC_WORD 0x24 provides sx1278 SYNC_WORD with bottom 8 bits (2)
// SYNC_WORD is 012
// Same methodfor PUBLIC_SYNCWORD
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  char  SYNC_WORD = 0xff;  //use 0f0f for SX1262  

// current limit:   100 mA
// preamble length: 8 symbols
// amplifier gain:  0 (automatic gain control)

  state = lora.begin(LORA_CARRIER_FREQUENCY,
                     LORA_BANDWIDTH,
                     LORA_SPREADING_FACTOR,
                     LORA_CODING_RATE,
                     SYNC_WORD,
                     LORA_OUTPUT_POWER,
                     (uint8_t)LORA_CURRENT_LIMIT);
  if (state == ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("Terminated: Failed with code "));
    if ( state == -2 ) {
      Serial.println(F("Chip is not found, check the connections")); 
    }else{
      Serial.println(state);
    }
    Serial.println(F("Error codes are listed in:  libraries/RadioLib/src/TypeDef.h"));
    while (true);
  }
  Serial.println();
#ifdef DEBUG
  Serial.print(F("Carrier Frequency:"));Serial.println(LORA_CARRIER_FREQUENCY);
  Serial.print(F("Spreading factor: "));Serial.println(LORA_SPREADING_FACTOR);
  Serial.print(F("Coding rate:      "));Serial.println(LORA_CODING_RATE);
#endif

  // reset buffers
  for (uint8_t i=0; i<63; i++) { 
    downlink[i] = 0x00;
    uplinked[i] = 0x00;
  }

  // set the function that will be called
  // when new packet is received
  // DIO0 will generate interrupt when data are available (on sx1278)
  // DIO1 will generate interrupt when data are available (on sx1268)
  lora.setDio0Action(setFlag);
  
  // start listening for LoRa packets
  Serial.print(F("[SX1278] Starting to listen ... "));
  state = lora.startReceive();
  if (state == ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("Terminated: startReceive failed, code "));
    Serial.println(state);
    Serial.println(F("Error codes are listed in:  libraries/RadioLib/src/TypeDef.h"));
    while (true);
  }
  enableInterrupt = true;
  receivedFlag = false;
}

void loop() {

  if(receivedFlag) {
    // disable the interrupt service routine while processing the data
    enableInterrupt = false;

    // reset flag
    receivedFlag = false;

    // read received data
    size_t respLen = lora.getPacketLength();
#ifdef DEBUG
    Serial.print(F("Packet length="));
    Serial.println(respLen);
#endif
    uint8_t* respFrame = new uint8_t[respLen];
    state = lora.readData(uplinked, respLen);
    float RSSI = lora.getRSSI();
    float SNR = lora.getSNR();

#ifdef DEBUG
    // Dump the uplinked data
    // <callsign><function ID>(optional data length)(optional data)
    for (uint8_t i = 0; i<10; i++) { Serial.print(char(uplinked[i])); }
    Serial.print(uplinked[10], HEX); // command
    for (uint8_t i = 11; i<respLen; i++) { Serial.print(F(" 0x")); Serial.print(uplinked[i], HEX); Serial.print(F(" ")); }
    Serial.println();
#endif

    // reset downlink array
    for (uint8_t i = 0; i<64; i++) { downlink[i] = '\0'; }

#ifdef DEBUG
    // Create the downlink package
    Serial.print(F("[SX1278] Transmitting packet ... Type = 0x"));
    Serial.println((uplinked[10]+10),HEX);
#endif
    // Set satellite call sign
    for (uint8_t i = 0; i < 10; i++ ) { downlink[i] = callsign[i]; }
    
    if ( uplinked[10] == 0x00 ) { // ping received
      // Receive: FOSSASAT-1 0x0
      // Send:    FOSSASAT-1 0x10
      downlink[10] = 0x10;
      downlink[11]= '\0';
      downlinklen=12;
    }
    
    if ( uplinked[10] == 0x01 ) { // cmd_retransmit
      // Receive: FOSSASAT-1 0x1  0x6 123456
      // Send:    FOSSASAT-1 011  0x6 123456
      downlink[10] = 0x11;      
      uint8_t datalen = uplinked[11];
      for (uint8_t i = 11; i < datalen + 11; i++ ) {
        downlink[i] = uplinked[i];
      }
      downlink[datalen + 11] = 0x00;
      downlinklen = datalen + 12;
    }
    
    if ( uplinked[10] == 0x03 ) {
      // Request satellite info
      // Receive: FOSSASAT-1 0x3
      // Send:    FOSSASAT-1 0x13 0x0F 0xC4 0xF1 0x03 0xC4 0x00 0x00 0x00 0x90 0x01 0x56 0xFA 0x4C 0x09 0x00 0x16
      
      // real data 13 0F C4 F1 03 C4 00 00 00 90 01 56 FA 4C 09 00 16   Received at 13/12/2019
      // Battery voltage = 3920mV
      // Battery charging current = 10.09mA
      // Solar cell voltage A, B and C = 0 mV
      
      downlink[10]= 0x13; //
      downlink[11]= 0x0F; // optionalDataLen
      downlink[12]= 0xC4; // battery charging voltage  * 20 mV
      downlink[13]= 0xF1; // battery charging current * 10uA
      downlink[14]= 0x03;
      downlink[15]= 0xC4; // Battery voltage
      downlink[16]= 0x00; // solar cell A voltage  * 20 mV
      downlink[17]= 0x00; // solar cell B voltage  * 20 mV
      downlink[18]= 0x00; // solar cell C voltage  * 20 mV
      downlink[19]= 0x90; // battery temp * 0.01 C
      downlink[20]= 0x01;
      downlink[21]= 0x56; // board temp * 0.01 C
      downlink[22]= 0xFA;
      downlink[23]= 0x4C; // mcu temp *0.1 C
      downlink[24]= 0x09; // reset counter
      downlink[25]= 0x00;
      downlink[26]= 0x16; //pwr signedinteger
      downlink[27]= '\0';
      downlinklen=28;
    }
    
    if ( uplinked[10] == 0x04 ) {
      // last packet info
      // Receive: FOSSASAT-1 0x4
      // Send:    FOSSASAT-1 0x14 nn nn
      downlink[10]= 0x14;
      downlink[11]= (lastSNR * 4);   // optionaldownlink[0] = SNR * 4 dB
      downlink[12]= (lastRSSI * -2); // optionaldownlink[1] = RSSI * -2 dBm
      downlink[13]= '\0';
      downlinklen=14;
    }

    lastRSSI = RSSI; // keep in case of a cmd = 0x04
    lastSNR = SNR;
    
    digitalWrite(LED_BUILTIN, HIGH);
    if ( flag1 == 0 ){
    state = lora.transmit(downlink, downlinklen);
    }
    else
    {
      state = ERR_NONE;
    }

#ifdef DEBUG
    // dump downlink data
    for (uint8_t i = 0; i<10; i++) { Serial.print(char(downlink[i])); }
    for (uint8_t i = 10; i<downlinklen; i++) { Serial.print(F(" 0x")); Serial.print(downlink[i], HEX); Serial.print(F(" ")); }
    Serial.println();
#endif
    digitalWrite(LED_BUILTIN, LOW);
     
    if (state == ERR_NONE) {
      // the packet was successfully transmitted
      //Serial.println(F(" success!"));
  
      // print measured data rate
      Serial.print(F("[SX1278] Datarate:\t"));
      Serial.print(lora.getDataRate());
      Serial.println(F(" bps"));
  
    } else if (state == ERR_PACKET_TOO_LONG) {
      // the supplied packet was longer than 256 bytes
      Serial.println(F(" too long!"));
  
    } else if (state == ERR_TX_TIMEOUT) {
      // timeout occured while transmitting packet
      Serial.println(F(" timeout!"));
  
    } else if (state == ERR_SPI_WRITE_FAILED) {
      // Real value in SPI register does not match the expected one.
      Serial.println(F(" SPI write failed!"));
  
    } else {
      // some other error occurred
      Serial.print(F("failed, code "));
      Serial.println(state);
    }

#ifdef DEBUG
    Serial.println(F("enable interrupt"));
#endif
    int newstate = lora.startReceive();
    if (newstate == ERR_NONE) {
      Serial.println(F("startReceive success!"));
    } else {
      Serial.print(F("Terminated: startReceive failed, code "));
      Serial.println(state);
      Serial.println(F("Error codes are listed in:  libraries/RadioLib/src/TypeDef.h"));
      while (true);
    }
    lora.setDio0Action(setFlag);
    enableInterrupt = true; // ready for next package
  }
}
