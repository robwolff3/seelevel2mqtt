from machine import Pin
from micropython import const
import utime

# ONE_MICROSECOND = const(1.0e-6)
# ONE_MILLISECOND = const(1.0e-3)
NUM_RESPONSE_BYTES = const(12)  # SeeLevel sensor returns 12 bytes when probed

# define GPIO pins used on Pico
SeeLevelSelectGPIO = 0
SeeLevelResponseGPIO = 1

# error counts
checksum_errs = 0
preamble_errs = 0
sensor_errs = 0

# create GPIO Pin objects for comms with SeeLevel sensor
SeeLevelSelectPin = Pin(SeeLevelSelectGPIO, Pin.OUT)
SeeLevelResponsePin = Pin(SeeLevelResponseGPIO, Pin.IN)

# calibration data for each tank
# Start with calibration data set to 1 to see output when the tank is full
# Set calibration data to output when tank is full
SensorCal = {0: [1], 1: [1], 2: [1]}

# de-select all SeeLevel sensors
SeeLevelSelectPin.off()


def powerupsensors():
    # raise power on the SeeLevel bus
    SeeLevelSelectPin.on()
    utime.sleep_us(2460)  # wait until sensors power up


def powerdownsensors():
    # lower power on the SeeLevel bus
    utime.sleep_ms(1)
    SeeLevelSelectPin.off()


def selectseelevel(sensornum):
    # pulse the bus "n" times to address sensor #n (n=0, 1, 2, ...)
    for dummy in range(sensornum + 1):
        SeeLevelSelectPin.off()
        utime.sleep_us(140)  # adjust to show 85 us pulse on oscilloscope
        SeeLevelSelectPin.on()
        utime.sleep_us(270)
    return


# my version of the pulseio.PulseIn method from CircuitPython
def pulsesin(num_pulses):
    pulse_widths = bytearray(num_pulses)
    value_method = SeeLevelResponsePin.value  # cache method calls to improve timing accuracy
    time_us = utime.ticks_us
    diff_us = utime.ticks_diff

    #    def time_out():
    #        # fail if we don't see any pulse activity within 10 milliseconds
    #        return diff_us(time_us(), start) > 10000

    for i in range(num_pulses):
        while value_method():  # wait for next pulse
            # if time_out(): return pulse_widths
            pass

        start = time_us()
        while not value_method():  # measure negative pulse width
            # if time_out(): return pulse_widths
            pass
        end = time_us()

        # pulse_widths.append(diff_us(end,start))
        pulse_widths[i] = diff_us(end, start)
    return pulse_widths


# collect the 12-byte sequence returned by the sensor
def readseelevelbytes():
    num_pulses = 8 * NUM_RESPONSE_BYTES
    # first, collect all the pulses in the sensor response
    # (use following if you are running with CircuitPython)
    # pulses = pulseio.PulseIn(SeeLevelResponsePin, num_pulses, True)
    # time.sleep(23*ONE_MILLISECOND)
    # while (len(pulses) != num_pulses): pass
    pulses = pulsesin(num_pulses)

    byte_data = []
    print("collected " + str(len(pulses)) + " pulses")
    print(pulses)
    if len(pulses) == num_pulses:  # if sensor responded
        # convert pulse widths into data bytes
        for byte_index in range(0, NUM_RESPONSE_BYTES * 8, 8):
            cur_byte_pulses = pulses[byte_index:byte_index + 8]  # select pulses for next byte
            # bits were sent Big-Endian, so reverse them to simplify following bit-shifts
            # cur_byte_pulses.reverse()
            # any pulse less than 26 microseconds is a logical "1", and "0" if greater
            sl_byte = sum([int(pw > 26) << 7 - i for i, pw in enumerate(cur_byte_pulses)])
            byte_data.append(sl_byte)
            print(sl_byte, hex(sl_byte))
    # pulses.clear()     # uncomment for CircuitPython
    return bytes(byte_data)


# decodetanklevel returns percentage of tank filled
def decodetanklevel(sensordata, calibrationdata):
    # print("sensor data: " + str(sensordata))
    # tanklevel = 0
    len_sd = len(sensordata)

    # get first non-empty segment
    level_seg = 0
    while level_seg < len_sd:
        if sensordata[level_seg] == 0:
            level_seg += 1
        else:
            break
    if level_seg == len_sd:
        return 0  # all segments are zero, so tank is empty

    if not calibrationdata:
        # calibration data is not available, so guestimate tank level
        # assuming tank is a rectangular solid.
        # compute differential percentage of tank represented by each segment
        avg_contribution_per_segment = 100.0 / len_sd
        # in case segment maximums don't max out (ie, reach 255),
        # average the filled segments to better estimate contribution
        # from the segment spanning current water level.
        if level_seg < len_sd - 1:
            avg_reading_per_seg = sum(sensordata[level_seg + 1:]) / (len_sd - level_seg - 1)
        else:
            avg_reading_per_seg = 255
        print("avg seg=" + str(avg_reading_per_seg))
        print("len=" + str(len_sd) + "  level=" + str(level_seg))
        print(avg_contribution_per_segment)
        tanklevel = (sensordata[level_seg] / avg_reading_per_seg + (
                len_sd - 1 - level_seg)) * avg_contribution_per_segment
        if tanklevel > 100:
            tanklevel = 100
        # print("short level: "+str(sensordata[0]/256.0*100))
    else:
        # calibration data is the sum of the sensordata when the tank is full
        tanklevel = (sum(sensordata[0:]) / calibrationdata[0]) * 100
        # tanklevel = -1      # TBD

    return tanklevel


def readtanklevel(sensorid):
    global sensor_errs, preamble_errs, checksum_errs
    tanklevel = -1  # default to "tank not read"

    # select a sensor
    selectseelevel(sensorid)

    # collect the bytes returned by the sensor
    slbytes = readseelevelbytes()

    # test whether sensor actually responded
    if len(slbytes) != NUM_RESPONSE_BYTES:
        print("*** sensor did not respond!")
        print(slbytes)
        sensor_errs += 1
    # test whether response stream has expected starting bits "1001----"
    elif slbytes[0] & 0xF0 != 0x90:
        print("*** invalid stream preamble!")
        preamble_errs += 1
    else:
        # determine number of segments used by sensor
        base_seg = len(slbytes) - 1
        # sensor returns 0xFF for unavailable segments
        while base_seg > 0:
            if slbytes[base_seg] == 0xFF:
                base_seg -= 1
            else:
                break
        byte_checksum = (sum(slbytes[2:base_seg + 1]) - 2) % 256
        if byte_checksum - slbytes[1] != 0:
            print(byte_checksum)
            print("***checksum error!" + str(slbytes[1]))
            checksum_errs += 1
        else:
            # convert data bytes to a tank level
            ba = bytearray(slbytes[2:base_seg + 1])
            if ba[1] == 0:
                ba[0] = 0
            tanklevel = decodetanklevel(ba, SensorCal[sensorid])

    return tanklevel


powerupsensors()
fresh_level = readtanklevel(0)
print("fresh tank level is " + str(fresh_level) + "%")
#grey_level = readtanklevel(1)
#print("gray tank level is " + str(grey_level) + "%")
#black_level = readtanklevel(2)
#print("black tank level is " + str(black_level) + "%")
powerdownsensors()
