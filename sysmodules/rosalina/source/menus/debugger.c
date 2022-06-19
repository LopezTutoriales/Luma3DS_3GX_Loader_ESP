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

#include "menus/debugger.h"
#include "memory.h"
#include "draw.h"
#include "minisoc.h"
#include "fmt.h"
#include "pmdbgext.h"
#include "gdb/server.h"
#include "gdb/debug.h"
#include "gdb/monitor.h"
#include "gdb/net.h"
#include "pmdbgext.h"

Menu debuggerMenu = {
    "Menu de opciones de Depuracion",
    {
        { "Habilitar depurador",                           METHOD, .method = &DebuggerMenu_EnableDebugger  },
        { "Deshabilitar depurador",                        METHOD, .method = &DebuggerMenu_DisableDebugger },
        { "Forzar depuracion en la siguiente app lanzada", METHOD, .method = &DebuggerMenu_DebugNextApplicationByForce },
        {},
    }
};

static MyThread debuggerSocketThread;
static MyThread debuggerDebugThread;
static u8 ALIGN(8) debuggerSocketThreadStack[0x5000];
static u8 ALIGN(8) debuggerDebugThreadStack[0x3000];

GDBServer gdbServer = { 0 };

GDBContext *nextApplicationGdbCtx = NULL;

void debuggerSocketThreadMain(void);
MyThread *debuggerCreateSocketThread(void)
{
    MyThread_Create(&debuggerSocketThread, debuggerSocketThreadMain, debuggerSocketThreadStack, 0x5000, 0x20, CORE_SYSTEM);
    return &debuggerSocketThread;
}

void debuggerDebugThreadMain(void);
MyThread *debuggerCreateDebugThread(void)
{
    MyThread_Create(&debuggerDebugThread, debuggerDebugThreadMain, debuggerDebugThreadStack, 0x3000, 0x20, CORE_SYSTEM);
    return &debuggerDebugThread;
}

void debuggerFetchAndSetNextApplicationDebugHandleTask(void *argdata)
{
    (void)argdata;
    Handle debug = 0;
    PMDBG_RunQueuedProcess(&debug);
    GDB_LockAllContexts(&gdbServer);
    nextApplicationGdbCtx->debug = debug;
    if (debug == 0)
        nextApplicationGdbCtx->flags = 0;
    else
        nextApplicationGdbCtx->flags |= GDB_FLAG_ATTACHED_AT_START;
    nextApplicationGdbCtx = NULL;
    GDB_UnlockAllContexts(&gdbServer);
}

Result debuggerDisable(s64 timeout)
{
    Result res = 0;
    bool initialized = gdbServer.referenceCount != 0;
    if(initialized)
    {
        svcSignalEvent(gdbServer.super.shall_terminate_event);
        server_kill_connections(&gdbServer.super);

        res = MyThread_Join(&debuggerDebugThread, timeout);
        if(res == 0)
            res = MyThread_Join(&debuggerSocketThread, timeout);

        Handle dummy = 0;
        PMDBG_RunQueuedProcess(&dummy);
        svcCloseHandle(dummy);
        PMDBG_DebugNextApplicationByForce(false);
        nextApplicationGdbCtx = NULL;
    }

    return res;
}

void DebuggerMenu_EnableDebugger(void)
{
    bool done = false, alreadyEnabled = gdbServer.super.running;
    Result res = 0;
    char buf[65];
    bool isSocURegistered;

    res = srvIsServiceRegistered(&isSocURegistered, "soc:U");
    isSocURegistered = R_SUCCEEDED(res) && isSocURegistered;

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones de Depuracion");

        if(alreadyEnabled)
            Draw_DrawString(10, 30, COLOR_WHITE, "Ya habilitado!");
        else if(!isSocURegistered)
            Draw_DrawString(10, 30, COLOR_WHITE, "Imposible iniciar el depurador antes de que el\nsistema termine de cargar.");
        else
        {
            Draw_DrawString(10, 30, COLOR_WHITE, "Iniciando el depurador...");

            if(!done)
            {
                res = GDB_InitializeServer(&gdbServer);
                Handle handles[3] = { gdbServer.super.started_event, gdbServer.super.shall_terminate_event, preTerminationEvent };
                s32 idx;
                if(R_SUCCEEDED(res))
                {
                    debuggerCreateSocketThread();
                    debuggerCreateDebugThread();
                    res = svcWaitSynchronizationN(&idx, handles, 3, false, 5 * 1000 * 1000 * 1000LL);
                    if(res == 0) res = gdbServer.super.init_result;
                }

                if(res != 0)
                    sprintf(buf, "Iniciando el depurador... fallo (0x%08lx).", (u32)res);
                done = true;
            }
            if(res == 0)
                Draw_DrawString(10, 30, COLOR_WHITE, "Iniciando el depurador... OK.");
            else
                Draw_DrawString(10, 30, COLOR_WHITE, buf);
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void DebuggerMenu_DisableDebugger(void)
{
    bool initialized = gdbServer.referenceCount != 0;

    Result res = initialized ? debuggerDisable(2 * 1000 * 1000 * 1000LL) : 0;
    char buf[65];

    if(res != 0)
        sprintf(buf, "Fallo al quitar el depurador (0x%08lx).", (u32)res);

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones de Depuracion");
        Draw_DrawString(10, 30, COLOR_WHITE, initialized ? (res == 0 ? "Depurador quitado con exito." : buf) : "Depurador no habilitado.");
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void DebuggerMenu_DebugNextApplicationByForce(void)
{
    bool initialized = gdbServer.referenceCount != 0;
    Result res = 0;
    char buf[256];

    if(initialized)
    {
        GDB_LockAllContexts(&gdbServer);

        if (nextApplicationGdbCtx != NULL)
            strcpy(buf, "Operacion ya realizada.");
        else
        {
            nextApplicationGdbCtx = GDB_SelectAvailableContext(&gdbServer, GDB_PORT_BASE + 3, GDB_PORT_BASE + 4);
            if (nextApplicationGdbCtx != NULL)
            {
                nextApplicationGdbCtx->debug = 0;
                nextApplicationGdbCtx->pid = 0xFFFFFFFF;
                res = PMDBG_DebugNextApplicationByForce(true);
                if(R_SUCCEEDED(res))
                    sprintf(buf, "Operacion realizada.\nUsa el puerto %d para conectarte en la sig.\naplicacion lanzada.", nextApplicationGdbCtx->localPort);
                else
                {
                    nextApplicationGdbCtx->flags = 0;
                    nextApplicationGdbCtx->localPort = 0;
                    nextApplicationGdbCtx = NULL;
                        sprintf(buf, "Operacion fallida (0x%08lx).", (u32)res);
                }
            }
            else
                strcpy(buf, "Fallo al asignar un espacio.\nAnula la seleccion de un proceso de la lista primero");
        }
        GDB_UnlockAllContexts(&gdbServer);
    }
    else
        strcpy(buf, "Depurador no habilitado.");

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de opciones de Depuracion");
        Draw_DrawString(10, 30, COLOR_WHITE, buf);
        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(waitInput() & KEY_B) && !menuShouldExit);
}

void debuggerSocketThreadMain(void)
{
    GDB_IncrementServerReferenceCount(&gdbServer);
    GDB_RunServer(&gdbServer);
    GDB_DecrementServerReferenceCount(&gdbServer);
}

void debuggerDebugThreadMain(void)
{
    GDB_IncrementServerReferenceCount(&gdbServer);
    GDB_RunMonitor(&gdbServer);
    GDB_DecrementServerReferenceCount(&gdbServer);
}
