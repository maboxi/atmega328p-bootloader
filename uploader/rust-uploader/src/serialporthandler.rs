use std::time::Duration;

use serialport::SerialPort;

use crate::{ErrorKind, UploaderError};

pub struct SerialPortHandler {
    sp: Box<dyn SerialPort>,
    data_buffer: Vec<u8>,
}

impl SerialPortHandler {
    pub fn open(port: &String, baudrate: u32) -> Result<Self, UploaderError> {
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
                    return Err(UploaderError::new(ErrorKind::PortNotFound, Box::new(err)));
                },
                _ => {
                    println!("Error connecting to serial port '{port}': {err} (ErrorKind: {:?})", err.kind());
                    return Err(UploaderError::new(ErrorKind::PortConnection, Box::new(err)));
                }
            }
        }

        println!("Success!");
        Ok(SerialPortHandler {
            sp: sp.unwrap(),
            data_buffer: vec![]
        })
    }

    pub fn write(&mut self, data: &[u8]) -> Result<(), UploaderError> {
        if self.sp.write(data).map_err(|err| UploaderError::new(ErrorKind::SerialWrite, Box::new(err)))? != data.len() {
            return Err(UploaderError::from(ErrorKind::SerialWrite));
        }
        return Ok(());
    }

    pub fn send_code(&mut self, code: char) -> Result<u8, UploaderError> {
        let write_buffer = [code as u8];

        if self.sp.write(&write_buffer).map_err(|err| UploaderError::new(ErrorKind::SerialWrite, Box::new(err)))? == 0 {
            panic!("Error sending code {code}");
            //return Err(Error::from(ErrorKind::TimedOut));
        }
        self.data_buffer.clear();
        return self.receive_byte();
    }
    
    pub fn receive_multiple(&mut self, n: usize) -> Result<Vec<u8>, UploaderError> {
        for _i in 0..5 {
            if self.data_buffer.len() < n {
                let mut read_buffer: [u8; 256] = [0; 256];
                let recv_info = self.sp.read(&mut read_buffer).map_err(|err| UploaderError::new(ErrorKind::SerialRead, Box::new(err)))?;

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

    pub fn receive_byte(&mut self) -> Result<u8, UploaderError> {
        let buf = self.receive_multiple(1)?;
        return Ok(buf[0]);
    }
}