#include <QUrl>

#include "dsl.hh"
#include "dsl_details.hh"
#include "folding.hh"
#include "langcoder.hh"
#include "htmlescape.hh"
#include "utf8.hh"
#include "filetype.hh"
#include "audiolink.hh"
#include "qt4x5.hh"
#include "wstring_qt.hh"
#include "gddebug.hh"
#include "fsencoding.hh"
#include <wctype.h>

#include <QTextStream>
#include <QDir>
#include <QFileInfo>

bool isDslWs( wchar ch )
{
  switch( ch )
  {
    case ' ':
    case '\t':
      return true;
    default:
      return false;
  }
}

string DslDictionary::getId()
{
  return string( "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
}

DslDictionary::~DslDictionary()
{
  if( dz )
    dict_data_close( dz );
}

int DslDictionary::setFiles( string const & dsl_name, string const & abr_name )
{
  int atLine = 0;

  try
  {
    if( !abr_name.empty() )
    {
      DslScanner abrvScanner( abr_name );

      wstring curString;
      size_t curOffset;

      for( ; ; )
      {
        // Skip any whitespace
        if ( !abrvScanner.readNextLineWithoutComments( curString, curOffset ) )
          break;
        if ( curString.empty() || isDslWs( curString[ 0 ] ) )
          continue;

        list< wstring > keys;

        bool eof = false;

        // Insert the key and read more, or get to the definition
        for( ; ; )
        {
          processUnsortedParts( curString, true );

          if ( keys.size() )
            expandTildes( curString, keys.front() );

          expandOptionalParts( curString, &keys );

          if ( !abrvScanner.readNextLineWithoutComments( curString, curOffset ) || curString.empty() )
          {
            gdWarning( "Warning: premature end of file %s\n", abr_name.c_str() );
            eof = true;
            break;
          }

          if ( isDslWs( curString[ 0 ] ) )
            break;
        }

        if ( eof )
          break;

        curString.erase( 0, curString.find_first_not_of( GD_NATIVE_TO_WS( L" \t" ) ) );

        if ( keys.size() )
          expandTildes( curString, keys.front() );

        // If the string has any dsl markup, we strip it
        string value = Utf8::encode( ArticleDom( curString ).root.renderAsText() );

        for( list< wstring >::iterator i = keys.begin(); i != keys.end();
             ++i )
        {
          unescapeDsl( *i );
          normalizeHeadword( *i );

          abrv[ Utf8::encode( Folding::trimWhitespace( *i ) ) ] = value;
        }
      }
    }

    DslScanner scanner( dsl_name );

    params.name = scanner.getDictionaryName();
    params.langFrom = scanner.getLangFrom();
    params.langTo = scanner.getLangTo();
    params.encoding = scanner.getEncoding();
    params.langFromRTL = LangCoder::isLanguageRTL( LangCoder::findIdForLanguage( params.langFrom ) );
    params.langToRTL = LangCoder::isLanguageRTL( LangCoder::findIdForLanguage( params.langTo ) );


    try
    {
      bool hasString = false;
      wstring curString;
      size_t curOffset;

      uint32_t articleCount = 0, wordCount = 0;

      for( ; ; )
      {
        // Find the main headword

        if ( !hasString && !scanner.readNextLineWithoutComments( curString, curOffset ) )
          break; // Clean end of file

        hasString = false;

        // The line read should either consist of pure whitespace, or be a
        // headword

        if ( curString.empty() )
          continue;

        if ( isDslWs( curString[ 0 ] ) )
        {
          // The first character is blank. Let's make sure that all other
          // characters are blank, too.
          for( size_t x = 1; x < curString.size(); ++x )
          {
            if ( !isDslWs( curString[ x ] ) )
            {
              printf( "Warning: garbage string at offset 0x%lX\n", (unsigned long) curOffset );
              break;
            }
          }
          continue;
        }

        // Ok, got the headword

        list< wstring > allEntryWords;

        processUnsortedParts( curString, true );
        expandOptionalParts( curString, &allEntryWords );

        unsigned articleOffset = curOffset;

        //DPRINTF( "Headword: %ls\n", curString.c_str() );

        // More headwords may follow

        for( ; ; )
        {
          if ( ! ( hasString = scanner.readNextLineWithoutComments( curString, curOffset ) ) )
          {
            printf( "Warning: premature end of file\n" );
            break;
          }

          // Lingvo skips empty strings between the headwords
          if ( curString.empty() )
            continue;

          if ( isDslWs( curString[ 0 ] ) )
            break; // No more headwords

          processUnsortedParts( curString, true );
          expandTildes( curString, allEntryWords.front() );
          expandOptionalParts( curString, &allEntryWords );
        }

        if ( !hasString )
          break;

        // Insert new entry

        DslCard newCard;
        newCard.offset = articleOffset;

        for( list< wstring >::iterator j = allEntryWords.begin();
             j != allEntryWords.end(); ++j )
        {
          unescapeDsl( *j );
          normalizeHeadword( *j );
          newCard.headwords.push_back( *j );
        }

        ++articleCount;
        wordCount += allEntryWords.size();
        allEntryWords.clear();

        int insideInsided = 0;
        wstring headword;
        QVector< InsidedCard > insidedCards;
        uint32_t offset = curOffset;
        QVector< wstring > insidedHeadwords;
        unsigned linesInsideCard = 0;
        int dogLine = 0;

        // Handle the article's body
        for( ; ; )
        {

          if ( ! ( hasString = scanner.readNextLineWithoutComments( curString, curOffset ) )
               || ( curString.size() && !isDslWs( curString[ 0 ] ) ) )
          {
            if( insideInsided )
            {
              gdWarning( "Unclosed tag '@' at line %i\n", dogLine );
              insidedCards.append( InsidedCard( offset, curOffset - offset, insidedHeadwords ) );
            }
            break;
          }

          // Find embedded cards

          wstring::size_type n = curString.find( L'@' );
          if( n == wstring::npos || curString[ n - 1 ] == L'\\' )
          {
            if( insideInsided )
              linesInsideCard++;

            continue;
          }
          else
          {
            // Embedded card tag must be placed at first position in line after spaces
            if( !isAtSignFirst( curString ) )
            {
              gdWarning( "Unescaped '@' symbol at line %i\n", scanner.getLinesRead() - 1 );

              if( insideInsided )
                linesInsideCard++;

              continue;
            }
          }

          dogLine = scanner.getLinesRead() - 1;

          // Handle embedded card

          if( insideInsided )
          {
            if( linesInsideCard )
            {
              insidedCards.append( InsidedCard( offset, curOffset - offset, insidedHeadwords ) );

              insidedHeadwords.clear();
              linesInsideCard = 0;
              offset = curOffset;
            }
          }
          else
          {
            offset = curOffset;
            linesInsideCard = 0;
          }

          headword = Folding::trimWhitespace( curString.substr( n + 1 ) );

          if( !headword.empty() )
          {
            processUnsortedParts( headword, true );
            expandTildes( headword, allEntryWords.front() );
            insidedHeadwords.append( headword );
            insideInsided = true;
          }
          else
            insideInsided = false;
        }

        // Now that we're having read the first string after the article
        // itself, we can use its offset to calculate the article's size.
        // An end of file works here, too.

        uint32_t articleSize = ( curOffset - articleOffset );
        newCard.size = articleSize;
        allCards.push_back( newCard );

        for( QVector< InsidedCard >::iterator i = insidedCards.begin(); i != insidedCards.end(); ++i )
        {
          newCard.headwords.clear();
          newCard.offset = (*i).offset;
          newCard.size = (*i).size;

          for( int x = 0; x < (*i).headwords.size(); x++ )
          {
            allEntryWords.clear();
            expandOptionalParts( (*i).headwords[ x ], &allEntryWords );

            for( list< wstring >::iterator j = allEntryWords.begin();
                 j != allEntryWords.end(); ++j )
            {
              unescapeDsl( *j );
              normalizeHeadword( *j );
              newCard.headwords.push_back( *j );
            }

            allCards.push_back( newCard );
            wordCount += allEntryWords.size();
          }
          ++articleCount;
        }

        if ( !hasString )
          break;
      }

      DZ_ERRORS error;
      dz = dict_data_open( dsl_name.c_str(), &error, 0 );
      if( !dz )
      {
        printf( "\nInput file opening error: %s\n", dz_error_str( error ) );
        return -1;
      }

      return 0;
    }
    catch( ... )
    {
      atLine = scanner.getLinesRead();
      throw;
    }
  }
  catch( std::exception & e )
  {
    printf( "\nDSL dictionary reading failed at %u line, error: %s\n",
            atLine, e.what() );
  }
  return -1;
}

void DslDictionary::loadArticle( DslCard const & card,
                                 wstring const & requestedHeadwordFolded,
                                 wstring & tildeValue,
                                 wstring & displayedHeadword,
                                 unsigned & headwordIndex,
                                 wstring & articleText )
{
  wstring articleData;

  if( !dz )
    return;

  {
    char * articleBody;

    articleBody = dict_data_read_( dz, card.offset, card.size, 0, 0 );

    if ( !articleBody )
    {
      printf( "\nDICTZIP error: %s\n", dict_error_str( dz ) );
      articleText.clear();
      return;
    }
    else
    {
      try
      {
        articleData =
          DslIconv::toWstring(
            DslIconv::getEncodingNameFor( params.encoding ),
            articleBody, card.size );
        free( articleBody );

        // Strip DSL comments
        bool b = false;
        stripComments( articleData, b );
      }
      catch( ... )
      {
        free( articleBody );
        throw;
      }
    }
  }

  size_t pos = 0;
  bool hadFirstHeadword = false;
  bool foundDisplayedHeadword = false;

  // Check is we retrieve insided card
  bool insidedCard = isDslWs( articleData.at( 0 ) );

  wstring tildeValueWithUnsorted; // This one has unsorted parts left
  for( headwordIndex = 0; ; )
  {
    size_t begin = pos;

    pos = articleData.find_first_of( GD_NATIVE_TO_WS( L"\n\r" ), begin );

    if ( pos == wstring::npos )
      pos = articleData.size();

    if ( !foundDisplayedHeadword )
    {
      // Process the headword

      wstring rawHeadword = wstring( articleData, begin, pos - begin );

      if( insidedCard && !rawHeadword.empty() && isDslWs( rawHeadword[ 0 ] ) )
      {
        // Headword of the insided card
        wstring::size_type hpos = rawHeadword.find( L'@' );
        if( hpos != string::npos )
        {
          wstring head = Folding::trimWhitespace( rawHeadword.substr( hpos + 1 ) );
          hpos = head.find( L'~' );
          while( hpos != string::npos )
          {
            if( hpos == 0 || head[ hpos ] != L'\\' )
              break;
            hpos = head.find( L'~', hpos + 1 );
          }
          if( hpos == string::npos )
            rawHeadword = head;
          else
            rawHeadword.clear();
        }
      }

      if( !rawHeadword.empty() )
      {
        if ( !hadFirstHeadword )
        {
          // We need our tilde expansion value
          tildeValue = rawHeadword;

          list< wstring > lst;

          expandOptionalParts( tildeValue, &lst );

          if ( lst.size() ) // Should always be
            tildeValue = lst.front();

          tildeValueWithUnsorted = tildeValue;

          processUnsortedParts( tildeValue, false );
        }
        wstring str = rawHeadword;

        if ( hadFirstHeadword )
          expandTildes( str, tildeValueWithUnsorted );

        processUnsortedParts( str, true );

        str = Folding::applySimpleCaseOnly( str );

        list< wstring > lst;
        expandOptionalParts( str, &lst );

        // Does one of the results match the requested word? If so, we'd choose
        // it as our headword.

        for( list< wstring >::iterator i = lst.begin(); i != lst.end(); ++i )
        {
          unescapeDsl( *i );
          normalizeHeadword( *i );

          if ( Folding::trimWhitespace( *i ) == requestedHeadwordFolded )
          {
            // Found it. Now we should make a displayed headword for it.
            if ( hadFirstHeadword )
              expandTildes( rawHeadword, tildeValueWithUnsorted );

            processUnsortedParts( rawHeadword, false );

            displayedHeadword = rawHeadword;

            foundDisplayedHeadword = true;
            break;
          }
        }

        if ( !foundDisplayedHeadword )
        {
          ++headwordIndex;
          hadFirstHeadword = true;
        }
      }
    }


    if ( pos == articleData.size() )
      break;

    // Skip \n\r

    if ( articleData[ pos ] == '\r' )
      ++pos;

    if ( pos != articleData.size() )
    {
      if ( articleData[ pos ] == '\n' )
        ++pos;
    }

    if ( pos == articleData.size() )
    {
      // Ok, it's end of article
      break;
    }
    if( isDslWs( articleData[ pos ] ) )
    {
     // Check for begin article text
      if( insidedCard )
      {
        // Check for next insided headword
        wstring::size_type hpos = articleData.find_first_of( GD_NATIVE_TO_WS( L"\n\r" ), pos );
        if ( hpos == wstring::npos )
          hpos = articleData.size();

        wstring str = wstring( articleData, pos, hpos - pos );

        hpos = str.find( L'@');
        if( hpos == wstring::npos || str[ hpos - 1 ] == L'\\' || !isAtSignFirst( str ) )
          break;
      }
      else
        break;
    }
  }

  if ( !foundDisplayedHeadword )
  {
    // This is strange. Anyway, use tilde expansion value, it's better
    // than nothing (or requestedHeadwordFolded for insided card.
    if( insidedCard )
      displayedHeadword = requestedHeadwordFolded;
    else
      displayedHeadword = tildeValue;
  }

  if ( pos != articleData.size() )
    articleText = wstring( articleData, pos );
  else
    articleText.clear();
}

string DslDictionary::nodeToHtml( ArticleDom::Node const & node )
{
  string result;

  if ( !node.isTag )
  {
    result = Html::escape( Utf8::encode( node.text ) );

    // Handle all end-of-line

    string::size_type n;

    // Strip all '\r'
    while( ( n = result.find( '\r' ) ) != string::npos )
      result.erase( n, 1 );

    // Replace all '\n'
    while( ( n = result.find( '\n' ) ) != string::npos )
      result.replace( n, 1, "<p></p>" );

    return result;
  }

  if ( node.tagName == GD_NATIVE_TO_WS( L"b" ) )
    result += "<b class=\"dsl_b\">" + processNodeChildren( node ) + "</b>";
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"i" ) )
    result += "<i class=\"dsl_i\">" + processNodeChildren( node ) + "</i>";
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"u" ) )
  {
    string nodeText = processNodeChildren( node );

    if ( nodeText.size() && isDslWs( nodeText[ 0 ] ) )
      result.push_back( ' ' ); // Fix a common problem where in "foo[i] bar[/i]"
                               // the space before "bar" gets underlined.

    result += "<span class=\"dsl_u\">" + nodeText + "</span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"c" ) )
  {
    result += "<font color=\"" + ( node.tagAttrs.size() ?
      Html::escape( Utf8::encode( node.tagAttrs ) ) : string( "c_default_color" ) )
      + "\">" + processNodeChildren( node ) + "</font>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"*" ) )
  {
      string id = string( "O" ) + "xxxxxxx" + "_" + "0" +
                "_opt_" + QString::number( optionalPartNom++ ).toStdString();
    result += "<span class=\"dsl_opt\" id=\"" + id + "\">" + processNodeChildren( node ) + "</span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"m" ) )
      result += "<div class=\"dsl_m\">" + processNodeChildren( node ) + "</div>";
  else
  if ( node.tagName.size() == 2 && node.tagName[ 0 ] == L'm' &&
       iswdigit( node.tagName[ 1 ] ) )
    result += "<div class=\"dsl_" + Utf8::encode( node.tagName ) + "\">" + processNodeChildren( node ) + "</div>";
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"trn" ) )
    result += "<span class=\"dsl_trn\">" + processNodeChildren( node ) + "</span>";
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"ex" ) )
    result += "<span class=\"dsl_ex\">" + processNodeChildren( node ) + "</span>";
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"com" ) )
    result += "<span class=\"dsl_com\">" + processNodeChildren( node ) + "</span>";
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"s" ) || node.tagName == GD_NATIVE_TO_WS( L"video" ) )
  {
    string filename = Utf8::encode( node.renderAsText() );

    if ( Filetype::isNameOfSound( filename ) )
    {
      // If we have the file here, do the exact reference to this dictionary.
      // Otherwise, make a global 'search' one.

      QUrl url;
      url.setScheme( "gdau" );
      url.setHost( "search" );
      url.setPath( Qt4x5::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      string ref = string( "\"" ) + url.toEncoded().data() + "\"";

      result += addAudioLink( ref, getId() );

      result += "<span class=\"dsl_s_wav\"><a href=" + ref
         + "><img src=\"qrcx://localhost/icons/playsound.png\" border=\"0\" align=\"absmiddle\" alt=\"Play\"/></a></span>";
    }
    else
    if ( Filetype::isNameOfPicture( filename ) )
    {
      QUrl url;
      url.setScheme( "bres" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Qt4x5::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      result += string( "<img src=\"" ) + url.toEncoded().data()
                + "\" alt=\"" + Html::escape( filename ) + "\"/>";
    }
    else
    if ( Filetype::isNameOfVideo( filename ) ) {
      QUrl url;
      url.setScheme( "gdvideo" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Qt4x5::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      result += string( "<a class=\"dsl_s dsl_video\" href=\"" ) + url.toEncoded().data() + "\">"
             + "<span class=\"img\"></span>"
             + "<span class=\"filename\">" + processNodeChildren( node ) + "</span>" + "</a>";
    }
    else
    {
      // Unknown file type, downgrade to a hyperlink

      QUrl url;
      url.setScheme( "bres" );
      url.setHost( QString::fromUtf8( getId().c_str() ) );
      url.setPath( Qt4x5::Url::ensureLeadingSlash( QString::fromUtf8( filename.c_str() ) ) );

      result += string( "<a class=\"dsl_s\" href=\"" ) + url.toEncoded().data()
             + "\">" + processNodeChildren( node ) + "</a>";
    }
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"url" ) )
  {
    string link = Html::escape( Utf8::encode( node.renderAsText() ) );
    if( QUrl::fromEncoded( link.c_str() ).scheme().isEmpty() )
      link = "http://" + link;

/*
    QUrl url( QString::fromUtf8( link.c_str() ) );
    if( url.isLocalFile() && url.host().isEmpty() )
    {
      // Convert relative links to local files to absolute ones
      QString name = QFileInfo( getMainFilename() ).absolutePath();
      name += url.toLocalFile();
      QFileInfo info( name );
      if( info.isFile() )
      {
        name = info.canonicalFilePath();
        url.setPath( Qt4x5::Url::ensureLeadingSlash( QUrl::fromLocalFile( name ).path() ) );
        link = string( url.toEncoded().data() );
      }
    }
*/
    result += "<a class=\"dsl_url\" href=\"" + link +"\">" + processNodeChildren( node ) + "</a>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"!trs" ) )
  {
    result += "<span class=\"dsl_trs\">" + processNodeChildren( node ) + "</span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"p") )
  {
    result += "<span class=\"dsl_p\"";

    string val = Utf8::encode( node.renderAsText() );

    // If we have such a key, display a title
    map< string, string >::const_iterator i = abrv.find( val );

    if ( i != abrv.end() )
    {
      string title;

      if ( Utf8::decode( i->second ).size() < 70 )
      {
        // Replace all spaces with non-breakable ones, since that's how
        // Lingvo shows tooltips
        title.reserve( i->second.size() );

        for( char const * c = i->second.c_str(); *c; ++c )
        {
          if ( *c == ' ' || *c == '\t' )
          {
            // u00A0 in utf8
            title.push_back( 0xC2 );
            title.push_back( 0xA0 );
          }
          else
          if( *c == '-' ) // Change minus to non-breaking hyphen (uE28091 in utf8)
          {
            title.push_back( 0xE2 );
            title.push_back( 0x80 );
            title.push_back( 0x91 );
          }
          else
            title.push_back( *c );
        }
      }
      else
        title = i->second;
      result += " title=\"" + Html::escape( title ) + "\"";
    }

    result += ">" + processNodeChildren( node ) + "</span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"'" ) )
  {
    // There are two ways to display the stress: by adding an accent sign or via font styles.
    // We generate two spans, one with accented data and another one without it, so the
    // user could pick up the best suitable option.
    string data = processNodeChildren( node );
    result += "<span class=\"dsl_stress\"><span class=\"dsl_stress_without_accent\">" + data + "</span>"
        + "<span class=\"dsl_stress_with_accent\">" + data + Utf8::encode( wstring( 1, 0x301 ) )
        + "</span></span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"lang" ) )
  {
    result += "<span class=\"dsl_lang\"";
    if( !node.tagAttrs.empty() )
    {
      // Find ISO 639-1 code
      string langcode;
      QString attr = gd::toQString( node.tagAttrs );
      int n = attr.indexOf( "id=" );
      if( n >= 0 )
      {
        int id = attr.mid( n + 3 ).toInt();
        if( id )
          langcode = findCodeForDslId( id );
      }
      else
      {
        n = attr.indexOf( "name=\"" );
        if( n >= 0 )
        {
          int n2 = attr.indexOf( '\"', n + 6 );
          if( n2 > 0 )
          {
            quint32 id = dslLanguageToId( gd::toWString( attr.mid( n + 6, n2 - n - 6 ) ) );
            langcode = LangCoder::intToCode2( id ).toStdString();
          }
        }
      }
      if( !langcode.empty() )
        result += " lang=\"" + langcode + "\"";
    }
    result += ">" + processNodeChildren( node ) + "</span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"ref" ) )
  {
    QUrl url;

    url.setScheme( "gdlookup" );
    url.setHost( "localhost" );
    url.setPath( Qt4x5::Url::ensureLeadingSlash( gd::toQString( node.renderAsText() ) ) );
    if( !node.tagAttrs.empty() )
    {
      QString attr = gd::toQString( node.tagAttrs ).remove( '\"' );
      int n = attr.indexOf( '=' );
      if( n > 0 )
      {
        QList< QPair< QString, QString > > query;
        query.append( QPair< QString, QString >( attr.left( n ), attr.mid( n + 1 ) ) );
        Qt4x5::Url::setQueryItems( url, query );
      }
    }

    result += string( "<a class=\"dsl_ref\" href=\"" ) + url.toEncoded().data() +"\">"
              + processNodeChildren( node ) + "</a>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"@" ) )
  {
    // Special case - insided card header was not parsed

    QUrl url;

    url.setScheme( "gdlookup" );
    url.setHost( "localhost" );
    wstring nodeStr = node.renderAsText();
    normalizeHeadword( nodeStr );
    url.setPath( Qt4x5::Url::ensureLeadingSlash( gd::toQString( nodeStr ) ) );

    result += string( "<a class=\"dsl_ref\" href=\"" ) + url.toEncoded().data() +"\">"
              + processNodeChildren( node ) + "</a>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"sub" ) )
  {
    result += "<sub>" + processNodeChildren( node ) + "</sub>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"sup" ) )
  {
    result += "<sup>" + processNodeChildren( node ) + "</sup>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"t" ) )
  {
    result += "<span class=\"dsl_t\">" + processNodeChildren( node ) + "</span>";
  }
  else
  if ( node.tagName == GD_NATIVE_TO_WS( L"br" ) )
  {
    result += "<br />";
  }
  else
    result += "<span class=\"dsl_unknown\">" + processNodeChildren( node ) + "</span>";

  return result;
}

string DslDictionary::processNodeChildren(ArticleDom::Node const & node )
{
  string result;

  for( ArticleDom::Node::const_iterator i = node.begin(); i != node.end();
       ++i )
    result += nodeToHtml( *i );

  return result;
}

string DslDictionary::dslToHtml( wstring const & str, wstring const & headword )
{
 // Normalize the string
  wstring normalizedStr = gd::normalize( str );

  ArticleDom dom( normalizedStr, string(), headword );

  string html = processNodeChildren( dom.root );

  return html;
}

int DslDictionary::getArticle( DslCard const & card, string & article )
{
  wstring word = card.headwords.at( 0 );
  wstring wordCaseFolded = Folding::applySimpleCaseOnly( word );

  // Grab that article

  wstring tildeValue;
  wstring displayedHeadword;
  wstring articleBody;
  unsigned headwordIndex;

  string articleText, articleAfter;

  try
  {
    loadArticle( card, wordCaseFolded, tildeValue,
                 displayedHeadword, headwordIndex, articleBody );

    if( displayedHeadword.empty() || isDslWs( displayedHeadword[ 0 ] ) )
      displayedHeadword = word; // Special case - insided card

    articleText += "<div class=\"dsl_article\">";

    articleText += "<div class=\"dsl_headwords\"";
    if( params.langFromRTL )
      articleText += " dir=\"rtl\"";
    articleText += "><p>";

    if( displayedHeadword.size() == 1 && displayedHeadword[0] == '<' )  // Fix special case - "<" header
        articleText += "<";                                             // dslToHtml can't handle it correctly.
    else
      articleText += dslToHtml( displayedHeadword, displayedHeadword );

    /// After this may be expand button will be inserted

    articleAfter += "</p></div>";

    expandTildes( articleBody, tildeValue );

    articleAfter += "<div class=\"dsl_definition\"";
    if( params.langToRTL )
      articleAfter += " dir=\"rtl\"";
    articleAfter += ">";

    int optionalPartNom = 0;

    articleAfter += dslToHtml( articleBody, displayedHeadword );
    articleAfter += "</div>";
    articleAfter += "</div>";

    if( optionalPartNom )
    {
      string prefix = "O" + getId().substr( 0, 7 ) + "_" + "0";
      string id1 = prefix + "_expand";
      string id2 = prefix + "_opt_";
      string button = " <img src=\"qrcx://localhost/icons/expand_opt.png\" class=\"hidden_expand_opt\" id=\"" + id1 +
                      "\" onclick=\"gdExpandOptPart('" + id1 + "','" + id2 +"')\" alt=\"[+]\"/>";
      if( articleText.compare( articleText.size() - 4, 4, "</p>" ) == 0 )
        articleText.insert( articleText.size() - 4, " " + button );
      else
        articleText += button;
    }

    articleText += articleAfter;
  }
  catch( std::exception &ex )
  {
    printf( "DSL: Failed loading article, reason: %s\n", ex.what() );
    articleText = string( "<span class=\"dsl_article\">" )
                  + string( "Article loading error" )
                  + "</span>";
  }

  article = articleText;
  return 0;
}

QString DslDictionary::getDescription( string const & dsl_name )
{
    QString dictionaryDescription;

    QString fileName =
      QDir::fromNativeSeparators( FsEncoding::decode( dsl_name.c_str() ) );

    // Remove the extension
    if ( fileName.endsWith( ".dsl.dz", Qt::CaseInsensitive ) )
      fileName.chop( 6 );
    else
      fileName.chop( 3 );

    fileName += "ann";
    QFileInfo info( fileName );

    if ( info.exists() )
    {
        QFile annFile( fileName );
        if( !annFile.open( QFile::ReadOnly | QFile::Text ) )
            return dictionaryDescription;

        QTextStream annStream( &annFile );
        dictionaryDescription = annStream.readAll();

        int pos = 0;
        int n_langs = 0;

        QRegExp regLang( "#LANGUAGE \"(\\w+)\"" );

        for( ; ; )
        {
          pos = regLang.indexIn( dictionaryDescription, pos );
          if( pos < 0 )
            break;

          QString newStr;

          if( n_langs > 0 )
            newStr += "<br><br><br><br>";
          newStr += regLang.cap( 1 ) + ":";

          dictionaryDescription.replace( pos, regLang.cap( 0 ).size(), newStr );
          pos += newStr.isEmpty() ? 1 : newStr.size();
          n_langs += 1;
        }

        dictionaryDescription.replace( '\n', "<br>" );
        dictionaryDescription.remove( '\r' );
    }

    return dictionaryDescription;
}
