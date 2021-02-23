/*
  M5Stack - ESP32 BT CAT for IC-705 OK1CDJ ondra@ok1cdj.com
  -----------------------------------------------------------
  Features: Display frequency for transverter on M5Stack or M5SrickC

  Usage: Pair Bluetooth with radio
         Set XVERT frequency and IF frequency

  TODO: there is bug in data format when data mirroring to serial is used
        not all modes of IC-705 are implemnented yet
        improve look

  ICOM CIV code is from https://github.com/darkbyte-ru/ICOM-CI-V-cat

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

 

*/

#define M5Stick
#include "BluetoothSerial.h"

#ifdef M5Stick
#include <M5StickC.h>
#endif

#define XVERT 1296000000
#define IF 144000000

//#define DEBUG 1
//#define MIRRORCAT 1

//#define MIRRORCAT_SPEED 9600

#define BROADCAST_ADDRESS    0x00 //Broadcast address
#define CONTROLLER_ADDRESS   0xE0 //Controller address

#define START_BYTE       0xFE //Start byte
#define STOP_BYTE       0xFD //Stop byte

#define CMD_TRANS_FREQ      0x00 //Transfers operating frequency data
#define CMD_TRANS_MODE      0x01 //Transfers operating mode data

#define CMD_READ_FREQ       0x03 //Read operating frequency data
#define CMD_READ_MODE       0x04 //Read operating mode data

#define CMD_WRITE_FREQ       0x05 //Write operating frequency data
#define CMD_WRITE_MODE       0x06 //Write operating mode data

#define IF_PASSBAND_WIDTH_WIDE   0x01
#define IF_PASSBAND_WIDTH_MEDIUM   0x02
#define IF_PASSBAND_WIDTH_NARROW   0x03

const uint32_t decMulti[]    = {1000000000, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1};

#define BAUD_RATES_SIZE 4
const uint16_t baudRates[BAUD_RATES_SIZE]       = {19200, 9600, 4800, 1200};

uint8_t  radio_address;     //Transiever address
uint16_t  baud_rate;        //Current baud speed
uint32_t  readtimeout;      //Serial port read timeout
uint8_t  read_buffer[12];   //Read buffer
uint32_t  frequency;        //Current frequency in Hz
uint32_t  timer;

const char* mode[] = {"LSB", "USB", "AM", "CW", "FSK", "FM", "WFM"};
#define MODE_TYPE_LSB   0x00
#define MODE_TYPE_USB   0x01
#define MODE_TYPE_AM    0x02
#define MODE_TYPE_CW    0x03
#define MODE_TYPE_RTTY  0x04
#define MODE_TYPE_FM    0x05


String modes;


BluetoothSerial CAT;

// call back to get info about connection
void callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    Serial.println("Client Connected");
  }

}

void configRadioBaud(uint16_t  baudrate)
{
  if (!CAT.begin("IC-705-CAT")) //Bluetooth device name
  {
    Serial.println("An error occurred initializing Bluetooth");

  } else {
    CAT.register_callback(callback);
    Serial.println("Bluetooth initialized");
    Serial.println("The device started, now you can pair it with bluetooth!");
  }
}

uint8_t readLine(void)
{
  uint8_t byte;
  uint8_t counter = 0;
  uint32_t ed = readtimeout;

  while (true)
  {
    while (!CAT.available()) {
      if (--ed == 0)return 0;
    }
    ed = readtimeout;
    byte = CAT.read();
    if (byte == 0xFF)continue; //TODO skip to start byte instead
#ifdef MIRRORCAT
    Serial2.write(byte); // !byte
#endif

    read_buffer[counter++] = byte;
    if (STOP_BYTE == byte) break;

    if (counter >= sizeof(read_buffer))return 0;
  }
  return counter;
}

void radioSetMode(uint8_t modeid, uint8_t modewidth)
{
  uint8_t req[] = {START_BYTE, START_BYTE, radio_address, CONTROLLER_ADDRESS, CMD_WRITE_MODE, modeid, modewidth, STOP_BYTE};
#ifdef DEBUG
  Serial.print(">");
#endif
  for (uint8_t i = 0; i < sizeof(req); i++) {
    CAT.write(req[i]);
#ifdef DEBUG
    if (req[i] < 16)Serial.print("0");
    Serial.print(req[i], HEX);
    Serial.print(" ");
#endif
  }
#ifdef DEBUG
  Serial.println();
#endif
}

void sendCatRequest(uint8_t requestCode)
{
  uint8_t req[] = {START_BYTE, START_BYTE, radio_address, CONTROLLER_ADDRESS, requestCode, STOP_BYTE};
#ifdef DEBUG
  Serial.print(">");
#endif
  for (uint8_t i = 0; i < sizeof(req); i++) {
    CAT.write(req[i]);
#ifdef DEBUG
    if (req[i] < 16)Serial.print("0");
    Serial.print(req[i], HEX);
    Serial.print(" ");
#endif
  }
#ifdef DEBUG
  Serial.println();
#endif
}

bool searchRadio()
{

  for (uint8_t baud = 0; baud < BAUD_RATES_SIZE; baud++) {
#ifdef DEBUG
    Serial.print("Try baudrate ");
    Serial.println(baudRates[baud]);
#endif
    configRadioBaud(baudRates[baud]);
    sendCatRequest(CMD_READ_FREQ);

    if (readLine() > 0)
    {
      if (read_buffer[0] == START_BYTE && read_buffer[1] == START_BYTE) {
        radio_address = read_buffer[3];
      }
      return true;
    }
  }

  radio_address = 0xFF;
  return false;
}

void printFrequency(void)
{
  frequency = 0;
  //FE FE E0 42 03 <00 00 58 45 01> FD ic-820
  //FE FE 00 40 00 <00 60 06 14> FD ic-732
  for (uint8_t i = 0; i < 5; i++) {
    if (read_buffer[9 - i] == 0xFD)continue; //spike
#ifdef DEBUG
    if (read_buffer[9 - i] < 16)Serial.print("0");
    Serial.print(read_buffer[9 - i], HEX);
#endif

    frequency += (read_buffer[9 - i] >> 4) * decMulti[i * 2];
    frequency += (read_buffer[9 - i] & 0x0F) * decMulti[i * 2 + 1];
  }
#ifdef DEBUG
  Serial.println();
#endif
}

void printMode(void)
{
  //FE FE E0 42 04 <00 01> FD
#ifdef DEBUG
  Serial.println(mode[read_buffer[5]]);
#endif
  modes = mode[read_buffer[5]];
  //read_buffer[6] -> 01 - Wide, 02 - Medium, 03 - Narrow

}

void setup()
{
  Serial.begin(115200);

#ifdef M5Stick
  M5.begin();
  M5.Lcd.setRotation(3);
  M5.Lcd.fillScreen(BLACK);

  //configRadioBaud(9600);
  radio_address = 0x00;
  M5.Lcd.setCursor(0, 3, 1);
  M5.Lcd.println("IC-705 Bluetooth CAT");
  M5.Lcd.println("by OK1CDJ");
  M5.Lcd.setCursor(10, 30, 2);
  M5.Lcd.println("Waiting for Radio...");
#endif

  while (radio_address == 0x00) {
    if (!searchRadio()) {
#ifdef DEBUG
      Serial.println("Radio not found");
#endif
    } else {
#ifdef DEBUG
      Serial.print("Radio found at ");
      Serial.print(radio_address, HEX);
      Serial.println();
#endif
#ifdef MIRRORCAT
      Serial2.begin(MIRRORCAT_SPEED, SERIAL_8N1, 26, 0);
#endif
    }
  }

}

void processCatMessages()
{
  /*
    <FE FE E0 42 04 00 01 FD  - LSB
    <FE FE E0 42 03 00 00 58 45 01 FD  -145.580.000

    FE FE - start bytes
    00/E0 - target address (broadcast/controller)
    42 - source address
    00/03 - data type
    <data>
    FD - stop byte
  */

  while (CAT.available()) {
    uint8_t knowncommand = 1;
    uint8_t r;
    if (readLine() > 0) {
      if (read_buffer[0] == START_BYTE && read_buffer[1] == START_BYTE) {
        if (read_buffer[3] == radio_address) {
          if (read_buffer[2] == BROADCAST_ADDRESS) {
            switch (read_buffer[4]) {
              case CMD_TRANS_FREQ:
                printFrequency();
                break;
              case CMD_TRANS_MODE:
                printMode();
                break;
              default:
                knowncommand = false;
            }
          } else if (read_buffer[2] == CONTROLLER_ADDRESS) {
            switch (read_buffer[4]) {
              case CMD_READ_FREQ:
                printFrequency();
                break;
              case CMD_READ_MODE:
                printMode();
                break;
              default:
                knowncommand = false;
            }
          }
        } else {
#ifdef DEBUG
          Serial.print(read_buffer[3]);
          Serial.println(" also on-line?!");
#endif
        }
      }
    }

#ifdef DEBUG
    //if(!knowncommand){
    Serial.print("<");
    for (uint8_t i = 0; i < sizeof(read_buffer); i++) {
      if (read_buffer[i] < 16)Serial.print("0");
      Serial.print(read_buffer[i], HEX);
      Serial.print(" ");
      if (read_buffer[i] == STOP_BYTE)break;
    }
    Serial.println();
    //}
#endif
  }

#ifdef MIRRORCAT
  while (Serial2.available()) {
    CAT.print((byte)Serial2.read());
  }
#endif
}


void loop()
{
  double qrg = 0;
  sendCatRequest(CMD_READ_FREQ);
  sendCatRequest(CMD_READ_MODE);
  processCatMessages();
  if (millis() - timer > 500) {
    timer = millis();
    qrg = (XVERT - IF + frequency);
    Serial.println(qrg / 1000);

#ifdef M5Stick
    M5.Lcd.setCursor(0, 3, 1);
    M5.Lcd.println("IC-705 Bluetooth CAT");
    M5.Lcd.println("by OK1CDJ");
    M5.Lcd.setCursor(3, 30, 4);
    M5.Lcd.printf("%.2f", qrg / 1000);
    M5.Lcd.setCursor(10, 60, 1);
    M5.Lcd.print(frequency);
    M5.Lcd.print("   ");
    M5.Lcd.println(modes);
#endif
  }

}



