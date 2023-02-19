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
*   Boyer-Moore Horspool algorithm adapted from http://www-igm.univ-mlv.fr/~lecroq/string/node18.html#SECTION00180
*   memcpy, memset and memcmp adapted from https://github.com/mid-kid/CakesForeveryWan/blob/557a8e8605ab3ee173af6497486e8f22c261d0e2/source/memfuncs.c
*/

#include "memory.h"

u8 *memsearch(u8 *startPos, const void *pattern, u32 size, u32 patternSize)
{
    const u8 *patternc = (const u8 *)pattern;
    u32 table[256];

    //Preprocessing
    for(u32 i = 0; i < 256; i++)
        table[i] = patternSize;
    for(u32 i = 0; i < patternSize - 1; i++)
        table[patternc[i]] = patternSize - i - 1;

    //Searching
    u32 j = 0;
    while(j <= size - patternSize)
    {
        u8 c = startPos[j + patternSize - 1];
        if(patternc[patternSize - 1] == c && memcmp(pattern, startPos + j, patternSize - 1) == 0)
            return startPos + j;
        j += table[c];
    }

    return NULL;
}

void *copyFromLegacyModeFcram(void *dst, const void *src, size_t size)
{
    // Copy 2 bytes with a stride of 8
    const u16 *src16 = (const u16 *)src;
    u16 *dst16 = (u16 *)dst;

    for (size_t i = 0; i < size / 2; i++)
        dst16[i] = src16[4 * i];

    return dst;
}

void *copyToLegacyModeFcram(void *dst, const void *src, size_t size)
{
    // Copy 2 bytes with a stride of 8
    const u16 *src16 = (const u16 *)src;
    u16 *dst16 = (u16 *)dst;

    for (size_t i = 0; i < size / 2; i++)
        dst16[4 * i] = src16[i];

    return dst;
}
