#![allow(dead_code)]
//use std::sync::{atomic::{AtomicBool, Ordering}, Arc};
use clap::{value_parser, Arg, ArgAction, Command};

pub mod serialporthandler;
use serialporthandler::SerialPortHandler;

pub mod atmega;
use atmega::{decode_fuses_locks, BootloaderInfo};

pub mod hexutils;
use hexutils::{read_hexfile, RT_DATARECORD};

pub mod uploader_errors;
use uploader_errors::*;

const TOOL_VERSION: &str = "0.1";

const BL_COM_BL_READY: char = 'r';

const BL_COM_CMD_QUIT: char = 'q';
const BL_COM_CMD_READFUSES: char = 'f';
const BL_COM_CMD_INFO: char = 'i';
const BL_COM_CMD_UPLOAD: char = 'u';
const BL_COM_CMD_VERIFY: char = 'v';

const BL_COM_REPLY_STATUSMASK: u8 = 0b01110000;
const BL_COM_REPLY_OK: u8 = 7<<4;
const BL_COM_REPLY_UNKNOWNCMD: u8 = 6<<4;
const BL_COM_REPLY_QUITTING: u8 = 5<<4;
const BL_COM_REPLY_NOTIMPLEMENTEDYET: u8 = 4<<4;
const BL_COM_REPLY_UPLOADERROR: u8 = 3<<4;

const BL_COM_UPLOADINFO_MASK: u8 = 0b00001111;

const BL_COM_UPLOADERR_COLON: u8 = 1;
const BL_COM_UPLOADERR_HEXVAL_8: u8 = 2;
const BL_COM_UPLOADERR_HEXVAL_16: u8 = 3;
const BL_COM_UPLOADERR_LINELEN: u8 = 4;
const BL_COM_UPLOADERR_CHECKSUM: u8 = 5;

const BL_COM_UPLOADOK_FINISHED: u8 = 1;
const BL_COM_UPLOADOK_HEADEROK: u8 = 2;
const BL_COM_UPLOADOK_LINEOK: u8 = 3;


fn main() -> Result<(), UploaderError> {
    // Command-line options
    let matches = Command::new("atmega-uploader")
        .arg(Arg::new("port").short('p').long("port").help("Serial Port name"))
        .arg(Arg::new("baudrate").long("baudrate").help("Baudrate of serial connection").value_parser(value_parser!(u32)).default_value("19200"))
        .arg(Arg::new("hexfile").short('f').long("file").help("Firmware Intel Hex file"))
        .arg(Arg::new("skip-upload").long("no-upload").help("Skip firmware upload").action(ArgAction::SetTrue))
        .arg(Arg::new("skip-verify").long("no-verify").help("Skip firmware verify").action(ArgAction::SetTrue))
        .arg(Arg::new("read-fuses").short('r').long("fuses").help("Read fuses from microcontroller").action(ArgAction::SetTrue))
        .arg(Arg::new("info").short('i').long("info").help("Print information about bootloader").action(ArgAction::SetTrue))
        .arg(Arg::new("dont-quit").long("no-quit").help("Don't quit bootloader after tasks are finished").action(ArgAction::SetTrue))
        .arg(Arg::new("verbose").short('v').long("verbose").help("Verbose output").action(ArgAction::SetTrue))
        .get_matches();

    let verbose = matches.get_flag("verbose");
    let do_upload = !matches.get_flag("skip-upload");
    let do_verify = !matches.get_flag("skip-verify");
    let do_quit = !matches.get_flag("dont-quit");
    let read_fuses = matches.get_flag("read-fuses");
    let hexfile = matches.get_one::<String>("hexfile");

    //println!("Options: verbose={verbose}, upload={do_upload}, verify={do_verify}, quit={do_quit}");

    let selected_port = matches.get_one::<String>("port").unwrap().to_uppercase();
    let selected_baudrate = *matches.get_one::<u32>("baudrate").unwrap();


    // ctrl c handler
    /*
    let keep_reading = Arc::new(AtomicBool::new(true));
    let keep_reading_hook = keep_reading.clone();

    ctrlc::set_handler(move|| {
        println!("Received CTRL-C! Exiting...");
        keep_reading_hook.store(false, Ordering::Relaxed);
    }).expect("Error setting CTRL-C hook");
    */


    // start connection
    let mut sp = SerialPortHandler::open(&selected_port, selected_baudrate)?;
    println!();
    let is_status = |reply: u8, status: u8| (reply & BL_COM_REPLY_STATUSMASK) == status;

    let mut bootloader_info = BootloaderInfo::new();

    // Request informaton from bootloader
    let status = sp.send_code(BL_COM_CMD_INFO)?;
    if is_status(status, BL_COM_REPLY_OK) {
        let ver_len = sp.receive_byte()? as usize;
        bootloader_info.version = String::from_utf8(sp.receive_multiple(ver_len)?).map_err(|e| UploaderError::new(ErrorKind::StringDecoding, Box::new(e)))?;

        let ssa_len = sp.receive_byte()? as usize;
        let ssa_bytes = sp.receive_multiple(ssa_len)?;
        bootloader_info.ssa = u16::from_le_bytes(ssa_bytes.try_into().expect("Error converting bytes to int"));

        println!("Bootloader info:");
        println!("\tVersion: {}", bootloader_info.version);
        println!("\tBLS Start Address: 0x{:X} (word address: 0x{:X})", bootloader_info.ssa, bootloader_info.ssa/2);
        println!();
    } else {
        return Err(UploaderError::from(ErrorKind::BootloaderCMDResponse(status, BL_COM_CMD_INFO)));
    }

    if read_fuses {
        let status = sp.send_code(BL_COM_CMD_READFUSES)?;
        if is_status(status, BL_COM_REPLY_OK) {
            println!("Reading fuses...");
            let fuse_values = sp.receive_multiple(4)?;

            decode_fuses_locks(fuse_values.try_into().unwrap_or_else(|_| panic!("Error converting fuse value vec into array (shouldn't be possible!)")));
            println!();
        } else {
            return Err(UploaderError::from(ErrorKind::BootloaderCMDResponse(status, BL_COM_CMD_READFUSES)));
        }

    }
    
    if let Some(hexfile) = hexfile {
        if do_upload || do_verify {
            println!("Reading in hex file {hexfile}...");
            let hexfile = read_hexfile(&hexfile, bootloader_info.ssa, verbose)?;
            println!();

            if do_upload {
                if hexfile.ok_to_upload {
                    println!("Initiating hex file upload...");

                    let status = sp.send_code(BL_COM_CMD_UPLOAD)?;

                    let upload_error_handling = |header_reply: u8, _linenum: usize, _is_header: bool| -> bool {
                        is_status(header_reply, BL_COM_REPLY_OK)
                    };

                    if is_status(status, BL_COM_REPLY_OK) {
                        let mut num_errors = 0;
                        for (linenum, line_data) in hexfile.lines.iter().enumerate() {
                            let (linestr, _address, _bytecount, _rtype, _linebytes) = line_data;
                            if verbose {
                                print!("Line {linenum:3}: Line = {} -> ", linestr);
                            }
                            sp.write(&linestr[..9].as_bytes())?;

                            let header_reply = sp.receive_byte()?;
                            if upload_error_handling(header_reply, linenum, true) {
                                sp.write(&linestr[9..].as_bytes())?;

                                let body_reply = sp.receive_byte()?;
                                if upload_error_handling(body_reply, linenum, false) {
                                    if verbose {
                                        println!("Upload OK");
                                    }
                                } else {
                                    num_errors += 1;
                                    break;
                                }
                            } else {
                                num_errors += 1;
                                break;
                            }
                        }

                        if num_errors == 0 {
                            if let Some(addr_min) = hexfile.addr_min {
                                if let Some(addr_max) = hexfile.addr_max {
                                    let mem_usage = (addr_max - addr_min) as f64 / (bootloader_info.ssa as f64);
                                    println!("\t=> Upload complete! Memory usage: {:.1}%", 100.0 *mem_usage);
                                }
                            }
                        } else {
                            println!("\t=> Upload: {num_errors} errors occured!");
                        }

                    } else {
                        return Err(UploaderError::from(ErrorKind::BootloaderCMDResponse(status, BL_COM_CMD_INFO)));
                    }

                    println!();
                } else {
                    println!("Skipping upload to preserve bootloader section!");
                }
            }

            if do_verify {
                println!("Verifying upload...");
                let mut num_errors = 0;

                for (linenum, line_data) in hexfile.lines.iter().enumerate() {
                    let (_linestr, address, bytecount, rtype, linebytes) = line_data;
                    if *rtype != RT_DATARECORD {
                        continue;
                    }

                    let status = sp.send_code(BL_COM_CMD_VERIFY)?;
                    if is_status(status, BL_COM_REPLY_OK) {
                        if verbose {
                            print!("0x{address:4X} | {bytecount:2} | ");
                        }
                        sp.write(&address.to_be_bytes())?;
                        sp.write(&bytecount.to_be_bytes())?;

                        let memory_data = sp.receive_multiple(*bytecount as usize)?;

                        let mut error_detected = false;

                        for (bytenum, byte) in memory_data.iter().enumerate() {
                            if verbose {
                                print!("{byte:2X} ");
                            }
                            if *byte != linebytes[bytenum + 4] {
                                num_errors += 1;
                                error_detected = true;
                            }
                        }
                        if verbose {println!()}

                        if error_detected {
                            if verbose {
                                print!("should be     ");
                                for byte in &linebytes[4..linebytes.len()-1] {
                                    print!("{byte:2X} ");
                                }
                                println!();
                                println!();
                            }
                        }

                    } else {
                        println!("Error verifying line {linenum}!");
                        return Err(UploaderError::from(ErrorKind::BootloaderCMDResponse(status, BL_COM_CMD_VERIFY)));
                    }
                }

                if verbose {
                    println!();
                }

                if num_errors > 0 {
                    println!("\t=> Errors detected: {num_errors}");
                } else {
                    println!("\t=> No errors detected!");
                }
            }
        }
    }

    if do_quit {
        print!("Quitting bootloader...");
        let status = sp.send_code(BL_COM_CMD_QUIT)?;
        if is_status(status, BL_COM_REPLY_QUITTING) {
            println!("OK!");
        }  else {
            return Err(UploaderError::from(ErrorKind::BootloaderCMDResponse(status, BL_COM_CMD_QUIT)));
        }
    }
    
    return Ok(())
}