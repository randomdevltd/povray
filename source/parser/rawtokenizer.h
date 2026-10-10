//******************************************************************************
///
/// @file parser/rawtokenizer.h
///
/// Declarations for the _raw tokenizer_ stage of the parser.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

#ifndef POVRAY_PARSER_RAWTOKENIZER_H
#define POVRAY_PARSER_RAWTOKENIZER_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "parser/configparser.h"

// C++ variants of C standard header files
//  (none at the moment)

// C++ standard header files
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// POV-Ray header files (base module)
#include "base/filesystem.h"
#include "base/stringtypes.h"

// POV-Ray header files (core module)
//  (none at the moment)

// POV-Ray header files (parser module)
#include "parser/parsertypes.h"
#include "parser/scanner.h"

namespace pov_parser
{

using namespace pov_base;

//------------------------------------------------------------------------------

/// Abstract structure representing an arbitrary literal or variable value.
struct Value
{
    virtual ~Value() {}
protected:
    Value() {}
};

using ValuePtr = std::shared_ptr<Value>;
using ConstValuePtr = std::shared_ptr<const Value>;

/// Structure representing a string value.
struct StringValue : Value
{
    UCS2String data;
    virtual const UCS2String& GetData() const { return data; }      ///< Get value in generic context.
    virtual const UCS2String& GetFileName() const { return data; }  ///< Get value in file name context.
    virtual bool IsAmbiguous() const { return false; }              ///< Whether value depends on context.
    virtual void Append(UCS2 codeUnit) { data += codeUnit; }        ///< Append an unambiguous character.
};

/// Structure representing a string value with backslashes.
struct AmbiguousStringValue final : StringValue
{
    struct InvalidEscapeSequenceInfo final
    {
        UCS2String fileName;
        LexemePosition position;
        UTF8String text;
        InvalidEscapeSequenceInfo(const UCS2String& s, LexemePosition p, UTF8String t) :
            fileName(s), position(p), text(t) {}
        InvalidEscapeSequenceInfo(const UCS2String& s, LexemePosition p,
                                  const UTF8String::const_iterator& b,
                                  const UTF8String::const_iterator& e) :
            fileName(s), position(p), text(b, e) {}
        void Throw() const;
    };

    UCS2String data;
    UCS2String fileName;
    InvalidEscapeSequenceInfo* invalidEscapeSequence;
    AmbiguousStringValue(const StringValue& o) : data(o.GetData()), fileName(o.GetFileName()), invalidEscapeSequence(nullptr) {}
    AmbiguousStringValue(const AmbiguousStringValue& o) : data(o.data), fileName(o.fileName), invalidEscapeSequence(o.invalidEscapeSequence) {}
    virtual ~AmbiguousStringValue() override { if (invalidEscapeSequence != nullptr) delete invalidEscapeSequence; }
    virtual const UCS2String& GetData() const override { if (invalidEscapeSequence != nullptr) invalidEscapeSequence->Throw(); return data; }
    virtual const UCS2String& GetFileName() const override { return fileName; }
    virtual bool IsAmbiguous() const override { return true; }
    virtual void Append(UCS2 codeUnit) override { data += codeUnit; fileName += codeUnit; }
};

//------------------------------------------------------------------------------

/// Structure representing an individual raw token.
struct RawToken final
{
    /// The original lexeme from which this raw token was created.
    Lexeme lexeme;

    /// A numeric ID representing the token.
    /// For reserved words, operators etc. this is the corresponding value from
    /// @ref TokenId. For literals, this is @ref FLOAT_TOKEN or
    /// @ref STRING_LITERAL_TOKEN, respectively. For identifiers, this is
    /// a numeric value larger than @ref TOKEN_COUNT uniquely identifying the
    /// identifier word.
    int id;

    /// A numeric ID representing the "expression type" of the token.
    /// For reserved word tokens (but not operators) that may occur at the start of a numeric expression, this is
    /// @ref FLOAT_TOKEN_CATEGORY. For reserved word tokens (but not operators) that may occur at the start of a
    /// vector expression, this is @ref VECTOR_TOKEN_CATEGORY. For reserved word tokens that may
    /// occur at the start of a colour expression, this is
    /// @ref COLOUR_TOKEN_CATEGORY. For identifiers, this is @ref IDENTIFIER_TOKEN.
    /// For file signature tokens, this is @ref SIGNATURE_TOKEN_CATEGORY.
    /// For other tokens, this is the corresponding value from @ref TokenId.
    TokenId expressionId;

    /// Associated numeric value.
    /// For float literal tokens this is the parsed value of the token. For
    /// other tokens this is undefined.
    /// @note
    ///     Since numeric values are used so frequently, we reserve a place for
    ///     their values here, rather than using the @ref value field.
    DBL floatValue;

    /// Symbol table hash of a word token's text.
    int symbolHash;

    /// Associated non-numeric value.
    /// For string literal tokens, this value is set by the _raw tokenizer_ to
    /// hold the parsed string. For identifiers, this value may be set by the
    /// _cooked tokenizer_ to carry the value of the identifier.
    ConstValuePtr value;

    /// Whether the token is a reserved word.
    /// @note
    ///     This flag is _not_ set for operators.
    bool isReservedWord : 1;

    /// Whether the token is a keyword with identifier-like properties.
    bool isPseudoIdentifier : 1;

    /// Helper function to get the token ID.
    /// For identifiers, this function returns @ref IDENTIFIER_TOKEN. For all
    /// other tokens, this function returns @ref id as a @ref TokenId value.
    TokenId GetTokenId() const;
};

//******************************************************************************

/// A raw token as kept in a file's token array.
struct CachedToken final
{
    POV_OFF_T           offset;
    std::uint32_t       line;
    std::uint32_t       column;
    int                 id;
    TokenId             expressionId;
    std::uint32_t       text;               ///< Index of the text in the file's `texts`.
    Lexeme::Category    category;
    bool                isReservedWord      : 1;
    bool                isPseudoIdentifier  : 1;
    union                                   ///< One payload per category keeps 40 bytes.
    {
        DBL             floatValue;
        std::size_t     value;              ///< A string literal's index in `values`.
        int             symbolHash;
    };
};

static_assert(sizeof(CachedToken) == 40, "a cached token should stay at 40 bytes");

/// A source file lexed once: its first tokens, and the scanner state after the last of them.
struct CacheBudget final
{
    std::size_t tokens = 0;
    std::size_t bytes = 0;
};

struct CachedFile final
{
    std::shared_ptr<CacheBudget> budget;    ///< Refunded when the tokens are freed.
    StreamPtr                   stream;     ///< The file, or once complete a named empty stream.
    std::vector<CachedToken>    tokens;
    std::vector<ConstValuePtr>  values;
    std::vector<UTF8String>     texts;
    std::unordered_map<std::size_t, LexemePosition> ends; ///< Ends not at start plus text length.
    Scanner::HotBookmark        end;
    POV_OFF_T                   size;       ///< File size when lexed, or -1.
    std::int_least64_t          time;       ///< Modification time when lexed.
    std::uint64_t               lastUse;
    std::size_t                 bytes;      ///< Its share of the cache's byte budget.
    bool                        complete;   ///< Whether `tokens` reach the end of the file.
    ~CachedFile() { if (budget) { budget->tokens -= tokens.size(); budget->bytes -= bytes; } }
};

using CachedFilePtr = std::shared_ptr<CachedFile>;

//******************************************************************************

/// Class implementing the parser's _raw tokenizer_ stage.
///
/// The parser's _raw tokenizer_ stage processes individual _lexemes_ from the
/// _scanner_ stage into so-called _raw tokens_, by earmarking each lexeme
/// with a numeric ID:
///   - Word-type (candidate keyword/identifier) lexemes matching a reserved
///     word are earmarked with an ID uniquely identifying that reserved word.
///   - Word-type lexemes _not_ matching any reserved word are earmarked with
///     an ID larger than @ref TOKEN_COUNT uniquely identifying that
///     non-reserved word for later reference. A mapping function exists that
///     maps such IDs to @ref IDENTIFIER_TOKEN.
///   - Numeric literal lexemes are earmarked with @ref FLOAT_TOKEN.
///   - String literal lexemes are earmarked with @ref STRING_LITERAL_TOKEN.
///   - Other lexemes found in the list of reserved words are earmarked with an
///     ID uniquely identifying that reserved word.
///
/// In addition, literal lexemes are evaluated, converting their textual
/// representation into the corresponding internal value representation.
///
class RawTokenizer final
{
public:

    /// Where in a file's token array (or past its cached part, in the file) a bookmark points.
    struct CachePosition
    {
        CachedFilePtr   file;
        std::size_t     index       = 0;
        unsigned int    instance    = 0;
        bool            cached      = false;
    };

    struct HotBookmark final : Scanner::HotBookmark, CachePosition
    {
        HotBookmark() = default;
        HotBookmark(const Scanner::HotBookmark& b, const CachePosition& p) :
            Scanner::HotBookmark(b), CachePosition(p) {}
    };

    struct ColdBookmark final : Scanner::ColdBookmark, CachePosition
    {
        ColdBookmark() = default;
        ColdBookmark(const Scanner::ColdBookmark& b, const CachePosition& p) :
            Scanner::ColdBookmark(b), CachePosition(p) {}
    };

    RawTokenizer();

    /// Set or change the input stream.
    /// @note
    ///     The input stream must already be opened.
    void SetInputStream(StreamPtr pStream);

    /// Set or change the input stream to the file at `path`, through its token array if caching.
    void SetInputStream(StreamPtr pStream, const UCS2String& path);

    /// Keep each file's tokens for re-reading.
    void EnableCache() { mBudget = std::make_shared<CacheBudget>(); }

    /// Drop the token array of the file at `path`, as it is being written.
    void ForgetFile(const UCS2String& canonicalPath);

    /// Where the next token comes from.
    const CachePosition& GetCachePosition() const { return mPosition; }

    /// Whether `bookmark` was taken in the current opening of the current file.
    bool IsCurrentInstance(const HotBookmark& bookmark) const
    {
        return bookmark.instance == mPosition.instance;
    }

    /// Report the current stream's line `i` as `lines[i - 1]`.
    void SetLineMap(ConstStreamPtr stream, std::vector<POV_LONG> lines) { mScanner.SetLineMap(std::move(stream), std::move(lines)); }

    /// Change encoding setting.
    void SetStringEncoding(CharacterEncodingID encoding);

    /// Change the behaviour with regards to nested block comments.
    void SetNestedBlockComments(bool allow);

    /// Get the next token from the input stream.
    bool GetNextToken(RawToken& token);

    /// Advance to the next `#` token in the input stream.
    bool GetNextDirective(RawToken& token);

    /// Read raw data.
    /// @deprecated
    ///     This method is only intended as a temporary measure to implement
    ///     binary-level macro caching, to provide a reference for performance
    ///     testing of the envisioned token-level macro caching.
    bool GetRaw(unsigned char* buffer, size_t size);

    /// Get current stream for comparison.
    ConstStreamPtr GetInputStream() const;

    /// Whether `stream` is the current stream, without copying a pointer to it.
    bool IsInputStream(const IStream* stream) const
    {
        if (mPosition.file != nullptr)
            return stream == mPosition.file->stream.get();
        return stream == mScanner.GetInputStreamPointer();
    }

    /// Get current stream name for comparison.
    UCS2String GetInputStreamName() const;

    /// Bookmark current stream and position for later rewinding.
    HotBookmark GetHotBookmark();

    /// Bookmark current stream position for later rewinding.
    ColdBookmark GetColdBookmark() const;

    /// Go to bookmark.
    bool GoToBookmark(const HotBookmark& bookmark);

    /// Go to bookmark.
    bool GoToBookmark(const ColdBookmark& bookmark);

private:

    struct KnownWordInfo final
    {
        int     id;
        TokenId expressionId;
        int     symbolHash;
        bool    isReservedWord     : 1;
        bool    isPseudoIdentifier : 1;
        KnownWordInfo();
    };

    Scanner                                         mScanner;
    std::unordered_map<UTF8String, KnownWordInfo>   mKnownWords;
    unsigned int                                    mNextIdentifierId;

    CachePosition                                   mPosition;
    unsigned int                                    mLastInstance;
    std::uint64_t                                   mUseCount;
    std::shared_ptr<CacheBudget>                    mBudget;
    std::unordered_map<UCS2String, CachedFilePtr>   mCachedFiles;

    bool GetNextScannedToken(RawToken& token);
    bool ProcessLexeme(RawToken& token);
    bool MakeRoom();
    CachedFilePtr LexFile(StreamPtr pStream, const Filesystem::FileStamp& stamp);
    void ReadCachedToken(RawToken& token);
    bool ContinueFromCache();
    LexemePosition CachedPosition() const;

    bool ProcessWordLexeme(RawToken& token);
    bool ProcessOtherLexeme(RawToken& token);
    bool ProcessFloatLiteralLexeme(RawToken& token);
    bool ProcessStringLiteralLexeme(RawToken& token);
    bool ProcessSignatureLexeme(RawToken& token);

    bool ProcessUCSEscapeDigits(UCS4& c, UTF8String::const_iterator& i, UTF8String::const_iterator& escapeSequenceEnd, unsigned int digits);
};

}
// end of namespace pov_parser

#endif // POVRAY_PARSER_RAWTOKENIZER_H
