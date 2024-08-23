from serial import Serial, SerialException 
import argparse

RT_DATARECORD = '00'
RT_EOF = '01'
RT_STARTSEGMENTADDRESSRECORD = '03'

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


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Upload firmware to Atmega328p based devices that run the corresponding bootloader')
    parser.add_argument('--port', '-p', help='serial port', required=True)
    parser.add_argument('--baudrate', type=int, default=19200, help="baudrate of serial connection")
    parser.add_argument('--file', '-f', help='firmware hex file', required=True)
    parser.add_argument('--verbose', '-v', action='store_true')

    try:
        args = parser.parse_args()

        print(f'Reading hex input file {args.file}: ', end='')
        with open(args.file, 'r') as fh:
            lines = fh.readlines()
            print(f'{len(lines)} lines')

            linenum = 0
            for line in lines:
                linenum += 1
                if not line.startswith(':'):
                    print(f'Invalid line: {line}')
                line = line[1:].strip()

                bytecount = int(line[0:2], 16)
                rtype = line[6:8]

                if rtype == RT_DATARECORD:
                    address = int(line[2:6], 16)
                    data = []
                    for i in range(bytecount):
                        data.append(int(line[(8+i*2):(10+i*2)], 16))

                    checksum = line[(8 + 2*bytecount):]

                    if not check_checksum(line, linenum):
                        print(f'Line {linenum:4}: Data Record Checksum Error: {[hex(s).upper() for s in data]}')
                elif rtype == RT_EOF:
                    print(f'Line {linenum:4}: End of file!')
                elif rtype == RT_STARTSEGMENTADDRESSRECORD:
                    segment = int(line[8:12], 16)
                    offset = int(line[12:16], 16)
                    print(f'Line {linenum:4}: Start Segment Address Record: segment={hex(segment)}, offset={hex(offset)}', end='')
                    if check_checksum(line, linenum):
                        print('(OK)')
                    else:
                        print('(CS ERROR)')
 
                else:
                    print(f'Line {linenum:4}: Unknown record type {rtype}!')

        print(f'Trying to connect to bootloader on serial port {args.port} with BR {args.baudrate}...')

        ser = Serial(args.port, args.baudrate, timeout=1)
#        while True:
#            output = str(ser.read_until())
#            print(output, end='', flush=True)
    except SerialException as e:
        print(f'Serial port exception: {e}')
    except KeyboardInterrupt:
        exit()