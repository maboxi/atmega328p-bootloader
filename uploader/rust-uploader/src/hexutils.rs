use std::{fs::File, io::{BufRead, BufReader}, num::ParseIntError};

use crate::uploader_errors::*;

pub const RT_DATARECORD: u8 = 0x0;
pub const RT_EOF: u8 = 0x1;
pub const RT_STARTSEGMENTADDRESSRECORD: u8 = 0x3;

pub struct Hexfile {
    pub parse_complete: bool,
    pub ok_to_upload: bool,

    pub eof_reached: bool,
    pub lines_after_eof: usize,
    pub num_checksum_errors: usize,
    pub byte_errors: Vec<(usize, usize, ParseIntError)>,

    pub ssa: Option<u32>,
    pub lines:Vec<(String, u16, u8, u8, Vec<u8>)>, // line, addr, bytecount, rtype, bytes
    pub addr_min: Option<u16>,
    pub addr_max: Option<u16>,
}

impl Hexfile {
    fn new() -> Self {
        Hexfile {
            eof_reached: false,
            lines_after_eof: 0,
            num_checksum_errors: 0,
            byte_errors: vec![],
            parse_complete: false,
            ok_to_upload: false,

            lines: vec![],
            ssa: None,
            addr_max: None,
            addr_min: None,
        }
    }
}


pub fn read_hexfile(hexfile: &str, bootloader_ssa: u16, verbose: bool) -> Result<Hexfile, UploaderError> {
    let file = File::open(hexfile).map_err(|err| UploaderError::new(ErrorKind::FileOpen, Box::new(err)))?;
    let lines = BufReader::new(file).lines();
    let mut hexfile = Hexfile::new();

    let checksum_ok = |bytes: &Vec<u8>| bytes.iter().map(|b8| *b8 as usize).sum::<usize>() % 256 == 0;

    let debug_print = |s: String| {
        if verbose {
            print!("{s}");
        }
    };

    let debug_println = |s: String| {
        if verbose {
            println!("{s}");
        }
    };


    for (linenum, line) in lines.enumerate() {
        if hexfile.eof_reached { hexfile.lines_after_eof += 1; }

        let line = line.map_err(|err| UploaderError::new(ErrorKind::HexfileLineParse, Box::new(err)))?;

        let mut byte_errors = vec![];
        let bytes: Vec<u8> = (0..(line.len()-1) / 2)
            .map(|i|    u8::from_str_radix(&line[(2*i+1)..(2*i+3)], 16))
            .enumerate()
            .filter_map(|(bytenum, elem)| elem.map_err(|err| byte_errors.push((linenum, bytenum, err))).ok())
            .collect();


        if byte_errors.len() > 0 {
            debug_println(format!("\t=> Error converting bytes:"));
            for (_, bytenum, err) in &byte_errors {
                debug_println(format!("\t\tByte {bytenum:2}: {err}"));
            }
            hexfile.byte_errors.extend(byte_errors);
            continue;
        }

        if verbose {
            print!("Line {linenum:4}: :");
            for (bytenum, byte) in bytes.iter().enumerate() {
                if bytenum == 4 || bytenum == bytes.len() - 1 { debug_print(format!(" ")); }
                debug_print(format!("{byte:0>2X}"));
            }
            debug_println("".to_owned());
        }

        let bytecount = bytes[0];
        let rtype = bytes[3];
        let address_bytes = [bytes[1], bytes[2]];
        let address = u16::from_be_bytes(address_bytes);

        debug_print(format!("\t=> "));

        match rtype {
            RT_DATARECORD => {
                debug_print(format!("Data Record: {bytecount} bytes"));

                if checksum_ok(&bytes) {
                    debug_print(format!(", Checksum OK"));
                } else {
                    debug_println(format!(", Checksum Error!"));
                    hexfile.num_checksum_errors += 1;

                    continue;
                }

                if let Some(addr_max) = hexfile.addr_max {
                    if address > addr_max {
                        hexfile.addr_max = Some(address);
                    }
                } else {
                    hexfile.addr_max = Some(address);
                }

                if let Some(addr_min) = hexfile.addr_min {
                    if address < addr_min {
                        hexfile.addr_min = Some(address);
                    }
                } else {
                    hexfile.addr_min = Some(address);
                }
           },
            RT_EOF => {
                hexfile.eof_reached = true;
                debug_print(format!("End of File"));

                if checksum_ok(&bytes) {
                    debug_print(format!(", Checksum OK"));
                } else {
                    debug_println(format!(", Checksum Error!"));
                    hexfile.num_checksum_errors += 1;

                    continue;
                }

            },
            RT_STARTSEGMENTADDRESSRECORD => {
                let ssa_bytes = [bytes[4], bytes[5], bytes[6], bytes[7]];
                let ssa = u32::from_be_bytes(ssa_bytes);

                debug_print(format!("Start Segment Address: 0x{ssa:4X}"));

                if checksum_ok(&bytes) {
                    debug_print(format!(", Checksum OK"));
                } else {
                    debug_println(format!(", Checksum Error!"));
                    hexfile.num_checksum_errors += 1;

                    continue;
                }

                hexfile.ssa = Some(ssa);
            },
            _ => debug_println(format!("Unknown Record Type: {rtype}")),
        }

        hexfile.lines.push((String::from(line.trim()), address, bytecount, rtype, bytes)); // line, addr, bytecount, rtype, bytes

        debug_println("".to_owned());
    }

    if hexfile.lines_after_eof > 0 {
        println!("Warning: {} lines after EOF record!", hexfile.lines_after_eof);
    }

    if hexfile.byte_errors.len() > 0 {
        println!("Error: {} byte conversion errors while parsing the hexfile!", hexfile.byte_errors.len());
        let (linenum, bytenum, err) = hexfile.byte_errors.remove(0);
        return Err(UploaderError::new(ErrorKind::HexfileByte(linenum, bytenum, hexfile.byte_errors.len() - 1), Box::new(err)));
    }

    if hexfile.num_checksum_errors > 0 {
        println!("Error: {} checksum errors!", hexfile.num_checksum_errors);
        return Err(UploaderError::from(ErrorKind::HexfileChecksum))
    }

    if let Some(addr_max) = hexfile.addr_max {
        if addr_max >= bootloader_ssa {
            println!("Warning: hex file contents intersect bootloader section!");
        } else {
            hexfile.ok_to_upload = true;
        }
    }

    println!("=> Hexfile parsing complete!");
    hexfile.parse_complete = true;

    Ok(hexfile)
}