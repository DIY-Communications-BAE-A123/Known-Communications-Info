/**
 * @brief Read cell voltages from BM.
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
 * 
 * @bugs While probing via oscilloscope and UART decoding, the cell voltages returned
 *       from the BM to arduino do appear correct but I am not yet able to get the
 *       arduino to receive these results. My best guess is that something
 *       funny is happening with the serial receive (like perhaps the baud rates are not
 *       exactly matched, because the arduino baud is 230400 but on the oscilloscope I 
 *       need to lower this to 225000 in order to interpret the bits correctly). One
 *       possible solution which I have not yet been able to try is add some capacitance
 *       to Tx and Rx to slow down the rise time.
 */

#include "crc8.h" /* crcCalcCRC8() leaked from doc */

/* Pin definitions */
#define BM_TRIGGER_IN            12
#define BM_TRIGGER_OUT           13

/* Packet Definitions */
#define BMS_COMMAND_LEN          8 
#define BM_RESPONSE_LEN          14
#define HEAD                     (0x58)
#define GLOBAL_ADDR              (0xff)
#define DEFAULT_ADDR             (0xfe)
#define NEW_ADDR                 (0xab) // This is arbitrary, set to whatever you want

/** 
 *  Command Definitions
 *  Per Protocol Spec document: 
 *    "The communications physical layer consists of a single BMS connected to 
 *     multiple BMs. The communication is via a multi-drop +5V TTL serial UART
 *     bus.... baud rate of 1152200... 8 data bits, 1 stop bit, no parity bit"
 *  
 *  BMS Command Format:
 *   0: HEAD, fixed 0x58
 *   1: ADDR, addr of intented module or 0xFF for Global Address
 *   2: Op Code, Identifies each command
 *   3: Param 1, Op code dependent
 *   4: Param 2, Op code dependent
 *   5: Param 2, Op code dependent
 *   6: padding, irrelevant
 *   7: CRC, CRC calculated from the previous 7 bytes and used for transmission error checking
 */
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

/* Startup States */
#define BOOT_MODE                (0x01)
#define PRIME_MODE               (0x02)
#define RESET_ONLY_MODE          (0x03)

/* Bitfield indexes for readability*/
#define CELL_A_LSB_IDX           (2)
#define CELL_A_MSB_IDX           (3)
#define CELL_B_LSB_IDX           (4)
#define CELL_B_MSB_IDX           (5)
#define CELL_C_LSB_IDX           (6)
#define CELL_C_MSB_IDX           (7)
#define CELL_D_LSB_IDX           (8)
#define CELL_D_MSB_IDX           (9)

/**
 * @brief Prime the Battery Manager
 * 
 * Upon boot:
 *   - When the BM first wakes up, it has amnesia and does not know who it is.
 *   - If TRIGGER_IN is pulled up to Vdd (5V), this is the "deasserted" state
 *     and 415ms after Vdd is connected, the BM will NOT send a packet
 *   - If TRIGGER_IN is pulled down to GND, this is the "asserted" state
 *     and 415ms after Vdd is connected, the BM will send a packet with 
 *     with its address of 0xFE.
 *   - Regardless of the state of TRIGGER_IN, the BM seems to adopt 0xFE by
 *     default because in either case, SEND_SUMMARY with address 0xFE will
 *     return a response
 *   - When the BM first boots, it is in a sort of "dormant" state and 
 *     will not respond to any SEND_ALL_VOLTAGE or BALANCE_TARGET
 *     commands. It does respond to SEND_SUMMARY but the information
 *     contained is all zeroed and thus does not appear meaningful.
 *     
 * Addressing
 *   - Since the BM has limited capability when it first boots, it must be
 *     given an address along with a start up mode before it can balance 
 *     cells or send interesting information
 *   - Addressing is accomplished by first pulling TRIGGER_IN down to 
 *     GND ("asserted" state), then sending the following packets. The
 *     exact contents of these packets can be seen in their definitions 
 *     below. The "BMS_Submodule Communication Protocol v1" provides
 *     an explanation of these bitfields.
 *     
 *     1) TRIGGER
 *     2) SET_ADDRESS
 *     3) AUTO_ADDR_DONE
 * 
 * After these commands have been sent, the BM will boot into 
 */
void BM_setup(void)
{
  /* Command definitions */
  byte global_set_addr_cmd[BMS_COMMAND_LEN] = 
  {
    HEAD,
    GLOBAL_ADDR, 
    CMD_SET_ADDRESS, 
    NEW_ADDR, 
    0x00, 
    0x00, 
    PRIME_MODE, 
    0xFF // Will be overwritten by CRC
  };
  global_set_addr_cmd[7] = crcCalcCRC8(global_set_addr_cmd, 7);
  
  byte trigger_cmd[BMS_COMMAND_LEN] = 
  {
    HEAD, 
    DEFAULT_ADDR,
    CMD_TRIGGER, 
    0x00, 
    0x00, 
    0x00, 
    0x00, 
    0xFF // Will be overwritten by CRC
  };
  trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);
  
  byte auto_addr_done_cmd[BMS_COMMAND_LEN] = 
  {
    HEAD, 
    GLOBAL_ADDR, 
    CMD_AUTOADDR_DONE, 
    0x00, 
    0x00,
    0x00, 
    0x00, 
    0xFF  // Will be overwritten by CRC
  };
  auto_addr_done_cmd[7] = crcCalcCRC8(auto_addr_done_cmd, 7);

  /* Pull TRIGGER_IN to GND */
  digitalWrite(BM_TRIGGER_IN, LOW);

  /* Give name to BM */
  Serial.write(trigger_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000); // This delay is arbitrary
  Serial.write(global_set_addr_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(auto_addr_done_cmd, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
}


/**
 * @brief Receive voltages from BM.
 * 
 * See Section 3.2.5 of "BMS_Submodule Communiction Protocol v1".
 * 
 * @param opcode  Opcode to use for sending cell voltages, per spec:
 * 
 *                Opcode | Logical Cell | Phsical Cell
 *                ------------------------------------
 *                160    | Cell A       | Cell 1
 *                       | Cell B       | Cell 2
 *                       | Cell C       | Cell 3
 *                       | Cell D       | Cell 4
 *                ------------------------------------
 *                161    | Cell A       | Cell 5
 *                       | Cell B       | Cell 6
 *                       | Cell C       | Cell 7
 *                       | Cell D       | Cell 8               
 *                ------------------------------------
 *                162    | Cell A       | Cell 9
 *                       | Cell B       | Cell 10
 *                       | Cell C       | Cell 11
 *                       | Cell D       | Cell 12
 *                       
 * @param a__mV   Pointer into which voltage of cell a will be written.
 * @param b__mV   Pointer into which voltage of cell a will be written.
 * @param c__mV   Pointer into which voltage of cell a will be written.
 * @param d__mV   Pointer into which voltage of cell a will be written.
 * 
 * @return Returns 0 upon success, -1 upon failure. If the failure occurs due to issues
 *         sending/receiving packets to the BM, the inputs are all set to 0xFFFF = 65535 mV
 *         which should not be physically possible for the device.
 */
int BM_voltages(
  byte opcode,
  uint16_t *a__mV, 
  uint16_t *b__mV, 
  uint16_t *c__mV, 
  uint16_t *d__mV
) {
  uint8_t bm_response[BM_RESPONSE_LEN];
  memset(bm_response, 0xFF, BM_RESPONSE_LEN); // Fill with 0xFF indicating invalid
  
  if (a__mV == NULL || b__mV == NULL || c__mV == NULL || d__mV == NULL || opcode < 160 || opcode > 162)
  { /* Error check inputs */
    char pbuf[512];
    sprintf(pbuf, "func %s : Line %d: Invalid input\n", __func__, __LINE__);
    Serial.print(pbuf);
    return -1;
  }

  /* Command definitions */
  byte global_snapshot[BMS_COMMAND_LEN] = 
  {
    HEAD, 
    GLOBAL_ADDR, 
    CMD_GLOBAL_SNAPSHOT, 
    0x00, 
    0x00, 
    0x00, 
    0x00, 
    0xFF  // Will be overwritten by CRC
  };
  global_snapshot[7] = crcCalcCRC8(global_snapshot, 7);
  
  byte send_all_v1_cmd[BMS_COMMAND_LEN] = 
  {
    HEAD, 
    NEW_ADDR, 
    opcode, 
    0x00, 
    0x00, 
    0x00, 
    0x00, 
    0xFF  // Will be overwritten by CRC
  };
  send_all_v1_cmd[7] = crcCalcCRC8(send_all_v1_cmd, 7);

  /* Ask BM to snapshot, then relay voltages */
  Serial.write(global_snapshot, BMS_COMMAND_LEN);
  delayMicroseconds(5000);
  Serial.write(send_all_v1_cmd, BMS_COMMAND_LEN);

  /* Parse response */
  int bytes_read = Serial.readBytes((byte*)bm_response, BM_RESPONSE_LEN);

  /* Check if all bytes were received + correct CRC */
  char crc8 = crcCalcCRC8(bm_response, BM_RESPONSE_LEN-1);
  if (bytes_read != BM_RESPONSE_LEN || crc8 != bm_response[BM_RESPONSE_LEN-1])
  {
    /* Print failure message */
    char pbuf[512];
    sprintf(
      pbuf, 
      "\nBM msg receive failed, got %d/%d bytes.\n"
      "Message= {0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x}.\n"
      "(0xff indicates unreceived byte)\n",
      bytes_read, BM_RESPONSE_LEN,
      bm_response[0],  bm_response[1], bm_response[2], bm_response[3],
      bm_response[4],  bm_response[5], bm_response[6], bm_response[7],
      bm_response[8],  bm_response[9], bm_response[10], bm_response[11],
      bm_response[12], bm_response[13]
     );
    Serial.print(pbuf);

    /* Write invalid bytes to user */
    *a__mV = 0xFFFF;
    *b__mV = 0xFFFF;
    *c__mV = 0xFFFF;
    *d__mV = 0xFFFF;
    
    return -1;
  }
  else
  {
    *a__mV = bm_response[CELL_A_MSB_IDX] << 8 | bm_response[CELL_A_LSB_IDX];
    *b__mV = bm_response[CELL_B_MSB_IDX] << 8 | bm_response[CELL_B_LSB_IDX];
    *c__mV = bm_response[CELL_C_MSB_IDX] << 8 | bm_response[CELL_C_LSB_IDX];
    *d__mV = bm_response[CELL_D_MSB_IDX] << 8 | bm_response[CELL_D_LSB_IDX]; 
    
    return 0;
  }
}

void setup() 
{
  /* Per specifications:
   *   "At the wriiting this document, latest release (V1_C1) use baud rate 
   *    of 115200. In the next release, the serial baud rate will be 230400.
   *    8 data bits, 1 stop bit, no parity are used"
   *    
   *    From testing, it was discovered that UART operates at 230400 baud.
   */
  Serial.begin(230400, SERIAL_8N1);
  delay(1000); // Give the UART a bit of time in case it needs it
  pinMode(BM_TRIGGER_IN, OUTPUT);
  pinMode(BM_TRIGGER_OUT, INPUT);

  BM_setup();
}

void loop() 
{
  delay(1500); // Poll as frequently as you like

  uint16_t cell_01__mV;
  uint16_t cell_02__mV;
  uint16_t cell_03__mV;
  uint16_t cell_04__mV;
  uint16_t cell_05__mV;
  uint16_t cell_06__mV;
  uint16_t cell_07__mV;
  uint16_t cell_08__mV;
  uint16_t cell_09__mV;
  uint16_t cell_10__mV;
  uint16_t cell_11__mV;
  uint16_t cell_12__mV;

  BM_voltages(CMD_SEND_ALL_VOLTAGES_1, &cell_01__mV, &cell_02__mV, &cell_03__mV, &cell_04__mV);
  BM_voltages(CMD_SEND_ALL_VOLTAGES_2, &cell_05__mV, &cell_06__mV, &cell_07__mV, &cell_08__mV);
  BM_voltages(CMD_SEND_ALL_VOLTAGES_3, &cell_09__mV, &cell_10__mV, &cell_11__mV, &cell_12__mV);

  char print_buf[512];
  sprintf(
    print_buf, 
    "\nCell voltages:\n"
    "  Cell 01=%u mV, Cell 02=%u mV, Cell 03=%u mV, Cell 04=%u mV\n" 
    "  Cell 05=%u mV, Cell 06=%u mV, Cell 07=%u mV, Cell 08=%u mV\n"
    "  Cell 09=%u mV, Cell 10=%u mV, Cell 11=%u mV, Cell 12=%u mV}\n\n",
    cell_01__mV, cell_02__mV, cell_03__mV, cell_04__mV,
    cell_05__mV, cell_06__mV, cell_07__mV, cell_08__mV,
    cell_09__mV, cell_10__mV, cell_11__mV, cell_12__mV
   );
   Serial.print(print_buf);
}
