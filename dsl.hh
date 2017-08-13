#ifndef __DSL_HH_INCLUDED__
#define __DSL_HH_INCLUDED__

#include <stdint.h>
#include <string>
#include <map>
#include <QVector>

#include "wstring.hh"
#include "dsl_details.hh"
#include "dictzip.h"

using gd::wstring;
using std::string;
using std::map;

using namespace Dsl::Details;

struct InsidedCard
{
  uint32_t offset;
  uint32_t size;
  QVector< wstring > headwords;
  InsidedCard( uint32_t _offset, uint32_t _size, QVector< wstring > const & words ) :
  offset( _offset ), size( _size ), headwords( words )
  {}
  InsidedCard( InsidedCard const & e ) :
  offset( e.offset ), size( e.size ), headwords( e.headwords )
  {}
  InsidedCard() {}

};

struct DslCard
{
  uint32_t offset;
  uint32_t size;
  QVector< wstring > headwords;

  DslCard( uint32_t _offset, uint32_t _size, QVector< wstring > const & words ) :
  offset( _offset ), size( _size ), headwords( words )
  {}
  DslCard( DslCard const & e ) :
  offset( e.offset ), size( e.size ), headwords( e.headwords )
  {}
  DslCard() {}
};

struct DictParameters
{
  wstring name;
  wstring langFrom;
  wstring langTo;
  DslEncoding encoding;
  bool langFromRTL;
  bool langToRTL;
};

class DslDictionary
{
  int optionalPartNom;
  QVector< DslCard > allCards;
  DictParameters params;
  map< string, string > abrv;
  dictData * dz;

public:

  DslDictionary() :
    optionalPartNom( 0 ),
    dz( 0 )
  {};

  ~DslDictionary();

  string getId();

  QVector< DslCard > const & getCards() const
  { return allCards; }

  DictParameters const & getParams() const
  { return params; }

  int setFiles( string const & dsl_name, string const & abr_name );

  int getArticle( DslCard const & card, string & article );

  QString getDescription( string const & dsl_name );

protected:

  void loadArticle( DslCard const & card,
                    wstring const & requestedHeadwordFolded,
                    wstring & tildeValue,
                    wstring & displayedHeadword,
                    unsigned & headwordIndex,
                    wstring & articleText );

  string dslToHtml( wstring const & str, wstring const & headword );

  string processNodeChildren( ArticleDom::Node const & node );

  string nodeToHtml( ArticleDom::Node const & node );
};



#endif // __DSL_HH_INCLUDED__
