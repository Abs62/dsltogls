#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <string>

#include <QVector>

#include "folding.hh"
#include "utf8.hh"
#include "dsl.hh"
#include "langcoder.hh"

using std::string;

int main()
{
  int num;
  WCHAR dslName[ MAX_PATH ], glsName[ MAX_PATH ], abbrName[ MAX_PATH ];
  char uName[ MAX_PATH * 4 ], uAbbrName[ MAX_PATH * 4 ];

  LPWSTR *pstr = CommandLineToArgvW( GetCommandLineW(), &num );
  if( pstr && num > 2 )
  {
    wcscpy_s( dslName, MAX_PATH, pstr[ 1 ] );
    wcscpy_s( glsName, MAX_PATH, pstr[ 2 ] );
    if( wcscmp( dslName, glsName ) == 0 )
    {
      printf( "Names must be different\n" );
      return -1;
    }
  }
  else
  {
    printf( "Usage: DslToGls dsl_file gls_file\n" );
    return -1;
  }

  if( WideCharToMultiByte( CP_UTF8, 0, dslName, -1, uName, MAX_PATH * 4, 0, 0 ) == 0 )
  {
    printf( "Invalid dsl file name\n" );
    return -1;
  }

// Check for abbreviations file
  abbrName[ 0 ] = 0;
  uAbbrName[ 0 ] = 0;

  int n = wcslen( dslName );
  if( _wcsicmp( dslName + n - 4, L".dsl" ) == 0 )
  {
    wcsncpy( abbrName, dslName, n - 4 );
    abbrName[ n - 4 ] = 0;
  }
  else
  if( _wcsicmp( dslName + n - 7, L".dsl.dz" ) == 0 )
  {
    wcsncpy( abbrName, dslName, n - 7 );
    abbrName[ n - 7 ] = 0;
  }
  if( abbrName[ 0 ] )
  {
    wcscat_s( abbrName, MAX_PATH, L"_abrv.dsl" );
    WIN32_FIND_DATA FindFileData;
    HANDLE handle = FindFirstFile( abbrName, &FindFileData );
    if( handle == INVALID_HANDLE_VALUE )
    {
      wcscat_s( abbrName, MAX_PATH, L".dz" );
      handle = FindFirstFile( abbrName, &FindFileData );
    }
    if( handle != INVALID_HANDLE_VALUE )
    {
      FindClose( handle );
      WideCharToMultiByte( CP_UTF8, 0, abbrName, -1, uAbbrName, MAX_PATH * 4, 0, 0 );
    }
  }

  DslDictionary dict;

  FILE * outFile = _wfopen( glsName, L"wt" );
  if( outFile == 0 )
  {
    printf( "\nCan't open output file\n" );
    return -1;
  }

  try
  {
    int n = dict.setFiles( string( uName ), uAbbrName );
    if( n )
    {
      fclose( outFile );
      return n;
    }

    fprintf( outFile, "### Glossary title:%s\n", Utf8::encode( dict.getParams().name ).c_str() );
    fprintf( outFile, "### Author:\n" );
    fprintf( outFile, "### Description:%s\n", dict.getDescription( uName ).toUtf8().data() );
    fprintf( outFile, "### Source language:%s\n", Utf8::encode( dict.getParams().langFrom ).c_str() );
    fprintf( outFile, "### Target language:%s\n", Utf8::encode( dict.getParams().langTo ).c_str() );
    fprintf( outFile, "### Glossary section:\n\n" );

    for( int i = 0; i < dict.getCards().size(); i++ )
    {
      DslCard const & card = dict.getCards()[ i ];

      string article;
      dict.getArticle( card, article );

      fprintf( outFile, "%s", Utf8::encode( card.headwords.at( 0 ) ).c_str() );

      for( int j = 1; j < card.headwords.size(); j++ )
        fprintf( outFile, "|%s", Utf8::encode( card.headwords.at( j ) ).c_str() );

      fprintf( outFile, "\n%s\n\n", article.c_str() );
    }

  }
  catch( std::exception & e )
  {
    fclose( outFile );
    printf( "\nConversion failed, error: %s\n", e.what() );
    return -1;
  }
  catch( ... )
  {
    fclose( outFile );
    printf( "\nConversion failed\n" );
    return -1;
  }

  fclose( outFile );
  return 0;
}
