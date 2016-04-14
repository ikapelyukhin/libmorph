# include "wildscan.h"
# include "scandict.h"
# include "lemmatiz.h"
# include <string.h>

namespace LIBMORPH_NAMESPACE
{

  inline  bool      IsWildChar( byte08_t  c )
    {
      return c == '*' || c == '?';
    }
  inline  bool      IsWildMask( const byte08_t* s, size_t l )
    {
      while ( l-- > 0 ) 
        if ( IsWildChar( *s++ ) )  return true;
      return false;
    }
  inline  void      InsertChar( word32_t* p, byte08_t c )
    {
      p[c / (sizeof(*p) * CHAR_BIT)] |= (1 << (c % (sizeof(*p) * CHAR_BIT)));
    }

  struct  doFastCheck
  {
    int   InsertStem( lexeme_t, const steminfo&, const byte08_t*, const byte08_t*, const SGramInfo*, unsigned ) const
      {  return 1;  }
    bool  VerifyCaps( word16_t ) const
      {  return true;  }
  };
  
  static  void  WildFlex( word32_t* output, const byte08_t* thedic, const byte08_t* thestr, size_t cchstr,
    unsigned  wdinfo, unsigned mpower, const byte08_t* szpost )
  {
    SGramInfo   grbuff[0x20];
    gramBuffer  grlist( grbuff );
    byte08_t    bflags = *thedic++;
    int         ncount = bflags & 0x7f;
    byte08_t    chfind = *thestr;

    assert( IsWildMask( thestr, cchstr ) );

    while ( ncount-- > 0 )
    {
      byte08_t        chnext = *thedic++;
      unsigned        sublen = getserial( thedic );
      const byte08_t* subdic = thedic;
                      thedic = subdic + sublen;

    // check character find type: wildcard, pattern or regular
      switch ( chfind )
      {
        case '*': InsertChar( output, chnext );
                  continue;
        case '?': if ( ScanDict<byte08_t, int>( gramLoader( grlist, wdinfo, mpower ), subdic, thestr + 1, cchstr - 1 ) ) InsertChar( output, chnext );
                  continue;
        default:  if ( chnext == chfind ) WildFlex( output, subdic, thestr + 1, cchstr - 1, wdinfo, mpower, szpost );
                  continue;
      }
    }

    if ( (bflags & 0x80) != 0 && cchstr == 1 )
    {
      int   nforms = getserial( thedic );

      assert( IsWildChar( *thestr ) );

      while ( nforms-- > 0 && (*output & 0x01) == 0 )
      {
        word16_t  grInfo = getword16( thedic );
        byte08_t  bflags = *thedic++;
        int       desire = GetMixPower( wdinfo, grInfo, bflags );

        assert( desire >= 1 );
            
        if ( ( (wdinfo & wfMultiple) == 0 || (grInfo & gfMultiple) != 0 ) && (mpower & (1 << (desire - 1))) != 0 )
          InsertChar( output, '\0' );
      }
    }
  }

  static
  int   WildList( word32_t* output, const byte08_t* pstems, const byte08_t* thestr, size_t  cchstr )
  {
    SGramInfo fxlist[0x40];     // ������ �������������� �� ����������
    unsigned  ucount = getserial( pstems );

    assert( cchstr > 0 && IsWildMask( thestr, cchstr ) );

    while ( ucount-- > 0 )
    {
      byte08_t        clower = *pstems++;
      byte08_t        cupper = *pstems++;
      lexeme_t        nlexid = getserial( pstems );
      word16_t        oclass = getword16( pstems );
      size_t          ccflex = cchstr;
      const byte08_t* szpost;
      steminfo        stinfo;

    // load stem info
      if ( (oclass & wfPostSt) != 0 ) pstems += 1 + *(szpost = pstems);
        else  szpost = NULL;

    // check if non-flective
      if ( (stinfo.Load( classmap + (oclass & 0x7fff) ).wdinfo & wfFlexes) == 0 || (stinfo.wdinfo & 0x3f) == 51 )
      {
        if ( *thestr == '*' || cchstr == 1 && *thestr == '?' )
          InsertChar( output, '\0' );
      }
        else
      if ( (stinfo.wdinfo & wfMixTab) == 0 )
      {
        WildFlex( output, flexTree + (stinfo.tfoffs << 4), thestr, cchstr,
          stinfo.wdinfo, (unsigned)-1, szpost );
      }
        else
      {
        const byte08_t* mixtab = stinfo.mtoffs + mxTables;  // ���������� �������
        int             mixcnt = *mixtab++;                 // ���������� �����������
        int             mindex;
        int             nforms;

        for ( mindex = nforms = 0; mindex < mixcnt; ++mindex, mixtab += 1 + (0x0f & *mixtab) )
        {
          const byte08_t* curmix = mixtab;
          unsigned        mixlen = 0x0f & *curmix;
          unsigned        powers = *curmix++ >> 4;
          const byte08_t* flextr = thestr;
          size_t          flexcc = cchstr;
          int             rescmp;

        // scan top match
          while ( flexcc > 0 && mixlen > 0 && (rescmp = *flextr - *curmix) == 0 )
            {  --flexcc;  --mixlen;  ++flextr;  ++curmix;  }

        // either wildcard found, or wildcard in the rest flexion
          assert( flexcc > 0 && IsWildMask( flextr, flexcc ) );

        // check full or partial match of template to mixstr;
        //  * on full match, call WildFlex( ... );
          if ( mixlen == 0 )    WildFlex( output, flexTree + (stinfo.tfoffs << 4), flextr, flexcc, stinfo.wdinfo, powers, szpost );
            else
          if ( *flextr == '*' ) InsertChar( output, *curmix );
            else
          if ( *flextr == '?' )
          {
            gramBuffer  grbuff( fxlist );
            byte08_t    chsave;

            if ( szpost != NULL )
              if ( flexcc <= *szpost || memcmp( flextr + flexcc - *szpost, szpost + 1, *szpost ) != 0 ) continue;
                else  flexcc -= *szpost;

            for ( chsave = *curmix++, ++flextr, --flexcc, --mixlen; flexcc > 0 && mixlen > 0 && *flextr == *curmix;
              --flexcc, --mixlen, ++flextr, ++curmix ) (void)NULL;

            if ( mixlen == 0 && ScanDict<byte08_t, int>( gramLoader( grbuff, stinfo.wdinfo, powers ), flexTree + (stinfo.tfoffs << 4), flextr, flexcc ) > 0 )
              InsertChar( output, chsave );
          }
            else
          if ( rescmp < 0 )
            break;
        }
      }
    }
    return 0;
  }

  static  int   WildScan( word32_t* output, const byte08_t* thedic, const byte08_t* thestr, size_t cchstr )
  {
    byte08_t  bflags = *thedic++;
    int       ncount = getlower( bflags );
    byte08_t  chfind;

    assert( IsWildMask( thestr, cchstr ) );

    switch ( chfind = *thestr )
    {
      case '*':
        while ( ncount-- > 0 )
        {
          InsertChar( output, *thedic++ );
            thedic += getserial( thedic );
        }
        break;
      case '?':
        while ( ncount-- > 0 )
        {
          byte08_t  chnext = *thedic++;
          unsigned  sublen = getserial( thedic );

          if ( ScanDict<byte08_t, int>( listLookup<const doFastCheck>( doFastCheck() ), thedic, thestr + 1, cchstr - 1 ) > 0 )
            InsertChar( output, chnext );
          thedic += sublen;
        }
        break;
      default:
        while ( ncount-- > 0 )
        {
          byte08_t  chnext = *thedic++;
          unsigned  sublen = getserial( thedic );

          if ( chfind == chnext )
            WildScan( output, thedic, thestr + 1, cchstr - 1 );
          thedic += sublen;
        }
    }
    return hasupper( bflags ) ? WildList( output, thedic, thestr, cchstr ) : 0;
  }

  size_t  WildScan( byte08_t* output, size_t  cchout, const byte08_t* ptempl, size_t cchstr )
  {
    byte08_t* outorg = output;
    byte08_t* outend = output + cchout;
    word32_t  chmask[256 / (sizeof(word32_t) * CHAR_BIT)];
    int       nindex;
    int       cindex;

    WildScan( (word32_t*)memset( chmask, 0, sizeof(chmask) ), stemtree, ptempl, cchstr );

    for ( nindex = 0; nindex < sizeof(chmask) / sizeof(chmask[0]); ++nindex )
      for ( cindex = 0; cindex < sizeof(word32_t) * CHAR_BIT; ++cindex )
        if ( (chmask[nindex] & (1 << cindex)) != 0 )
          if ( output < outend ) *output++ = (byte08_t)(nindex * sizeof(word32_t) * CHAR_BIT + cindex);
            else  return WORDBUFF_FAILED;

    return output - outorg;
  }

} // end namespace
