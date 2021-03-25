/**
 * @brief Tests probing BM to establish initial communication.
 * 
 * @author Hale Konopka (jkonopka)
 * 
 * See BM Testing Harness Schematic. Below is overview of testbench specs for which this was written:
 *   - Arduino UNO 
 *       - note this only has 1 serial port which is annoying because you get 
 *         garbage characters printed to the console when BM is talking
 *   - 40V power supply 
 *      - TOPPOWER STP6005DH), set to V=40, A=0.25A. 
 *      - The BM will nominally pull 0.05A when inactive and 0.15A when in BOOT
 *        state (connect Vdd to arduino), and 0.2A when it is in PRIME state
 *        (Arduino has given it name and is reading voltages)
 *   - BM board
 *   - Voltage divider resistor network on breadboard to simulate cells
 *
 * @note This document is not intended to be the most complete and consise verification plan,
 *       but rather capture the key tests which were needed to establish communication with
 *       the BM (which can now be done reliably). Many intermediate functions and hours of
 *       debugging were intermittently used, this is a summary of important tests to complement
 *       notes.
 */

#include "crc8.h" /* crcCalcCRC8() leaked from doc */

#define BMS_COMMAND_LEN 8
#define BM_RESPONSE_LEN 14

/* Packet Definitions */
#define HEAD                     (0x58)
#define GLOBAL_ADDR              (0xff)

/* Command Definitions */
#define CMD_TRIGGER              (50)
#define CMD_SET_ADDRESS          (60)
#define CMD_AUTOADDR_DONE        (65)
#define CMD_GLOBAL_SNAPSHOT      (70)
#define CMD_SEND_SUMMARY         (80)
#define CMD_SEND_ALL_VOLTAGES_1  (160)
#define CMD_SEND_ALL_VOLTAGES_2  (161)
#define CMD_SEND_ALL_VOLTAGES_3  (162)
#define CMD_BALANCE_TARGET       (170)
#define CMD_SUSI                 (251)

/**
 * @brief Test startup sequence with triggering
 */
void test5(void)
{
  /* Command definitions */
  byte global_set_addr_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_SET_ADDRESS, 0xFE, 0x00, 0x00, 0x02, 0xFF};
  global_set_addr_cmd[7] = crcCalcCRC8(global_set_addr_cmd, 7);
  
  byte trigger_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_TRIGGER, 0x00, 0x00, 0x00, 0x00, 0xFF};
  trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);
  
  byte auto_addr_done_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_AUTOADDR_DONE, 0x00, 0x00, 0x00, 0x00, 0xFF};
  auto_addr_done_cmd[7] = crcCalcCRC8(auto_addr_done_cmd, 7);

  byte send_summary[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_SEND_SUMMARY, 0x00, 0x00, 0x00, 0x00, 0xFF};
  send_summary[7] = crcCalcCRC8(send_summary, 7);
  
  Serial.write(trigger_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
}

/**
 * @brief Test whether BM responds to send_summary with address other than 0xFE which 
 *        appears to be the address which it adopts.
 *        
 * @result The BM only responds by 0xFE which is the address it seems to be given upon startup!
 *         This means that it has truly adopted the address and isn't just responding to any
 *         address!
 */
void test6(void)
{
  /* Command definitions */
  byte send_summary[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_SEND_SUMMARY, 0x00, 0x00, 0x00, 0x00, 0xFF};
  send_summary[7] = crcCalcCRC8(send_summary, 7);
  
  /* send summary using address 0xFE */
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  /* Send summary using different address */
  send_summary[1] = 0xAB;
  send_summary[7] = crcCalcCRC8(send_summary, 7);
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
}

/**
 * @brief See notes
 */
void test9(void)
{
  /* Command definitions */
  byte global_set_addr_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_SET_ADDRESS, 0xAB, 0x00, 0x00, 0x01, 0xFF};
  global_set_addr_cmd[7] = crcCalcCRC8(global_set_addr_cmd, 7);
  
  byte trigger_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_TRIGGER, 0x00, 0x00, 0x00, 0x00, 0xFF};
  trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);
  
  byte auto_addr_done_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_AUTOADDR_DONE, 0x00, 0x00, 0x00, 0x00, 0xFF};
  auto_addr_done_cmd[7] = crcCalcCRC8(auto_addr_done_cmd, 7);

  byte send_summary[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_SEND_SUMMARY, 0x00, 0x00, 0x00, 0x00, 0xFF};
  send_summary[7] = crcCalcCRC8(send_summary, 7);


  /* SETUP: Ensure EMITTER(TRIGGER_IN)= FLOATING, ANODE(TRIGGER_OUT)= Vdd */
  
  /* Summary should return address 0xFE */
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  /* Change BM address to 0xAB */
  Serial.write(trigger_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(global_set_addr_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(auto_addr_done_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  /* Addressing 0xFE should no longer respond */
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  /* Addressing 0xAB should now respond */
  send_summary[1] = 0xAB;
  send_summary[7] = crcCalcCRC8(send_summary, 7);
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
}

/**
 * @brief See notes
 */
void test10(void)
{
  byte send_summary[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_SEND_SUMMARY, 0x00, 0x00, 0x00, 0x00, 0xFF};
  send_summary[7] = crcCalcCRC8(send_summary, 7);

  /* Send summary with correct CRC */
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  /* Send summary with incorrect CRC */
  send_summary[7] = send_summary[7] + 1 % 0xFF;
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
}

/**
 * @brief See notes
 */
void test11(void)
{
  /* Command definitions */
  byte global_set_addr_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_SET_ADDRESS, 0xAB, 0x00, 0x00, 0x02, 0xFF};
  global_set_addr_cmd[7] = crcCalcCRC8(global_set_addr_cmd, 7);
  
  byte trigger_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFE, CMD_TRIGGER, 0x00, 0x00, 0x00, 0x00, 0xFF};
  trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);
  
  byte auto_addr_done_cmd[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_AUTOADDR_DONE, 0x00, 0x00, 0x00, 0x00, 0xFF};
  auto_addr_done_cmd[7] = crcCalcCRC8(auto_addr_done_cmd, 7);

  byte send_summary[BMS_COMMAND_LEN] = {HEAD, 0xAB, CMD_SEND_SUMMARY, 0x00, 0x00, 0x00, 0x00, 0xFF};
  send_summary[7] = crcCalcCRC8(send_summary, 7);

  byte global_snapshot[BMS_COMMAND_LEN] = {HEAD, 0xFF, CMD_GLOBAL_SNAPSHOT, 0x00, 0x00, 0x00, 0x00, 0xFF};
  global_snapshot[7] = crcCalcCRC8(global_snapshot, 7);
  
  byte send_all_v1_cmd[BMS_COMMAND_LEN] = {HEAD, 0xAB, CMD_SEND_ALL_VOLTAGES_1, 0x00, 0x00, 0x00, 0x00, 0xFF};
  send_all_v1_cmd[7] = crcCalcCRC8(send_all_v1_cmd, 7);


  /* 
   * SETUP: 
   *  - Ensure EMITTER(TRIGGER_IN)= FLOATING, ANODE(TRIGGER_OUT)= Vdd 
   *  - Tie pin 12 to Vdd until you are ready to receive data
   */
  
  pinMode(12, INPUT); /* TEST PIN TO USE AS GATE, connect to ground */

  /* Setup the BM with address 0xAB */
  Serial.write(trigger_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(global_set_addr_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(auto_addr_done_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  while (digitalRead(12) == 0) {continue;} /* Tie pin 12 high when you are ready to move on to next section */

  /* SNAPSHOT */
  Serial.write(global_snapshot, BMS_COMMAND_LEN);
  delayMicroseconds(5000);

  /* Get data back */
  Serial.write(send_all_v1_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(send_summary, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
}

void setup() 
{
  /* Per specifications:
   *   "At the wriiting this document, latest release (V1_C1) use baud rate 
   *    of 115200. In the next release, the serial baud rate will be 230400.
   *    8 data bits, 1 stop bit, no parity are used"
   */
  Serial.begin(230400, SERIAL_8N1);
  delay(1000); // Give the UART a bit of time in case it needs it

  /* Execute a test*/
  test10(); 
}

void loop() 
{
}
