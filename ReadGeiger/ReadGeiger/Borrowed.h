
// ----------------------------------------------------------------------
// Borrowed.h
//
// Fredric L. Rice, August 2020. Last update: 31/May/23
// 
// This header describes aspects of the data format as it is found in
// the 65K of ROM data that may be downloaded from the GMC-300E Geiger
// Muller Counter Data Logger. The documentation describing the data
// format from the company that manufactures the device is not very
// well formatted so some guess work is needed to discern the format
// despite the text files provided by the company to document the 
// data format.
// 
// Document GQ-RFC1201 describes the communications between the 
// GMC-300E device and the computer.
// 
// GQ-GMC-ICD.odt describes the data format insofar as the history
// data that gets downloaded is concerned.
// 
// The manufacturer's web pages for the device generally start here:
// https://www.gqelectronicsllc.com/comersus/store/comersus_viewItem.asp?idProduct=4570
// 
// ----------------------------------------------------------------------

#pragma once

// ----------------------------------------------------------------------
// 0x55 0xAA is the two byte frame header
// 0x00 - For date and time or timestamp
// 0x01 - CPS is double byte
// 0x02 - Location data
// 0x03 - triple byte CPS/CPM/CPH
// 0x04 - 4 bytes CPS/CPM/CPH
// 0x05 - which tube is selected 0 is both
// 
// Anything outside of those frames is CPS/CPM/CPH data
// 
// When two octets of 255 are encountered, this software considers 
// that to indicate the end of valid history data.
//
// ----------------------------------------------------------------------

/// <summary>
/// Declare a structure for storage of the configuration data.
/// This is a replica of the GQ GMC's internal configuration data.
/// This is referred to as the host computer's local copy of the
/// GQ GMC's NVM configuration data elsewhere in the documentation.
/// 
/// The getConfigurationData method will deposit the GQ GMC's NVM data
/// into this structure. All configuration data is the binary value.
/// Booleans are just 0 or 1. But remember that the byte order is
/// big endian for multibyte data. So multibyte data may require
/// reversing the byte order for display to a user. Those data
/// whose semantics are understood are documented below, otherwise
/// no explanation means either we have no interest or the parameter
/// is better left to being updated physically using the GQ GMC's
/// front panel keys.
/// </summary>
typedef struct cfg_data_t
{
    uint8_t powerOnOff;              // byte 0
    uint8_t alarmOnOff;
    uint8_t speakerOnOff;
    uint8_t graphicModeOnOff;
    uint8_t backlightTimeoutSeconds; // byte 4
    uint8_t idleTitleDisplayMode;
    uint8_t alarmCPMValueHiByte;
    uint8_t alarmCPMValueLoByte;
    uint8_t calibrationCPMHiByte_0; // byte 8
    uint8_t calibrationCPMLoByte_0;
    uint8_t calibrationSvUcByte3_0;
    uint8_t calibrationSvUcByte2_0;
    uint8_t calibrationSvUcByte1_0; // byte 12
    uint8_t calibrationSvUcByte0_0;
    uint8_t calibrationCPMHiByte_1;
    uint8_t calibrationCPMLoByte_1;
    uint8_t calibrationSvUcByte3_1; // byte 16
    uint8_t calibrationSvUcByte2_1;
    uint8_t calibrationSvUcByte1_1;
    uint8_t calibrationSvUcByte0_1;
    uint8_t calibrationCPMHiByte_2; // byte 20
    uint8_t calibrationCPMLoByte_2;
    uint8_t calibrationSvUcByte3_2;
    uint8_t calibrationSvUcByte2_2;
    uint8_t calibrationSvUcByte1_2; // byte 24
    uint8_t calibrationSvUcByte0_2;
    uint8_t idleDisplayMode;
    uint8_t alarmValueuSvUcByte3;
    uint8_t alarmValueuSvUcByte2;   // byte 28
    uint8_t alarmValueuSvUcByte1;
    uint8_t alarmValueuSvUcByte0;
    uint8_t alarmType;
    // saveDataType specifies both the interval of data logging where
    // 0 = data logging is off, 1 = once per second, 2 = once per minute,
    // 3 = once per hour and the type of data saved where 0 = don't care,
    // 1 = counts/second, 2 = counts/minute, 3 = CPM averaged over hour.
    // Whenever this is changed, the GQ GMC inserts a date/timestamp
    // into the history data buffer.
    uint8_t saveDataType;           // byte 32
    uint8_t swivelDisplay;
    uint8_t zoomByte3;
    uint8_t zoomByte2;
    uint8_t zoomByte1;              // byte 36
    uint8_t zoomByte0;
    // dataSaveAddress represents the address of the first sample following
    // the insertion of a date/timestamp or label tag into the data buffer.
    // Periodically, a label or date/timestamp will be put into the buffer
    // without a change made to dataSaveAddress. So you always have to be
    // on the lookout for 55AA sequence when parsing data buffer. But you
    // have to do that anyway because you might encounter double byte data.
    uint8_t dataSaveAddress2;
    uint8_t dataSaveAddress1;
    uint8_t dataSaveAddress0;       // byte 40
                                    // dataReadAddress semantics is unknown. As far as I have seen,
                                    // its value is always zero.
    uint8_t dataReadAddress2;
    uint8_t dataReadAddress1;
    uint8_t dataReadAddress0;
    uint8_t nPowerSavingMode;       // byte 44
    uint8_t nSensitivityMode;
    uint8_t nCounterDelayHiByte;
    uint8_t nCounterDelayLoByte;
    uint8_t nVoltageOffset;         // byte 48
    uint8_t maxCPMHiByte;
    uint8_t maxCPMLoByte;
    uint8_t nSensitivityAutoModeThreshold;
    // saveDateTimeStamp is the date/timestamp of the logging run,
    // all data following up to the next date/timestamp are marked
    // in time by this date/timestamp, where
    uint8_t saveDateTimeStampByte5; // = year (last two digits) // byte 52
    uint8_t saveDateTimeStampByte4; // = month
    uint8_t saveDateTimeStampByte3; // = day
    uint8_t saveDateTimeStampByte2; // = hour
    uint8_t saveDateTimeStampByte1; // = minute // byte 56
    uint8_t saveDateTimeStampByte0; // = second
                                    // maxBytes is always 0xff
    uint8_t maxBytes;
    uint8_t spare[197];             // add spare to total 256 bytes
} CFG_Data;

#ifndef stristr
extern char* stristr(const char* str1, const char* str2);
#endif

extern void SetColorAndBackground(int ForgC, int BackC = BLACK);

