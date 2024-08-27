/*
 * bootloader_communication.h
 *
 * Created: 24.08.2024 00:36:44
 *  Author: maxip
 */ 


#ifndef BOOTLOADER_COMMUNICATION_H_
#define BOOTLOADER_COMMUNICATION_H_

#define BL_COM_BL_READY 'r'

#define BL_COM_CMD_QUIT 'q'
#define BL_COM_CMD_READFUSES 'f'
#define BL_COM_CMD_INFO 'i'
#define BL_COM_CMD_UPLOAD 'u'
#define BL_COM_CMD_VERIFY 'v'

#define BL_COM_REPLY_STATUSMASK 0b01110000
#define BL_COM_REPLY_OK (7<<4)
#define BL_COM_REPLY_UNKNOWNCMD (6<<4)
#define BL_COM_REPLY_QUITTING (5<<4)
#define BL_COM_REPLY_NOTIMPLEMENTEDYET (4<<4)

#endif /* BOOTLOADER_COMMUNICATION_H_ */