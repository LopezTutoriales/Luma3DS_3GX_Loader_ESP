/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2020 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/

/*
*   Code originally by reworks
*/

#include "draw.h"
#include "config.h"
#include "screen.h"
#include "utils.h"
#include "memory.h"
#include "buttons.h"
#include "fs.h"
#include "pin.h"
#include "crypto.h"

static char pinKeyToLetter(u32 pressed)
{
    static const char *keys = "AB--RLUD--XY";

    u32 i;
    for(i = 31; pressed > 1; i--) pressed /= 2;

    return keys[31 - i];
}

void newPin(bool allowSkipping, u32 pinMode)
{
    clearScreens(false);

    u8 length = 4 + 2 * (pinMode - 1);

    drawString(true, 10, 10, COLOR_TITLE, "Pon un nuevo PIN usando ABXY y la cruceta");
    drawString(true, 10, 10 + SPACING_Y, COLOR_TITLE, allowSkipping ? "Pulsa START para saltar, SELECT para resetear" : "Pulsa SELECT para resetear");

    drawFormattedString(true, 10, 10 + 3 * SPACING_Y, COLOR_WHITE, "PIN (%u digitos): ", length);

    //Pad to AES block length with zeroes
    __attribute__((aligned(4))) u8 enteredPassword[AES_BLOCK_SIZE] = {0};

    bool reset = false;
    u8 cnt = 0;

    while(cnt < length)
    {
        if(reset)
        {
            for(u32 i = 0; i < cnt; i++)
                drawCharacter(true, 10 + (16 + 2 * i) * SPACING_X, 10 + 3 * SPACING_Y, COLOR_BLACK, (char)enteredPassword[i]);

            cnt = 0;
            reset = false;
        }

        u32 pressed;
        do
        {
            pressed = waitInput(false);
        }
        while(!(pressed & PIN_BUTTONS));

        pressed &= PIN_BUTTONS;
        if(!allowSkipping) pressed &= ~BUTTON_START;

        if(pressed & BUTTON_START) return;

        if(pressed & BUTTON_SELECT)
        {
            reset = true;
            continue;
        }

        if(!pressed) continue;

        //Add character to password
        enteredPassword[cnt] = (u8)pinKeyToLetter(pressed);

        //Visualize character on screen
        drawCharacter(true, 10 + (16 + 2 * cnt) * SPACING_X, 10 + 3 * SPACING_Y, COLOR_WHITE, enteredPassword[cnt]);

        cnt++;
    }

    PinData pin;

    memcpy(pin.magic, "PINF", 4);
    pin.formatVersionMajor = PIN_VERSIONMAJOR;
    pin.formatVersionMinor = PIN_VERSIONMINOR;

    __attribute__((aligned(4))) u8 tmp[SHA_256_HASH_SIZE],
                                   lengthBlock[AES_BLOCK_SIZE] = {0};
    lengthBlock[0] = length;

    computePinHash(tmp, lengthBlock);
    memcpy(pin.lengthHash, tmp, sizeof(tmp));

    computePinHash(tmp, enteredPassword);
    memcpy(pin.hash, tmp, sizeof(tmp));

    if(!fileWrite(&pin, PIN_FILE, sizeof(PinData)))
        error("Error escribiendo arch. de PIN");
}

bool verifyPin(u32 pinMode)
{
    PinData pin;

    if(fileRead(&pin, PIN_FILE, sizeof(PinData)) != sizeof(PinData) ||
       memcmp(pin.magic, "PINF", 4) != 0 ||
       pin.formatVersionMajor != PIN_VERSIONMAJOR ||
       pin.formatVersionMinor != PIN_VERSIONMINOR)
        return false;

    __attribute__((aligned(4))) u8 tmp[SHA_256_HASH_SIZE],
                                   lengthBlock[AES_BLOCK_SIZE] = {0};
    lengthBlock[0] = 4 + 2 * (pinMode - 1);

    computePinHash(tmp, lengthBlock);

    //Test vector verification (check if SD card has been used on another console or PIN length changed)
    if(memcmp(pin.lengthHash, tmp, sizeof(tmp)) != 0) return false;

    initScreens();

    swapFramebuffers(true);

    drawString(true, 10, 10, COLOR_TITLE, "Pon el PIN usando ABXY y la cruceta para proceder");
    drawString(true, 10, 10 + SPACING_Y, COLOR_TITLE, "Pulsa START para apagar, SELECT para borrar");

    drawFormattedString(true, 10, 10 + 3 * SPACING_Y, COLOR_WHITE, "PIN (%u digitos): ", lengthBlock[0]);

    bool isBottomSplashValid = getFileSize("splashpin.bin") == SCREEN_BOTTOM_FBSIZE;
    if(isBottomSplashValid)
    {
        isBottomSplashValid = fileRead(fbs[0].bottom, "splashpin.bin", SCREEN_BOTTOM_FBSIZE) == SCREEN_BOTTOM_FBSIZE;
    }
    else
    {
        static const char *messageFile = "pinmessage.txt";
        char message[801];

        u32 messageSize = fileRead(message, messageFile, sizeof(message) - 1);

        if(messageSize != 0)
        {
            message[messageSize] = 0;
            drawString(false, 10, 10, COLOR_WHITE, message);
        }
    }
    swapFramebuffers(false);

    //Pad to AES block length with zeroes
    __attribute__((aligned(4))) u8 enteredPassword[AES_BLOCK_SIZE] = {0};

    bool unlock = false,
         reset = false;
    u8 cnt = 0;

    while(!unlock)
    {
        if(reset)
        {
            for(u32 i = 0; i < cnt; i++)
                drawCharacter(true, 10 + (16 + 2 * i) * SPACING_X, 10 + 3 * SPACING_Y, COLOR_BLACK, '*');

            cnt = 0;
            reset = false;
        }

        u32 pressed;
        do
        {
            pressed = waitInput(false);
        }
        while(!(pressed & PIN_BUTTONS));

        if(pressed & BUTTON_START) mcuPowerOff();

        pressed &= PIN_BUTTONS;

        if(pressed & BUTTON_SELECT)
        {
            reset = true;
            continue;
        }

        if(!pressed) continue;

        //Add character to password
        enteredPassword[cnt] = (u8)pinKeyToLetter(pressed);

        //Visualize character on screen
        drawCharacter(true, 10 + (16 + 2 * cnt) * SPACING_X, 10 + 3 * SPACING_Y, COLOR_WHITE, '*');

        if(++cnt < lengthBlock[0]) continue;

        computePinHash(tmp, enteredPassword);
        unlock = memcmp(pin.hash, tmp, sizeof(tmp)) == 0;

        if(!unlock)
        {
            reset = true;

            drawString(true, 10, 10 + 5 * SPACING_Y, COLOR_RED, "PIN equivocado, intentalo de nuevo");
        }
    }

    return true;
}
