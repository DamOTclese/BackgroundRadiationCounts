
// ----------------------------------------------------------------------
// Borrowed.cpp
// 
// Fredric L. Rice, August 2020. Last update: 31/May/23
//
// This module contains code which has been borrowed from either other
// projects or from web sites. It was open source or otherwise public
// code taken without express permission.
//
// ----------------------------------------------------------------------

#include <ctype.h>
#include <wtypes.h>

#ifndef stristr
/// <summary>
/// Performs a case-insensitive NULL-terminated "C" type string compare
/// </summary>
/// <param name="str1">The string that we are scanning</param>
/// <param name="str2">The string that we are searching for</param>
/// <returns>A pointer to the string we searched for if it exists, otherwise NULL</returns>
char* stristr(const char* str1, const char* str2)
{
    const char* p1 = str1;
    const char* p2 = str2;
    const char* r = *p2 == 0 ? str1 : 0;

    while (*p1 != 0 && *p2 != 0)
    {
        if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2))
        {
            if (r == 0)
            {
                r = p1;
            }

            p2++;
        }
        else
        {
            p2 = str2;
            if (r != 0)
            {
                p1 = r + 1;
            }

            if (tolower((unsigned char)*p1) == tolower((unsigned char)*p2))
            {
                r = p1;
                p2++;
            }
            else
            {
                r = 0;
            }
        }

        p1++;
    }

    return *p2 == 0 ? (char*)r : 0;
}
#endif

/// <summary>
/// Sets the foreground and optionally the background colors.
/// </summary>
/// <param name="ForgC">The forground color to set the screen to</param>
/// <param name="BackC">The background color to set the screen to</param>
void SetColorAndBackground(int ForgC, int BackC)
{
    WORD wColor = ((BackC & 0x0F) << 4) + (ForgC & 0x0F);

    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), wColor);
}

