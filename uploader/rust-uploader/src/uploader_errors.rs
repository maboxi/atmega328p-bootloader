use core::fmt;
use std::error::Error;

#[derive(Debug)]
pub enum ErrorKind {
    IO,
    PortNotFound,
    PortConnection,
    SerialWrite,
    SerialRead,
    
    BootloaderCMDResponse(u8, char),

    HexfileByte(usize, usize, usize),
    HexfileChecksum,
    HexfileLineParse,

    FileOpen,

    U32Parse,
    StringDecoding,
}

//type Result<'a, T> = std::result::Result<T, UploaderError>;

#[derive(Debug)]
pub struct UploaderError {
    kind: ErrorKind,
    source: Option<Box<dyn Error + 'static>>,
}

impl UploaderError {
    pub fn from(kind: ErrorKind) -> Self {
        UploaderError {
            kind, 
            source: None,
        }
    }

    pub fn new(kind: ErrorKind, src: Box<dyn Error>) -> Self {
        UploaderError {
            kind,
            source: Some(src),
        }
    }
}

impl fmt::Display for UploaderError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let info_kind = match self.kind {
            ErrorKind::HexfileByte(linenum, bytenum, morenum) => Some(format!("line {linenum}, byte {bytenum} + {morenum}")),
            ErrorKind::BootloaderCMDResponse(response, code) => Some(format!("code {code} => response = {response:b}")),
            _ => None
        };
        
        if let Some(info_kind) = info_kind {
            write!(f, "UploaderError: Kind = {:?}({}), source = {:?}", self.kind, info_kind, self.source())
        } else {
            write!(f, "UploaderError: Kind = {:?}, source = {:?}", self.kind, self.source())
        }
    }
}

impl std::error::Error for UploaderError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        self.source.as_deref()
    }

    fn cause(&self) -> Option<&(dyn Error + 'static)> {
        self.source()
    }
}