/*
 * Code page functions
 *
 * Copyright 2000 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "winbase.h"
#include "winerror.h"
#include "winnls.h"
#include "wine/unicode.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(string);

/* current code pages */
static const union cptable *ansi_cptable;
static const union cptable *oem_cptable;
static const union cptable *mac_cptable;

/* retrieve a code page table from the locale info */
static const union cptable *get_locale_cp( LCID lcid, LCTYPE type )
{
    const union cptable *table = NULL;
    char buf[32];

    if (GetLocaleInfoA( lcid, type, buf, sizeof(buf) )) table = cp_get_table( atoi(buf) );
    return table;
}

/* setup default codepage info before we can get at the locale stuff */
static void init_codepages(void)
{
    ansi_cptable = cp_get_table( 1252 );
    oem_cptable  = cp_get_table( 437 );
    mac_cptable  = cp_get_table( 10000 );
    assert( ansi_cptable );
    assert( oem_cptable );
    assert( mac_cptable );
}

/* find the table for a given codepage, handling CP_ACP etc. pseudo-codepages */
static const union cptable *get_codepage_table( unsigned int codepage )
{
    const union cptable *ret = NULL;

    if (!ansi_cptable) init_codepages();

    switch(codepage)
    {
    case CP_ACP:        return ansi_cptable;
    case CP_OEMCP:      return oem_cptable;
    case CP_MACCP:      return mac_cptable;
    case CP_THREAD_ACP: return get_locale_cp( GetThreadLocale(), LOCALE_IDEFAULTANSICODEPAGE );
    case CP_UTF7:
    case CP_UTF8:
        break;
    default:
        if (codepage == ansi_cptable->info.codepage) return ansi_cptable;
        if (codepage == oem_cptable->info.codepage) return oem_cptable;
        if (codepage == mac_cptable->info.codepage) return mac_cptable;
        ret = cp_get_table( codepage );
        break;
    }
    return ret;
}

/* initialize default code pages from locale info */
/* FIXME: should be done in init_codepages, but it can't right now */
/* since it needs KERNEL32 to be loaded for the locale info. */
void CODEPAGE_Init(void)
{
    extern void __wine_init_codepages( const union cptable *ansi, const union cptable *oem );
    const union cptable *table;
    LCID lcid = GetUserDefaultLCID();

    if (!ansi_cptable) init_codepages();  /* just in case */

    if ((table = get_locale_cp( lcid, LOCALE_IDEFAULTANSICODEPAGE ))) ansi_cptable = table;
    if ((table = get_locale_cp( lcid, LOCALE_IDEFAULTMACCODEPAGE ))) mac_cptable = table;
    if ((table = get_locale_cp( lcid, LOCALE_IDEFAULTCODEPAGE ))) oem_cptable = table;
    __wine_init_codepages( ansi_cptable, oem_cptable );

    TRACE( "ansi=%03d oem=%03d mac=%03d\n", ansi_cptable->info.codepage,
           oem_cptable->info.codepage, mac_cptable->info.codepage );
}

/******************************************************************************
 *              GetACP   (KERNEL32.@)
 *
 * RETURNS
 *    Current ANSI code-page identifier, default if no current defined
 */
UINT WINAPI GetACP(void)
{
    if (!ansi_cptable) init_codepages();
    return ansi_cptable->info.codepage;
}


/***********************************************************************
 *              GetOEMCP   (KERNEL32.@)
 */
UINT WINAPI GetOEMCP(void)
{
    if (!oem_cptable) init_codepages();
    return oem_cptable->info.codepage;
}


/***********************************************************************
 *           IsValidCodePage   (KERNEL32.@)
 */
BOOL WINAPI IsValidCodePage( UINT codepage )
{
    switch(codepage) {
    case CP_SYMBOL:
        return FALSE;
    case CP_UTF7:
    case CP_UTF8:
        return TRUE;
    default:
        return cp_get_table( codepage ) != NULL;
    }
}


/***********************************************************************
 *           IsDBCSLeadByteEx   (KERNEL32.@)
 */
BOOL WINAPI IsDBCSLeadByteEx( UINT codepage, BYTE testchar )
{
    const union cptable *table = get_codepage_table( codepage );
    return table && is_dbcs_leadbyte( table, testchar );
}


/***********************************************************************
 *           IsDBCSLeadByte   (KERNEL32.@)
 *           IsDBCSLeadByte   (KERNEL.207)
 */
BOOL WINAPI IsDBCSLeadByte( BYTE testchar )
{
    if (!ansi_cptable) init_codepages();
    return is_dbcs_leadbyte( ansi_cptable, testchar );
}


/***********************************************************************
 *           GetCPInfo   (KERNEL32.@)
 */
BOOL WINAPI GetCPInfo( UINT codepage, LPCPINFO cpinfo )
{
    const union cptable *table = get_codepage_table( codepage );

    if (!table)
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    if (table->info.def_char & 0xff00)
    {
        cpinfo->DefaultChar[0] = table->info.def_char & 0xff00;
        cpinfo->DefaultChar[1] = table->info.def_char & 0x00ff;
    }
    else
    {
        cpinfo->DefaultChar[0] = table->info.def_char & 0xff;
        cpinfo->DefaultChar[1] = 0;
    }
    if ((cpinfo->MaxCharSize = table->info.char_size) == 2)
        memcpy( cpinfo->LeadByte, table->dbcs.lead_bytes, sizeof(cpinfo->LeadByte) );
    else
        cpinfo->LeadByte[0] = cpinfo->LeadByte[1] = 0;

    return TRUE;
}


/***********************************************************************
 *              EnumSystemCodePagesA   (KERNEL32.@)
 */
BOOL WINAPI EnumSystemCodePagesA( CODEPAGE_ENUMPROCA lpfnCodePageEnum, DWORD flags )
{
    const union cptable *table;
    char buffer[10];
    int index = 0;

    for (;;)
    {
        if (!(table = cp_enum_table( index++ ))) break;
        sprintf( buffer, "%d", table->info.codepage );
        if (!lpfnCodePageEnum( buffer )) break;
    }
    return TRUE;
}


/***********************************************************************
 *              EnumSystemCodePagesW   (KERNEL32.@)
 */
BOOL WINAPI EnumSystemCodePagesW( CODEPAGE_ENUMPROCW lpfnCodePageEnum, DWORD flags )
{
    const union cptable *table;
    WCHAR buffer[10], *p;
    int page, index = 0;

    for (;;)
    {
        if (!(table = cp_enum_table( index++ ))) break;
        p = buffer + sizeof(buffer)/sizeof(WCHAR);
        *--p = 0;
        page = table->info.codepage;
        do
        {
            *--p = '0' + (page % 10);
            page /= 10;
        } while( page );
        if (!lpfnCodePageEnum( p )) break;
    }
    return TRUE;
}


/***********************************************************************
 *              MultiByteToWideChar   (KERNEL32.@)
 *
 * PARAMS
 *   page [in]    Codepage character set to convert from
 *   flags [in]   Character mapping flags
 *   src [in]     Source string buffer
 *   srclen [in]  Length of source string buffer
 *   dst [in]     Destination buffer
 *   dstlen [in]  Length of destination buffer
 *
 * NOTES
 *   The returned length includes the null terminator character.
 *
 * RETURNS
 *   Success: If dstlen > 0, number of characters written to destination
 *            buffer.  If dstlen == 0, number of characters needed to do
 *            conversion.
 *   Failure: 0. Occurs if not enough space is available.
 *
 * ERRORS
 *   ERROR_INSUFFICIENT_BUFFER
 *   ERROR_INVALID_PARAMETER
 *   ERROR_NO_UNICODE_TRANSLATION
 *
 */
INT WINAPI MultiByteToWideChar( UINT page, DWORD flags, LPCSTR src, INT srclen,
                                LPWSTR dst, INT dstlen )
{
    const union cptable *table;
    int ret;

    if (!src || (!dst && dstlen))
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    if (srclen == -1) srclen = strlen(src) + 1;

    if (flags & MB_USEGLYPHCHARS) FIXME("MB_USEGLYPHCHARS not supported\n");

    switch(page)
    {
    case CP_UTF7:
        FIXME("UTF not supported\n");
        SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
        return 0;
    case CP_UTF8:
        ret = utf8_mbstowcs( flags, src, srclen, dst, dstlen );
        break;
    default:
        if (!(table = get_codepage_table( page )))
        {
            SetLastError( ERROR_INVALID_PARAMETER );
            return 0;
        }
        ret = cp_mbstowcs( table, flags, src, srclen, dst, dstlen );
        break;
    }

    if (ret < 0)
    {
        switch(ret)
        {
        case -1: SetLastError( ERROR_INSUFFICIENT_BUFFER ); break;
        case -2: SetLastError( ERROR_NO_UNICODE_TRANSLATION ); break;
        }
        ret = 0;
    }
    return ret;
}


/***********************************************************************
 *              WideCharToMultiByte   (KERNEL32.@)
 *
 * PARAMS
 *   page [in]    Codepage character set to convert to
 *   flags [in]   Character mapping flags
 *   src [in]     Source string buffer
 *   srclen [in]  Length of source string buffer
 *   dst [in]     Destination buffer
 *   dstlen [in]  Length of destination buffer
 *   defchar [in] Default character to use for conversion if no exact
 *		    conversion can be made
 *   used [out]   Set if default character was used in the conversion
 *
 * NOTES
 *   The returned length includes the null terminator character.
 *
 * RETURNS
 *   Success: If dstlen > 0, number of characters written to destination
 *            buffer.  If dstlen == 0, number of characters needed to do
 *            conversion.
 *   Failure: 0. Occurs if not enough space is available.
 *
 * ERRORS
 *   ERROR_INSUFFICIENT_BUFFER
 *   ERROR_INVALID_PARAMETER
 */
INT WINAPI WideCharToMultiByte( UINT page, DWORD flags, LPCWSTR src, INT srclen,
                                LPSTR dst, INT dstlen, LPCSTR defchar, BOOL *used )
{
    const union cptable *table;
    int ret, used_tmp;

    if (!src || (!dst && dstlen))
    {
        SetLastError( ERROR_INVALID_PARAMETER );
        return 0;
    }

    if (srclen == -1) srclen = strlenW(src) + 1;

    switch(page)
    {
    case CP_UTF7:
        FIXME("UTF-7 not supported\n");
        SetLastError( ERROR_CALL_NOT_IMPLEMENTED );
        return 0;
    case CP_UTF8:
        ret = utf8_wcstombs( src, srclen, dst, dstlen );
        break;
    default:
        if (!(table = get_codepage_table( page )))
        {
            SetLastError( ERROR_INVALID_PARAMETER );
            return 0;
        }
        ret = cp_wcstombs( table, flags, src, srclen, dst, dstlen,
                           defchar, used ? &used_tmp : NULL );
        if (used) *used = used_tmp;
        break;
    }

    if (ret == -1)
    {
        SetLastError( ERROR_INSUFFICIENT_BUFFER );
        ret = 0;
    }
    return ret;
}


/******************************************************************************
 *              GetStringTypeW   (KERNEL32.@)
 *
 */
BOOL WINAPI GetStringTypeW( DWORD type, LPCWSTR src, INT count, LPWORD chartype )
{
    if (count == -1) count = strlenW(src) + 1;
    switch(type)
    {
    case CT_CTYPE1:
        while (count--) *chartype++ = get_char_typeW( *src++ ) & 0xfff;
        break;
    case CT_CTYPE2:
        while (count--) *chartype++ = get_char_typeW( *src++ ) >> 12;
        break;
    case CT_CTYPE3:
    {
	WARN("CT_CTYPE3: semi-stub.\n");
	while (count--)
	{
        int c = *src;
	    WORD type1, type3 = 0; /* C3_NOTAPPLICABLE */

	    type1 = get_char_typeW( *src++ ) & 0xfff;
	    /* try to construct type3 from type1 */
	    if(type1 & C1_SPACE) type3 |= C3_SYMBOL;
	    if(type1 & C1_ALPHA) type3 |= C3_ALPHA;
        if ((c>=0x30A0)&&(c<=0x30FF)) type3 |= C3_KATAKANA;
        if ((c>=0x3040)&&(c<=0x309F)) type3 |= C3_HIRAGANA;
        if ((c>=0x4E00)&&(c<=0x9FAF)) type3 |= C3_IDEOGRAPH;
        if ((c>=0x0600)&&(c<=0x06FF)) type3 |= C3_KASHIDA;
        if ((c>=0x3000)&&(c<=0x303F)) type3 |= C3_SYMBOL;

        if ((c>=0xFF00)&&(c<=0xFF60)) type3 |= C3_FULLWIDTH;
        if ((c>=0xFF00)&&(c<=0xFF20)) type3 |= C3_SYMBOL;
        if ((c>=0xFF3B)&&(c<=0xFF40)) type3 |= C3_SYMBOL;
        if ((c>=0xFF5B)&&(c<=0xFF60)) type3 |= C3_SYMBOL;
        if ((c>=0xFF21)&&(c<=0xFF3A)) type3 |= C3_ALPHA;
        if ((c>=0xFF41)&&(c<=0xFF5A)) type3 |= C3_ALPHA;
        if ((c>=0xFFE0)&&(c<=0xFFE6)) type3 |= C3_FULLWIDTH;
        if ((c>=0xFFE0)&&(c<=0xFFE6)) type3 |= C3_SYMBOL;

        if ((c>=0xFF61)&&(c<=0xFFDC)) type3 |= C3_HALFWIDTH;
        if ((c>=0xFF61)&&(c<=0xFF64)) type3 |= C3_SYMBOL;
        if ((c>=0xFF65)&&(c<=0xFF9F)) type3 |= C3_KATAKANA;
        if ((c>=0xFF65)&&(c<=0xFF9F)) type3 |= C3_ALPHA;
        if ((c>=0xFFE8)&&(c<=0xFFEE)) type3 |= C3_HALFWIDTH;
        if ((c>=0xFFE8)&&(c<=0xFFEE)) type3 |= C3_SYMBOL;
	    *chartype++ = type3;
	}
	break;
    }
    default:
        SetLastError( ERROR_INVALID_PARAMETER );
        return FALSE;
    }
    return TRUE;
}


/******************************************************************************
 *              GetStringTypeExW   (KERNEL32.@)
 */
BOOL WINAPI GetStringTypeExW( LCID locale, DWORD type, LPCWSTR src, INT count, LPWORD chartype )
{
    /* locale is ignored for Unicode */
    return GetStringTypeW( type, src, count, chartype );
}

/******************************************************************************
 *		GetStringTypeA	[KERNEL32.@]
 */
BOOL WINAPI GetStringTypeA(LCID locale, DWORD type, LPCSTR src, INT count, LPWORD chartype)
{
    char buf[20];
    UINT cp;
    INT countW;
    LPWSTR srcW;
    BOOL ret = FALSE;

    if(count == -1) count = strlen(src) + 1;

    if(!GetLocaleInfoA(locale, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_NOUSEROVERRIDE,
		   buf, sizeof(buf)))
    {
	FIXME("For locale %04lx using current ANSI code page\n", locale);
	cp = GetACP();
    }
    else
	cp = atoi(buf);

    countW = MultiByteToWideChar(cp, 0, src, count, NULL, 0);
    if((srcW = HeapAlloc(GetProcessHeap(), 0, countW * sizeof(WCHAR))))
    {
	MultiByteToWideChar(cp, 0, src, count, srcW, countW);
    /* 
     * NOTE: the target buffer has 1 word for each CHARACTER in the source
     * string, with multibyte characters there maybe be more bytes in count 
     * than character space in the buffer!
     */
	ret = GetStringTypeW(type, srcW, countW, chartype);
	HeapFree(GetProcessHeap(), 0, srcW);
    }
    return ret;
}

/******************************************************************************
 *		GetStringTypeExA	[KERNEL32.@]
 */
BOOL WINAPI GetStringTypeExA(LCID locale, DWORD type, LPCSTR src, INT count, LPWORD chartype)
{
    return GetStringTypeA(locale, type, src, count, chartype);
}
