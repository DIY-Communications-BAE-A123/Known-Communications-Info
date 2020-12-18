# Known-Communications-Info
Information, referenced to photographs, about the physical and other layers of the communications arrangement between outside devices and the battery conditioner box, and between the battery conditioner box and the module boards.

As A123 is no longer in operation, and the equipment was designed in 2008, documentation is difficult to find. The strongest form of documentation is one level up in the architecture, viewing the ESS as a device to be troubleshot, in the file "BAE HDS System ...pdf" (this repository). It has diagnostic procedures for diagnosing  communications problems. Note in particular page 196, pg 208.

Insights may be gleaned from comments on related BMS made by the same manufacturer and general communications practice.

See end of thread here:nissan leaf BMS
https://mit-evt.blogspot.com/2010/07/talking-to-batteries.html#comment-form

Same Nissam Leaf BMS, comment on pins:
pin1...VCC
pin2...VCC
pin3...CAN HI
pin4...CAN LO
pin5...CAN LO
pin6...CAN HI
pin7...GND
pin8...GND
pin9...???
pin10...???

comment that 9 and 10 are fault status..
https://endless-sphere.com/forums/viewtopic.php?t=43564

## BM Communication
Hale Konopka was able to establish communication between an Arduino UNO and BM to read cell voltages and summary information using the document "BMS_Submodule Communication Protocol v1" as reference. This specification is an earlier version but contained enough information to piece together the protocol. This communication will be explained here, while relevant documents can be found in /BM_Comm

### Overview <br>
The physical interface used to communicate with the BM includes 
- Vdd= 5V
- GND
- 5V UART= Tx and Rx at 230400 baud, 8 data bits, 1 stop bit, no parity, idle at 5V, BM only pulls down
- TRIGGER_OUT= 5V digital output
- TRIGGER_IN= 5V digital input

### Notes from testing
- The spec provides the following description of the Communication Physical layer: 
  > "At the writing this document, latest release (V1_1C) use baud rate of 115200. In the next release, the serial baud rate will be 230400. 8 data bits, 1 stop bit, no parity are used." 

  However, from testing the BM will not respond to 115200 baud. When 230400 is used, the device responds.

- BMS_Connectors.png shows an anode and emitter pin. These appear to be wrong and the "emitter" pin is instead used as TRIGGER_OUT and "anode" as TRIGGER_IN. When using these pins in this way, GND=ASSERTED while Vdd(5V)=DEASSERTED

- Upon Boot, if TRIGGER_IN is asserted(gnd), the BM will send a packet with address 0xFE, 415ms after Vdd is connected. When TRIGGER_IN is deasserted(5V), no packet will be sent. 

- Upon Boot, regardless of the state of TRIGGER_IN, the BM adopts the address 0xFE as its default and wakes up in BOOT mode in which it will respond to SEND_SUMMARY and GLOBAL_SET_ADDRESS, but won't send any measurements back.

- In order to get the BM to send meaningful information, it must be put in PRIME mode.

- To wake the BM from BOOT into PRIME mode, the following startup sequence must be followed:
  > 0. Assert TRIGGER_IN (pull down to GND). 
  > 2. Send TRIGGER_CMD over UART: Use default address 0xFE
  > 3. Send GLOBAL_SET_ADDRESS over UART: Use whatever new address you wish, and set "Start Up Mode" to Prime=0x2
  > 4. Send AUTO_ADDRESSING_DONE over UART

- Once the BM is in prime mode, the remainder of the commands such as GLOBAL_SNAPSHOT, SEND_ALL_VOLTAGES, BALANCE_TARGET (as of yet untested), and SEND_SUMMARY can be used and the results will be sent back of the BM's Tx. 

- While using an oscilloscope to decode UART signals, using a baud rate of exactly 230400 failed to decode some of the bytes sent back from the BM. Upon looking closely at the waveforms, if you set cursors on either end of the shortest pulse, the frequency is around 225000 Hz (i.e. 225000 baud) as opposed to the expected 230400 Hz. Setting the oscilloscope to decode at this frequency appears to correctly decode the response bytes (so that they are all consistent with what we would expect from the datasheet). This is especially odd because the arduino transmits aat 230400 baud which the BM correctly interprets, yet the responses seem to be at 225000 baud. Presumably related to this, the arduino is unable to correctly resconstruct the message on its serial input at 230400 baud. Figuring out why this is and how to get the Arduino to read this data correctly is an area for future investigation.