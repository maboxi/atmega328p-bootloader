from serial import Serial, SerialException 
import argparse
import os
import argparse
import re
import pprint
import time
from termcolor import colored

TOOL_VERSION = "0.1"

RT_DATARECORD = '00'
RT_EOF = '01'
RT_STARTSEGMENTADDRESSRECORD = '03'

def decode_fuse_ext(fuse):
    f_bod210 = fuse & 7
    print('\tBrown Out Detection: ', end='')
    min_typ_max = []
    match(f_bod210):
        case 0b111:
            print('BOD Disabled')
        case 0b110:
            min_typ_max.extend([1.7, 1.8, 2.0])
        case 0b101:
            min_typ_max.extend([2.5, 2.7, 2.9])
        case 0b100:
            min_typ_max.extend([4.1, 4.3, 4.5])
        case _:
            print(f'Unknown ({f_bod210} is reserved)')

    if(len(min_typ_max) > 0):
        print(f'Min. V_bot = {min_typ_max[0]}, Typ. V_bot = {min_typ_max[1]}, Max. V_bot = {min_typ_max[2]}')

def print_fuse(fuse_val, prompt, text_unprogrammed, text_programmed):
    print(f'\t{prompt:<20}{text_unprogrammed if fuse_val > 0 else text_programmed}')

def decode_fuse_high(fuse):
    f_rstdisbl = (fuse & (1<<7)) >> 7
    f_dwen = (fuse & (1<<6)) >> 6
    f_spien = (fuse & (1<<5)) >> 5
    f_wdton = (fuse & (1<<4)) >> 4
    f_eesave = (fuse & (1<<3)) >> 3
    f_bootsz10 = (fuse & (1<<2 | 1<<1)) >> 1
    f_bootrst = (fuse & 1)

    print_fuse(f_rstdisbl, 'External Reset: ', 'Enabled', 'Disabled')
    print_fuse(f_dwen, 'debugWIRE: ', 'Disabled', 'Enabled')
    print_fuse(f_spien, 'Serial Program and Data Downloading: ', 'Disabled', 'Enabled')
    print_fuse(f_wdton, 'Watchdog Timer: ', 'Not Always On', 'Always On')
    print_fuse(f_eesave, 'EEPROM Memory: ', 'Preserved through the Chip Erase', 'Deleted on Chip Erase')

    bootsz_info = []
    match(f_bootsz10):
        case 0b11:
            bootsz_info = [64, 4, 0x3F00, 0x3FFF]
        case 0b10:
            bootsz_info = [64, 8, 0x3E00, 0x3FFF]
        case 0b01:
            bootsz_info = [64, 16, 0x3C00, 0x3FFF]
        case 0b00:
            bootsz_info = [64, 32, 0x3800, 0x3FFF]

    print(f'\tBoot Size: {bootsz_info[1]} pages, page size = {bootsz_info[0]} -> {bootsz_info[0] * bootsz_info[1]} bytes')
    print(f'\t\t=> Application Section: 0x0000 - 0x{bootsz_info[2] - 1:4X}')
    print(f'\t\t=> Boot Section: 0x{bootsz_info[2]:4X} - 0x{bootsz_info[3]:4X}')

    print_fuse(f_bootrst, 'Selected Reset Vector: ', 'Application (0x0000)', f'Boot Loader (0x{bootsz_info[2]:4X})')


def decode_fuse_low(fuse):
    f_ckdiv8 = (fuse & (1<<7)) >> 7
    f_ckout = (fuse & (1<<6)) >> 6
    f_sut10 = (fuse & (1<<5 | 1<<4)) >> 4
    f_cksel3210 = (fuse & (1<<3 | 1<<2 | 1<<1 | 1))
    print_fuse(f_ckdiv8, 'Clock Divided by 8: ', 'No', 'Yes')
    print_fuse(f_ckout, 'Clock Output (PORTB0): ', 'Disabled', 'Enabled')

    print('\tClock Source: ', end='')
    match f_cksel3210:
        case 0b1111 | 0b1110 | 0b1101 | 0b1100 | 0b1011 | 0b1010 | 0b1001 | 0b1000:
            print('Lower Power Crystal Oscillator')
        case 0b0111 | 0b0110:
            print('Full Swing Crystal Oscillator')
        case 0b0101 | 0b0100:
            print('Lower Frequency Crystal Oscillator')
        case 0b0011:
            print('Internal 128kHz RC Oscillator')
        case 0b0010:
            print('Calibrated Internal RC Oscillator')
        case 0b0000:
            print('External Clock')
        case _:
            print(f'Unknown ({f_cksel3210} is reserved)')

    print(f'\tStart-up times for clock selection: SUT1..0 = {f_sut10} ')



def read_fuses(ser, comdefines, args):
    status = serial_send_code(ser, 'BL_COM_CMD_READFUSES')
    if(status & comdefines['BL_COM_REPLY_STATUSMASK'] == comdefines['BL_COM_REPLY_OK']):
        fuse_values = ser.read(size=3)
        print('-'*90)
        print('Fuse values:')

        print(f'Extended: -----{(fuse_values[2] & 7):b}')
        decode_fuse_ext(fuse_values[2])
        print(f'High: {fuse_values[1]:b}')
        decode_fuse_high(fuse_values[1])
        print(f'Low: {fuse_values[0]:b}')
        decode_fuse_low(fuse_values[0])
        print('-'*90)
    else:
        print(f'Error: fuse read request returned: {status}')



def serial_send_code(ser: Serial, code):
    ser.write(comdefines[code])
    return int.from_bytes(ser.read(size=1))

def verify_program(ser, hexfile, comdefines, args):
    print('Verifying memory...')
    pagecounter = 0
    num_errors = 0
    for line in hexfile['data']:
        status = serial_send_code(ser, 'BL_COM_CMD_VERIFY')

        if(status & comdefines['BL_COM_REPLY_STATUSMASK'] == comdefines['BL_COM_REPLY_OK']):
            # request hex file data
            (bytecount, address, data, checksum_ok, data_binary) = line

            addrh = (address >> 8) & 0xFF
            addrl = address & 0xFF

            # is
            if(args.verbose):
                print(f'0x{addrh:02x}{addrl:02x} | {bytecount:2} | ', end='')

            ser.write(addrh.to_bytes(1))
            ser.write(addrl.to_bytes(1))
            ser.write(bytecount.to_bytes(1))

            memory_data = ser.read(size=bytecount)

            error_detected = False
            
            for i, byte in enumerate(memory_data):
                if(args.verbose):
                    print(f'{byte:02x} ', end='')
                if(byte != data_binary[i]):
                    num_errors += 1
                    error_detected = True
            if(args.verbose):
                print()

            # should
            if(error_detected):
                if(args.verbose):
                    print('should be     ', end='')

                pagecounter += bytecount
                if(args.verbose):
                    for byte in data_binary:
                        print(colored(f'{byte:02x} ', 'red'), end='')
                    print()
                    print()
        else:
            print(f'Error: verify request returned: {status}')
            break

    if(args.verbose): 
        print() 
    if(num_errors == 0):
        print('\t=> No errors detected!')
    else:
        print(f'\t=> Errors detected: {num_errors}')

def upload_error_handling(reply, linenum, header:bool, comdefines, args):
    status = reply & comdefines['BL_COM_REPLY_STATUSMASK']
    info = reply & comdefines['BL_COM_UPLOADINFO_MASK']
    hbstr = 'Header' if header else 'Body'

    if(status == comdefines['BL_COM_REPLY_OK']):
        if(args.verbose):
            print(f'{hbstr} OK, ', end='')
        return True
    elif(status == comdefines['BL_COM_REPLY_UPLOADERROR']):
        if(args.verbose):
            if(info == comdefines['BL_COM_UPLOADERR_COLON']):
                print(f'Line {linenum:3}: Upload info {hbstr}: Colon')
            if(info == comdefines['BL_COM_UPLOADERR_HEXVAL_8']):
                print(f'Line {linenum:3}: Upload info {hbstr}: Hexval 8')
            if(info == comdefines['BL_COM_UPLOADERR_HEXVAL_16']):
                print(f'Line {linenum:3}: Upload info {hbstr}: Hexval 16')
            if(info == comdefines['BL_COM_UPLOADERR_LINELEN']):
                print(f'Line {linenum:3}: Upload info {hbstr}: Line length')
            if(info == comdefines['BL_COM_UPLOADERR_CHECKSUM']):
                print(f'Line {linenum:3}: Upload info {hbstr}: Checksum')
        return False
    else:
        print(f'Line {linenum:3} {hbstr}: Unknown status {status}')
        return False

def upload_program(ser:Serial, hexfile: dict, comdefines, args):
    print(f'Starting upload: {len(hexfile['lines'])} lines...')
    num_errors = 0

    status = serial_send_code(ser, 'BL_COM_CMD_UPLOAD')
    if(status & comdefines['BL_COM_REPLY_STATUSMASK'] == comdefines['BL_COM_REPLY_OK']):
        for linenum, line in enumerate(hexfile['lines']):
            if(args.verbose):
                print(f'Line {linenum:3}: Line = {line.encode('ascii')} -> ', end='')
            ser.write(line[:9].encode('ascii'))

            header_reply = int.from_bytes(ser.read(size=1))
            if(upload_error_handling(header_reply, linenum, True, comdefines, args)):
                ser.write(line[9:].encode('ascii'))

                body_reply = int.from_bytes(ser.read(size=1))
                if(upload_error_handling(body_reply, linenum, False, comdefines, args)):
                    if(args.verbose):
                        print('Upload OK')
                else:
                    num_errors += 1
                    break
            else:
                num_errors += 1
                break

    else:
        print(f'Line {linenum:3} Error: upload request returned {status}')

    if(num_errors == 0):
        print(f'\t=> Upload complete!')
    else:
        print(f'\t=> Upload: {num_errors} errors occured!')
    

def extract_com_constants(filename):
    with open(filename, 'r') as fh:
        content = ''.join(fh.readlines())
        #matches = re.findall('#define (\\w+) ([0-9a-z\'"()<]+)\\n', content)
        char_matches = re.findall('#define (\\w+) \'([a-zA-Z])\'\n', content)
        char_matches = {a:(b.encode('ascii')) for (a,b) in char_matches}

        num_matches = re.findall('#define (\\w+) ([0-9]+)\n', content)
        num_matches  = {a:int(b) for (a,b) in num_matches}

        binary_matches = re.findall('#define (\\w+) 0b([0-9]+)\n', content)
        binary_matches = {a:int(b,2) for (a,b) in binary_matches}

        lshift_matches = re.findall('#define (\\w+) \\(([0-9]+)<<([0-9]+)\\)\n', content)
        lshift_matches = {a:(int(b)<<(int(c))) for (a,b,c) in lshift_matches}

        matches = char_matches | binary_matches | lshift_matches | num_matches

        return matches
    return {}

def check_checksum(line_without_colon, linenum):
    line = line_without_colon
    result = 0
    checksum = int(line[len(line)-2:], 16)
    to_check = line[:len(line) - 2]


    for i in range(int(len(to_check) / 2)):
        byte = int(line[i*2 : i*2 + 2], 16)
        result += byte
    
    calculated_checksum = (256 - result) % 256
    #print(f'Checksum calc on line {linenum}: Calculated: {hex(result)} => (inverse + 1) => {hex(calculated_checksum)} checksum: {hex(checksum)}')

    return calculated_checksum == checksum


def read_hex_file(filename, bootloader_start_address, verbose):
    hexfile = {}
    hexfile['data'] = []
    hexfile['lines'] = []
    hexfile['num_unknown_records'] = 0
    hexfile['bootloader_section_intersect'] = False
    hexfile['address_lowest'] = None
    hexfile['address_highest'] = None

    with open(filename, 'r') as fh:
        lines = fh.readlines()
        print(f'{len(lines)} lines')
        hexfile['lines'] = [line.strip() for line in lines]

        linenum = 0
        end_reached = False
        for line in lines:
            linenum += 1
            if not line.startswith(':'):
                print(f'Invalid line: {line}')
            line = line[1:].strip()

            bytecount = int(line[0:2], 16)
            rtype = line[6:8]

            if rtype == RT_DATARECORD:
                address = int(line[2:6], 16)
                if(address >= bootloader_start_address):
                    if not hexfile['bootloader_section_intersect']:
                        print(f'Warning: hex file contents intersect with bootloader')
                    hexfile['bootloader_section_intersect'] = True

                if(hexfile['address_lowest'] is None):
                    hexfile['address_lowest'] = address
                elif(address < hexfile['address_lowest']):
                    hexfile['address_lowest'] = address

                if(hexfile['address_highest'] is None):
                    hexfile['address_highest'] = address
                elif(address > hexfile['address_highest']):
                    hexfile['address_highest'] = address

                data = []
                data_binary = bytearray()
                for i in range(bytecount):
                    byte_str = line[(8+i*2):(10+i*2)]
                    data.append(int(byte_str, 16))
                    data_binary.extend(bytearray.fromhex(byte_str))

                checksum_ok = check_checksum(line, linenum)

                if not checksum_ok:
                    print(f'Line {linenum:4}: Data Record Checksum Error: {line}')
                if not end_reached:
                    hexfile['data'].append((bytecount, address, data, checksum_ok, data_binary))
            elif rtype == RT_EOF:
                if(verbose):
                    print(f'Line {linenum:4}: End of file reached')
                end_reached = True
            elif rtype == RT_STARTSEGMENTADDRESSRECORD:
                segment = int(line[8:12], 16)
                offset = int(line[12:16], 16)

                if(verbose):
                    print(f'Line {linenum:4}: Start Segment Address Record: segment={hex(segment)}, offset={hex(offset)}', end='')

                checksum_ok = check_checksum(line, linenum)
                if(verbose):
                    if checksum_ok:
                        print('(OK)')
                    else:
                        print('(CS ERROR)')

                hexfile['ssar'] = (segment, offset, checksum_ok)

            else:
                print(f'Line {linenum:4}: Unknown record type {rtype}!')
                hexfile['num_unknown_records'] += 1
    
    return hexfile



if __name__ == '__main__':
    os.system('color')

    parser = argparse.ArgumentParser(description='Upload firmware to Atmega328p based devices that run the corresponding bootloader')
    parser.add_argument('--port', '-p', help='serial port', required=True)
    parser.add_argument('--baudrate', type=int, default=19200, help="baudrate of serial connection")
    parser.add_argument('-f', '--file', help='firmware hex file')
    parser.add_argument('--no-upload', action='store_true', help='skip upload')
    parser.add_argument('--no-verify', action='store_true', help='skip upload verification')
    parser.add_argument('-r', '--fuses', action='store_true', help='read fuses')
    parser.add_argument('-i', '--info', action='store_true')
    parser.add_argument('--no-quit', action='store_true', help='don\'t quit bootloader after tasks are finished')
    parser.add_argument('-v', '--verbose', action='store_true')

    try:
        args = parser.parse_args()

        comheader_filename = 'uart-bootloader/uart-bootloader/bootloader-communication.h'
        print(f'Reading com header file: {comheader_filename}')
        comdefines = extract_com_constants(comheader_filename)
        #print('Com Defines:')
        #pprint.pp(comdefines)

        print(f'Trying to connect to bootloader on serial port {args.port} with BR {args.baudrate}...')

        ser = Serial(args.port, args.baudrate, timeout=100)
        time.sleep(0.1)

        # request misc information from bootloader
        bl_version = None
        bl_section_start = None
        status = serial_send_code(ser, 'BL_COM_CMD_INFO')
        if(status & comdefines['BL_COM_REPLY_STATUSMASK'] == comdefines['BL_COM_REPLY_OK']):
            bl_version_len = int.from_bytes(ser.read())
            bl_version = ser.read(size=bl_version_len).decode('ascii')
            bl_section_start_len = int.from_bytes(ser.read())
            bl_section_start = int.from_bytes(ser.read(size=bl_section_start_len), byteorder='little')

            if(args.info):
                print('Bootloader Information:')
                print(f'\tVersion: {bl_version}')
                print(f'\tTool Version: {TOOL_VERSION}')
                print(f'\tBootloader Section Start Address: 0x{bl_section_start:X}')
        else:
            print(f'Error: information request returned: {status}')

        # read fuses
        if(args.fuses):
            read_fuses(ser, comdefines, args)

        # hex file: upload and / or verify
        verify = not args.no_verify
        upload = not args.no_upload
        if(args.file and (verify or upload)):
            print(f'Reading hex input file {args.file}: ', end='')
            
            hexfile = read_hex_file(args.file, bl_section_start, args.verbose)
            
            if(upload):
                if(hexfile['bootloader_section_intersect']):
                    print('Skipping upload to preserve bootloader...')
                else:
                    upload_program(ser, hexfile, comdefines, args)
            else:
                print('Skipping upload (--no-upload)...')
            
            if(verify):
                verify_program(ser, hexfile, comdefines, args)
            else:
                print('Skipping verification (--no-verify)...')

        # Quit bootloader
        if(not args.no_quit):
            ser.write(comdefines['BL_COM_CMD_QUIT'])
            status = int.from_bytes(ser.read(size=1))
            if(status == comdefines['BL_COM_REPLY_QUITTING']):
                print('Bootloader quit OK')
            else:
                print(f'Error: Bootloader quit returned {status}')



    except SerialException as e:
        print(f'Serial port exception: {e}')
    except KeyboardInterrupt:
        exit()