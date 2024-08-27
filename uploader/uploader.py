from serial import Serial, SerialException 
import argparse
import re
import pprint
import time

TOOL_VERSION = "0.1"

RT_DATARECORD = '00'
RT_EOF = '01'
RT_STARTSEGMENTADDRESSRECORD = '03'

def serial_send_code(ser: Serial, code):
    ser.write(comdefines[code])
    return int.from_bytes(ser.read(size=1))


def extract_com_constants(filename):
    with open(filename, 'r') as fh:
        content = ''.join(fh.readlines())
        #matches = re.findall('#define (\\w+) ([0-9a-z\'"()<]+)\\n', content)
        char_matches = re.findall('#define (\\w+) \'([a-zA-Z])\'\n', content)
        char_matches = {a:(b.encode('ascii')) for (a,b) in char_matches}

        binary_matches = re.findall('#define (\\w+) 0b([0-9]+)\n', content)
        binary_matches = {a:int(b,2) for (a,b) in binary_matches}

        lshift_matches = re.findall('#define (\\w+) \\(([0-9]+)<<([0-9]+)\\)\n', content)
        lshift_matches = {a:(int(b)<<(int(c))) for (a,b,c) in lshift_matches}

        matches = char_matches | binary_matches | lshift_matches

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
    hexfile['num_unknown_records'] = 0
    hexfile['bootloader_section_intersect'] = False
    hexfile['address_lowest'] = None
    hexfile['address_highest'] = None

    with open(filename, 'r') as fh:
        lines = fh.readlines()
        print(f'{len(lines)} lines')

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
                data_binary = bytearray(source=':', encoding='ascii')
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
    import argparse

    parser = argparse.ArgumentParser(description='Upload firmware to Atmega328p based devices that run the corresponding bootloader')
    parser.add_argument('--port', '-p', help='serial port', required=True)
    parser.add_argument('--baudrate', type=int, default=19200, help="baudrate of serial connection")
    parser.add_argument('-f', '--file', help='firmware hex file')
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

        ser = Serial(args.port, args.baudrate, timeout=1)
        time.sleep(0.1)
        # request misc information from bootloader
        bl_version = None
        bl_section_start = None
        status = serial_send_code(ser, 'BL_COM_CMD_INFO')
        if(status & comdefines['BL_COM_REPLY_OK']):
            bl_version_len = int.from_bytes(ser.read())
            bl_version = ser.read(size=bl_version_len).decode('ascii')
            bl_section_start_len = int.from_bytes(ser.read())
            bl_section_start = int.from_bytes(ser.read(size=bl_section_start_len), byteorder='little')

            if(args.info):
                print('Bootloader Information:')
                print(f'\tVersion: {bl_version}')
                print(f'\tTool Version: {TOOL_VERSION}')
                print(f'\tBootloader Section Start Address: 0x{bl_section_start:x}')
        else:
            print(f'Error: information request returned: {status}')


        if(args.fuses):
            status = serial_send_code(ser, 'BL_COM_CMD_READFUSES')
            if(status & comdefines['BL_COM_REPLY_OK']):
                fuse_values = ser.read(size=3)
                print('Fuse values:')
                print(f'\tExtended: -----{(fuse_values[2] & 7):b}')
                print(f'\tHigh:     {fuse_values[1]:b}')
                print(f'\tLow:      {fuse_values[0]:b}')
            else:
                print(f'Error: fuse read request returned: {status}')

        if(args.file):
            verify = not args.no_verify
            print(f'Reading hex input file {args.file}: ', end='')
            
            hexfile = read_hex_file(args.file, bl_section_start, args.verbose)
            
            if(hexfile['bootloader_section_intersect']):
                print('Skipping upload to preserve bootloader...')
            else:
                print(f'TODO: upload hex file contents (verify: {verify})')

            if(verify):
                print('Verifying memory...')
                pagecounter = 0
                num_errors = 0
                for line in hexfile['data']:
                    status = serial_send_code(ser, 'BL_COM_CMD_VERIFY')
                    if(status & comdefines['BL_COM_REPLY_OK']):
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
                            if(byte != data_binary[i + 1]):
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
                                for byte in data_binary[1:]:
                                    print(f'{byte:02x} ', end='')
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




        if not args.no_quit:
            # Quit bootloader
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