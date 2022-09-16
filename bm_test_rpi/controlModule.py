import controlFunctions as cf
"""
This module will be used to send control commands to the BMS and other devices
the controlFunctions module will be used to construct the functions required for the control system
"""
addressAssigned = True # Set to False to assign addresses to the bms

addresses = cf.addresses
battery_voltages = cf.np.zeros([cf.NO_BMS,12], dtype = cf.np.uint16)
Ess_current = cf.Ess_current

# ------------------------------------ CONTROL COMMANDS -----------------------------------------
# Assign addresses if they have not been assignned
if (addressAssigned == False): cf.assignAddresses()

current_time = 0
while True: 
    
    # Send commands every 55 seconds
    if (cf.time.time() - current_time) >= 55:

        cf.buzzer.on()
        cf.time.sleep(0.5)
        cf.buzzer.off()
        cf.time.sleep(0.5)

        for i in range(len(addresses)):
            print('Getting voltages from BM',i, 'with address', hex(addresses[i]))
            if cf.simulate == True: print("Simulated voltages are being published.")
            volt = cf.pollForVoltages(addresses[i])
            print("Voltages for BM with address",hex(addresses[i]), "is ", volt)
            battery_voltages[i,:] = volt
            print("Summary: ",cf.sendSummary(addresses[i], Ess_current))
            if i == (len(addresses) - 1) : print ("All voltages = ", battery_voltages)
            
        # ---- Uncomment any of these to test them -----------------
        # print("Summary: ",cf.sendSummary(addresses[2], Ess_current))
        #print(cf.balanceTarget(addresses[0],3300))
        #print(cf.resetTarget(addresses[0]))
        current_time = cf.time.time()
        