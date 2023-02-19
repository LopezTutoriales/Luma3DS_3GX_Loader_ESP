/*
*   This file is part of Luma3DS
*   Copyright (C) 2017-2018 Sono (https://github.com/MarcuzD), panicbit
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
#include <math.h>
#include "memory.h"
#include "menu.h"
#include "menus/screen_filters.h"
#include "draw.h"
#include "redshift/colorramp.h"

typedef union {
    struct {
        u8 r;
        u8 g;
        u8 b;
        u8 z;
    };
    u32 raw;
} Pixel;

ScreenFilter topScreenFilter;
ScreenFilter bottomScreenFilter;

static inline bool ScreenFiltersMenu_IsDefaultSettings(void)
{
    static const ScreenFilter defaultFilter = { 6500, false, 1.0f, 1.0f, 0.0f };
    bool ok1 = memcmp(&topScreenFilter, &defaultFilter, sizeof(defaultFilter)) == 0;
    bool ok2 = memcmp(&bottomScreenFilter, &defaultFilter, sizeof(defaultFilter)) == 0;
    return ok1 && ok2;
}

static inline u8 ScreenFiltersMenu_GetColorLevel(int inLevel, float wp, float a, float b, float g)
{
    float level = inLevel / 255.0f;
    level = a * wp * level + b;
    level = powf(CLAMP(level, 0.0f, 1.0f), g);
    s32 levelInt = (s32)(255.0f * level + 0.5f); // round to nearest integer
    return levelInt <= 0 ? 0 : levelInt >= 255 ? 255 : (u8)levelInt; // clamp
}

static u8 ScreenFilterMenu_CalculatePolynomialColorLutComponent(const float coeffs[][3], u32 component, float gamma, u32 dim, int inLevel)
{
    float x = inLevel / 255.0f;
    float level = 0.0f;
    float xN = 1.0f;

    // Compute a_n * x^n + ... a_0, then clamp, then exponentiate by "gamma" and clamp again
    for (u32 i = 0; i < dim + 1; i++)
    {
        level += coeffs[i][component] * xN;
        xN *= x;
    }

    level = powf(CLAMP(level, 0.0f, 1.0f), gamma);
    s32 levelInt = (s32)(255.0f * level + 0.5f); // round up
    return (u8)CLAMP(levelInt, 0, 255); // clamp again just to be sure
}

static void ScreenFilterMenu_WritePolynomialColorLut(bool top, const float coeffs[][3], bool invert, float gamma, u32 dim)
{
    if (top)
        GPU_FB_TOP_COL_LUT_INDEX = 0;
    else
        GPU_FB_BOTTOM_COL_LUT_INDEX = 0;

    for (int i = 0; i <= 255; i++) {
        Pixel px;
        int inLevel = invert ? 255 - i : i;
        px.r = ScreenFilterMenu_CalculatePolynomialColorLutComponent(coeffs, 0, gamma, dim, inLevel);
        px.g = ScreenFilterMenu_CalculatePolynomialColorLutComponent(coeffs, 1, gamma, dim, inLevel);
        px.b = ScreenFilterMenu_CalculatePolynomialColorLutComponent(coeffs, 2, gamma, dim, inLevel);
        px.z = 0;

        if (top)
            GPU_FB_TOP_COL_LUT_ELEM = px.raw;
        else
            GPU_FB_BOTTOM_COL_LUT_ELEM = px.raw;
    }
}

static void ScreenFiltersMenu_ApplyColorSettings(bool top)
{
    const ScreenFilter *filter = top ? &topScreenFilter : &bottomScreenFilter;

    float wp[3];
    colorramp_get_white_point(wp, filter->cct);
    float a = filter->contrast;
    float b = filter->brightness;
    float g = filter->gamma;
    bool inv = filter->invert;

    float poly[][3] = {
        { b, b, b },                            // x^0
        { a * wp[0], a * wp[1], a * wp[2] },    // x^1
    };

    ScreenFilterMenu_WritePolynomialColorLut(top, poly, inv, g, 1);
}

static void ScreenFiltersMenu_SetCct(u16 cct)
{
    topScreenFilter.cct = cct;
    bottomScreenFilter.cct = cct;
    ScreenFiltersMenu_ApplyColorSettings(true);
    ScreenFiltersMenu_ApplyColorSettings(false);
}

Menu screenFiltersMenu = {
    "Menu de filtros de pantalla",
    {
        { "[6500K]  Por defecto", METHOD, .method = &ScreenFiltersMenu_SetDefault },
        { "[10000K] Acuario", METHOD, .method = &ScreenFiltersMenu_SetAquarium },
        { "[7500K]  Cielo nublado", METHOD, .method = &ScreenFiltersMenu_SetOvercastSky },
        { "[5500K]  Luz", METHOD, .method = &ScreenFiltersMenu_SetDaylight },
        { "[4200K]  Fluorescente", METHOD, .method = &ScreenFiltersMenu_SetFluorescent },
        { "[3400K]  Halogeno", METHOD, .method = &ScreenFiltersMenu_SetHalogen },
        { "[2700K]  Incandescente", METHOD, .method = &ScreenFiltersMenu_SetIncandescent },
        { "[2300K]  Incandescente calido", METHOD, .method = &ScreenFiltersMenu_SetWarmIncandescent },
        { "[1900K]  Vela", METHOD, .method = &ScreenFiltersMenu_SetCandle },
        { "[1200K]  Ascua", METHOD, .method = &ScreenFiltersMenu_SetEmber },
        { "Configuracion Avanzada", METHOD, .method = &ScreenFiltersMenu_AdvancedConfiguration },
        {},
    }
};

#define DEF_CCT_SETTER(temp, name)\
void ScreenFiltersMenu_Set##name(void)\
{\
    ScreenFiltersMenu_SetCct(temp);\
}

void ScreenFiltersMenu_RestoreSettings(void)
{
    // Precondition: menu has not been entered

    // Not initialized/default: return
    if (ScreenFiltersMenu_IsDefaultSettings())
        return;

    // Wait for GSP to restore the CCT table
    svcSleepThread(20 * 1000 * 1000LL);

    // Pause GSP, then wait a bit to ensure GPU commands complete
    // We need to ensure no GPU stuff is running when changing
    // the gamma table (otherwise colors become and stay glitched).
    svcKernelSetState(0x10000, 2);
    svcSleepThread(5 * 1000 * 100LL);

    ScreenFiltersMenu_ApplyColorSettings(true);
    ScreenFiltersMenu_ApplyColorSettings(false);

    // Unpause GSP
    svcKernelSetState(0x10000, 2);
    svcSleepThread(5 * 1000 * 100LL);
}

void ScreenFiltersMenu_LoadConfig(void)
{
    s64 out = 0;

    svcGetSystemInfo(&out, 0x10000, 0x102);
    topScreenFilter.cct = (u16)out;
    if (topScreenFilter.cct < 1000 || topScreenFilter.cct > 25100)
        topScreenFilter.cct = 6500;

    svcGetSystemInfo(&out, 0x10000, 0x104);
    topScreenFilter.gamma = (float)(out / FLOAT_CONV_MULT);
    if (topScreenFilter.gamma < 0.0f || topScreenFilter.gamma > 1411.0f)
        topScreenFilter.gamma = 1.0f;

    svcGetSystemInfo(&out, 0x10000, 0x105);
    topScreenFilter.contrast = (float)(out / FLOAT_CONV_MULT);
    if (topScreenFilter.contrast < 0.0f || topScreenFilter.contrast > 255.0f)
        topScreenFilter.contrast = 1.0f;

    svcGetSystemInfo(&out, 0x10000, 0x106);
    topScreenFilter.brightness = (float)(out / FLOAT_CONV_MULT);
    if (topScreenFilter.brightness < -1.0f || topScreenFilter.brightness > 1.0f)
        topScreenFilter.brightness = 0.0f;

    svcGetSystemInfo(&out, 0x10000, 0x107);
    topScreenFilter.invert = (bool)out;

    svcGetSystemInfo(&out, 0x10000, 0x108);
    bottomScreenFilter.cct = (u16)out;
    if (bottomScreenFilter.cct < 1000 || bottomScreenFilter.cct > 25100)
        bottomScreenFilter.cct = 6500;

    svcGetSystemInfo(&out, 0x10000, 0x109);
    bottomScreenFilter.gamma = (float)(out / FLOAT_CONV_MULT);
    if (bottomScreenFilter.gamma < 0.0f || bottomScreenFilter.gamma > 1411.0f)
        bottomScreenFilter.gamma = 1.0f;

    svcGetSystemInfo(&out, 0x10000, 0x10A);
    bottomScreenFilter.contrast = (float)(out / FLOAT_CONV_MULT);
    if (bottomScreenFilter.contrast < 0.0f || bottomScreenFilter.contrast > 255.0f)
        bottomScreenFilter.contrast = 1.0f;

    svcGetSystemInfo(&out, 0x10000, 0x10B);
    bottomScreenFilter.brightness = (float)(out / FLOAT_CONV_MULT);
    if (bottomScreenFilter.brightness < -1.0f || bottomScreenFilter.brightness > 1.0f)
        bottomScreenFilter.brightness = 0.0f;

    svcGetSystemInfo(&out, 0x10000, 0x10C);
    bottomScreenFilter.invert = (bool)out;
}

DEF_CCT_SETTER(6500, Default)

DEF_CCT_SETTER(10000, Aquarium)
DEF_CCT_SETTER(7500, OvercastSky)
DEF_CCT_SETTER(5500, Daylight)
DEF_CCT_SETTER(4200, Fluorescent)
DEF_CCT_SETTER(3400, Halogen)
DEF_CCT_SETTER(2700, Incandescent)
DEF_CCT_SETTER(2300, WarmIncandescent)
DEF_CCT_SETTER(1900, Candle)
DEF_CCT_SETTER(1200, Ember)

static void ScreenFiltersMenu_ClampFilter(ScreenFilter *filter)
{
    filter->cct = CLAMP(filter->cct, 1000, 25100);
    filter->gamma = CLAMP(filter->gamma, 0.0f, 1411.0f); // ln(255) / ln(254/255): (254/255)^1411 <= 1/255
    filter->contrast = CLAMP(filter->contrast, 0.0f, 255.0f);
    filter->brightness = CLAMP(filter->brightness, -1.0f, 1.0f);
}

static void ScreenFiltersMenu_AdvancedConfigurationChangeValue(int pos, int mult, bool sync)
{
    ScreenFilter *filter = pos >= 5 ? &bottomScreenFilter : &topScreenFilter;
    ScreenFilter *otherFilter = pos >= 5 ? &topScreenFilter : &bottomScreenFilter;

    int otherMult = sync ? mult : 0;

    switch (pos % 5)
    {
        case 0:
            filter->cct += 100 * mult;
            otherFilter->cct += 100 * otherMult;
            break;
        case 1:
            filter->gamma += 0.01f * mult;
            otherFilter->gamma += 0.01f * otherMult;
            break;
        case 2:
            filter->contrast += 0.01f * mult;
            otherFilter->contrast += 0.01f * otherMult;
            break;
        case 3:
            filter->brightness += 0.01f * mult;
            otherFilter->brightness += 0.01f * otherMult;
            break;
        case 4:
            filter->invert = !filter->invert;
            otherFilter->invert = sync ? !otherFilter->invert : otherFilter->invert;
            break;
    }

    // Clamp
    ScreenFiltersMenu_ClampFilter(&topScreenFilter);
    ScreenFiltersMenu_ClampFilter(&bottomScreenFilter);

    // Update the LUT
    ScreenFiltersMenu_ApplyColorSettings(true);
    ScreenFiltersMenu_ApplyColorSettings(false);
}

static u32 ScreenFiltersMenu_AdvancedConfigurationHelper(const ScreenFilter *filter, int offset, int pos, u32 posY)
{
    char buf[64];

    Draw_DrawCharacter(10, posY, COLOR_TITLE, pos == offset++ ? '>' : ' ');
    posY = Draw_DrawFormattedString(30, posY, COLOR_WHITE, "Temperatura: %12dK    \n", filter->cct);

    floatToString(buf, filter->gamma, 2, true);
    Draw_DrawCharacter(10, posY, COLOR_TITLE, pos == offset++ ? '>' : ' ');
    posY = Draw_DrawFormattedString(30, posY, COLOR_WHITE, "Gama:        %13s    \n", buf);

    floatToString(buf, filter->contrast, 2, true);
    Draw_DrawCharacter(10, posY, COLOR_TITLE, pos == offset++ ? '>' : ' ');
    posY = Draw_DrawFormattedString(30, posY, COLOR_WHITE, "Contraste:   %13s    \n", buf);

    floatToString(buf, filter->brightness, 2, true);
    Draw_DrawCharacter(10, posY, COLOR_TITLE, pos == offset++ ? '>' : ' ');
    posY = Draw_DrawFormattedString(30, posY, COLOR_WHITE, "Brillo:      %13s    \n", buf);

    Draw_DrawCharacter(10, posY, COLOR_TITLE, pos == offset++ ? '>' : ' ');
    posY = Draw_DrawFormattedString(30, posY, COLOR_WHITE, "Invertir:    %13s    \n", filter->invert ? "true" : "false");

    return posY;
}

void ScreenFiltersMenu_AdvancedConfiguration(void)
{
    u32 posY;
    u32 input = 0;
    u32 held = 0;

    int pos = 0;
    int mult = 1;

    bool sync = true;

    do
    {
        Draw_Lock();
        Draw_DrawString(10, 10, COLOR_TITLE, "Menu de filtros de pantalla");

        posY = 30;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Usa izq/der para subir/bajar el valor seleccionado.\n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Manten R para cambiar el valor mas rapido.\n");
        posY = Draw_DrawFormattedString(10, posY, COLOR_WHITE, "Actualizar ambas pant: %s (L para cambiar) \n", sync ? "si" : "no") + SPACING_Y;

        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Pantalla superior:\n");
        posY = ScreenFiltersMenu_AdvancedConfigurationHelper(&topScreenFilter, 0, pos, posY) + SPACING_Y;

        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Pantalla inferior:\n");
        posY = ScreenFiltersMenu_AdvancedConfigurationHelper(&bottomScreenFilter, 5, pos, posY) + SPACING_Y;

        input = waitInputWithTimeoutEx(&held, -1);
        mult = (held & KEY_R) ? 10 : 1;

        if (input & KEY_L)
            sync = !sync;
        if (input & KEY_LEFT)
            ScreenFiltersMenu_AdvancedConfigurationChangeValue(pos, -mult, sync);
        if (input & KEY_RIGHT)
            ScreenFiltersMenu_AdvancedConfigurationChangeValue(pos, mult, sync);
        if (input & KEY_UP)
            pos = (10 + pos - 1) % 10;
        if (input & KEY_DOWN)
            pos = (pos + 1) % 10;

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while(!(input & (KEY_A | KEY_B)) && !menuShouldExit);
}
