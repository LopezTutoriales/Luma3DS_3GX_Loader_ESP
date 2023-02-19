/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2021 Aurora Wright, TuxSH
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

#include <3ds.h>
#include "menus/miscellaneous.h"
#include "luma_config.h"
#include "input_redirection.h"
#include "ntp.h"
#include "memory.h"
#include "draw.h"
#include "fmt.h"
#include "utils.h" // for makeArmBranch
#include "minisoc.h"
#include "ifile.h"
#include "pmdbgext.h"
#include "plugin.h"
#include "process_patches.h"

typedef struct DspFirmSegmentHeader {
    u32 offset;
    u32 loadAddrHalfwords;
    u32 size;
    u8 _0x0C[3];
    u8 memType;
    u8 hash[0x20];
} DspFirmSegmentHeader;

typedef struct DspFirm {
    u8 signature[0x100];
    char magic[4];
    u32 totalSize; // no more than 0x10000
    u16 layoutBitfield;
    u8 _0x10A[3];
    u8 surroundSegmentMemType;
    u8 numSegments; // no more than 10
    u8 flags;
    u32 surroundSegmentLoadAddrHalfwords;
    u32 surroundSegmentSize;
    u8 _0x118[8];
    DspFirmSegmentHeader segmentHdrs[10];
    u8 data[];
} DspFirm;

Menu miscellaneousMenu = {
    "Menu de opciones Miscelaneas",
    {
        { "Cambiar Homebrew Launcher por esta app", METHOD, .method = &MiscellaneousMenu_SwitchBoot3dsxTargetTitle },
        { "Cambiar botones para abrir Rosalina", METHOD, .method = &MiscellaneousMenu_ChangeMenuCombo },
        { "Iniciar InputRedirection", METHOD, .method = &MiscellaneousMenu_InputRedirection },
        { "Actualizar hora y fecha por internet", METHOD, .method = &MiscellaneousMenu_UpdateTimeDateNtp },
        { "Anular compensacion horaria del usuario", METHOD, .method = &MiscellaneousMenu_NullifyUserTimeOffset },
        { "Dumpear firmware DSP", METHOD, .method = &MiscellaneousMenu_DumpDspFirm },
        {},
    }
};

int lastNtpTzOffset = 0;

static inline bool compareTids(u64 tidA, u64 tidB)
{
    // Just like p9 clears them, ignore platform/N3DS bits
    return ((tidA ^ tidB) & ~0xF0000000ull) == 0;
}

void MiscellaneousMenu_SwitchBoot3dsxTargetTitle(void)
{
    Result res;
    char failureReason[64];
    u64 currentTid = Luma_SharedConfig->selected_hbldr_3dsx_tid;
    u64 newTid = currentTid;

    FS_ProgramInfo progInfo;
    u32 pid;
    u32 launchFlags;
    res = PMDBG_GetCurrentAppInfo(&progInfo, &pid, &launchFlags);
    bool appRunning = R_SUCCEEDED(res);

    if(compareTids(currentTid, HBLDR_DEFAULT_3DSX_TID))
    {
        if(appRunning)
            newTid = progInfo.programId;
        else
        {
            res = -1;
            strcpy(failureReason, "No se encontro un proceso\nadecuado");
        }
    }
    else
    {
        res = 0;
        newTid = HBLDR_DEFAULT_3DSX_TID;
    }

    Luma_SharedConfig->selected_hbldr_3dsx_tid = newTid;

    // Move "selected" field to "current" if no app is currently running.
    // Otherwise, PM will do it on app exit.
    // There's a small possibility of race condition but it shouldn't matter
    // here.
    // We need to do that to ensure that the ExHeader at init matches the ExHeader
    // at termination at all times, otherwise the process refcounts of sysmodules
    // get all messed up.
    if (!appRunning)
        Luma_SharedConfig->hbldr_3dsx_tid = newTid;

    if (compareTids(newTid, HBLDR_DEFAULT_3DSX_TID))
        miscellaneousMenu.items[0].title = "Cambiar Homebrew Launcher por esta app.";
    else
        miscellaneousMenu.items[0].title = "Cambiar HBL. por " HBLDR_DEFAULT_3DSX_TITLE_NAME;

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();
    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");

        if(R_SUCCEEDED(res))
            Draw_DrawString(10, 30, COLOR_WHITE, "Operacion existosa.");
        else
            Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Operacion fallida (%s).", failureReason);

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void MiscellaneousMenu_ChangeMenuCombo(void)
{
    char comboStrOrig[128], comboStr[128];
    u32 posY;

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    LumaConfig_ConvertComboToString(comboStrOrig, menuCombo);

    Draw_Lock();
    Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");

    posY = Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Combinacion actual:  %s", comboStrOrig);
    posY = Draw_DrawString(10, posY + SPACING_Y, COLOR_WHITE, "Ingrese nueva combinacion:");

    menuCombo = waitCombo();
    LumaConfig_ConvertComboToString(comboStr, menuCombo);

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");

        posY = Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Combinacion actual:  %s", comboStrOrig);
        posY = Draw_DrawFormattedString(10, posY + SPACING_Y, COLOR_WHITE, "Ingrese nueva combinacion: %s", comboStr) + SPACING_Y;

        posY = Draw_DrawString(10, posY + SPACING_Y, COLOR_WHITE, "Combinacion cambiada con exito.");

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void MiscellaneousMenu_InputRedirection(void)
{
    bool done = false;

    Result res;
    char buf[65];
    bool wasEnabled = inputRedirectionEnabled;
    bool cantStart = false;

    if(wasEnabled)
    {
        res = InputRedirection_Disable(5 * 1000 * 1000 * 1000LL);
        if(res != 0)
            sprintf(buf, "Error al parar InputRedirection (0x%08lx).", (u32)res);
        else
            miscellaneousMenu.items[2].title = "Iniciar InputRedirection";
    }
    else
    {
        s64     dummyInfo;
        bool    isN3DS = svcGetSystemInfo(&dummyInfo, 0x10001, 0) == 0;
        bool    isSocURegistered;

        res = srvIsServiceRegistered(&isSocURegistered, "soc:U");
        cantStart = R_FAILED(res) || !isSocURegistered;

        if(!cantStart && isN3DS)
        {
            bool    isIrRstRegistered;

            res = srvIsServiceRegistered(&isIrRstRegistered, "ir:rst");
            cantStart = R_FAILED(res) || !isIrRstRegistered;
        }
    }

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");

        if(!wasEnabled && cantStart)
            Draw_DrawString(10, 30, COLOR_WHITE, "No se puede iniciar InputRedirection antes de que\nel sistema se haya cargado.");
        else if(!wasEnabled)
        {
            Draw_DrawString(10, 30, COLOR_WHITE, "Iniciando InputRedirection...");
            if(!done)
            {
                res = InputRedirection_DoOrUndoPatches();
                if(R_SUCCEEDED(res))
                {
                    res = svcCreateEvent(&inputRedirectionThreadStartedEvent, RESET_STICKY);
                    if(R_SUCCEEDED(res))
                    {
                        inputRedirectionCreateThread();
                        res = svcWaitSynchronization(inputRedirectionThreadStartedEvent, 10 * 1000 * 1000 * 1000LL);
                        if(res == 0)
                            res = (Result)inputRedirectionStartResult;

                        if(res != 0)
                        {
                            svcCloseHandle(inputRedirectionThreadStartedEvent);
                            InputRedirection_DoOrUndoPatches();
                            inputRedirectionEnabled = false;
                        }
                        inputRedirectionStartResult = 0;
                    }
                }

                if(res != 0)
                    sprintf(buf, "Iniciando InputRedirection... fallo (0x%08lx).", (u32)res);
                else
                    miscellaneousMenu.items[2].title = "Parar InputRedirection";

                done = true;
            }

            if(res == 0)
                Draw_DrawString(10, 30, COLOR_WHITE, "Iniciando InputRedirection... OK.");
            else
                Draw_DrawString(10, 30, COLOR_WHITE, buf);
        }
        else
        {
            if(res == 0)
            {
                u32 posY = 30;
                posY = Draw_DrawString(10, posY, COLOR_WHITE, "InputRedirection parado con exito.\n\n");
                if (isN3DS)
                {
                    posY = Draw_DrawString(
                        10,
                        posY,
                        COLOR_WHITE,
                        "Esto puede hacer que se repita la pulsacion de\n"
                        "teclas en el menu Home sin motivo.\n\n"
                        "Pulsar ZL/ZR puede solucionar este problema.\n"
                    );
                }
            }
            else
                Draw_DrawString(10, 30, COLOR_WHITE, buf);
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void MiscellaneousMenu_UpdateTimeDateNtp(void)
{
    u32 posY;
    u32 input = 0;

    Result res;
    bool cantStart = false;

    bool isSocURegistered;

    u64 msSince1900, samplingTick;

    res = srvIsServiceRegistered(&isSocURegistered, "soc:U");
    cantStart = R_FAILED(res) || !isSocURegistered;

    int dt = 12*60 + lastNtpTzOffset;
    int utcOffset = dt / 60;
    int utcOffsetMinute = dt%60;
    int absOffset;
    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");

        absOffset = utcOffset - 12;
        absOffset = absOffset < 0 ? -absOffset : absOffset;
        posY = Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Offset UTC actual:  %c%02d%02d", utcOffset < 12 ? '-' : '+', absOffset, utcOffsetMinute);
        posY = Draw_DrawFormattedString(10, posY + SPACING_Y, COLOR_WHITE, "Usa izq/der para cambiar el offset de hora.\nUsa arr/aba para cambiar el offset de minutos.\nPulsa A cuando acabes.") + SPACING_Y;

        input = waitInput();

        if(input & KEY_LEFT) utcOffset = (27 + utcOffset - 1) % 27; // ensure utcOffset >= 0
        if(input & KEY_RIGHT) utcOffset = (utcOffset + 1) % 27;
        if(input & KEY_UP) utcOffsetMinute = (utcOffsetMinute + 1) % 60;
        if(input & KEY_DOWN) utcOffsetMinute = (60 + utcOffsetMinute - 1) % 60;
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(input & (KEY_A | KEY_B)) && !menuShouldExit);

    if (input & KEY_B)
        return;

    utcOffset -= 12;
    lastNtpTzOffset = 60 * utcOffset + utcOffsetMinute;

    res = srvIsServiceRegistered(&isSocURegistered, "soc:U");
    cantStart = R_FAILED(res) || !isSocURegistered;
    res = 0;
    if(!cantStart)
    {
        res = ntpGetTimeStamp(&msSince1900, &samplingTick);
        if(R_SUCCEEDED(res))
        {
            msSince1900 += 1000 * (3600 * utcOffset + 60 * utcOffsetMinute);
            res = ntpSetTimeDate(msSince1900, samplingTick);
        }
    }

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");

        absOffset = utcOffset;
        absOffset = absOffset < 0 ? -absOffset : absOffset;
        Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Offset UTC actual:  %c%02d", utcOffset < 0 ? '-' : '+', absOffset);
        if (cantStart)
            Draw_DrawFormattedString(10, posY + 2 * SPACING_Y, COLOR_WHITE, "No se puede sincronizar fecha/hora antes\nde que el sistema haya cargado.") + SPACING_Y;
        else if (R_FAILED(res))
            Draw_DrawFormattedString(10, posY + 2 * SPACING_Y, COLOR_WHITE, "Operacion fallida (%08lx).", (u32)res) + SPACING_Y;
        else
            Draw_DrawFormattedString(10, posY + 2 * SPACING_Y, COLOR_WHITE, "Fecha/hora actualizada con exito.") + SPACING_Y;

        input = waitInput();

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(input & KEY_B) && !menuShouldExit);

}

void MiscellaneousMenu_NullifyUserTimeOffset(void)
{
    Result res = ntpNullifyUserTimeOffset();

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");
        if(R_SUCCEEDED(res))
            Draw_DrawString(10, 30, COLOR_WHITE, "Operacion exitosa.\n\nReinicia para aplicar los cambios.");
        else
            Draw_DrawFormattedString(10, 30, COLOR_WHITE, "Operacion fallida (0x%08lx).", res);
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

static Result MiscellaneousMenu_DumpDspFirmCallback(Handle procHandle, u32 textSz, u32 roSz, u32 rwSz)
{
    (void)procHandle;
    Result res = 0;

    // NOTE: we suppose .text, .rodata, .data+.bss are contiguous & in that order
    u32 rwStart = 0x00100000 + textSz + roSz;
    u32 rwEnd = rwStart + rwSz;

    // Locate the DSP firm (it's in .data, not .rodata, suprisingly)
    u32 magic;
    memcpy(&magic, "DSP1", 4);
    const u32 *off = (u32 *)rwStart;

    for (; off < (u32 *)rwEnd && *off != magic; off++);

    if (off >= (u32 *)rwEnd || off < (u32 *)(rwStart + 0x100))
        return -2;

    // Do some sanity checks
    const DspFirm *firm = (const DspFirm *)((u32)off - 0x100);
    if (firm->totalSize > 0x10000 || firm->numSegments > 10)
        return -3;
    if ((u32)firm + firm->totalSize >= rwEnd)
        return -3;

    // Dump to SD card (no point in dumping to CTRNAND as 3dsx stuff doesn't work there)
    IFile file;
    FS_Archive archive;

    // Create sdmc:/3ds directory if it doesn't exist yet
    res = FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""));
    if(R_SUCCEEDED(res))
    {
        res = FSUSER_CreateDirectory(archive, fsMakePath(PATH_ASCII, "/3ds"), 0);
        if((u32)res == 0xC82044BE) // directory already exists
            res = 0;
        FSUSER_CloseArchive(archive);
    }

    if (R_SUCCEEDED(res))
        res = IFile_Open(
            &file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
            fsMakePath(PATH_ASCII, "/3ds/dspfirm.cdc"), FS_OPEN_CREATE | FS_OPEN_WRITE
        );

    u64 total;
    if(R_SUCCEEDED(res))
        res = IFile_Write(&file, &total, firm, firm->totalSize, 0);
    if(R_SUCCEEDED(res))
        res = IFile_SetSize(&file, firm->totalSize); // truncate accordingly

    IFile_Close(&file);

    return res;
}
void MiscellaneousMenu_DumpDspFirm(void)
{
    Result res = OperateOnProcessByName("menu", MiscellaneousMenu_DumpDspFirmCallback);

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones Miscelaneas");
        if(R_SUCCEEDED(res))
            Draw_DrawString(10, 30, COLOR_WHITE, "Firm. DSP guardado con exito en /3ds/dspfirm.cdc\nen la tarjeta SD.");
        else
            Draw_DrawFormattedString(
                10, 30, COLOR_WHITE,
                "Operacion fallida (0x%08lx).\n\nAsegurate de que el menu Home esta funcionando y\nque tu SD esta insertada.",
                res
            );
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}
