use core::str;
use std::{io::{self, ErrorKind, Write}, sync::{atomic::{AtomicBool, Ordering}, Arc}, time::Duration};


fn main() -> io::Result<()> {
    let keep_reading = Arc::new(AtomicBool::new(true));
    let keep_reading_hook = keep_reading.clone();

    ctrlc::set_handler(move|| {
        println!("Received CTRL-C! Exiting...");
        keep_reading_hook.store(false, Ordering::Relaxed);
    }).expect("Error setting CTRL-C hook");


    let selected_port = "COM8";
    let selected_baudrate = 19200;

    println!("Attempting to connect to serial port '{selected_port}'...");

    let port_builder = serialport::new(selected_port, selected_baudrate);

    let mut sp = port_builder
        .timeout(Duration::from_millis(1000))
        .open()
        .unwrap_or_else(|err| {
            println!("Error connecting to serial port '{selected_port}': {err}");
            println!("Available Serial Ports: ");

            let avail_ports = serialport::available_ports().expect("Error reading available serial ports");
            for (num,portinfo) in avail_ports.iter().enumerate() {
                println!("\tPort {}: {}", num, portinfo.port_name);
            }

            panic!("Error connecting to selected serial port '{selected_port}'!");
        });

    let test_msg = "Hello World! Ready...".as_bytes();
    let num_transmitted = sp.write(test_msg).expect("Error writing to serial port...");

    assert_eq!(test_msg.len(), num_transmitted);

    println!("Receiving...");
    while keep_reading.load(Ordering::Relaxed) {
        let mut buffer = [0;10];
        let recv = sp.read(&mut buffer);
        match recv {
            Ok(num_recv) => {
               if num_recv == 0 {
                    continue;
                }
                
                print!("{}", str::from_utf8(&buffer).expect("Error parsing received data into utf-8 string"));
                io::stdout().flush().expect("Error flushing stdout...");
            },
            Err(e) => {
                match e.kind() {
                    ErrorKind::TimedOut => {
                        //println!("Serial port read timed out");
                    },
                    _ => {
                        println!("Serial port: other error {e}");
                    }
                }
            }
        }

    }
    
    return Ok(())
}
