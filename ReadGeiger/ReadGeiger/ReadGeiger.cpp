
// ----------------------------------------------------------------------
// ReadGeiger.cpp
//
// Fredric L. Rice, August 2020. Last update: 31/May/23
//
// Report mistakes and bugs to: Fred@CrystalLake.Name
//
// ----------------------------------------------------------------------

#include <list>
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <time.h>
#include <fstream>
#include "ReadGeiger.h"
#include "Borrowed.h"

using namespace std;

    /// <summary>
    /// Locally-allocated data which we ask the compiler to establish for us. We do not
    /// always expet these data elements to be allocated in ZeroVars so before they are
    /// used, the code considers the fact that their values may not be initialized.
    /// 
    /// </summary>
    static HANDLE       hComm;                                               // Handle we will use when we need one to the communication channel
    static char         deviceModelAndVersion[21];                           // Usually 14 bytes
    static char         deviceSerialNumber[11];                              // Usually 7 bytes
    static char         deviceTemperature[11];                               // Usually 4 bytes
    static char         devicBatteryVoltage[11];                             // Usually 1 byte
    static char         deviceDateAndTime[11];                               // Usually 7 bytes
    static CFG_Data     deviceConfiguration;                                 // Documentation says to expect 256 bytes
    static char         receivedData[MAX_DATA_READ_BLOCK_SIZE + 0x100];      // Maximum receive frame
    static char         entireFlashImage[MAX_FLASH_MEMORY];                  // Stores the entire FLASH data
    static bool         hasRawData;                                          // TRUE if we have the device's raw data, else FALSE
    static bool         hasClicksPerMinute;                                  // TRUE if we have clicks per minute information, else FALSE
    static list<ushort> listCPMData;                                         // Clicks Per Minute data in a list container
    static list<ulong>  listSuperHighEventIndexValues;                       // Holds the index in to the raw data where high events happen
    static char         labelString[255];                                    // Holds a NULL-terminated ASCII text as retrieved from the last scanned label string

    static char * theMonths[] = 
    {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    } ;

/// <summary>
/// The current Windows date and time is retrieved using either Universal Time or Local Time.
/// The value is converted in to an ASCII null-terminated string and gets returned.
/// </summary>
/// <returns>A pointer to the statically-allocated NULL-terminated ASCII string
/// containing the current date and time</returns>
static char * GetDateAndTimeString(void)
{
    static char DateAndTime[31] = { 0 };
    time_t      currentTime     = time(NULL);
    struct tm   currentGMTTime;

    // Acquire the current date and time, conditionally Universal or Local based onwindows time zone
#if 0
    (void)gmtime_s(&currentGMTTime, &currentTime);
#else
    (void)localtime_s(&currentGMTTime, &currentTime);
#endif

    // Build a C NULL-terminated string containing the date and time
    (void)sprintf_s(DateAndTime, sizeof(DateAndTime), "%02u%s%02u.%02u.%02u.%02u",
        currentGMTTime.tm_mday,
        theMonths[currentGMTTime.tm_mon],
        currentGMTTime.tm_year - 100,
        currentGMTTime.tm_hour,
        currentGMTTime.tm_min,
        currentGMTTime.tm_sec);

    // Return a pointer to the string
    return DateAndTime;
}

/// <summary>
/// This will send the string passed to it by argument to the communications
/// device using the handle which must already be opened to the device.
/// </summary>
/// <param name="pThisString">A pointer to the string of data to send to the device</param>
/// <param name="numberOfBytesToSend">The umber ofbytes to send to the device</param>
/// <returns>true if verything was successful, otherwise false</returns>
static bool SendThisString(char * pThisString, 
    DWORD numberOfBytesToSend)
{
    if (! WriteFile(hComm, pThisString, numberOfBytesToSend, &numberOfBytesToSend, NULL))
    {
        return false;
    }

    return true;
}

/// <summary>
/// The bytes waiting to be received from the device is accumulated and returned
/// in the buffer provided by parameter argument, returning the umber of bytes,
/// if any, which were received.
/// </summary>
/// <param name="inToThisBuffer">A pointer to the buffer to hold the received data from the device</param>
/// <param name="maxReceiveCount">The maximum number of bytes which the buffer may safely hold</param>
/// <returns>The number of bytes received, or 0 if no bytes were received</returns>
static DWORD ReceiveResponse(char *inToThisBuffer, 
    DWORD maxReceiveCount)
{
    DWORD resultByteCount       = static_cast<DWORD>(0);
    DWORD receivedByteCount     = static_cast<DWORD>(0);
    char  afterReadToClear[101] = { 0 };

    // Make sure that we were passed valid arguments
    if (inToThisBuffer != nullptr && maxReceiveCount > static_cast<DWORD>(0))
    {
        while(receivedByteCount < maxReceiveCount)
        {
            // Attempt to read the serial interface for a response
            if (! ReadFile(hComm, &inToThisBuffer[receivedByteCount], maxReceiveCount, &resultByteCount, NULL))
            {
                // That failed so make sure that the returning  byte count indicates no response
                receivedByteCount = static_cast<DWORD>(0);
                break;
            }
            else
            {
                if (resultByteCount == static_cast<DWORD>(0))
                {
                    // It timed out so there are no more bytes
                    break;
                }
                else
                {
                    // We expect to get the full block size however we keep track of what was sent
                    receivedByteCount += resultByteCount;
                }
            }
        }

        // For some inbound data we have a response termination of 0xAA so
        // what we do here is a fairly large read from the receive buffer
        // to see if anything else is in the device as a response to the
        // command we just sent, any data of which we simply discard
        if (ReadFile(hComm, &afterReadToClear, sizeof(afterReadToClear), &resultByteCount, NULL))
        {
            if (resultByteCount > static_cast<DWORD>(0))
            {
            }
        }
    }

    // Report the number of bytes received
    return receivedByteCount;
}
/// <summary>
/// This functon will send a command passed to it by argument consisting of
/// ASCII text which is NULL-terminated, it will perform a slight delay to
/// afford the device time to respond, if it wants to, and will read the 
/// response from the device.
/// </summary>
/// <param name="thisCommand">A pointer to the NULL-terminated string containing the
/// command to send to the device</param>
/// <param name="numberOfBytesToSend">The umber ofbytes in the command to send to the device</param>
/// <param name="inToThisReceiveBuffer">A buffer which will contain the received response, if any</param>
/// <param name="maxReceiveCount">The maximum number of bytes which the receive buffer may hold</param>
/// <returns></returns>
static bool RetrySendCommandAndGetResponse(char * thisCommand, 
    DWORD numberOfBytesToSend, 
    char * inToThisReceiveBuffer, 
    DWORD maxReceiveCount)
{
    bool theReturnResult = false;

    // Make sure that we have a valid command to send
    if (thisCommand != nullptr && numberOfBytesToSend > 0)
    {
        // In the event we do not get a response and we expect one, we re-send and try again
        for (int thisAttempt = static_cast<int>(0); thisAttempt < MAX_COMMAND_RETRIES; thisAttempt++)
        {
            // Send the command
            SendThisString(thisCommand, numberOfBytesToSend);

            // Afford the device some time to formulate and send a response, if any
            Sleep(static_cast<DWORD>(250));

            // Are we expecting a response from the device?
            if (inToThisReceiveBuffer != nullptr && maxReceiveCount > static_cast<DWORD>(0))
            {
                // See if there was a response. Any response is considered acceptable
                if (ReceiveResponse(inToThisReceiveBuffer, maxReceiveCount) > static_cast<DWORD>(0))
                {
                    // We receive a response so report success
                    theReturnResult = true;
                    break;
                }

                // We did not get a response, were we not expecting one?
                if (maxReceiveCount == static_cast<DWORD>(0))
                {
                    // We did not get a response and we were not expecting one so that's acceptable
                    theReturnResult = true;
                    break;
                }
            }
            else
            {
                // No receive buffer was provided so a response was not expected
                theReturnResult = true;
                break;
            }
        }
    }

    // Report on whether a response was received or not
    return theReturnResult;
}

/// <summary>
/// Sends a command to the device to retrieve the device model and version and stores the result
/// </summary>
static void AcquireDeviceModelAndVersion(void)
{
    // Send the command and expect a response
    if (true == RetrySendCommandAndGetResponse(CommandGetModelAndVersion,
        strlen(CommandGetModelAndVersion),
        deviceModelAndVersion, 
        sizeof(deviceModelAndVersion)))
    {
        (void)printf("Model and version: %s\n\r", deviceModelAndVersion);
    }
}

/// <summary>
/// ends a command to the device to retrieve the device's configuratin and stores the result
/// </summary>
/// <returns></returns>
static bool AcquireDeviceConfiguration(void)
{
    // Send the command and expect a response
    return (true == RetrySendCommandAndGetResponse(CommandGetConfiguration,
        strlen(CommandGetConfiguration),
        (char *)&deviceConfiguration,
        sizeof(deviceConfiguration)));
}

/// <summary>
/// Sends a command to the device to retrieve the device's serial number and stores the result
/// </summary>
static void AcquireDeviceSerialNumber(void)
{
    // Send the command and expect a response
    if (true == RetrySendCommandAndGetResponse(CommandGetSerialNumber,
        strlen(CommandGetSerialNumber),
        deviceSerialNumber, 
        sizeof(deviceSerialNumber)))
    {
        (void)printf("Model serial number: %02x%02x%02x%02x%02x%02x%02x\n\r", 
            (uchar)deviceSerialNumber[0],
            (uchar)deviceSerialNumber[1],
            (uchar)deviceSerialNumber[2],
            (uchar)deviceSerialNumber[3],
            (uchar)deviceSerialNumber[4],
            (uchar)deviceSerialNumber[5],
            (uchar)deviceSerialNumber[6]);
    }
}

/// <summary>
/// Sends a command to the device to retrieve the device's temperature and stores the result
/// </summary>
static void AcquireDeviceTemperature(void)
{
    // Send the command and expect a response
    if (true == RetrySendCommandAndGetResponse(CommandGetTemperature,
        strlen(CommandGetTemperature),
        deviceTemperature, 
        sizeof(deviceTemperature)))
    {
        (void)printf("Device temperature: %s%u.%u\n\r", 
            deviceTemperature[2] == 1 ? "-" : "+",
            (uchar)deviceTemperature[0],
            (uchar)deviceTemperature[1]);
    }
}

/// <summary>
/// Sends a command to the device to retrieve the device's battery voltage and stores the result
/// </summary>
static void AcquireDeviceBatteryVoltage(void)
{
    // Send the command and expect a response
    if (true == RetrySendCommandAndGetResponse(CommandGetBatteryVoltage,
        strlen(CommandGetBatteryVoltage),
        devicBatteryVoltage, 
        sizeof(devicBatteryVoltage)))
    {
        (void)printf("Battery voltage: %f\n\r", (float)devicBatteryVoltage[0] / 10.0f);
    }
}

/// <summary>
/// Sends a command to the device to retrieve the device's date and time and stores the result
/// </summary>
static void AcquireDeviceDateAndTime(void)
{
    // Send the command and expect a response
    if (true == RetrySendCommandAndGetResponse(CommandSetDateAndTime,
        strlen(CommandSetDateAndTime),
        deviceDateAndTime, 
        sizeof(deviceDateAndTime)))
    {
        // The data is in this sequence: YY MM DD HH MM SS 0xAA
        (void)printf("Device date %02u/%s/%02u time %02u:%02u:%02u\n\r",
            deviceDateAndTime[2],
            theMonths[deviceDateAndTime[1] - 1],
            deviceDateAndTime[0],
            deviceDateAndTime[3],
            deviceDateAndTime[4],
            deviceDateAndTime[5]);
    }
}

/// <summary>
/// Sends a command to the device to turn the device ON or OFF. When the device is OFF,
/// it is in a quasi-powered-up state when the USB is plugged in, allowing the device to
/// be fully turned on via USB command.
/// </summary>
/// <param name="b_OnOrOff"></param>
static void TurnDeviceOnOrOff(bool b_OnOrOff)
{
    // Are we turning power on?
    if (true == b_OnOrOff)
    {
        // Send the command and do not expect a response
        if (true == RetrySendCommandAndGetResponse(CommandTurnPowerOn,
            strlen(CommandTurnPowerOn),
            receivedData,
            NO_RESPONSE_EXPECTED))
        {
            (void)printf("\n\rPower has been turned ON\n\r\n\r");
        }
    }
    else
    {
        // Send the command and do not expect a response
        if (true == RetrySendCommandAndGetResponse(CommandTurnPowerOff,
            strlen(CommandTurnPowerOff),
            receivedData,
            NO_RESPONSE_EXPECTED))
        {
            (void)printf("\n\rPower has been turned OFF\n\r\n\r");
        }
    }
}

/// <summary>
/// Sends a command to the device to order the device to return to its factory default settings
/// </summary>
static void PerformFactoryReset(void)
{
    // Send the command and do not expect a response
    if (true == RetrySendCommandAndGetResponse(CommandFactoryReset,
        strlen(CommandFactoryReset),
        receivedData,
        NO_RESPONSE_EXPECTED))
    {
    }
}

/// <summary>
/// This function displays the various elements of the device's configuration
/// </summary>
static void DisplayConfiguration(void)
{
    (void)printf("\n\r\n\r");
    (void)printf("PowerOnOff: %d\n\r", deviceConfiguration.powerOnOff);
    (void)printf("AlarmOnOff: %d\n\r", deviceConfiguration.alarmOnOff);
    (void)printf("SpeakerOnOff: %d\n\r", deviceConfiguration.speakerOnOff);
    (void)printf("GraphicModeOnOff: %d\n\r", deviceConfiguration.graphicModeOnOff);
    (void)printf("BacklightTimeoutSeconds: %d\n\r", deviceConfiguration.backlightTimeoutSeconds);
    (void)printf("IdleTitleDisplayMode: %d\n\r",     deviceConfiguration.idleTitleDisplayMode);
    (void)printf("AlarmCPMValueHigh: %d\n\r", deviceConfiguration.alarmCPMValueHiByte);
    (void)printf("AlarmCPMValueLow: %d\n\r", deviceConfiguration.alarmCPMValueLoByte);
    (void)printf("IdleDisplayMode: %d\n\r", deviceConfiguration.idleDisplayMode);
    (void)printf("AlarmType: %d\n\r", deviceConfiguration.alarmType);
    (void)printf("Save Data Type: %d ", deviceConfiguration.saveDataType);
    switch (deviceConfiguration.saveDataType)
    {
        case 0: printf("(OFF)\n\r"); break;
        case 1: printf("(Once a minute)\n\r"); break;
        case 2: printf("(Once an hour)\n\r"); break;
        default: printf("(Invalid)\n\r"); break;
    }
    (void)printf("DataSaveAddress-0: %d\n\r", deviceConfiguration.dataSaveAddress0);
    (void)printf("DataSaveAddress-1: %d\n\r", deviceConfiguration.dataSaveAddress1);
    (void)printf("DataSaveAddress-2: %d\n\r", deviceConfiguration.dataSaveAddress2);
    (void)printf("PowerSavingMode: %d\n\r",     deviceConfiguration.nPowerSavingMode);
    (void)printf("SensitivityMode: %d\n\r",     deviceConfiguration.nSensitivityMode);
    (void)printf("CounterDelayHigh: %d\n\r",     deviceConfiguration.nCounterDelayHiByte);
    (void)printf("CounterDelatLow: %d\n\r",     deviceConfiguration.nCounterDelayLoByte);
    (void)printf("VoltageOffset: %d\n\r",     deviceConfiguration.nVoltageOffset);
    (void)printf("MaxCPMHigh: %d\n\r",     deviceConfiguration.maxCPMHiByte);
    (void)printf("MaxCPMLow: %d\n\r",     deviceConfiguration.maxCPMLoByte);
    (void)printf("SensitivityAutoModeThreshold: %d\n\r",     deviceConfiguration.nSensitivityAutoModeThreshold);
    (void)printf("\n\r\n\r");
}

/// <summary>
/// The history data stored in the device is retrieved in 4K blocks until all 64K has
/// been retrieved, storing the data in a simple array. The data is merelt retrieved,
/// it is not processed.
/// </summary>
/// <returns>true if the data retrieval was successful, otherwise false</returns>
static bool AcquireAndStoreDeviceData(void)
{
    char  thisCommandString[sizeof(COMMAND_GET_HISTORY) + 1] = { 0 };
    int   blockAddress     = static_cast<int>(0x0000);
    bool  wasSuccessful    = true;
    char  outFileName[101] = { 0 };
    DWORD byteCountWritten = 0;

    // Built a file name using the date and time and followed by the standard file name
    (void)sprintf_s(outFileName, sizeof(outFileName), "%s.%s", GetDateAndTimeString(), DATA_OUTPUT_FILE_NAME);

    // Create the output file in the same directory as the executable
    HANDLE hOutputFile = CreateFile(outFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (INVALID_HANDLE_VALUE == hOutputFile)
    {
        (void)printf("Error: I was unable to create file: %s", outFileName);
    }
    else
    {
        // Get the data retrieval command and make a copy that we may modify locally
        (void)strcpy_s(thisCommandString, COMMAND_GET_HISTORY);

        // Plug the length of the retrievals
        thisCommandString[8] = (uchar)((MAX_DATA_READ_BLOCK_SIZE >> 8) & 0x00ff);
        thisCommandString[9] = (uchar)((MAX_DATA_READ_BLOCK_SIZE >> 0) & 0x00ff);

        // retrieve all of the device's data, even data that is not yet "in use."
        for (int blockCount = static_cast<int>(0); blockCount < ((MAX_FLASH_MEMORY / MAX_DATA_READ_BLOCK_SIZE)) + 1; blockCount++)
        {
            // Plug the address to retrieve
            thisCommandString[5] = (uchar) ((blockAddress >> 16) & 0x000000ff);
            thisCommandString[6] = (uchar) ((blockAddress >> 8)  & 0x000000ff);
            thisCommandString[7] = (uchar) ((blockAddress >> 0)  & 0x000000ff);

            (void)printf("Retrieving block number %u of %u : %c%c%c%c%c %02x %02x %02x %02x %02x %c%c\r", 
                (blockCount + 1), 
                ((MAX_FLASH_MEMORY / MAX_DATA_READ_BLOCK_SIZE)) + 1,
                thisCommandString[0], thisCommandString[1], thisCommandString[2],
                thisCommandString[3], thisCommandString[4],
                (uchar)thisCommandString[5], (uchar)thisCommandString[6],
                (uchar)thisCommandString[7],
                thisCommandString[8], thisCommandString[9], thisCommandString[10],
                thisCommandString[11]);

            // Send the command to the device and retrieve the block of data as a response
            if (true == RetrySendCommandAndGetResponse(thisCommandString, 
                sizeof(COMMAND_GET_HISTORY), 
                receivedData,
                MAX_DATA_READ_BLOCK_SIZE))
            {
                // We keep track of the FLASH data imagine locally, assuming we got the full block
                (void)memcpy_s(&entireFlashImage[blockAddress], 
                    MAX_DATA_READ_BLOCK_SIZE, 
                    receivedData, 
                    MAX_DATA_READ_BLOCK_SIZE);

                // Write the block of data to the raw binary output file
                if (! WriteFile(hOutputFile, receivedData, MAX_DATA_READ_BLOCK_SIZE, &byteCountWritten, NULL))
                {
                    // Writing the output file failed
                    wasSuccessful = FALSE;
                    break;
                }

                // We assume that we read the full receive block size
                blockAddress += MAX_DATA_READ_BLOCK_SIZE;
            }
            else
            {
                wasSuccessful = FALSE;
                break;
            }

            // Before sending the next retrieval command, pause a moment
            Sleep(static_cast<DWORD>(100));
        }

        // Finished with the output file
        CloseHandle(hOutputFile);

        // Report on whether the retrieval was successful or not
        if (TRUE == wasSuccessful)
        {
            (void)printf("\n\rAcquired the device's data successfully\n\r");

            // Flag the fact that we have valid data
            hasRawData = true;
        }
        else
        {
            (void)printf("\n\rThere was a problem retrieving the device's data\n\r");
        }
    }

    // Response with a flag indicating success or failure
    return wasSuccessful;
}

/// <summary>
/// The history data from the device stored locally in an array gets processed with the
/// raw data getting converted from binary to ASCII text as decimal values seporated by
/// space characters, grouped in to lines of 16 data octets.
/// 
/// The ASCII text file is created predicated upon the date and time of the computer
/// we are running on, not the date and time stored within the device.
/// </summary>
static void ExportFlashDatatoASCIITextFile(void)
{
    DWORD textLinePutputCount = static_cast<DWORD>(0);
    DWORD byteCountWritten    = static_cast<DWORD>(0);
    char  outputRecord[101]   = { 0 };
    char  toHex[5]            = { 0 };
    char  outFileName[101]    = { 0 };

    // Built a file name using the date and time and followed by the standard file name
    (void)sprintf_s(outFileName, sizeof(outFileName), "%s.%s", GetDateAndTimeString(), DATA_OUTPUT_ASCII_FILE_NAME);

    // Create the ASCII text output file in the same directory as the executable
    HANDLE hOutputFile = CreateFile(outFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (INVALID_HANDLE_VALUE == hOutputFile)
    {
        (void)printf("Error: I was unable to create file: %s", outFileName);
    }
    else
    {
        // Go through the entire flash data image. Note that this assumes
        // that the raw data has already been retrieved.
        for (int thisByteCount = static_cast<int>(0); thisByteCount < MAX_FLASH_MEMORY; thisByteCount++)
        {
            // Convert this byte in to a decimal value with leading zeros
            (void)sprintf_s(toHex, "%03u ", (uchar)entireFlashImage[thisByteCount]);

            // Append that data to the output record
            (void)strcat_s(outputRecord, toHex);

            // Is it time to write the record to the output file?
            if (++textLinePutputCount == static_cast<DWORD>(16))
            {
                // It is so append a new line to the output record
                (void)strcat_s(outputRecord, "\n");

                // Write that ASCII text to the output file
                if (! WriteFile(hOutputFile, outputRecord, strlen(outputRecord), &byteCountWritten, NULL))
                {
                }

                // Clear the items-per-record counter
                textLinePutputCount = static_cast<DWORD>(0);

                // Start the output record over again
                outputRecord[0] = static_cast<char>(0x00);
            }
        }

        // Do we have a final output record to write?
        if (textLinePutputCount > static_cast<DWORD>(0))
        {
            // We do so append a new line to the final record
            (void)strcat_s(outputRecord, "\n");

            // Write the final record to the output file
            if (! WriteFile(hOutputFile, outputRecord, strlen(outputRecord), &byteCountWritten, NULL))
            {
            }
        }

        // We are finished
        CloseHandle(hOutputFile);
    }
}

/// <summary>
/// This function assumes that a comma-delimited file whose name is passed via parameter
/// argument has been created.
/// 
/// A temporary file gets created, a command-delimited header record gets written to it
/// which includes the last-retrieved location information from the device, if there is
/// one, otherwise if no location data is stored on the device, "Timestamp" is used as
/// the column name of the header being written.
/// 
/// The existing comma-delimited file is read and the data written to the temporary file.
/// 
/// When that is completed, the existing comma-delimited file is deleted, the temporary
/// file gets renamed to the file name of the original comma-delimited file.
/// 
/// This is done because before the history data is retrieved from the device, the
/// comma-delimited file is created without a header record. This function provides the
/// missing header record.
/// </summary>
/// <param name="pch_UsingThisString"></param>
/// <param name="pch_ThisCSVFileName"></param>
void WriteCSVHeaderRecord(char* pch_UsingThisString, char* pch_ThisCSVFileName)
{
    // Create a temporary file
    std::ofstream outputFile("temporary.csv");

    // Open the existing CSV file
    std::ifstream inputFile(pch_ThisCSVFileName);

    // Put the header record at the start of the temporary file
    outputFile << pch_UsingThisString;
    outputFile << ",Counts\n";

    // Copy the existing CSV file to the temporary file
    outputFile << inputFile.rdbuf();

    // We are done with the stream handles
    inputFile.close();
    outputFile.close();

    // Delete the existing CSV file
    std::remove(pch_ThisCSVFileName);

    // Rename the temporary file to be the original CSV file
    std::rename("temporary.csv", pch_ThisCSVFileName);
}

/// <summary>
/// This function examines the history data retrieved from the device, parsing the frames
/// and CPS/CPM/CPH octets, and creates the comma-delimited file which reports what the
/// history data contains.
/// </summary>
static void ExportCSVFile(void)
{
    char outFileName[101] = { 0 };

    // Built a file name using the date and time and followed by the standard file name
    (void)sprintf_s(outFileName, sizeof(outFileName), "%s.%s", GetDateAndTimeString(), DATA_OUTPUT_CSV_FILE_NAME);

    // Create the ASCII text output file in the same directory as the executable
    HANDLE hOutputFile = CreateFile(outFileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);

    if (INVALID_HANDLE_VALUE == hOutputFile)
    {
        (void)printf("Error: I was unable to create file: %s", outFileName);
    }
    else
    {
        char  outputRecord[101]    = { 0 };
        char  currentTimestamp[31] = { 0 };
        DWORD currentImageIndex    = static_cast<DWORD>(0);
        uchar headerRecord         = static_cast<uchar>(0);
        bool  endOfValidData       = false;
        bool  foundLocationString  = false;
        uchar theYear;
        uchar theMonth;
        uchar theDay;
        uchar theHour;
        uchar theMinute;
        uchar theSecond;
        uchar theRecordRate;

        // Go through the endire raw data image
        while (currentImageIndex < (MAX_FLASH_MEMORY - 1) && false == endOfValidData)
        {
            // Acquire the raw data's expected record header byte
            headerRecord = entireFlashImage[currentImageIndex++];

            switch (headerRecord)
            {
                case RawDataTerm1:
                {
                    // It's the first terminator byte. Advance until we get the second terminator byte
                    while (RawDataTerm2 != entireFlashImage[currentImageIndex] &&
                        currentImageIndex < (MAX_FLASH_MEMORY - 1))
                    {
                        // Found the second terminator so point to the next octet which should be the frame type
                        currentImageIndex++;
                        break;
                    }
                    break;
                }

                case RawDataHeaderTimestamp:
                {
                    // Retrieve the date and time timestamp 
                    theYear   = entireFlashImage[currentImageIndex++];
                    theMonth  = entireFlashImage[currentImageIndex++];
                    theDay    = entireFlashImage[currentImageIndex++];
                    theHour   = entireFlashImage[currentImageIndex++];
                    theMinute = entireFlashImage[currentImageIndex++];
                    theSecond = entireFlashImage[currentImageIndex++];

                    // Discard the anticipated two terminator bytes
                    currentImageIndex += 2;

                    // The next byte is the count rate:
                    //  0 = off(history is off),
                    //  1 = CPS every second,
                    //  2 = CPM every minute,
                    //  3 = CPM recorded once per hour.
                    theRecordRate = entireFlashImage[currentImageIndex++];

                    // Convert the date and time bytes in to an ASCII text string for reporting
                    (void)sprintf_s(currentTimestamp, "%02u/%s/%02u %02u:%02u:%02u",
                        theDay, theMonths[theMonth], theYear,
                        theHour, theMinute, theSecond);

                    break;
                }

                case RawDataHeaderCPSIsDoubleByte:
                {
                    DWORD byteCountWritten = static_cast<DWORD>(0);

                    // The value is a double byte CPM reading. The next two bytes are the MSB and the LSB
                    ushort theDoubleCount = (entireFlashImage[currentImageIndex++] << 8) + entireFlashImage[currentImageIndex++];

                    // It's a counts per minute so make an output record
                    (void)sprintf_s(outputRecord, "%s,%u\n", currentTimestamp, theDoubleCount);

                    // Write the output record
                    if (! WriteFile(hOutputFile, outputRecord, strlen(outputRecord), &byteCountWritten, NULL))
                    {
                    }
                    break;
                }

                case RawDataHeaderCPSLocationData:
                {
                    // It is an ASCII String. The next byte is the length
                    uchar stringLength = entireFlashImage[currentImageIndex++];

                    for (uchar thisOctet = 0; thisOctet < stringLength; thisOctet++)
                    {
                        labelString[thisOctet] = entireFlashImage[currentImageIndex++];

                        // Since the llocation information will be used as the name of the
                        // date/tie column in the output file, if the octet is a comma, 
                        // replace it with a space
                        if (labelString[thisOctet] == ',')
                        {
                            labelString[thisOctet] = ' ';
                        }
                    }

                    // Make sure to NULL terminate the string
                    labelString[stringLength] = 0x00;

                    // Flag the fact that we have location information
                    foundLocationString = true;
                    break;
                }

                case RawDataHeaderEndOfData:
                {
                    // We have reached the end of vaild data if we see this frame type
                    endOfValidData = true;
                    break;
                }

                default:
                {
                    // It's CPM data. We extract each octet and write it to the output file since it
                    // is either counts a second, counts a minute, or counts an hour until we find
                    // a pair of header octets which indicates the start of a new frame. We also look
                    // for "end of data" if we encounter two octets of 255
                    bool endOfCPMData = false;

                    // Go through the data record until the terminator bytes are located
                    while (false == endOfCPMData && currentImageIndex < (MAX_FLASH_MEMORY - 1))
                    {
                        DWORD byteCountWritten = static_cast<DWORD>(0);

                        // todo For some reason the compiler generates strange code if
                        // we put the normal way of coding in to a conditional so we must
                        // copy the two bytes. Maybe an order of execution which is not
                        // correct but that seems unlikely since order of execution does
                        // not matter here. The index plus 1 test equals the second
                        // terminator byte test fails despite the fact that the byte is
                        // in fact the second terminator byte. Breaking it out like this
                        // worked so there's something curious going on, perhaps with
                        // the optimizer though again, that's very unlikely.
                        uchar testbyte1 = entireFlashImage[currentImageIndex];
                        uchar testbyte2 = entireFlashImage[currentImageIndex + 1];

                        // Are we at the end of the CPM data record?
                        if (RawDataTerm1 == testbyte1 && RawDataTerm2 == testbyte2)
                        {
                            // We are at the end of CPM data and at the start of a new frame, 
                            // so discard the frame header bytes
                            currentImageIndex += 2;

                            endOfCPMData = true;
                            break;
                        }

                        // We consider two end-of-data markers to also be an end of CPM data
                        if (RawDataHeaderEndOfData == testbyte1 && RawDataHeaderEndOfData == testbyte2)
                        {
                            // We are at the end of data so discard the end of file marker
                            currentImageIndex += 2;

                            endOfCPMData = true;
                            break;
                        }

                        // It's a counts per minute data byte so make an output record
                        (void)sprintf_s(outputRecord, "%s,%u\n", currentTimestamp, (uchar)entireFlashImage[currentImageIndex]);

                        // If the counts per minute is zero, that may be due to the device
                        // having lost power and then coming back, so we filter out zeros.
                        if ((uchar)entireFlashImage[currentImageIndex] > 0)
                        {
                            // Write the output record
                            if (! WriteFile(hOutputFile, outputRecord, strlen(outputRecord), &byteCountWritten, NULL))
                            {
                            }
                        }

                        // Increment the date/time stamp by one minute. Note that we usually get to see 
                        // a new date/time stamp in the raw data once an hour, so typically we could
                        // expect to only need to increment the minute and wrap it to zero, then we would
                        // expect to see a new date/time stamp. In case we do not, we increment the hour
                        // in case the device's clock is somewhat muddy. 
                        if (++theMinute >= 60)
                        {
                            theMinute = 0;

                            if (++theHour >= 60)
                            {
                                theHour = 0;

                                ++theDay;
                            }
                        }

                        // Convert the updated date and time bytes in to an ASCII text string for reporting
                        (void)sprintf_s(currentTimestamp, "%02u/%s/%02u %02u:%02u:%02u",
                            theDay, theMonths[theMonth], theYear,
                            theHour, theMinute, theSecond);

                        // Index to the next byte which may be another data item or the first terminator byte
                        currentImageIndex++;
                    }

                    break;
                }
            }
        }

        // We are finished
        CloseHandle(hOutputFile);

        // Did we encounter a location string in the raw data?
        if (foundLocationString == true)
        {
            // We re-build the CSV file since we know that there is a location string. The header
            // for the CSV file will show the locartion above the date/time column and labeling
            // ythe counts column
            WriteCSVHeaderRecord(labelString, outFileName);
        }
        else
        {
            // There is no location information so we re-buid the CSV file and provide a header
            // record labeling the date/time and count columns
            WriteCSVHeaderRecord("Date/Time", outFileName);
        }
    }
}

/// <summary>
/// This function will emit a console question then await a response, storing the answer in
/// a locally-held character array, converted in to upper case.
/// </summary>
/// <param name="p_thisQuestion">A null-terminated character array holding the console string to display</param>
/// <returns>true if an answer was entered and the answer starts with the letter "Y" for yes, otherwise false</returns>
static bool AskThisQuestion(char * p_thisQuestion)
{
    uchar theAnswer;

    (void)printf(p_thisQuestion);

    theAnswer = _getche();

    (void)printf("\n\r");

    if ('Y' == toupper(theAnswer))
    {
        return true;
    }

    return false;
}

/// <summary>
/// This function takes an argument which indicates which keyboard key should be sent
/// to the device, and a null-terminated string indicating what the key means.
/// 
/// The key is sent to the device and 2 seconds is delayed to afford the device --
/// which can be fairly slow -- to process and keep up with the key press.
/// </summary>
/// <param name="thisChar">The display key to send to the device. This is either left,
/// up down, or enter</param>
/// <param name="TitleOfKey">A null-terminated ascii text string describing what the 
/// key press is doing. This gets displayed before the key is sent to the device.</param>
static void SendKeyboardKeyPress(char thisChar, char *TitleOfKey)
{
    char theKeyCommand[10] = { 0 };

    // Copy the KEY command to the buffer we will send
    (void)strcpy_s(theKeyCommand, COMMAND_PRESS_A_KEY);

    // Build the key command to send
    theKeyCommand[4] = thisChar;

    (void)printf("Sending '%c' for %s to the device...\n\r", thisChar, TitleOfKey);

    // Send the key command and discard the answer
    if (true == RetrySendCommandAndGetResponse(theKeyCommand,
        sizeof(COMMAND_PRESS_A_KEY),
        receivedData,
        NO_RESPONSE_EXPECTED))
    {
        // Since that was successful, give it some time
        Sleep(static_cast<DWORD>(2000));
    }
}

/// <summary>
/// This function will send key commands to the device stepping through the device's menu
/// items so that the history data stored in the device gets erased. Because the device
/// needs to be powered up, the power-up command gets sent first.
/// 
/// The menu system on the device is fairly slow, so each key that gets sent to the device
/// has a delay after the key is sent, so the completion of this function may take a fair
/// amount of time.
/// 
/// Note that this assumes that the menu system on the device has not been changed by the
/// manufacturer. If you are using a different version of the Geiger Counter, you may need
/// to alter this menu stepping unless the new device has a single command which will
/// delete the history data.
/// </summary>
static void EraseRawData(void)
{
    (void)printf("\n\rPowering up device...\n\r");

    // Get the device powered up, if it is not already powered
    if (true == RetrySendCommandAndGetResponse(CommandTurnPowerOn,
        strlen(CommandTurnPowerOn),
        receivedData,
        NO_RESPONSE_EXPECTED))
    {
        // Because it takes a few seconds for the device to power up, wait a bit
        Sleep(static_cast<DWORD>(5000));

        (void)printf("\n\rRequesting the erase of stored data...\n\r");

        // Send the key sequence needed to request erase of data.
        // Key numbers are 0 through 3 inclusive for S1 to S4
        (void)SendKeyboardKeyPress('3', "Enter");           // Enter
        (void)SendKeyboardKeyPress('2', "Down arrow");      // DOWN to "Display Option"
        (void)SendKeyboardKeyPress('2', "Down arrow");      // DOWN to "Save Data"
        (void)SendKeyboardKeyPress('3', "Enter");           // Enter
        (void)SendKeyboardKeyPress('2', "Down arrow");      // DOWN to "Note/Location"
        (void)SendKeyboardKeyPress('2', "Down arrow");      // DOWN to "History Data"
        (void)SendKeyboardKeyPress('2', "Down arrow");      // DOWN to "Erase Saved Data"
        (void)SendKeyboardKeyPress('3', "Enter");           // Enter which gives us YES?

        // Let the YES command rest for a period of time
        (void)printf("Giving device time to erase RAW Data...\n\r");
        Sleep(static_cast<DWORD>(4000));

        (void)SendKeyboardKeyPress('3', "Enter");           // We hit ENTER to accept YES
        Sleep(static_cast<DWORD>(1000));

        (void)SendKeyboardKeyPress('0', "Left arrow");      // LEFT arrow to exit sub menu
        (void)SendKeyboardKeyPress('0', "Left arrow");      // LEFT arrow to exit sub menu
        (void)SendKeyboardKeyPress('0', "Left arrow");      // LEFT arrow to exit sub menu

        (void)printf("\n\r\n\r");
    }
}

/// <summary>
/// The computer's current date and time gets retrieved and a command string is
/// assembled to send to the device to set its current date and time.
/// </summary>
static void SetDateAndTime(void)
{
    uchar     ch_CommandString[sizeof(COMMAND_SET_DATE_AND_TIME)]{};
    time_t    t_CurrentTime;
    struct tm CurrentTime;

    // Copy the Set Date And Time command string to our output command buffer
    (void)strcpy_s((char *)ch_CommandString, sizeof(ch_CommandString), COMMAND_SET_DATE_AND_TIME);

    // Get the current date and time
    (void)time(&t_CurrentTime);

    // Convert that in to GMT  and out in to a useful structure
    (void)gmtime_s(&CurrentTime, &t_CurrentTime);

    ch_CommandString[SET_TIME_OFFSET_YEAR]   = CurrentTime.tm_year - 100;      // Year minus 1900 so for a single octet year we subtract 100
    ch_CommandString[SET_TIME_OFFSET_MONTH]  = CurrentTime.tm_mon + 1;
    ch_CommandString[SET_TIME_OFFSET_DAY]    = CurrentTime.tm_mday;
    ch_CommandString[SET_TIME_OFFSET_HOUR]   = CurrentTime.tm_hour;
    ch_CommandString[SET_TIME_OFFSET_MINUTE] = CurrentTime.tm_min;
    ch_CommandString[SET_TIME_OFFSET_SECOND] = CurrentTime.tm_sec;

    // Send the set date and time command then and discard the answer
    if (true == RetrySendCommandAndGetResponse((char *)&ch_CommandString,
        sizeof(COMMAND_SET_DATE_AND_TIME),
        receivedData,
        NO_RESPONSE_EXPECTED))
    {
        // Since that was successful, give it some time
        Sleep(static_cast<DWORD>(100));
    }

    // Now that the date and time has been set, retrieve and display it again
    (void)printf("\n\r\n\rThe new date and time: ");
    AcquireDeviceDateAndTime();

    // We offer a blank line to delineate the display
    (void)printf("\n\r");
}

/// <summary>
/// The code in this function is a near duplicate of previous code which does much
/// the same thing: The raw history data is examined and parsed, retrieving the
/// clicks per second / minute / hour data.
/// 
/// Each data item is stored in a container, and the lowest value observed as well
/// as the highest value observed is computed, wth the result being returned by
/// reference.
/// </summary>
/// <param name="ru16_Lowest"></param>
/// <param name="ru16_Highest"></param>
static void ExtractClicksPerMinuteFromRawData(ushort& ru16_Lowest, ushort& ru16_Highest)
{
    bool  endOfValidData    = false;
    DWORD currentImageIndex = static_cast<DWORD>(0);
    uchar headerRecord      = static_cast<uchar>(0);

    // Start out saying how many counts per minute are the lowest and highest
    ru16_Lowest  = static_cast<ushort>(0xFFFF);
    ru16_Highest = static_cast<ushort>(0x0000);

    // Skip past the expected start of frame header octets
    currentImageIndex += static_cast<DWORD>(2);

    // Go through the entire raw data image until we run out of frames and data
    while (currentImageIndex < (MAX_FLASH_MEMORY - 1) && FALSE == endOfValidData)
    {
        // Acquire the raw data's expected record header byte indicating what type of data follows
        headerRecord = entireFlashImage[currentImageIndex++];

        switch (headerRecord)
        {
            case RawDataHeaderTimestamp:
            {
                // Skip past the date and time timestamp and 2 byte terminator plus one byte storage rate
                currentImageIndex += static_cast<DWORD>(9);
                break;
            }

            case RawDataHeaderCPSIsDoubleByte:
            {
                // The value is a double byte CPM reading. The next two bytes are the MSB and the LSB
                ushort theDoubleCount = (entireFlashImage[currentImageIndex++] << 8) + entireFlashImage[currentImageIndex++];

                // It's a counts per minute data byte so make an output record
                listCPMData.push_back(theDoubleCount);

                // Is this value less than our lowest known value?
                if (theDoubleCount < ru16_Lowest)
                {
                    // Yes, so this value becomes the newest lowest value
                    ru16_Lowest = theDoubleCount;
                }

                // Is this value greater than our highest known value?
                if (theDoubleCount > ru16_Highest)
                {
                    // Yes, so this value becomes the newest highest value
                    ru16_Highest = theDoubleCount;
                }
                break;
            }

            case RawDataHeaderCPSLocationData:
            {
                // It is an ASCII String. The next byte is the length.
                uchar stringLength = entireFlashImage[currentImageIndex++];

                // Skip past the ASCII string
                currentImageIndex += static_cast<DWORD>(stringLength);
                break;
            }

            case RawDataTerm1:
            {
                // It's the first frame marker byte. Advance until we get the second frame marker byte
                while (RawDataTerm2 != entireFlashImage[currentImageIndex] &&
                    currentImageIndex < (MAX_FLASH_MEMORY - 1))
                {
                    // Found the second terminator so point to the next command
                    currentImageIndex++;
                    break;
                }
                break;
            }

            case RawDataHeaderEndOfData:
            {
                // We have reached the end of vaild data
                endOfValidData = true;
                break;
            }

            default:
            {
                bool endOfCPMData = false;

                // Go through the data record until the a frame header is located
                while (false == endOfCPMData && currentImageIndex < (MAX_FLASH_MEMORY - 1))
                {
                    DWORD byteCountWritten = static_cast<DWORD>(0);

                    // todo For some reason the compiler generates strange code if
                    // we put the normal way of coding in to a conditional so we must
                    // copy the two bytes. Maybe an order of execution which is not
                    // correct but that seems unlikely since order of execution does
                    // not matter here. The index plus 1 test equals the second
                    // terminator byte test fails despite the fact that the byte is
                    // in fact the second terminator byte. Breaking it out like this
                    // worked so there's something curious going on, perhaps with
                    // the optimizer though again, that's very unlikely.
                    uchar testbyte1 = entireFlashImage[currentImageIndex];
                    uchar testbyte2 = entireFlashImage[currentImageIndex + 1];

                    // Are we at the end of the CPM data?
                    if (RawDataTerm1 == testbyte1 && RawDataTerm2 == testbyte2)
                    {
                        // We are at the end of CPM so discard the next frame's header octets
                        currentImageIndex += static_cast<DWORD>(2);

                        endOfCPMData = false;
                        break;
                    }

                    // We consider two end-of-file markers to also be a terminator
                    if (RawDataHeaderEndOfData == testbyte1 && RawDataHeaderEndOfData == testbyte2)
                    {
                        // We are at the end of valid CPM data so discard the end of frame marker
                        currentImageIndex += static_cast<DWORD>(2);

                        endOfCPMData = false;
                        break;
                    }

                    // Extract the count and  index to the next byte which may be another data item or the start of a new frame
                    ushort theCount = static_cast<ushort>(entireFlashImage[currentImageIndex++]);

                    // It's a counts per minute data byte so make an output record
                    listCPMData.push_back(theCount);

                    // Is this value less than our lowest known value?
                    if (theCount < ru16_Lowest)
                    {
                        // Yes, so this value becomes the newest lowest value
                        ru16_Lowest = theCount;
                    }

                    // Is this value greater than our highest known value?
                    if (theCount > ru16_Highest)
                    {
                        // Yes, so this value becomes the newest highest value
                        ru16_Highest = theCount;
                    }
                }

                break;
            }
        }
    }

    // Flag the fact that we have clicks per minute information
    hasClicksPerMinute = true;
}

/// <summary>
/// The CPOS/CPM/SPH data that was collected and stored in to a container gets summed and
/// then divided by the number of elements to compute the average which gets returned.
/// </summary>
/// <returns>The average CPS/CPM/CPH</returns>
static ulong ComputeAverageAcrossAllCPMData(void)
{
    ulong accumulationValue = static_cast<ulong>(0);
    list<ushort>::iterator listIterator;

    // Go through the list of CPM data and add them all up
    for (listIterator = listCPMData.begin(); listIterator != listCPMData.end(); listIterator++)
    {
        accumulationValue += *listIterator;
    }

    // We have the sum, now compute the average
    accumulationValue /= listCPMData.size();

    // Return the average across all of the data
    return accumulationValue;
}

/// <summary>
/// The CPS/CPM/CPH data collected and stored in the local container gets examined to see if
/// there are any data items that are considered to be excessively high. The average across
/// 10 minutes is used to make the decision.
/// 
/// This is rather vague and is only used to give some indication that a single data item
/// stands out from its surrounding.
/// </summary>
/// <param name="thisUpperValue">This is the upper expected "not extreme" value which the
/// 10 minute average is compared to</param>
/// <param name="superHighValue">This is the super high extreme value which the 10 minute
/// average is compared to</param>
/// <returns>true if there was at least one section of 10 minutes which contained what is
/// considered to contain a high, extreme value, otherwise false if no 10 minute average
/// was found to meet the expected "that's high" value</returns>
static bool ScanTenMinuteIntervalsForExcessHigh(ulong thisUpperValue, ulong superHighValue)
{
    uchar  thisTenMinuteSegment = static_cast<uchar>(0);
    ulong  accumulationValue    = static_cast<ulong>(0);
    ushort whichTenMinuteBlock  = static_cast<ushort>(0);
    bool   foundAnyHighSections = false;
    ushort iteratorCount        = 0;
    list<ushort>::iterator listIterator;

    // Go through the data performing an average across ten minutes
    for (listIterator = listCPMData.begin(); listIterator != listCPMData.end(); listIterator++, iteratorCount++)
    {
        accumulationValue += static_cast<ushort>(*listIterator);

        // Has this been ten minutes?
        if (++thisTenMinuteSegment == static_cast<uchar>(10))
        {
            // It has been ten minutes. What is the average?
            accumulationValue /= static_cast<ulong>(10);

            // Does the average meet our threshold of reporting?
            if (accumulationValue >= thisUpperValue)
            {
                // Yes it does so report this ten minute period
                (void)printf("Samples at index %05u about %04lu minutes in to the data has higher average of %03lu\n\r", 
                    iteratorCount,
                    whichTenMinuteBlock * 10, 
                    accumulationValue);

                // Flag the fact that we found at least one
                foundAnyHighSections = true;

                // Is the value considered to be a super high value?
                if (accumulationValue >= superHighValue)
                {
                    // Report the index where that super high value is located
                    listSuperHighEventIndexValues.push_back(*listIterator - static_cast<ulong>(10));
                }
            }

            // Finished with this ten minute section
            thisTenMinuteSegment = static_cast<uchar>(0);
            accumulationValue    = static_cast<ulong>(0);

            // Keep track of how many 10 minute blocks we have examined
            whichTenMinuteBlock++;
        }
    }

    // Report on whether any were located or not
    return foundAnyHighSections;
}

/// <summary>
/// The raw history data gets evaluated and a commentary about what is found, if anything,
/// gets emitted to the console.
/// </summary>
static void ScanRawDataForHighPeriods(void)
{
    ulong entireDataDatAverageClicksPerMinute = static_cast<ulong>(0);
    float entireAveragePlus30Percent          = static_cast<float>(0);
    ulong superHighValue                      = static_cast<ulong>(0);
    ushort lowestKnownValue                   = static_cast<ushort>(0);
    ushort highestKnownValue                  = static_cast<ushort>(0);

    // Do we need to retrieve the device's raw data?
    if (false == hasRawData)
    {
        // Yes, so retrieve the device's raw data
        (void)printf("\n\r\n\rRetrieving raw data\n\r");

        (void)AcquireAndStoreDeviceData();
    }

    // Do we have valid raw data to work with?
    if (true == hasRawData)
    {
        // We have valid raw data so extract the clicks per minute
        (void)printf("\n\rExtracting clicks per minute from the raw data...");

        if (false == hasClicksPerMinute)
        {
            // Acquire the information that we need
            ExtractClicksPerMinuteFromRawData(lowestKnownValue, highestKnownValue);
        }

        (void)printf("\n\r\n\rThere are %lu clicks per minute data elements stored in the raw data\n\r", listCPMData.size());

        // We only evaluate the data if there is some
        if (listCPMData.size() > static_cast<ulong>(0))
        {
            // Compute the average clicks per minute for the entire data set
            entireDataDatAverageClicksPerMinute = ComputeAverageAcrossAllCPMData();

            (void)printf("The average clicks per minute is %lu\n\r", entireDataDatAverageClicksPerMinute);
            (void)printf("The lowest value was: %u, the highest was: %u\n\r\n\r", lowestKnownValue, highestKnownValue);

            // Determine what the average plus 30 % is
            entireAveragePlus30Percent = 0.30f * static_cast<float>(entireDataDatAverageClicksPerMinute);

            entireAveragePlus30Percent += static_cast<float>(entireDataDatAverageClicksPerMinute);

            // Determine what the super high value is. That's the average times 2
            superHighValue = static_cast<ulong>(entireAveragePlus30Percent) * static_cast<ulong>(2);

            (void)printf("The average plus 30%% is %f. A super high value is considered to be %lu\n\r\n\r", 
                entireAveragePlus30Percent, 
                superHighValue);

            (void)printf("Searching for 10 minute periods where the average meets or exceets that upper value\n\r");

            // Scan 10 minute sections of the raw data for any period that meets or exceeds that high boundary
            if (false == ScanTenMinuteIntervalsForExcessHigh(static_cast<ulong>(entireAveragePlus30Percent), superHighValue))
            {
                // Since we need to inform the operayor about negative findings, report that fact
                (void)printf("There were not any high counts per 10 minute interval found in the data\n\r");
            }

            // Were there any super high events in the data?
            if (listSuperHighEventIndexValues.size() > static_cast<ushort>(0))
            {
                (void)printf("There were %u super high events in the raw data\n\r", listSuperHighEventIndexValues.size());

                // Export the super high events to comma-delimited files for further evaluation
                // fredr tbd todo
            }
            else
            {
                (void)printf("There were no super high events in the raw data\n\r");
            }

            // Completed with this evaluation
            (void)printf("\n\r\n\r");
        }
    }
}

/// <summary>
/// This function will talk wioth the device to extract various aspects of the device's
/// configuration, serial number, and other things, then a menu is offered on the console.
/// A keyboard key is retrieved, if there is one, to drive functionality from the menu.
/// </summary>
static void Perform_Basic_functionality(void)
{
    bool  b_LoopUntilExit   = true;
    uchar uch_MenuSelection = static_cast<uchar>(0x00);

    // Retrieve various bits of data and report them
    (void)printf("\n\r\n\r");
    AcquireDeviceModelAndVersion();
    AcquireDeviceSerialNumber();
    AcquireDeviceTemperature();
    AcquireDeviceBatteryVoltage();
    AcquireDeviceDateAndTime();
    (void)printf("\n\r\n\r");

    // We will offer a menu
    while (true == b_LoopUntilExit)
    {
        SetColorAndBackground(LIGHTGREEN);
        (void)printf("%c: Export raw data to output files\n\r",                     MenuItemRetrieveData);
        (void)printf("%c: Scan raw data for high anomolies, average plus 30%%\n\r", MenuItemScanHighPeriods);
        (void)printf("%c: Set Geiger Counter's date and time\n\r",                  MenuItemSetDateAndTime);
        (void)printf("%c: Turn power ON\n\r",                                       MenuItemTurnPowerOn);
        (void)printf("%c: Turn power OFF\n\r",                                      MenuItemTurnPowerOff);
        (void)printf("%c: Display Configuration\n\r",                               MenuItemDisplayConfiguration);
        SetColorAndBackground(LIGHTRED);
        (void)printf("%c: Erase accumulated Geiger Counter history\n\r",            MenuItemEraseRawData);
        (void)printf("%c: Factory Reset to original settings\n\r",                  MenuItemFactoryReset);
        SetColorAndBackground(LIGHTGREEN);
        (void)printf("%c: Exit this program\n\r",                                   MenuItemExitTheProgram);
        (void)printf("\n\rMake a selection: ");

        // Get a single keystroke, no need for an ENTER
        uch_MenuSelection = toupper(_getch());

        // Determine what operation we should perform, if any
        switch (uch_MenuSelection)
        {
            case MenuItemRetrieveData:
            {
                (void)printf("\n\r");

                // Do we need to retrieve the raw data?
                if (false == hasRawData)
                {
                    // Attempt to acquire the raw data from the device and see if that was successful
                    (void)AcquireAndStoreDeviceData();
                }

                // Do we have the rawdata that we need?
                if (true == hasRawData)
                {
                    (void)printf("Exporting the data to various output files\n\r");

                    // Since we appear to have acquired the device's FLASH data, make an ASCII output file
                    // containing all of that data and export the data in ASCII with decimal values
                    ExportFlashDatatoASCIITextFile();

                    // Parse out the FLASH image and create a CSV output file
                    ExportCSVFile();

                    (void)printf("Export completed.\n\r\n\r");
                }
                break;
            }
            case MenuItemEraseRawData:
            {
                // Erase the raw data
                EraseRawData();
                break;
            }
            case MenuItemSetDateAndTime:
            {
                // Set the device's date and time to the PC's date and time and
                // then retrieve it from the device again and display it.
                SetDateAndTime();
                break;
            }
            case MenuItemScanHighPeriods:
            {
                // Acquire the raw data if we do not already have it and scan it
                // for high incidents where the typical reading exceeds the average
                // by at least 30 percent. We look at 10 minute sections of the
                // raw data after we compute the entire raw data's average.
                ScanRawDataForHighPeriods();
                break;
            }
            case MenuItemTurnPowerOn:
            {
                TurnDeviceOnOrOff(true);
                break;
            }
            case MenuItemTurnPowerOff:
            {
                TurnDeviceOnOrOff(false);
                break;
            }
            case MenuItemDisplayConfiguration:
            {
                if (true == AcquireDeviceConfiguration())
                {
                    DisplayConfiguration();
                }
                else
                {
                    (void)printf("\n\r !!! Unable to acquire deivce's configuration !!!\n\r");
                }
                break;
            }
            case MenuItemFactoryReset:
            {
                PerformFactoryReset();
                break;
            }
            case MenuItemExitTheProgram:
            {
                // The operator wants to end the program
                b_LoopUntilExit = false;
                break;
            }
            default:
            {
                // It is not an option that we support
                break;
            }
        }
    }
}

/// <summary>
/// Various aspects of the locall-held data storage in this module gets set to
/// known values. Some things we leave to the compilet to initialize to its 
/// "zero vars," or what ever the compiler that genberates the initialization code
/// calls its zero-initialized local data.
/// </summary>
static void InitializeThisModule(void)
{
    hasRawData         = false;
    hasClicksPerMinute = false;
}

/// <summary>
/// The main entry point of the program.
/// </summary>
/// <param name="argc">Not used, command line arguments in an array of pointers</param>
/// <param name="argv">Notused, the number of command line arguments</param>
/// <returns>Always returns an ERRORLEVEL of 0</returns>
int main(int argc, char * argv[])
{
    TCHAR        lpTargetPath[1000] = { 0 };
    DWORD        comPortTest        = static_cast<DWORD>(0);
    COMMTIMEOUTS timeouts           = { 0 };
    char         comName[101]       = { 0 };
    bool         foundComPort       = false;
    BOOL         writeStatus        = false;
    DCB          dcbSerialParams{};

    // Initialize any locally-held data that should be set to known values
    InitializeThisModule();

    // We set green text on the default black background
    SetColorAndBackground(LIGHTGREEN);

    (void)printf("\n\r");

    // Find out which COM port should be used to access the Geiger Counter
    for (int thisComNumber = static_cast<int>(0); thisComNumber < static_cast<int>(255); thisComNumber++)
    {
        // Build a COM port name
        (void)sprintf_s(comName, "COM%u", thisComNumber);

        // Does this COM port exist?
        comPortTest = QueryDosDevice(comName, (LPSTR)lpTargetPath, sizeof(lpTargetPath));

        // If we got a result saying that it exists, display what we found
        if (comPortTest != static_cast<DWORD>(0))
        {
            if ((char *)NULL != stristr(lpTargetPath, "serial"))
            {
                (void)sprintf_s(lpTargetPath, "Is the Geiger Counter on %s? ", comName);

                if (true == AskThisQuestion(lpTargetPath))
                {
                    foundComPort = true;
                    break;
                }
            }
        }
    }

    // Only if the correct COM port was discovered do we attempt to communicate with the device
    if (true == foundComPort)
    {
        // Build a COM port name to open
        (void)sprintf_s(lpTargetPath, "\\\\.\\%s", comName);

        // Attempt to open the serial interface
        hComm = CreateFile(lpTargetPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

        if (hComm == INVALID_HANDLE_VALUE)
        {
            // It appears that the COM port does not exist
            (void)printf("Error: I could not open %s\n\r", lpTargetPath);

            if (GetLastError() == ERROR_FILE_NOT_FOUND)
            {
                (void)printf("COM PORT %s was not located\n\r", lpTargetPath);
            }
            else
            {
                (void)printf("The problem was: %d\n\r", GetLastError());
            }

            // We will terminate after a short period of time
            Sleep(static_cast<DWORD>(3000));
        }
        else
        {
            // Since the COM port appears to be open, set the line configuration
            dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

            if (FALSE == (writeStatus = GetCommState(hComm, &dcbSerialParams)))
            {
                (void)printf("Error: I was unable to retrieve the serial interface's status\n\r");

                CloseHandle(hComm);
            }
            else
            {
                dcbSerialParams.BaudRate = CBR_57600;
                dcbSerialParams.ByteSize = static_cast<BYTE>(8);
                dcbSerialParams.StopBits = ONESTOPBIT;
                dcbSerialParams.Parity   = NOPARITY;

                // Set the serial interface's bit characteristics
                if (FALSE == (writeStatus = SetCommState(hComm, &dcbSerialParams)))
                {
                    (void)printf("Error: I was unable to configure the serial interface\n\r");

                    Sleep(static_cast<DWORD>(3000));

                    CloseHandle(hComm);
                }
                else
                {
                    // Set COM port timeout settings
                    timeouts.ReadIntervalTimeout         = static_cast<DWORD>(50);
                    timeouts.ReadTotalTimeoutConstant    = static_cast<DWORD>(50);
                    timeouts.ReadTotalTimeoutMultiplier  = static_cast<DWORD>(2);
                    timeouts.WriteTotalTimeoutConstant   = static_cast<DWORD>(50);
                    timeouts.WriteTotalTimeoutMultiplier = static_cast<DWORD>(2);

                    // Attempt to set the various timeout values on the serial interface
                    if (SetCommTimeouts(hComm, &timeouts) == false)
                    {
                        (void)printf("Error: I was unable to set the serial interface's timeout values\n\r");

                        Sleep(static_cast<DWORD>(3000));

                        CloseHandle(hComm);
                    }
                    else
                    {
                        Perform_Basic_functionality();
                    }
                }
            }
        }
    }
    else
    {
        (void)printf("\n\rI can't find any other COM ports so the program will end shortly\n\r");

        Sleep(static_cast<DWORD>(3000));
    }

    return 0;
}

