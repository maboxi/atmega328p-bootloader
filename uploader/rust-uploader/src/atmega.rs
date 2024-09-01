pub struct BootloaderInfo {
    pub ssa: u16,
    pub version: String,
}
impl BootloaderInfo {
    pub fn new() -> Self {
        BootloaderInfo {
            ssa: 0,
            version: String::new(),
        }
    }
}


pub fn decode_fuses_locks(fuse_values: [u8; 4]) {
    let print_fuse = |fuse_val: u8, prompt: &str, text_unprogrammed: &str, text_programmed: &str| {
        println!("\t{prompt:<20}{}", if fuse_val > 0 {text_unprogrammed} else {text_programmed});
    };

    let locks = fuse_values[3];
    let efuse = fuse_values[2];
    let hfuse = fuse_values[1];
    let lfuse = fuse_values[0];


    // Extended Fuse Byte
    let ext_str = format!("{:b}", fuse_values[2]);
    println!("Extended: -----{}", &ext_str[5..]);

    let f_bod210 = efuse & 0b111;
    let mut min_typ_max_opt = None;

    print!("\tBrown Out Detection: ");
    match f_bod210 {
        0b111 => println!("BOD disabled!"),
        0b110 => min_typ_max_opt = Some([1.7, 1.8, 2.0]),
        0b101 => min_typ_max_opt = Some([2.5, 2.7, 2.9]),
        0b100 => min_typ_max_opt = Some([4.1, 4.3, 4.5]),
        _ => println!("Unknown / reserved BOD value {f_bod210}"),
    }

    if let Some([min, typ, max]) = min_typ_max_opt {
        print!("Min. V_bot = {min}, Typ. V_bot = {typ}, Max. V_bot = {max}");

    }


    // High Fuse Byte
    println!("High: {:b}", fuse_values[1]);
    let f_rstdisbl = (hfuse & (1<<7)) >> 7;
    let f_dwen = (hfuse & (1<<6)) >> 6;
    let f_spien = (hfuse & (1<<5)) >> 5;
    let f_wdton = (hfuse & (1<<4)) >> 4;
    let f_eesave = (hfuse & (1<<3)) >> 3;
    let f_bootsz10 = (hfuse & (1<<2 | 1<<1)) >> 1;
    let f_bootrst = hfuse & 1;
    
    print_fuse(f_rstdisbl, "External Reset: ", "Enabled", "Disabled");
    print_fuse(f_dwen, "debugWIRE: ", "Disabled", "Enabled");
    print_fuse(f_spien, "Serial Program and Data Downloading: ", "Disabled", "Enabled");
    print_fuse(f_wdton, "Watchdog Timer: ", "Not Always On", "Always On");
    print_fuse(f_eesave, "EEPROM Memory: ", "Preserved through the Chip Erase", "Deleted on Chip Erase");

    let mut bootsz_info = None;
    match f_bootsz10 {
        0b11 => bootsz_info = Some([64, 4, 0x3F00, 0x3FFF]),
        0b10 => bootsz_info = Some([64, 8, 0x3E00, 0x3FFF]),
        0b01 => bootsz_info = Some([64, 16, 0x3C00, 0x3FFF]),
        0b00 => bootsz_info = Some([64, 32, 0x3800, 0x3FFF]),
        _ => println!("\tError: Impossible bootsz value {f_bootsz10}")
    }

    if let Some(bootsz_info) = bootsz_info {
        println!("\tBoot Size: {} pages, page size = {} -> {} bytes", bootsz_info[1], bootsz_info[0], bootsz_info[0] * bootsz_info[1]);
        println!("\t\t=> Application Section: 0x0000 - 0x{:4X}", bootsz_info[2] - 1);
        println!("\t\t=> Boot Section: 0x{:4X} - 0x{:4X}", bootsz_info[2], bootsz_info[3]);

        let bootloader_start_str = format!("Boot Loader (0x{:4X})", bootsz_info[2]);
        print_fuse(f_bootrst, "Selected Reset Vector: ", "Application (0x0000)", &bootloader_start_str);
    }


    // Low Fuse Byte
    println!("Low: {:b}", fuse_values[0]);
    let f_ckdiv8 = (lfuse & (1<<7)) >> 7;
    let f_ckout = (lfuse & (1<<6)) >> 6;
    let f_sut10 = (lfuse & (1<<5 | 1<<4)) >> 4;
    let f_cksel3210 = lfuse & (1<<3 | 1<<2 | 1<<1 | 1);
    
    print_fuse(f_ckdiv8, "Clock Divided by 8: ", "No", "Yes");
    print_fuse(f_ckout, "Clock Output (PORTB0): ", "Disabled", "Enabled");

    print!("\tClock Source: ");
    match f_cksel3210 {
        0b1111 | 0b1110 | 0b1101 | 0b1100 | 0b1011 | 0b1010 | 0b1001 | 0b1000 => 
            println!("Lower Power Crystal Oscillator"),
        0b0111 | 0b0110 =>
            println!("Full Swing Crystal Oscillator"),
        0b0101 | 0b0100 =>
            println!("Lower Frequency Crystal Oscillator"),
        0b0011 =>
            println!("Internal 128kHz RC Oscillator"),
        0b0010 =>
            println!("Calibrated Internal RC Oscillator"),
        0b0000 => 
            println!("External Clock"),
        _ => 
            println!("Unknown ({f_cksel3210} is reserved)"),
    }

    println!("\tStart-up times for clock selection: SUT1..0 = {f_sut10} ");


    // Locks Byte
    println!("Locks: {:b}", fuse_values[3]);
    let blb1211 = (locks & 0b00110000) >> 4;
    let blb0201 = (locks & 0b00001100) >> 2;
    let lb21 = locks & 0b00000011;

    print!("\tExternal Protection Mode: ");
    match lb21 {
        0b11 => println!("1 => No memory lock features enabled"),
        0b10 => println!("2 => Programming of Flash and EEPROM is disabled for both Serial and Parallel programming, but Verification is still possible. The Fuse bits are locked in both Serial and Parallel programming mode"),
        0b00 => println!("3 => Programming and verification of Flash and EEPROM is disabled for both Serial and Parallel programming. The Fuse bits are locked in both Serial and Parallel programming mode"),
        _ => println!("Unknown value {lb21}"),
    }
    
    print!("\tInteral Protection Mode for Application section: ");
    match blb0201 {
        0b11 => println!("1 => No restrictions for SPM or LPM accessing the Application section"),
        0b10 => println!("2 => No write via SPM"),
        0b00 => println!("3 => No write via SPM, and no read from Boot Loader section via LPM"),
        0b01 => println!("4 => No read from Boot Loader section via LPM"),
        _ => println!("Unknown value {blb0201}"),
    }

    print!("\tInteral Protection Mode for Boot Loader section: ");
    match blb1211 {
        0b11 => println!("1 => No restrictions for SPM or LPM accessing the Boot Loader section"),
        0b10 => println!("2 => No write via SPM"),
        0b00 => println!("3 => No write via SPM, and no read from Application section via LPM"),
        0b01 => println!("4 => No read from Application section via LPM"),
        _ => println!("Unknown value {blb1211}"),
    }
}