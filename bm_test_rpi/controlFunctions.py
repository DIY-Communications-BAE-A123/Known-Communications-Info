''' this file specifies the functions required to communicate with the BMs.
# The BMs are connected in daisy chain architecture
# If you want to simulate the system without having any connection with the actual device,
# set the Simulation flag to True; else set it to False.
# This code works with three BMs only. Adjust the code to match what you have
# 
# @Author: Emenike Goodluck
# @Email: emenikegoodluck09@gmail.com
'''

# ================================== Import modules ======================================
import random
from crc8 import crcCalcCRC8 as getCRC
import time
import serial
import gpiozero as gpio
import numpy as np

# ======================== Declarations and initializations ===========================
trigger_in = gpio.Buzzer(23) # Connect the TRIGGER_IN to GPIO 23
buzzer = gpio.Buzzer(25) # Connect the Buzzer to GPIO 25
NO_BMS = 3 # Number of BMs. 
addresses = [0xad, 0xbc, 0xde] # Address of the BMs. You can 
BMS_COMMAND_LEN = 8
BM_RESPONSE_LEN = 14

Ess_current = 285 # This is the current through the system in mA

simulate = False # Set to True if you are not using the real device

if simulate == False:
    # Configure serial port
    serialPort = serial.Serial("/dev/serial0", baudrate=230400, bytesize=serial.EIGHTBITS, timeout=1)


# ==================== Packet definitions =====================
HEAD            = 0X58
GLOBAL_ADDR     = 0xff
DEFAULT_ADDR    = 0xfe

#===================== Command definitions ===========================

CMD_TRIGGER             = (0x32)
CMD_SET_ADDRESS         = (0x3c)
CMD_AUTOADDR_DONE       = (0x41)
CMD_GLOBAL_SNAPSHOT     = (0x46)
CMD_SEND_SUMMARY        = (0x50)
CMD_SEND_ALL_VOLTAGES_1 = (0xa0)
CMD_SEND_ALL_VOLTAGES_2 = (0xa1)
CMD_SEND_ALL_VOLTAGES_3 = (0xa2)
CMD_BALANCE_TARGET      = (0xaa)
CMD_SUSI                = (0xfb)

# --------------------------------- FUNCTIONS -----------------------------------------------

# ======================== Trigger control functions ===========================================
def assertTrigger():
    trigger_in.off() # Active low. This activates the BM

def deassertTrigger():
    trigger_in.on() # Deactivates the BM


# ======================== Assign addresses to the BMs ==========================================
def assignAddresses():
    if simulate == True:
        for i in range(len(addresses)):
            print("Sending address ", hex(addresses[i]), " to BM ",i)
    else:

        auto_addr_done_cmd = [HEAD, GLOBAL_ADDR, CMD_AUTOADDR_DONE, 0x00, 0x00, 0x00, 0x00, 0x00]
        auto_addr_done_cmd[7] = getCRC(auto_addr_done_cmd,7)
        trigger_cmd = [HEAD, DEFAULT_ADDR, CMD_TRIGGER, 0x00, 0x00, 0x00, 0x00, 0x00]
        global_set_addr_cmd = [HEAD, GLOBAL_ADDR, CMD_SET_ADDRESS, DEFAULT_ADDR, 0x00, 0x00, 0x02, 0x00]
        # Sound buzzer to indicate transmittion of data
        buzzer.on()
        time.sleep(1)
        buzzer.off()
        time.sleep(0.5)
        serialPort.reset_input_buffer()
        for i in range(NO_BMS):

            # If we are communicating with the first BM, do these
            if i==0:
                # Activate the first BM
                assertTrigger()
                time.sleep(0.01) #10 ms to ensure that the command is received

                #update the commands 
                global_set_addr_cmd[3] = addresses[i]          
                global_set_addr_cmd[7] = getCRC(global_set_addr_cmd,7)
                               
                trigger_cmd[1] = addresses[i]
                trigger_cmd[7] = getCRC(trigger_cmd,7)

                # Assign address to the first BM, response expected         
                print("Sending global address to BM ",i)
                print('Bytes written = ', serialPort.write(global_set_addr_cmd))
                time.sleep(0.001) # 1 millisecond

                #------ Uncomment this block to print received values; used in debugging ------------
                #print('Amount received = ',serialPort.inWaiting())
                #bm_response_buffer = serialPort.read(BM_RESPONSE_LEN)
                #print('Received bytes = ', list(hex(c) for c in bm_response_buffer),'data left = ', serialPort.inWaiting())
                
                # Trigger the next BM; No response expected
                serialPort.write(trigger_cmd)
                time.sleep(0.001)

                # Deactivate the first BM
                deassertTrigger()
                time.sleep(0.01) # 10 ms to ensure that the trigger is deasserted

            # To assign address to the rest of the BMs, do these
            else:
                #update the commands                 
                global_set_addr_cmd[3] = addresses[i]         
                global_set_addr_cmd[7] = getCRC(global_set_addr_cmd,7)
                
                trigger_cmd[1] = addresses[i]
                trigger_cmd[7] = getCRC(trigger_cmd,7) 

                # Assign address to the current BM, response expected         
                print("Sending global address to BM ",i)
                serialPort.write(global_set_addr_cmd)
                time.sleep(0.001) # 1 millisecond
               
                # Command this BM to activate the next BM
                serialPort.write(trigger_cmd)
                time.sleep(0.001) # 1 millisecond

                # Tell the previous BM to deactivate this BM
                trigger_cmd[1] = addresses[i-1]
                trigger_cmd[7] = getCRC(trigger_cmd,7)
                serialPort.write(trigger_cmd)
                time.sleep(0.001) # 1 millisecond
        # Inform the BMs that addresses have been assigned        
        serialPort.write(auto_addr_done_cmd)
        time.sleep(0.01) # 10 millisecond
        print("Addresses assigned to BMs.")

# ============================== Send global snapshot command ===============================
def globalSnapshot(Ess_curr):

    # Strip the current into bytes
    lsb_byte = Ess_curr & 0xff
    msb_byte = (Ess_curr >> 8) & 0xff
    # Send global snapshot command            
    global_snapshot = [HEAD, GLOBAL_ADDR, CMD_GLOBAL_SNAPSHOT, 0x00, lsb_byte, msb_byte, 0x00, 0xFF] 
    global_snapshot[7] = getCRC(global_snapshot, 7)
    serialPort.write(global_snapshot)
    time.sleep(0.01) # 10 ms

# ============================== Get voltage readings =======================================
def pollForVoltages(address):
    bm_response_buffer = np.zeros(BM_RESPONSE_LEN, dtype = np.uint8)
    allVoltages = np.zeros(12, dtype = np.uint16)
    send_voltage = bytearray([HEAD, address, CMD_SEND_ALL_VOLTAGES_1, 0x00, 0x00, 0x00, 0x00, 0xFF])
    # Send voltage command
    if simulate == False:
        # Send global snapshot command
        globalSnapshot(Ess_current)
        for j in range(3):
            # Update voltage command
            send_voltage[2] = CMD_SEND_ALL_VOLTAGES_1 + j 
            send_voltage[7] = getCRC(send_voltage, 7)

            serialPort.reset_input_buffer()
            #print ("Command sent = ", list(hex(c)for c in send_voltage))
            serialPort.write(send_voltage)
            time.sleep(0.001) # 10 ms
            #print('Amount received = ',serialPort.inWaiting())
            bm_response_buffer = serialPort.read(BM_RESPONSE_LEN)
            #print('Received bytes = ', list(hex(c) for c in bm_response_buffer),'data left = ', serialPort.inWaiting())

            # Check for correctness of the message received
            if len(bm_response_buffer) == BM_RESPONSE_LEN and getCRC(bm_response_buffer,BM_RESPONSE_LEN)==0:
                #print("Response is correct")

                # Get cell voltages
                cellA = np.uint16(0)
                cellA = bm_response_buffer[3] & 0xff 
                cellA = (cellA << 8) | bm_response_buffer[2]

                cellB = np.uint16(0)
                cellB = bm_response_buffer[5] & 0xff
                cellB = (cellB << 8) | bm_response_buffer[4]

                cellC = np.uint16(0)
                cellC = bm_response_buffer[7] & 0xff
                cellC = (cellC << 8) | bm_response_buffer[6]

                cellD = np.uint16(0)
                cellD = bm_response_buffer[9] & 0xff
                cellD = (cellD << 8) | bm_response_buffer[8]
                # print(hex(cellA))
                # print(hex(cellB))
                # print(hex(cellC))
                # print(hex(cellD))
                cellVoltages = [cellA,cellB,cellC,cellD]
                # Store the cell voltages
                for k in range(4):
                    allVoltages[j*4+k] = cellVoltages[k]
            else:
                print("Error in response received")   
            
    else:
        for j in range(3):
            #print("Simulated voltages are being published for voltage CMD ", j)
            bm_response_buffer[0] = HEAD
            bm_response_buffer[1] = address
            for i in range(2,10):
                if i%2 == 0:
                    bm_response_buffer[i] = random.randint(0,255)
                else:
                    bm_response_buffer[i] = random.randint(0,15)
            for i in range(10,14):
                bm_response_buffer[i] = 0
            bm_response_buffer[13] = getCRC(bm_response_buffer,13)

            bm_response_buffer = bytearray(bm_response_buffer)
            #print(bm_response_buffer)
            #print("Response buffer = ", list(hex(c) for c in bm_response_buffer))

            # Check for correctness of the message received
            if len(bm_response_buffer) == BM_RESPONSE_LEN and getCRC(bm_response_buffer,BM_RESPONSE_LEN)==0:
                #print("Response is correct")

                # Get cell voltages
                cellA = np.uint16(0)
                cellA = bm_response_buffer[3] & 0xff 
                cellA = (cellA << 8) | bm_response_buffer[2]

                cellB = np.uint16(0)
                cellB = bm_response_buffer[5] & 0xff
                cellB = (cellB << 8) | bm_response_buffer[4]

                cellC = np.uint16(0)
                cellC = bm_response_buffer[7] & 0xff
                cellC = (cellC << 8) | bm_response_buffer[6]

                cellD = np.uint16(0)
                cellD = bm_response_buffer[9] & 0xff
                cellD = (cellD << 8) | bm_response_buffer[8]
                # print(hex(cellA))
                # print(hex(cellB))
                # print(hex(cellC))
                # print(hex(cellD))
                cellVoltages = [cellA,cellB,cellC,cellD]
                # Store the cell voltages
                for k in range(4):
                    allVoltages[j*4+k] = cellVoltages[k] + 1 # Add 1 to compensate for the binary representation of the voltages
            else:
                print("Error in response received")
    return allVoltages

# ========================= Balance target function =================================================

def balanceTarget (addr, target):
    print("Sending balance target command to address ",hex(addr)," with a target of ", target)
    buzzer.on()
    time.sleep(0.5)
    buzzer.off()
    time.sleep(0.5)
    ack = False
    tg = target -1
    lsb_byte = tg & 0xff
    msb_byte = (tg >> 8) & 0xff
    balance_target= bytearray([HEAD, addr, CMD_BALANCE_TARGET, 0x00, lsb_byte, msb_byte, 0x00, 0xFF])
    balance_target[7] = getCRC(balance_target, 7)
    
    # Send balance target 
    if simulate == False: 
        serialPort.reset_input_buffer()
        serialPort.write(balance_target)
        time.sleep(0.001)

        # Check if the command was received
        if addr != 0xff and serialPort.inWaiting() == 14: ack = True
        elif addr == 0xff: ack = True
        else: ack = False

    else: 
        ack = True
        print("Balance target command sent to address ",hex(addr), "with target ",target,"mV is\n", 
        list(hex(c) for c in balance_target))
    return ack

# ============================== Reset target function ===========================================
def resetTarget(addr):
    """
    * This function commands the BM to stop balancing the cells
    *  The reset target voltage is 4096 mV = 0x0FFF (That is 4096 -1) 
    *  The N-1 has been taken care of in the balanceTarget() function

    """
    print("Reseting target command to address ",hex(addr))
    return balanceTarget(addr,4096)
    
    
# ============================= Send Summary command ===================================
def sendSummary(addr, Ess_curr):
    print("Sending summary command to address ",hex(addr)," with system current of ", Ess_curr)
    buzzer.on()
    time.sleep(0.5)
    buzzer.off()
    time.sleep(0.5)
    lsb_byte = Ess_curr & 0xff
    msb_byte = (Ess_curr >> 8) & 0xff
    send_summary= bytearray([HEAD, addr, CMD_SEND_SUMMARY, 0x00, lsb_byte, msb_byte, 0x00, 0xFF])
    send_summary[7] = getCRC(send_summary, 7)
    bm_response_buffer = np.zeros(BM_RESPONSE_LEN, dtype = np.uint8)
    summary = np.zeros(8, dtype = np.uint16)

    #print('Summary Command sent = ', list(hex(c) for c in send_summary))
    
    # Send balance target 
    if simulate == False: 
        serialPort.reset_input_buffer()
        # Send global snapshot command            
        #globalSnapshot(Ess_current)

        # Send summary command
        serialPort.write(send_summary)
        time.sleep(0.001)
        #print('Amount received from send summary= ',serialPort.inWaiting()," bytes")
        bm_response_buffer = serialPort.read(BM_RESPONSE_LEN)
        #print('Received bytes = ', list(hex(c) for c in bm_response_buffer),'data left = ', serialPort.inWaiting())

        # Check for correctness of the message received
        if len(bm_response_buffer) == BM_RESPONSE_LEN and getCRC(bm_response_buffer,BM_RESPONSE_LEN)==0:
            #print("Response is correct")

            # Minimum cell voltage
            minV = np.uint16(0)
            minV = bm_response_buffer[3] & 0xff
            minV = ((minV << 8) | (bm_response_buffer[2] & 0xff))

            # Maximum cell voltage
            maxV = np.uint16(0)
            maxV = bm_response_buffer[5] & 0xff
            maxV = ((maxV << 8) | (bm_response_buffer[4] & 0xff))

            # Average cell voltage
            avgV = np.uint16(0)
            avgV = bm_response_buffer[7] & 0xff
            avgV = ((avgV << 8) | (bm_response_buffer[6] & 0xff))

            # Minimum voltage cell location
            minVLoc = bm_response_buffer[8] & 0xf # lower nibble

            # Maximum voltage cell location
            maxVLoc = (bm_response_buffer[8]>>4) & 0xf # Upper nibble

            # Temperature sensor 1
            temp1 = np.uint16(0)
            temp1 = bm_response_buffer[10] & 0xf
            temp1 = (temp1 << 8) | (bm_response_buffer[9] & 0xff)

            # Temperature sensor 2
            temp2 = np.uint16(0)
            temp2 = bm_response_buffer[11] & 0xff
            temp2 = (temp2 << 4) | (bm_response_buffer[10] >> 4)

            # Status
            status = bm_response_buffer[12]

            summary = [minV,minVLoc,maxV,maxVLoc,avgV,temp1,temp2,status]
        else:
            print("Error in response received")
   
    else: 
        # Create a mock summary response if we are simulating
        bm_response_buffer[0] = HEAD
        bm_response_buffer[1] = addr
        for i in range(2,8):
            if i%2 == 0:
                bm_response_buffer[i] = random.randint(0,255)
            else:
                bm_response_buffer[i] = random.randint(0,15)
        bm_response_buffer[8] = random.randint(0,0xbb)
        for i in range(9,12):
            bm_response_buffer[i] = random.randint(0,255)
        bm_response_buffer[12] = random.randint(0,0b111)
        bm_response_buffer[13] = getCRC(bm_response_buffer,13)

        bm_response_buffer = bytearray(bm_response_buffer)

        # Check for correctness of the message received
        if len(bm_response_buffer) == BM_RESPONSE_LEN and getCRC(bm_response_buffer,BM_RESPONSE_LEN)==0:
            print("Response is correct")

            # Minimum cell voltage
            minV = np.uint16(0)
            minV = bm_response_buffer[3] & 0xff
            minV = ((minV << 8) | (bm_response_buffer[2] & 0xff))

            # Maximum cell voltage
            maxV = np.uint16(0)
            maxV = bm_response_buffer[5] & 0xff
            maxV = ((maxV << 8) | (bm_response_buffer[4] & 0xff))

            # Average cell voltage
            avgV = np.uint16(0)
            avgV = bm_response_buffer[7] & 0xff
            avgV = ((avgV << 8) | (bm_response_buffer[6] & 0xff))

            # Minimum voltage cell location
            minVLoc = bm_response_buffer[8] & 0xf # lower nibble

            # Maximum voltage cell location
            maxVLoc = (bm_response_buffer[8]>>4) & 0xf # Upper nibble

            # Temperature sensor 1
            temp1 = np.uint16(0)
            temp1 = bm_response_buffer[10] & 0xf
            temp1 = (temp1 << 8) | (bm_response_buffer[9] & 0xff)

            # Temperature sensor 2
            temp2 = np.uint16(0)
            temp2 = bm_response_buffer[11] & 0xff
            temp2 = (temp2 << 4) | (bm_response_buffer[10] >> 4)

            # Status
            status = bm_response_buffer[12]

            summary = [minV,minVLoc,maxV,maxVLoc,avgV,temp1,temp2,status]
        else:
            print("Error in response received")
    return summary

