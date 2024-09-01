use std::{fs::read, io::{Error, ErrorKind, Write}, sync::{atomic::{AtomicBool, Ordering}, Arc}, time::Duration};
use clap::{value_parser, Arg, ArgAction, Command};
use serialport::SerialPort;

const STATUS_MASK: u8 = 0b01110000;
const STATUS_OK: u8 = 7<<4;

struct SerialPortHandler {
    sp: Box<dyn SerialPort>,
    data_buffer: Vec<u8>,
}

impl SerialPortHandler {
    fn open(port: &String, baudrate: u32) -> Result<Self, Error> {
        print!("Trying to connect to serial port {port} with baudrate {baudrate}... ");
        let port_builder = serialport::new(port, baudrate);

        let sp = port_builder
            .timeout(Duration::from_millis(5000))
            .open();

        if sp.is_err() {
            println!();
            let err = sp.unwrap_err();

            match err.kind() {
                serialport::ErrorKind::NoDevice => {
                    let avail_ports = serialport::available_ports().expect("Error reading available serial ports");
                    let port_blocked= avail_ports.iter().find(|port_info| port_info.port_name.eq_ignore_ascii_case(port.as_str())).is_some();

                    if port_blocked {
                        println!("Serial port '{port}' is in use!");
                    } else {
                        println!("Serial port '{port}' does not exist!");
                        print!("Available Serial Ports: ");
                        for (i, portinfo) in avail_ports.iter().enumerate() {
                            if i != 0 {
                                print!(", ");
                            }
                            print!("{}", portinfo.port_name);
                        }
                        println!();
                    }
                    return Err(Error::new(ErrorKind::NotFound, err));
                },
                _ => {
                    println!("Error connecting to serial port '{port}': {err} (ErrorKind: {:?})", err.kind());
                    return Err(Error::new(ErrorKind::Other, err));
                }
            }
        }

        println!("Success!");
        Ok(SerialPortHandler {
            sp: sp.unwrap(),
            data_buffer: vec![]
        })
    }

    fn send_code(&mut self, code: char) -> Result<u8, Error> {
        let write_buffer = [code as u8];

        if self.sp.write(&write_buffer)? == 0 {
            panic!("Error sending code {code}");
            //return Err(Error::from(ErrorKind::TimedOut));
        }
        self.data_buffer.clear();
        return self.receive_byte();
    }
    
    fn receive_multiple(&mut self, n: usize) -> Result<Vec<u8>, Error> {
        for _i in 0..5 {
            if self.data_buffer.len() < n {
                let mut read_buffer: [u8; 256] = [0; 256];
                let recv_info = self.sp.read(&mut read_buffer)?;

                if recv_info == 0 {
                    panic!("Error receiving {n} bytes");
                    //return Err(Error::from(ErrorKind::TimedOut));
                }

                self.data_buffer.extend(&read_buffer[..recv_info]);
            } else {
                break;
            }
        }

        if self.data_buffer.len() >= n {
            let ret_vec = self.data_buffer.drain(..n).collect();
            return Ok(ret_vec);
        } else {
            panic!("Couldn't read enough data from serial port!"); // TODO: change into return Err(...)
        }
    }

    fn receive_byte(&mut self) -> Result<u8, Error> {
        let buf = self.receive_multiple(1)?;
        return Ok(buf[0]);
    }
}

fn status_ok(status: u8) -> bool {
    (status & STATUS_MASK) == STATUS_OK
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Command-line options
    let matches = Command::new("atmega-uploader")
        .arg(Arg::new("port").short('p').long("port").help("Serial Port name").required(true))
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

    //println!("Options: verbose={verbose}, upload={do_upload}, verify={do_verify}, quit={do_quit}");

    let selected_port = matches.get_one::<String>("port").unwrap().to_uppercase();
    let selected_baudrate = *matches.get_one::<u32>("baudrate").unwrap();


    // ctrl c handler
    let keep_reading = Arc::new(AtomicBool::new(true));
    let keep_reading_hook = keep_reading.clone();

    ctrlc::set_handler(move|| {
        println!("Received CTRL-C! Exiting...");
        keep_reading_hook.store(false, Ordering::Relaxed);
    }).expect("Error setting CTRL-C hook");


    // start connection
    let mut sp = SerialPortHandler::open(&selected_port, selected_baudrate)?;
    println!();

    let mut version_str: String;

    // Request informaton from bootloader
    let status = sp.send_code('i')?;
    if status_ok(status) {
        println!("Receiving info");
        let ver_len = sp.receive_byte()? as usize;
        let ver_bytes = sp.receive_multiple(ver_len)?;
        version_str = String::from_utf8(ver_bytes).map_err(|e| Error::new(ErrorKind::InvalidData, e))?;

        let ssa_len = sp.receive_byte()? as usize;
        let ssa_bytes = sp.receive_multiple(ssa_len)?;
        let ssa = u16::from_le_bytes(ssa_bytes.try_into().expect("Error converting bytes to int"));

        println!("Bootloader info:");
        println!("\tVersion: {version_str}");
        println!("\tBLS Start Address: {ssa:x}")
    }
    
    return Ok(())
}
