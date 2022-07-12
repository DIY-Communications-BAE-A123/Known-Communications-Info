/**
 * @brief Tests probing multiple BM to establish initial communication.
 * 
 * @Based on  Hale Konopka (jkonopka's work in communicating with one BM
 * @author by Emenike Goodluck
 * 
 * See BM Testing Harness Schematic. Below is overview of testbench specs for which this was written:
 *   - Arduino UNO 
 *       - The baud rate is 230400 bps. Arduino serial monitor do not operate up to this rate. 
 *       - So, you will get useless characters displayed on your screen due to mismatched baud rate
 *   - 40V power supply 
 *      - TOPPOWER STP6005DH), set to V=40, A=0.5A. 
 *      - The BM will nominally pull 0.05A when inactive and 0.15A when in BOOT
 *        state (connect Vdd to arduino), and 0.2A when it is in PRIME state
 *   - Mock BM boards with voltage divider resistor network to simulate the BM
 *   - LHT00SU1 Oscilloscope and logic analyzer to decode the communication (Set this to capture 5M samples at 4MHz)
 *
 * @note This document is not intended to be the most complete and consise verification plan,
 *       but rather capture the key tests which were needed to establish communication with
 *       the BM (which can now be done reliably). Many intermediate functions and hours of
 *       debugging were intermittently used, this is a summary of important tests to complement
 *       notes.
 *       
 *       - The round trip time of the communication between the master and each slave is about 800 microseconds
 *       - So allow a delay of at least 1 millisecond after sending a command before sending another command.
 */

#include "crc8.h" /* crcCalcCRC8() leaked from doc */

#define BMS_COMMAND_LEN 8
#define BM_RESPONSE_LEN 14
#define NO_BMS          3 //Number BM of modules
#define BAUD            230400


/* Packet Definitions */
#define HEAD                     (0x58)
#define GLOBAL_ADDR              (0xff)
#define DEFAULT_ADDR             (0xfe)

/* Command Definitions */
#define CMD_TRIGGER              (0x32)
#define CMD_SET_ADDRESS          (0x3c)
#define CMD_AUTOADDR_DONE        (0x41)
#define CMD_GLOBAL_SNAPSHOT      (0x46)
#define CMD_SEND_SUMMARY         (0x50)
#define CMD_SEND_ALL_VOLTAGES_1  (0xa0)
#define CMD_SEND_ALL_VOLTAGES_2  (0xa1)
#define CMD_SEND_ALL_VOLTAGES_3  (0xa2)
#define CMD_BALANCE_TARGET       (0xaa)
#define CMD_SUSI                 (0xfb)


  
byte TRIGGER_IN   = 13; // Used to set the BM for communication (active low)
byte buzzer       = 7; // Attach Buzzer to pin 7
byte button       = 6; // Attach button to pin 6 (Active high)
byte i            = 0; // Counter variable

//byte addresses[NO_BMS]; //Uncomment to use random addresses

/* 
 *  Note that when using randon generated addresses, the address changes when you reset the arduino 
 *  Therefore, you need to disconnect and reconnect the first BM's VCC pin to restart them.
 *  Random addresses are best when using many BMs
 */

byte addresses[NO_BMS] = {0xda, 0xbb, 0xac}; // comment to use random addresses
word cellVoltages [NO_BMS][12]; // Holds the voltages received from the BMs
word voltageSummaries[NO_BMS][6]; // {Min_voltage, Min_V_Location, Max_Voltage,Max_V_Location, AVG_Voltage,Status}
word temperatures[NO_BMS][2]; // {temp_1, temp_2}

//======================================= Setup Function ===========================================
void setup() 
{
  // generate random addresses for the BMs
  randomSeed(analogRead(A0));
  
  /* Uncomment this block to use random addresses */ 
//  for (i = 0; i<NO_BMS; i++)
//  {
//    addresses[i] = random(0x00, 0xFD); //FE and FF are special addresses
//  }


  pinMode (TRIGGER_IN, OUTPUT);
  pinMode (buzzer, OUTPUT);
  pinMode (button, INPUT);
  
  Serial.begin(BAUD, SERIAL_8N1);
  Serial.setTimeout(2000);
  delay(1000); // Give the UART a bit of time in case it needs it
  
  //Sound buzzer to indicate that setup is complete and is about to assign addresses to the BMs
  digitalWrite(buzzer, HIGH);
  delay(500);
  digitalWrite(buzzer,LOW);
  delay(500);
  digitalWrite(buzzer, HIGH);
  delay(500);
  digitalWrite(buzzer,LOW);
  delay(500);

  // run the startup sequence
  assignAddresses(); //Assign addresses to the BMs

  //sound buzzer to know when it is done
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer,LOW);
  delay(500);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer,LOW);
  delay(500);
  digitalWrite(buzzer, HIGH);
  delay(200);
  digitalWrite(buzzer,LOW);
  delay(500);
  deassertTrigger();
  
}

//========================================== Loop function ===========================
void loop() 
{
  /*
   * Use the button to control which command to send
   */
  if(digitalRead(button) == HIGH) balanceTarget(addresses[0], 3300);
  //if(digitalRead(button) == HIGH)pollForVoltages();
  //if(digitalRead(button) == HIGH)resetTarget(0xff);
 
}

//============================ Functions to control Trigger pin ================================
/* 
 *  During the assignment of addresses, the BM with active TRIGGER_IN captures the address
 *  This means that only one BM should have its TRIGGER_IN set to low
 *  The TRIGGER_IN is active low.
 */
 void assertTrigger()
 {
   digitalWrite(TRIGGER_IN, LOW);
 }

  void deassertTrigger()
 {
   digitalWrite(TRIGGER_IN, HIGH);
 }


//=========================================== Starting up the modules ===========================================
void assignAddresses()
{
  //declare commands
  byte auto_addr_done_cmd[BMS_COMMAND_LEN] = {HEAD, GLOBAL_ADDR, CMD_AUTOADDR_DONE, 0x00, 0x00, 0x00, 0x00, 0x00};
  auto_addr_done_cmd[7] = crcCalcCRC8(auto_addr_done_cmd, 7);
  
  byte global_set_addr_cmd[BMS_COMMAND_LEN] = {HEAD, GLOBAL_ADDR, CMD_SET_ADDRESS, DEFAULT_ADDR, 0x00, 0x00, 0x02, 0x00};
  
  byte trigger_cmd[BMS_COMMAND_LEN] = {HEAD, DEFAULT_ADDR, CMD_TRIGGER, 0x00, 0x00, 0x00, 0x00, 0x00};

  /* 
   * Sound buzzer to indicate when communication starts so that you can start capture 
   * This will help you to get ready for capture as the capture takes only 3 seconds due to the settings
   */
   
  digitalWrite(buzzer,HIGH);
  delay(1000);
  digitalWrite(buzzer,LOW);
  delay(500);

  // Now the assignment of the addresses begins
  for (i = 0; i<NO_BMS; i++)
  {
    /*
     * Since the TRIGGER_IN of the first BM is connected to the arduino, we have to treat it specially
     * At startup, all BMs set their TRIGGER_OUT high. This deactivates the next BM in the chain
     * This ensures that the BM connected to the master activates others in the chain
     * The algorithm is as follows:
     * 1. Assert the trigger of the BM connected to the master
     * 2. Send global set address command with the intended address (Lets say 0xBB). Response is expected with the given address
     * 3. Send trigger command using the address in (2) above (0xBB). This activates the next BM by setting TRIGGER_OUT low
     * No response is expected but the BM responds with address 0xFE
     * 4. Deassert the trigger of the BM connected to the master
     * Now we are done with the first BM. To set the address of others,do the following:
     * 5. Send global set address command with the intended address
     * 6. Send trigger command with the address in (5)
     * 7. Send trigger command with the address of the previous BM
     * 8. Repeat steps 5 to 7 till all BMs have gotten their addresses
     * 9. Send auto address done command to exit addressing mode. No response is expected
     */
    if(i==0)
    {
      //Activate the trigger (Step 1)
      assertTrigger();
      
      //update commands
      global_set_addr_cmd[3] = addresses[i];
      global_set_addr_cmd[7] = crcCalcCRC8(global_set_addr_cmd, 7);

      trigger_cmd[1] = addresses[i];
      trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);


      //set address; response expected (Step 2)
      Serial.write(global_set_addr_cmd, BMS_COMMAND_LEN);
      delay(1);

      
      //Trigger the next BM; No response expected (Step 3)
      Serial.write(trigger_cmd, BMS_COMMAND_LEN); //This activates the next BM
      delay(1);

      //deactivate the first BM so that it won't capture the subsequent addresses (Step 4)
      deassertTrigger();
      delay(1);

    }
    
    // After the first BM has gotten its address, do these:    
    else
    {
      global_set_addr_cmd[3] = addresses[i];
      global_set_addr_cmd[7] = crcCalcCRC8(global_set_addr_cmd, 7);

      trigger_cmd[1] = addresses[i];
      trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);


      //set address; response expected (Step 5)
      Serial.write(global_set_addr_cmd, BMS_COMMAND_LEN);
      delay(1);
      
      //Activate the next BM; no response (You may see a response with address 0xFE) (Step 6)
      Serial.write(trigger_cmd, BMS_COMMAND_LEN); //This activates the next BM
      delay(1);

      //Deactivate this BM by triggering the previous BM; response expected (Step 7)
      trigger_cmd[1] =  addresses[i-1]; //Address the previous BM
      trigger_cmd[7] = crcCalcCRC8(trigger_cmd, 7);

      Serial.write(trigger_cmd, BMS_COMMAND_LEN); 
      delay(1);

    }
      // Repeat till all BMs have gotten addresses
  }

  //Send auto address done command as all BMs now have an address; No response expected (Step 8)
  Serial.write(auto_addr_done_cmd, BMS_COMMAND_LEN);
}

//======================================= Requesting for voltages ==============================================
void pollForVoltages()
{
  byte bm_response_buffer[BM_RESPONSE_LEN]; // Buffer to store the responses from the BM
  memset(bm_response_buffer, 0xff, BM_RESPONSE_LEN);
    /* 
     * Sound buzzer to indicate when communication starts so that we can start capture 
     * This will help you to get ready for capture as the capture takes only 3 seconds due to the settings
   */
  digitalWrite(buzzer, HIGH);
  delay(2000);
  digitalWrite(buzzer,LOW);
  delay(500);

  // Declaring and initializing command packets
  byte global_snapshot[BMS_COMMAND_LEN] = {HEAD, GLOBAL_ADDR, CMD_GLOBAL_SNAPSHOT, 0x00, 0x1A, 0x01, 0x00, 0xFF}; //replace ox011A (283mA) with the current 
  global_snapshot[7] = crcCalcCRC8(global_snapshot, 7);

  byte send_voltages1[BMS_COMMAND_LEN] = {HEAD, DEFAULT_ADDR, CMD_SEND_ALL_VOLTAGES_1, 0x00, 0x00, 0x00, 0x00, 0xFF};
  byte send_voltages2[BMS_COMMAND_LEN] = {HEAD, DEFAULT_ADDR, CMD_SEND_ALL_VOLTAGES_2, 0x00, 0x00, 0x00, 0x00, 0xFF};
  byte send_voltages3[BMS_COMMAND_LEN] = {HEAD, DEFAULT_ADDR, CMD_SEND_ALL_VOLTAGES_3, 0x00, 0x00, 0x00, 0x00, 0xFF};

  //Send command for the BMs to store the voltages in their memory
  Serial.write(global_snapshot, BMS_COMMAND_LEN); //no response
  delay(1);

  // Loop through all the BMs
  for(i=0; i<NO_BMS; i++)
  {
    send_voltages1[1] = addresses[i]; 
    send_voltages1[7] = crcCalcCRC8(send_voltages1, 7);

    send_voltages2[1] = addresses[i]; 
    send_voltages2[7] = crcCalcCRC8(send_voltages2, 7);

    send_voltages3[1] = addresses[i]; 
    send_voltages3[7] = crcCalcCRC8(send_voltages3, 7);

    //Get voltage readings

    /*
      * You can write a function that reads the response and stores it for analysis
      * This may be to display it on an LCD or send it for visualization
      * The reading is not implemented here as it is assumed that you are using a logic analyzer
      * With the received voltages, you can send a balance target command with the target voltage.
    */
    //clearSerialBuffer();
    Serial.write(send_voltages1, BMS_COMMAND_LEN); //There will be response from addressed BM
    Serial.flush();
    delay(1);

    if (Serial.available()>0) 
    {
    // read the incoming byte:
    Serial.print("\nAvailable bytes: ");
    Serial.println(Serial.available(),DEC);
    int response_bytes = Serial.readBytes((byte*)bm_response_buffer, BM_RESPONSE_LEN);
    Serial.print ("Message received for address: ox");
    Serial.println(send_voltages1[1],HEX);
    Serial.print ("{");
    for(int j = 0; j<sizeof(bm_response_buffer); j++)
    {
      Serial.print(bm_response_buffer[j], HEX);
      Serial.print(",");
    }
    Serial.println("}");
    
    byte CRC8 = crcCalcCRC8(bm_response_buffer, BM_RESPONSE_LEN);
    if (response_bytes !=BM_RESPONSE_LEN || CRC8 !=0x00) Serial.println ("Error in message received");
    else
    {
      cellVoltages[i][0] = word(bm_response_buffer[3],bm_response_buffer[2]); // Cell 1 voltage
      cellVoltages[i][1] = word(bm_response_buffer[5],bm_response_buffer[4]); // Cell 2 voltage
      cellVoltages[i][2] = word(bm_response_buffer[7],bm_response_buffer[6]); // Cell 3 voltage
      cellVoltages[i][3] = word(bm_response_buffer[9],bm_response_buffer[8]); // Cell 4 voltage
    }
    }


     // word voltage = word(MSB, LSB)+1; // use this to interprete the voltage
    //clearSerialBuffer();
    memset(bm_response_buffer, 0xff, BM_RESPONSE_LEN); // Reset the buffer
    Serial.write(send_voltages2, BMS_COMMAND_LEN); //There will be response from addressed BM
    Serial.flush();
    delay(1);
    if (Serial.available() > 0) 
    {
    // read the incoming byte:
    int response_bytes = Serial.readBytes((byte*)bm_response_buffer, BM_RESPONSE_LEN);
    byte CRC8 = crcCalcCRC8(bm_response_buffer, BM_RESPONSE_LEN);
    
    if (response_bytes !=BM_RESPONSE_LEN /*|| CRC8 !=0x00*/) 
    {
      Serial.println ("Error in message received");
    }
    else
    {
      cellVoltages[i][4] = word(bm_response_buffer[3],bm_response_buffer[2]); // Cell 5 voltage
      cellVoltages[i][5] = word(bm_response_buffer[5],bm_response_buffer[4]); // Cell 6 voltage
      cellVoltages[i][6] = word(bm_response_buffer[7],bm_response_buffer[6]); // Cell 7 voltage
      cellVoltages[i][7] = word(bm_response_buffer[9],bm_response_buffer[8]); // Cell 8 voltage
    }
    }

    //clearSerialBuffer();
    memset(bm_response_buffer, 0xff, BM_RESPONSE_LEN);
    Serial.write(send_voltages3, BMS_COMMAND_LEN); //There will be response from addressed BM
    Serial.flush();
    delay(1);
    if (Serial.available() > 0) 
    {
    // read the incoming byte:
    int response_bytes = Serial.readBytes((byte*)bm_response_buffer, BM_RESPONSE_LEN);
    byte CRC8 = crcCalcCRC8(bm_response_buffer, BM_RESPONSE_LEN);
    if (response_bytes !=BM_RESPONSE_LEN /*|| CRC8 !=0x00*/) Serial.println ("Error in message received");
    else
    {
      cellVoltages[i][8] = word(bm_response_buffer[3],bm_response_buffer[2]); // Cell 9 voltage
      cellVoltages[i][9] = word(bm_response_buffer[5],bm_response_buffer[4]); // Cell 10 voltage
      cellVoltages[i][10] = word(bm_response_buffer[7],bm_response_buffer[6]); // Cell 11 voltage
      cellVoltages[i][11] = word(bm_response_buffer[9],bm_response_buffer[8]); // Cell 12 voltage
    }
    }

    //clearSerialBuffer();
    memset(bm_response_buffer, 0xff, BM_RESPONSE_LEN);
    sendSummary(addresses[i], 283); // 283 mA as the ESS_Current. Change this value to the actual current
    Serial.flush();
    if (Serial.available() > 0) 
    {
      // read the incoming byte:
      int response_bytes = Serial.readBytes((byte*)bm_response_buffer, BM_RESPONSE_LEN);
      byte CRC8 = crcCalcCRC8(bm_response_buffer, BM_RESPONSE_LEN);
      if (response_bytes !=BM_RESPONSE_LEN /*|| CRC8 !=0x00*/) Serial.println ("Error in message received");
      else
      {
        voltageSummaries[i][0] = word(bm_response_buffer[3],bm_response_buffer[2])+1; // Minimum cell Voltage
        voltageSummaries[i][1] = (uint8_t)(bm_response_buffer[8]<< 4) >> 4; // Minimum cell voltage location (Lower nibble)
        voltageSummaries[i][2] = word(bm_response_buffer[5],bm_response_buffer[4])+1; // Maximum cell voltage
        voltageSummaries[i][3] = (uint8_t)bm_response_buffer[8] >> 4; // Maximum cell voltage Location (upper nibble)
        voltageSummaries[i][4] = word(bm_response_buffer[7],bm_response_buffer[6])+1; // Average voltage
        voltageSummaries[i][5] = bm_response_buffer[12]; //status
        
        /*
         * Calculating the temperatures is a bit tricky as they are represented using 12 bits each and combined to form 24 bit data
         * Temperature sensor 1 reading is contained in the lower 12 bits while
         * Temprature sensor 2 reading is contained in the upper 12 bits
         * Since message is received as lsb-first, if we received {0xAB, 0xCD, 0xEF}, it is acually 0xEFCDAB when written
         * In this case, temp 1 sensor reading is 0xDAB and temp 2 reading is 0xEFC
         * Interpreting the received temperature is as follows:
         */
        word temp1_lsb = bm_response_buffer[9]; //lsb-first
        word temp1_msb = (uint8_t)(bm_response_buffer[10]<<4) >> 4;  //lsb of response byte[10]
        word temp1 = temp1_msb << 4 | temp1_lsb;;
        temperatures[i][0] = temp1; // Store the temperature reading

        word temp2_msb = bm_response_buffer[11]; //lsb-first
        word temp2_lsb = (uint8_t)bm_response_buffer[10] >> 4;//msb of response byte[10]
        word temp2 = temp2_msb << 4 | temp2_lsb;
        temperatures[i][1] = temp2;        
        
      }
     }        

  }
}

// ================================== Balance target function ==================================
void balanceTarget(byte address, word target)
{
  /* 
   *  This command tells the BM to maintain the voltage of the batteries within the specified target voltage
   *  This is best when using a battery module otherwise it will start balancing the resistors
   *  If you are using the mock battery module, run resetTarget shortly after running this
 */
 digitalWrite(buzzer,HIGH);
 delay(1000);
 digitalWrite(buzzer, LOW);
  // address can be in hex or dec. target is in millivolts
  byte lsb_byte = lowByte(target-1);
  byte msb_byte = highByte(target-1); 
  byte balance_target[BMS_COMMAND_LEN] = {HEAD, address, CMD_BALANCE_TARGET, 0x00, lsb_byte, msb_byte, 0x00, 0xFF};
  balance_target[7] = crcCalcCRC8(balance_target, 7);
  
  /* Send balance target */
  Serial.write(balance_target, BMS_COMMAND_LEN);
  Serial.flush();
  delay(1);
}

// ============================================ Reset Target Function ===========================================
void resetTarget(byte address)
{
  /* 
   *  This function commands the BM to stop balancing the cells
   *  The reset target voltage is 4096 mV = 0x0FFF (That is 4096 -1) 
   *  The N-1 has been taken care of in the balanceTarget() function
   */
  balanceTarget(address, 4096);
}

// ========================================== Send summarry function ===========================================
void sendSummary(byte address, word Ess_Current)
{
  /*
   * This commands the BM to send the sumarry of its status
   * It returns the minimun, maximum and average voltages of the cells
   * It also returns the temperature readings from the temperature sensors
   * Use this function to make fast decisions  and to get temperature readings
   */
 
  // address can be in hex or dec. Ess_Current is in milliamps. It is the current entering or leaving the pack
  byte lsb_byte = lowByte(Ess_Current-1);
  byte msb_byte = highByte(Ess_Current-1); 
  byte send_summary[BMS_COMMAND_LEN] = {HEAD, address, CMD_SEND_SUMMARY, 0x00, lsb_byte, msb_byte, 0x00, 0xFF};
  send_summary[7] = crcCalcCRC8(send_summary, 7);

  /* Send summary */
  Serial.write(send_summary, BMS_COMMAND_LEN);
  Serial.flush();
  delay(1);
}

/*
 * You can write a function that displays the voltages here.
 * Note that Arduino does not decode the BM responses correctly
 */
