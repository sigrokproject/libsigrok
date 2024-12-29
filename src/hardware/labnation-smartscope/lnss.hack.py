import enum
import time
import math
import struct
import usb.core
import usb.util as uu
import matplotlib.pyplot as plt

VENDOR_ID=0x04d8
PRODUCT_ID=0xf4b5


dev = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)
if not dev:
    print("failed to find smartscope?")

print("device iSerial == ", dev.serial_number) # good, this works


# get pic version..
HEADER_CMD_BYTE = 0xC0 #C0 as in Command
HEADER_RESPONSE_BYTE = 0xAD #AD as in Answer Dude
FLASH_USER_ADDRESS_MASK = 0x0FFF

class CONSTANTS():
        validDividers = [ 1, 6, 36 ];
        validMultipliers = [ 1.1, 2, 3 ];
        BASE_SAMPLE_PERIOD = 10e-9 # 10MHz sample rate


class PIC_CMDS(enum.IntEnum):
            PIC_VERSION = 1,
            PIC_WRITE = 2,
            PIC_READ = 3,
            PIC_RESET = 4,
            PIC_BOOTLOADER = 5,
            EEPROM_READ = 6,
            EEPROM_WRITE = 7,
            FLASH_ROM_READ = 8,
            FLASH_ROM_WRITE = 9,
            I2C_WRITE = 10, # 0xa
            I2C_READ = 11, #  0xb
            PROGRAM_FPGA_START = 12, #0xc
            PROGRAM_FPGA_END = 13,#0xd
            I2C_WRITE_START = 14, #0xe
            I2C_WRITE_BULK = 15, #0xf
            I2C_WRITE_STOP = 16, #0x10

class REG(enum.IntEnum):
		STROBE_UPDATE = 0,
		SPI_ADDRESS = 1,
		SPI_WRITE_VALUE = 2,
		DIVIDER_MULTIPLIER = 3,
		CHA_YOFFSET_VOLTAGE = 4,
		CHB_YOFFSET_VOLTAGE = 5,
		TRIGGER_PWM = 6,
		TRIGGER_LEVEL = 7,
		TRIGGER_MODE = 8,
		TRIGGER_PW_MIN_B0 = 9,
		TRIGGER_PW_MIN_B1 = 10, # 0xa
		TRIGGER_PW_MIN_B2 = 11, # 0xb
		TRIGGER_PW_MAX_B0 = 12, #0xc
		TRIGGER_PW_MAX_B1 = 13, #0xd
		TRIGGER_PW_MAX_B2 = 14, #0xe
		INPUT_DECIMATION = 15, #0xf
		ACQUISITION_DEPTH = 16, #0x10
		TRIGGERHOLDOFF_B0 = 17, #0x11
		TRIGGERHOLDOFF_B1 = 18, #0x12
		TRIGGERHOLDOFF_B2 = 19, #0x13
		TRIGGERHOLDOFF_B3 = 20, #0x14
		VIEW_DECIMATION = 21, #0x15
		VIEW_OFFSET_B0 = 22, #0x16
		VIEW_OFFSET_B1 = 23, #0x17
		VIEW_OFFSET_B2 = 24, #0x18
		VIEW_ACQUISITIONS = 25, #0x19
		VIEW_BURSTS = 26, #0x1a
		VIEW_EXCESS_B0 = 27, #0x1b
		VIEW_EXCESS_B1 = 28, #0x1c
		DIGITAL_TRIGGER_RISING = 29, #0x1d
		DIGITAL_TRIGGER_FALLING = 30, #0x1e
		DIGITAL_TRIGGER_HIGH = 31, #0x1f
		DIGITAL_TRIGGER_LOW = 32, #0x20
		DIGITAL_OUT = 33, #0x21
		GENERATOR_DECIMATION_B0 = 34, #0x22
		GENERATOR_DECIMATION_B1 = 35, #0x23
		GENERATOR_DECIMATION_B2 = 36, #0x24
		GENERATOR_SAMPLES_B0 = 37, #0x25
		GENERATOR_SAMPLES_B1 = 38, #0x26

def REG2HREG(reg):
    """
    Convert a class REG(enum.IntEnum) into the offset needed for the header regs
    will raise if you try to get theh index of a register not available in teh headers
    """
    # straight from Scope_Constants_GEN.cs
    rmap = [
		[ REG.TRIGGER_LEVEL, 0 ],
		[ REG.TRIGGER_MODE, 1 ],
		[ REG.TRIGGERHOLDOFF_B0, 2 ],
		[ REG.TRIGGERHOLDOFF_B1, 3 ],
		[ REG.TRIGGERHOLDOFF_B2, 4 ],
		[ REG.TRIGGERHOLDOFF_B3, 5 ],
		[ REG.CHA_YOFFSET_VOLTAGE, 6 ],
		[ REG.CHB_YOFFSET_VOLTAGE, 7 ],
		[ REG.DIVIDER_MULTIPLIER, 8 ],
		[ REG.INPUT_DECIMATION, 9 ],
		[ REG.TRIGGER_PW_MIN_B0, 10 ],
		[ REG.TRIGGER_PW_MIN_B1, 11 ],
		[ REG.TRIGGER_PW_MIN_B2, 12 ],
		[ REG.TRIGGER_PW_MAX_B0, 13 ],
		[ REG.TRIGGER_PW_MAX_B1, 14 ],
		[ REG.TRIGGER_PW_MAX_B2, 15 ],
		[ REG.TRIGGER_PWM, 16 ],
		[ REG.DIGITAL_TRIGGER_RISING, 17 ],
		[ REG.DIGITAL_TRIGGER_FALLING, 18 ],
		[ REG.DIGITAL_TRIGGER_HIGH, 19 ],
		[ REG.DIGITAL_TRIGGER_LOW, 20 ],
		[ REG.ACQUISITION_DEPTH, 21 ],
		[ REG.VIEW_DECIMATION, 22 ],
		[ REG.VIEW_OFFSET_B0, 23 ],
		[ REG.VIEW_OFFSET_B1, 24 ],
		[ REG.VIEW_OFFSET_B2, 25 ],
		[ REG.VIEW_ACQUISITIONS, 26 ],
		[ REG.VIEW_BURSTS, 27 ],
		[ REG.VIEW_EXCESS_B0, 28 ],
		[ REG.VIEW_EXCESS_B1, 29 ],
    ]
    matches = [x for x in rmap if x[0] == reg]
    if len(matches) != 1:
        raise Exception("register requested not a header register" + reg.name)
    return matches[0][1]

class STR(enum.IntEnum):
		GLOBAL_RESET = 0,
		INIT_SPI_TRANSFER = 1,
		GENERATOR_TO_AWG = 2,
		LA_ENABLE = 3,
		SCOPE_ENABLE = 4,
		SCOPE_UPDATE = 5,
		FORCE_TRIGGER = 6,
		VIEW_UPDATE = 7,
		VIEW_SEND_OVERVIEW = 8,
		VIEW_SEND_PARTIAL = 9,
		ACQ_START = 10,
		ACQ_STOP = 11,
		CHA_DCCOUPLING = 12,
		CHB_DCCOUPLING = 13,
		ENABLE_ADC = 14,
		ENABLE_NEG = 15,
		ENABLE_RAM = 16,
		DOUT_3V_5V = 17,
		EN_OPAMP_B = 18,
		GENERATOR_TO_DIGITAL = 19,
		ROLL = 20,
		LA_CHANNEL = 21,

# HeaderFlags
class HDRF(enum.IntEnum):
        Acquiring = 1,
        IsOverview = 2,
        IsLastAcquisition = 4,
        Rolling = 8,
        TimedOut = 16,
        AwaitingTrigger = 32,
        Armded = 64,
        IsFullAcqusition = 128,


# send command = header_cmd, pic_version, and then read 16

EP_CMD_IN = 0x83
EP_CMD_OUT = 0x2
EP_DATA = 0x81

#print("Sending PIC RESET command", dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.PIC_RESET]))

def get_pic_ver():
    x = dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.PIC_VERSION.value])
    assert(x == 2)
    y = dev.read(EP_CMD_IN, 16)
    assert(len(y) == 16)
    print("got back on other ep: ", y)
    print([hex(q) for q in y])
    print("as per labnation: %x%x%x or bytes 6,5,4" % (y[6],y[5],y[4]))
    print([hex(q) for q in y])

def get_eeprom(i, n=1):
    """Reads the internal pic eeprom presumably.  Mine's all 0xff, so... never used as far as I can tell"""
    x = dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.EEPROM_READ.value, i, n])
    y = dev.read(EP_CMD_IN, 16)
    assert(len(y) == 16)
    # All we know unfortunately is taht the first 4 bytes are "garbage"
    # We'll try a few reads of different sizes, see what we can work out ;)
    print("For geteeprom ", i, n, [hex(q) for q in y[4:]])

def get_rom(i, n=1):
    """
    Reads the scope "ROM".  not the fpga "ROM", this appears to be a flash page in the PIC maybe?
    see the "unsafe struct Map" in src/Devices/SmartScopeRom.cs
    It contains all the calibration data that we need later.
    """
    if (i + n > FLASH_USER_ADDRESS_MASK):
        print("Not allowed to try accessing flash (""rom"") beyond: ", FLASH_USER_ADDRESS_MASK)
        return
    dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.FLASH_ROM_READ.value, i & 0xff, n, (i>>8) & 0xff])
    y = dev.read(EP_CMD_IN, 16)
    assert(len(y) == 16)
    data = y[5:5+n]
    #print("Get rom ", i, n, [hex(q) for q in data])
    return data


get_pic_ver()


def dump_calibration_raw():
    # Not clear yet which of them are actually used!
    OFF_PLUGCNT=0
    OFF_GAINCAL = 4
    OFF_MAG = 220
    OFF_MAGIDX = 604
    OFF_PHASE = 796
    OFF_PHASEIDX = 1036
    OFF_ADCTV = 1156  # UNUSED!

    plugcount = get_rom(0, 4)
    plugcnt = struct.unpack("<L", plugcount)[0]
    print("plug count is ", plugcnt, hex(plugcnt))
    adctimingvalue = get_rom(OFF_ADCTV, 1)[0]
    print("ADC timing value is ", adctimingvalue, hex(adctimingvalue))
    # ohh yeah, fuck you. We have space in rom for calibratint this, but
    # we're acttually just going to hardcode it anyway later regardless.
    
    # gaincalibration is 54 floats, 3 coeffs * nDivider(3) * nMultiplier(3) * nChannel(2)
    gaincalibration = []
    for n in range(OFF_GAINCAL, OFF_MAG, 4):
        d = get_rom(OFF_GAINCAL + n, 4)
        gaincalibration.append(struct.unpack("<f", d))
    print("got gain calibration values, len", len(gaincalibration))

#dump_calibration_raw()
    

# src/Hardware/SmartScopeInterfaceUsb.cs is key
# Now, do an fpga i2c read of the fpga rom, to get the git version!

# ask the pic to make i2c comms to two different addresses the fpga responds on.  0xc for settings and 0xd for rom, 0xe for AWG
FPGA_I2C_ADDRESS_SETTINGS = 0x0c
FPGA_I2C_ADDRESS_ROM = 0x0D
#             FpgaRom = new Memories.ScopeFpgaRom(hardwareInterface, FPGA_I2C_ADDRESS_ROM);
#814:            return (UInt32)(FpgaRom[ROM.FW_GIT0].Read().GetByte() +

# for the next bits https://github.com/labnation/DeviceInterface/blob/master/src/Memories/ScopeFpgaI2cMemory.cs

# so, get controller register for FW_GIT0, 1,2,3) is

# Only known to work for the fpga regs
def get_i2c_reg(i2caddr, idx):
    dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.I2C_WRITE, 2, i2caddr<<1, idx])
    dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.I2C_READ, i2caddr, 1])
    y = dev.read(EP_CMD_IN, 16)
    return y[4]

def get_hw_rev():
    """Needed for working out which blob to load"""
    return dev.serial_number[-3:]

print("hw rev is ", get_hw_rev())
    

def maybe(idx):
    print("maybe for ", idx)
    x = dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.I2C_WRITE, 2, FPGA_I2C_ADDRESS_ROM<<1, idx]) # read reg 0 from i2c 0x0d.
    #print("set reg to read", x)
    # now the read itself
    x = dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.I2C_READ, FPGA_I2C_ADDRESS_ROM, 1]) # read 1 value from i2c 0x0d.
    #print("requested read", x)
    y = dev.read(EP_CMD_IN, 16)
    print("got back on other ep: ", y)
    print([hex(q) for q in y])
    return y[4]



def get_fpga_ver():
    fpgaver = [get_i2c_reg(FPGA_I2C_ADDRESS_ROM, x) for x in range(0,4)]
    fpgaver.reverse()
    return [hex(x) for x in fpgaver]

print("fpga version is ", get_fpga_ver())

# This works, but _only_ after running the smartscope softare to upload the fpga!
# now it gets the fpga rom version and it matches the version displayed in the ui


# Try and grab data
# ScopeConstants_GEN.cs
FETCH_SIZE_MAX = 2048 * 2 
SZ_HDR = 64 
PACKAGE_MAX = 64



def get_acquisition():
    tries = 0
    hdr = None
    # seriously, trying 64 times sounds completely broken, but it's what the venndor code does...
    # ok, later thought, 64*64 is 4096, it's trying to make sure that it finishes reading a data
    # packet if one wsa in progress. (Still sounds buggy honestly, butt ok...)
    while True:
        hdr = dev.read(EP_DATA, SZ_HDR, 1000) # smartscope uses 3000, but what....
        print("tries = ", tries, "reply was", hdr)
        # silly unpacking bytearray
        if hdr[:2].tolist() == [ord(q) for q in "LN"]:
            break
        tries += 1
        if tries > PACKAGE_MAX:
            raise Exception("too many tries trying to get header")
        
    if tries > 0:
        print("had to try %d times to get a header" % tries)

    print("Found matchin hdr", hdr)
    return hdr

def get_acquisition_mode():
#   return (AcquisitionMode)((FpgaSettingsMemory[REG.TRIGGER_MODE].GetByte() & 0xC0) >> 6);
    x = get_i2c_reg(FPGA_I2C_ADDRESS_SETTINGS, REG.TRIGGER_MODE)
    print("raw trigger mode raw = %x" % x)
    x = get_i2c_reg(FPGA_I2C_ADDRESS_SETTINGS, REG.TRIGGER_LEVEL)
    print("raw trigger level = %x" % x)

#get_acquisition_mode()

    

BLOBPATH = "/home/karlp/src/smartscope-DeviceInterface/blobs"

def load_blob(path, hwrev):
    fn = "%s/SmartScope_%s.bin" % (path, hwrev)
    PACKSIZE = 32
    PADDING = 2048/8 # wat
    print("loading blob from %s" % fn)
    blob = None
    with open(fn, "rb") as _in:
        # fw blob will never be that big...
        blob = _in.read()
    plen = len(blob)
    cmd = int(plen / PACKSIZE + PADDING)
    print("loaded blob of length %d bytes, with cmd %d" % (plen, cmd))

    print("write start =>", dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.PROGRAM_FPGA_START, cmd >> 8, cmd & 0xff]))
    usb.control.clear_feature(dev, usb.control.ENDPOINT_HALT, EP_DATA)
    # workaround copied from labnation, not _entirely_sure it's necessary
    time.sleep(1)
    chunk, rem = blob[:2048], blob[2048:]
    while chunk:
        blob = rem
        print("write chunk =>", dev.write(EP_CMD_OUT, chunk))
        chunk, rem = blob[:2048], blob[2048:]
    # moah wat honestly. but ok.
    for i in range(int(PADDING)):
        dev.write(EP_CMD_OUT, [0xff] * 32)
    print("write finish =>", dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.PROGRAM_FPGA_END]))
    usb.control.clear_feature(dev, usb.control.ENDPOINT_HALT, EP_DATA)

def pic_reset():
    print("reset pic")
    x = dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.PIC_RESET.value])
    assert(x == 2)

    

def get_acquisition_depth():
    reggs = [
        REG.ACQUISITION_DEPTH,
        REG.CHA_YOFFSET_VOLTAGE ,
        REG.CHB_YOFFSET_VOLTAGE ,
        REG.TRIGGER_PWM ,
        REG.TRIGGER_LEVEL ,
        REG.TRIGGER_MODE ,
        REG.TRIGGER_PW_MIN_B0 ,
        REG.TRIGGER_PW_MIN_B1 ,
        REG.TRIGGER_PW_MIN_B2 ,
        REG.TRIGGER_PW_MAX_B0 ,
        REG.TRIGGER_PW_MAX_B1 ,
        REG.TRIGGER_PW_MAX_B2 ,
        REG.INPUT_DECIMATION ,
        REG.ACQUISITION_DEPTH ,
        REG.TRIGGERHOLDOFF_B0 ,
        REG.TRIGGERHOLDOFF_B1 ,
        REG.TRIGGERHOLDOFF_B2 ,
        REG.TRIGGERHOLDOFF_B3 ,
        REG.VIEW_DECIMATION ,
        REG.VIEW_OFFSET_B0 ,
        REG.VIEW_OFFSET_B1 ,
	REG.VIEW_OFFSET_B2 ,
    ]
    for x in reggs:
        print("attempting to read reg", x.name)
        val = get_i2c_reg(FPGA_I2C_ADDRESS_SETTINGS, x.value)
        print("=>", val)

#get_acquisition_depth()


def read_strobes():
    # read all of them
    print("strobe0", get_i2c_reg(FPGA_I2C_ADDRESS_ROM, 5 + 0))
    print("strobe1", get_i2c_reg(FPGA_I2C_ADDRESS_ROM, 5 + 1))
#read_strobes()

#        private void EnableEssentials(bool enable)
 #       {
 #           StrobeMemory[STR.ENABLE_ADC].Set(enable);
 #           StrobeMemory[STR.ENABLE_RAM].Set(enable);
 #           StrobeMemory[STR.ENABLE_NEG].Set(enable);
 #           StrobeMemory[STR.SCOPE_ENABLE].Set(enable);
 #       }

#def enable_essentials():
# gotta be aa controller register thaat I can't follow
# strobes is reg 5 on fpga?
# that's thhe baase address for strobes.  StrobeToRomAddress() in .net 
#             return (uint)ROM.STROBES + (uint)Math.Floor((double)strobe / 8.0);
#  == 5 + strobe/8
#             int offset = (int)(address % 8);
#            Registers[address].Set( ((readMemory[romAddress].GetByte() >> offset) & 0x01) == 0x01);
# ok, looks like it's a few bytes in a row, but bitfields.
# write to REG.STROBE_UPDATE, strobeaddrr<<1|strobestate
# 99% sure this is an fpga i2c write?

#      return UsbCommandHeaderI2c((uint8_t)((address >> 8) & 0x7F), op, (uint8_t)(address & 0xFF), length, buffer);

def i2c_write1(reg, val):
    data = [HEADER_CMD_BYTE, PIC_CMDS.I2C_WRITE, 3, FPGA_I2C_ADDRESS_SETTINGS<<1, reg.value, val]
    print("i2c_write1", [hex(d) for d in data])
    dev.write(EP_CMD_OUT, data)

def i2c_writen(reg, data):
    header = [HEADER_CMD_BYTE, PIC_CMDS.I2C_WRITE, 2+len(data), FPGA_I2C_ADDRESS_SETTINGS<<1, reg.value]
    out = header + data
    print("i2c_writeN", [hex(d) for d in out])
    dev.write(EP_CMD_OUT, out)

def spi_write(reg, val):
    print("spi writing ", reg, val)
    i2c_write1(REG.SPI_ADDRESS, reg)
    i2c_write1(REG.SPI_WRITE_VALUE, val)
    strobe_set(STR.INIT_SPI_TRANSFER, 0)
    strobe_set(STR.INIT_SPI_TRANSFER, 1)

# FIXME not tested!
def spi_read(reg):
    i2c_write1(REG.SPI_ADDRESS, reg|0x80) # top bit set to read
    strobe_set(STR.INIT_SPI_TRANSFER, 0)
    strobe_set(STR.INIT_SPI_TRANSFER, 1)
    spival = get_i2c_reg(FPGA_I2C_ADDRESS_ROM, 4)
    print("spi reading ", reg, "returned: ", hex(spival))
    return spival
    
# no, this doesn't seem to work yet?
def strobe_set(strobe, val):
    print("setting strobe ", strobe.name, val)
    i2c_write1(REG.STROBE_UPDATE, strobe.value << 1 | val)

def configure_adc():
    # upstream code does this in mixed orders, and relies on their "commit" code to sort it all out
    spi_write(0xa, 0x5a)# AdcMemory[MAX19506.SOFT_RESET].WriteImmediate(90);
    spi_write(0, 3) # AdcMemory[MAX19506.POWER_MANAGEMENT].Set(3);
    spi_write(1, 2) #AdcMemory[MAX19506.OUTPUT_FORMAT].Set(0x02); //DDR on chA
    spi_write(2, 0) #AdcMemory[MAX19506.OUTPUT_PWR_MNGMNT].Set(0);
    # XXX This is hard coded, despite in theory being a "calibration" value from ROM :(
    spi_write(3, 0x18) # AdcMemory[MAX19506.DATA_CLK_TIMING].Set(24);
    spi_write(4, 0) # AdcMemory[MAX19506.CHA_TERMINATION].Set(0);
    # no, don't do that, that just turns it to test patterns, screw that
    #spi_write(6, 0x50)# AdcMemory[MAX19506.FORMAT_PATTERN].Set(80);

def post_blob_load():
    """From usb trace"""
    print("after loading fw, fw rev is", get_fpga_ver())
    i2c_write1(REG.DIGITAL_OUT, 0)
    i2c_write1(REG.GENERATOR_SAMPLES_B0, 0xff)
    i2c_write1(REG.GENERATOR_SAMPLES_B1, 7)
    strobe_set(STR.GLOBAL_RESET, 1)
    # UNKNONNWN!!!  FORCE_STREAMIN = 0?
    dev.write(EP_CMD_OUT, [HEADER_CMD_BYTE, PIC_CMDS.PIC_WRITE, 0, 1, 0])
    
    # karl, inserted to test, should have been 3, per manual, but perhaps another reset elsewhere?
    #print("reading spi 0 at defualt", spi_read(0))

    # reset's all spi registers to 0 on max19506
    # TODO - seems _completely_ unnnecessary when the next step is to reset the chip!
    for spireg in [0,1,2,3,4,5,6,8]:
        spi_write(spireg, 0)
    spi_write(0xa, 0) # appears to be a bug in labnation?

    # unknown, writing 27 bytes to DIVIDER_MULTIPLIER
        #static readonly double[] validDividers = { 1, 6, 36 };
        #static readonly double[] validMultipliers = { 1.1, 2, 3 };
    # definitely eeds to cover both channels here?
    # XXX this almost definitely comes from the rom calibraton!
    x = "99 70 70 00 7e 00 00 00 00 ff ff ff 00 00 00 00 00 00 00 00 00 00 00 06 00 00 00"
    i2c_writen(REG.DIVIDER_MULTIPLIER, [int(q,16) for q in x.split()])

    i2c_writen(REG.DIGITAL_TRIGGER_FALLING, [0,0,0])
    i2c_writen(REG.GENERATOR_DECIMATION_B0, [0,0,0])

    strobe_set(STR.GENERATOR_TO_AWG, 0)
    strobe_set(STR.LA_ENABLE, 0)
    strobe_set(STR.SCOPE_ENABLE, 1)
    strobe_set(STR.SCOPE_UPDATE, 1)
    strobe_set(STR.FORCE_TRIGGER, 1)
    strobe_set(STR.VIEW_UPDATE, 1)
    strobe_set(STR.VIEW_SEND_OVERVIEW, 0)
    strobe_set(STR.VIEW_SEND_PARTIAL, 0)
    strobe_set(STR.ACQ_START, 0)
    strobe_set(STR.ACQ_STOP, 0)
    strobe_set(STR.CHA_DCCOUPLING, 1)
    strobe_set(STR.CHB_DCCOUPLING, 1)
    strobe_set(STR.ENABLE_ADC, 1)
    strobe_set(STR.ENABLE_NEG, 1)
    strobe_set(STR.ENABLE_RAM, 1)
    strobe_set(STR.DOUT_3V_5V, 0)
    strobe_set(STR.EN_OPAMP_B, 0)
    strobe_set(STR.GENERATOR_TO_DIGITAL, 0)
    strobe_set(STR.ROLL, 0)
    strobe_set(STR.LA_CHANNEL, 0)
    strobe_set(STR.SCOPE_UPDATE, 1) #again
    strobe_set(STR.VIEW_UPDATE, 1) # againn
    
    usb.control.clear_feature(dev, usb.control.ENDPOINT_HALT, EP_DATA)

    configure_adc()

    # There might be a big missing chunk here of adc calibration!
    #TestAdcRamp() !!!

    i2c_writen(REG.TRIGGER_LEVEL, [0x7f,80]) # 0x7f is 127, and code is SetTriggerByte(127);
    i2c_write1(REG.ACQUISITION_DEPTH, 1)
    i2c_writen(REG.VIEW_DECIMATION, [1,0,0,0])
    i2c_writen(REG.VIEW_BURSTS, [6,0,0])
    strobe_set(STR.LA_ENABLE, 0) # again, yes.
    strobe_set(STR.VIEW_SEND_OVERVIEW, 0)
    strobe_set(STR.VIEW_SEND_PARTIAL, 0)
    strobe_set(STR.ACQ_START, 1)  # woo, it begins!
    strobe_set(STR.LA_CHANNEL, 0)
    strobe_set(STR.SCOPE_UPDATE, 1)
    strobe_set(STR.VIEW_UPDATE, 1)
    

def tryread(plot=False):
    hdr = get_acquisition()
    data = dev.read(EP_DATA, FETCH_SIZE_MAX, 1000)
    # unsafe public struct SmartScopeHeader is the format of hdr.
    print("got data of len", len(data))
    structs = "2sbbHH2xBB3x30s2s17x"
    magic, hoff, bpb, nburst, off, flags, aid, hregs, hstrobes = struct.unpack(structs, hdr)
    #print("stuff", magic, hoff, bpb, nburst, off, flags, aid, hregs, hstrobes)
    print("Header flags = %#x" % flags)
    for hf in HDRF:
        if flags & hf.value:
            print("header flag had", hf.name)
    print("burst size = ", nburst)
    print("bytes per burst ", bpb)
    print("acq id ", aid)

    # Dictionary<AnalogChannel, GainCalibration> channelConfig = hdr.ChannelSettings(this.rom);
    # TODO we don't actually use thihs, but we should at some point...
    for ch in [("CHA",0), ("CHB",1)]:
        # hregs is a subset of all the regs, presented everytime in the header.
        divmul = hregs[REG2HREG(REG.DIVIDER_MULTIPLIER)]
        div = CONSTANTS.validDividers[divmul >> (0 + ch[1]*4) & 0x3]
        mul = CONSTANTS.validMultipliers[divmul >> (2 + ch[1]*4) & 0x3]
        print("chanel: %s div: %f, mul=%f" % (ch[0], div, mul))
        # TODO - we should correlate this with calibration at some point.
        # We need to look up ... somewhere, some calibrationnn coeffs thata are
        # per channnel/div/mul set.

    
    #, so, the FUN! SplitAndConvert() the 4k data into channel/data pair
    # aiui, it's 2 channels, no matter whether you have one or both enabled?
    # I presume LA just replaces one channel?
    # yes, always two channnels, 
    # if (header.GetStrobe(STR.LA_ENABLE) && header.GetStrobe(STR.LA_CHANNEL) == ch.Value > 0)
    # is hhow you know whether a channel is logic or analog, and which is which.
    # TODO - eed a STR2HSTR() function too for the header strobes
    

    # Not sure I even care about shorter reads... so.
    dlen = FETCH_SIZE_MAX / 2
    for ch in [("CHA",0), ("CHB",1)]:
#       will eventually need to handle things like these...
#       # TODO I don't really understannd where they're kept yet, but I'm betting it's
#       # the big traansfer of 27 bytes to REG.DIVIDER_MULTIPLIER above
#       coeff is from the calibration stuff above
#                byte yOffset = header.GetRegister(ch.YOffsetRegister());
#                float probeGain = probeSettings[ch].Gain;
#                float probeOffset = probeSettings[ch].Offset;
#                float totalOffset = (float)(yOffset * coeff[1] + coeff[2]);
        pass
    # Data is just A,B,A,B,A,B to thhe ennd...
    #channeldata = zip(*[data[idx:idx + 2] for idx in range(0, len(data), 2)])
    #chad = channeldata[0]
    #chbd = channeldata[1]
    # or just, if itt works on the pyusb data
    chad = data[0::2]
    chbd = data[1::2]
    print("have %d chA and %d chB" % (len(chad), len(chbd)))
    # FIXME here's where you could scale/multiply the cal stuff

    if flags & HDRF.IsOverview.value:
        print("Acquisitionn is of type OVERVIEW")
    else:
        if flags & HDRF.IsFullAcqusition:
            print("Acquisition is of type ACQUISITION")
        else:
            print("acquisition is of type VIEWPORT")

    print("innputdeci is ", REG2HREG(REG.INPUT_DECIMATION), hregs[REG2HREG(REG.INPUT_DECIMATION)])
    sample_period = CONSTANTS.BASE_SAMPLE_PERIOD * math.pow(2, hregs[REG2HREG(REG.INPUT_DECIMATION)])
    print("Sample period is ", sample_period)
    # default acquisitionn is 512k, so will take, at 2k/transfer,
    # 256 transfers? shouldn't it keep acqid thhen?
    
    acq_depth = 2048 << hregs[REG2HREG(REG.ACQUISITION_DEPTH)]
    print("acq depth = ", acq_depth)
    vp_excess_samps = hregs[REG2HREG(REG.VIEW_EXCESS_B0)] + hregs[REG2HREG(REG.VIEW_EXCESS_B1)] << 8
    print("vp excess = ", vp_excess_samps)

    # I think that's it, now need to write stobes to switch to the other modes?
    # try plotting the data!
    #print(chad)
    # heh, it's a ramp.  guess those settings above set up thhe calibrationn ramp
    if plot:
        plt.plot(chad)
        plt.ylabel('rawbitval')
        plt.show()


def read_forever():
    # This works, I get a 64byte header, annd then 4k blocks continuously, at least with my
    # test setup.
    while True:
        tryread()


#load_blob(BLOBPATH, get_hw_rev())
print("after loading, can still get pic rev: ", get_pic_ver())
#pic_reset()
print("after loading fw, fw rev is", get_fpga_ver())
#print("after loading", get_acquisition_mode())

post_blob_load()
#read_strobes()
tryread(True)
#read_forever()

