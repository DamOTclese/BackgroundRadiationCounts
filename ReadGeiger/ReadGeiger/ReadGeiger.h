#pragma once

// ----------------------------------------------------------------------
// ReadGeiger.h
//
// Fredric L. Rice, August 2020. Last update: 31/May/23
//
// Report mistakes and bugs to Fred@CrystalLake.Name
//
// ----------------------------------------------------------------------

#ifndef uchar
#define uchar   unsigned char
#endif
#ifndef ushort
#define ushort  unsigned short
#endif
#ifndef ulong
#define ulong   unsigned long
#endif

// ----------------------------------------------------------------------
// Consult GQ-GMC-ICD.odt for what we are descriving here.
// 
// For temperature, four bytes celsius degree data in hexdecimal:
//      BYTE1,BYTE2,BYTE3,BYTE4
//
// BYTE1 is the integer part of the temperature.
// BYTE2 is the decimal part of the temperature.
// BYTE3 is the negative sign if it is not 0. If this byte is 0, the
//       then temperature is  greater than 0, otherwise the temperature
//       is below 0.
// BYTE4 always 0xAA
//
// For retrieving the history using COMMAND_GET_HISTORY, A2,A1,A0 are
// three bytes address data, from MSB to LSB.  The L1,L0 are the data
// length requested.  L1 is high byte of 16 bit integer  and L0 is
// low byte.
//
// The length should not exceed 4096 bytes in each request.
//
// The minimum address is 0, and maximum address value is the size of
// the flash memory of the GQ GMC Geiger count. Check the user manual
// for particular model flash size.
//
// ----------------------------------------------------------------------

// ----------------------------------------------------------------------
// Consult GQ-GMC-ICD.odt for what we are descriving here.
// 
// Raw data from the ROM can contain date/time stamps that looks like
// this, with the stored count value appearing with every date/time frame
//
// 085 170 000 021 003 011 023 030 011 085 170 001
//   |   |   |   |   |   |   |   |   |   |   |   |____ Storing Counts (0-OFF, 1-CPS, 2-CPM, 3-Once per hour)
//   |   |   |   |   |   |   |   |   |   |   |________ Field Terminator 2
//   |   |   |   |   |   |   |   |   |   |____________ Field Terminator 1
//   |   |   |   |   |   |   |   |   |________________ Seconds
//   |   |   |   |   |   |   |   |____________________ Minutes
//   |   |   |   |   |   |   |________________________ Hours
//   |   |   |   |   |   |____________________________ Day
//   |   |   |   |   |________________________________ Month
//   |   |   |   |____________________________________ Year
//   |   |   |________________________________________ Field is date / time stamp RawDataHeaderTimestamp
//   |   |____________________________________________ Field Header 2
//   |________________________________________________ Field Header 1
//
// The double byte data sample format looks like this. When a value exceeds
// 255, then this header is used to indicate what the raw data that follows
// looks like in 2-byte values.
//
// 085 170 001 DHI DLO
//   |   |   |   |   |____ Least significant byte
//   |   |   |   |________ Most significant byte
//   |   |   |____________ Indicates that 2 byte data follows RawDataHeaderCPSIsDoubleByte
//   |   |________________ Field Header 2
//   |____________________ Field Header 1
//
// The double byte header is followed by DHI+DLO number of frames which contain
// two bytes oper value. So if DHI is the value 00 and DLO is the value 3, then
// three frames would follow containing doube byte readings which start with
// a header and look like this:
//
// 085 170 17 33 -- Start of frame followed by the 16 bit value
// 085 170 88 14 -- Start of frame followed by the 16 bit value
//
// Location data or label data is stored like this
//
// 085 170 002 LLL CCC CCC CCC CCC CCC CCC...
//   |   |   |   |   |__________________________ A string of ASCII characters
//   |   |   |   |______________________________ The number of bytes in the ASCII character list
//   |   |   |__________________________________ Location data a.k.a. Label RawDataHeaderCPSLocationData
//   |   |______________________________________ Field Header 2
//   |__________________________________________ Field Header 1
//

#define DATA_OUTPUT_FILE_NAME           "ReadGeiger.bin"
#define DATA_OUTPUT_ASCII_FILE_NAME     "ReadGeiger.txt"
#define DATA_OUTPUT_CSV_FILE_NAME       "ReadReiger.csv"
#define MAX_COMMAND_RETRIES             static_cast<int>(3)
#define MAX_FLASH_MEMORY                0xFFFF
#define MAX_DATA_READ_BLOCK_SIZE        2048
#define NO_RESPONSE_EXPECTED            static_cast<DWORD>(0)

// ----------------------------------------------------------------------
// The Geiger Counter's commands
//
// ----------------------------------------------------------------------
static char * CommandGetModelAndVersion  = "<GETVER>>";
static char * CommandGetCountsPerMinute  = "<GETCPM>>";
static char * CommandTurnOnHeartbeat     = "<HEARTBEAT1>>";
static char * CommandTurnOffHeartbeat    = "<HEARTBEAT0>>";
static char * CommandGetBatteryVoltage   = "<GETVOLT>>";
static char * CommandGetConfiguration    = "<GETCFG>>";
static char * CommandEraseConfiguration  = "<ECFG>>";
static char * CommandWriteConfiguration  = "<WCFGAD>>";
static char * CommandGetSerialNumber     = "<GETSERIAL>>";
static char * CommandTurnPowerOff        = "<POWEROFF>>";
static char * CommandReloadConfiguration = "<CFGUPDATE>>";
static char * CommandFactoryReset        = "<FACTORYRESET>>";
static char * CommandReboot              = "<REBOOT>>";
static char * CommandSetDateAndTime      = "<GETDATETIME>>";
static char * CommandGetTemperature      = "<GETTEMP>>";
static char * CommandGetGyroscope        = "<GETGYRO>>";
static char * CommandTurnPowerOn         = "<POWERON>>";
#define COMMAND_SET_DATE_AND_TIME       "<SETDATETIMEYMDHMS>>"
#define COMMAND_SET_YEAR                "<SETDATEYD>>"
#define COMMAND_SET_MONTH               "<SETDATEMD>>"
#define COMMAND_SET_DAY                 "<SETDATED>>"
#define COMMAND_SET_HOUR                "<SETTIMEH>>"
#define COMMAND_SET_MINUTE              "<SETTIMEM>>"
#define COMMAND_SET_SECOND              "<SETTIMES>>"
#define COMMAND_GET_HISTORY             "<SPIRAAALL>>"
#define COMMAND_PRESS_A_KEY             "<KEYD>>"

// ----------------------------------------------------------------------
// Consult GQ-GMC-ICD.odt for what we are descriving here.
//
// These octets are the frame type codes which follow the two octet
// frame marker. We also define a few more constants so that we may use
// them when scanning the raw data.
// 
// ----------------------------------------------------------------------
static const uchar RawDataHeaderTimestamp           = static_cast<uchar>(0);
static const uchar RawDataHeaderCPSIsDoubleByte     = static_cast<uchar>(1);
static const uchar RawDataHeaderCPSLocationData     = static_cast<uchar>(2);
static const uchar RawDataHeaderTripleByteCPS       = static_cast<uchar>(3);
static const uchar RawDataHeader4ByteCPS            = static_cast<uchar>(4);
static const uchar RawDataHeaderWhichTubeIsSelected = static_cast<uchar>(5);
static const uchar RawDataHeaderEndOfData           = static_cast<uchar>(0xFF);
static const uchar RawDataTerm1                     = static_cast<uchar>(0x55);
static const uchar RawDataTerm2                     = static_cast<uchar>(0xAA);

// ----------------------------------------------------------------------
// When we offer a menu, we provide these characters for each option
//
// ----------------------------------------------------------------------
static const uchar MenuItemRetrieveData         = static_cast<uchar>('1');
static const uchar MenuItemScanHighPeriods      = static_cast<uchar>('2');
static const uchar MenuItemSetDateAndTime       = static_cast<uchar>('3');
static const uchar MenuItemTurnPowerOn          = static_cast<uchar>('4');
static const uchar MenuItemTurnPowerOff         = static_cast<uchar>('5');
static const uchar MenuItemDisplayConfiguration = static_cast<uchar>('6');
static const uchar MenuItemEraseRawData         = static_cast<uchar>('E');
static const uchar MenuItemFactoryReset         = static_cast<uchar>('F');
static const uchar MenuItemExitTheProgram       = static_cast<uchar>('X');

// ----------------------------------------------------------------------
// For offering various colors, here are the usual defined values
//
// ----------------------------------------------------------------------
#ifndef BLACK
#define BLACK           0
#define BLUE            1
#define GREEN           2
#define CYAN            3
#define RED             4
#define MAGENTA         5
#define BROWN           6
#define LIGHTGRAY       7
#define DARKGRAY        8
#define LIGHTBLUE       9
#define LIGHTGREEN      10
#define LIGHTCYAN       11
#define LIGHTRED        12
#define LIGHTMAGENTA    13
#define YELLOW          14
#define WHITE           15
#endif

// The command to set the date and time in a single command frame is described
// here where the year, month, day, hour, minute, second is a single byte. We
// describe the offset in to the command byte array so that we can use them with
// some commentary in the source code. The offset is the typical 0-based C array.
// 01234567890123456789
// <SETDATETIMEYMDHMS>>
//             ||||||_______ Offset 17
//             |||||________ Offset 16
//             ||||_________ Offset 15
//             |||__________ Offset 14
//             ||___________ Offset 13
//             |____________ Offset 12

static const uchar SET_TIME_OFFSET_YEAR   = static_cast<uchar>(12);
static const uchar SET_TIME_OFFSET_MONTH  = static_cast<uchar>(13);
static const uchar SET_TIME_OFFSET_DAY    = static_cast<uchar>(14);
static const uchar SET_TIME_OFFSET_HOUR   = static_cast<uchar>(15);
static const uchar SET_TIME_OFFSET_MINUTE = static_cast<uchar>(16);
static const uchar SET_TIME_OFFSET_SECOND = static_cast<uchar>(17);

