//******************************************************************************
///
/// @file parser/rawtokenizer.cpp
///
/// Implementation of the _tokenizer_ stage of the parser.
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

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "parser/rawtokenizer.h"

// C++ variants of C standard header files
#include <climits>
#include <cstdlib>

// C++ standard header files
#include <algorithm>
#include <type_traits>

// POV-Ray header files (base module)
#include "base/fileinputoutput.h"
#include "base/povassert.h"
#include "base/stringutilities.h"

// POV-Ray header files (core module)
//  (none at the moment)

// POV-Ray header files (parser module)
#include "parser/reservedwords.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov_parser
{

//******************************************************************************

static int HexDigitToInt(UTF8String::value_type c)
{
    if ((c >= '0') && (c <= '9')) return c - '0' + 0x00;
    if ((c >= 'A') && (c <= 'F')) return c - 'A' + 0x0A;
    if ((c >= 'a') && (c <= 'f')) return c - 'a' + 0x0A;
    return -1;
}

static bool IsUCS4ScalarValue(UCS4 c)
{
    return (c <= 0x10FFFFu) && ((c < 0xD800u) || (c > 0xDFFFu));
}

//******************************************************************************

void AmbiguousStringValue::InvalidEscapeSequenceInfo::Throw() const
{
    throw InvalidEscapeSequenceException(stream->Name(), position, text);
}

//******************************************************************************

TokenId RawToken::GetTokenId() const
{
    if (id <= TOKEN_COUNT)
        return TokenId(id);
    else
        return IDENTIFIER_TOKEN;
}

//******************************************************************************

RawTokenizer::KnownWordInfo::KnownWordInfo() :
    id(int(NOT_A_TOKEN)),
    expressionId(NOT_A_TOKEN),
    isReservedWord(false),
    isPseudoIdentifier(false)
{}

//******************************************************************************

static constexpr std::size_t kMaxCachedTokensPerFile = 1 << 20;
static constexpr std::size_t kMaxCachedTokens = 1 << 21;

RawTokenizer::RawTokenizer() :
    mNextIdentifierId(TOKEN_COUNT+1),
    mLastInstance(0),
    mCacheEnabled(false),
    mRelexOnOpen(false),
    mCachedTokenCount(0)
{
    for (auto i = Reserved_Words; i->Token_Name != nullptr; ++i)
    {
        if (!isalpha(i->Token_Name[0]))
            continue;
        if (strchr(i->Token_Name, ' ') != nullptr)
            continue;
        KnownWordInfo& knownWord        = mKnownWords[i->Token_Name];
        knownWord.id                    = i->Token_Number;
        knownWord.expressionId          = GetCategorizedTokenId(i->Token_Number);
        knownWord.isReservedWord        = true;
        knownWord.isPseudoIdentifier    = ((knownWord.id == GLOBAL_TOKEN) || (knownWord.id == LOCAL_TOKEN));
    }
}

void RawTokenizer::SetInputStream(StreamPtr pStream)
{
    mPosition = CachePosition();
    mPosition.instance = ++mLastInstance;
    mScanner.SetInputStream(pStream);
}

void RawTokenizer::SetInputStream(StreamPtr pStream, const UCS2String& path)
{
    if (mCacheEnabled && mRelexOnOpen)
        mCachedFiles.erase(path);
    auto cached = mCachedFiles.find(path);
    if (cached == mCachedFiles.end())
    {
        if (!mCacheEnabled || (mCachedTokenCount >= kMaxCachedTokens))
        {
            SetInputStream(pStream);
            return;
        }
        cached = mCachedFiles.emplace(path, LexFile(pStream)).first;
    }
    mPosition.file = cached->second;
    mPosition.index = 0;
    mPosition.instance = ++mLastInstance;
    mPosition.cached = true;
}

CachedFilePtr RawTokenizer::LexFile(StreamPtr pStream)
{
    CachedFilePtr file = std::make_shared<CachedFile>();
    file->stream = pStream;
    file->complete = false;
    mScanner.SetInputStream(pStream);
    const std::size_t limit = std::min(kMaxCachedTokensPerFile, kMaxCachedTokens - mCachedTokenCount);
    Scanner::State before = mScanner.GetState();
    RawToken token;
    try
    {
        while (file->tokens.size() < limit)
        {
            before = mScanner.GetState();
            if (!GetNextScannedToken(token))
            {
                file->complete = true;
                break;
            }
            if ((token.lexeme.position.line > UINT32_MAX) || (token.lexeme.position.column > UINT32_MAX))
                break;
            if (file->tokens.empty() && (token.expressionId == SIGNATURE_TOKEN_CATEGORY))
                mScanner.SetCharacterEncoding(CharacterEncodingID::kUTF8);
            file->tokens.emplace_back();
            CachedToken& t = file->tokens.back();
            t.offset = token.lexeme.position.offset;
            t.line = std::uint32_t(token.lexeme.position.line);
            t.column = std::uint32_t(token.lexeme.position.column);
            t.id = token.id;
            t.expressionId = token.expressionId;
            t.category = token.lexeme.category;
            t.isReservedWord = token.isReservedWord;
            t.isPseudoIdentifier = token.isPseudoIdentifier;
            if (token.lexeme.category == Lexeme::kFloatLiteral)
            {
                t.text = std::uint32_t(file->floatTexts.size());
                file->floatTexts.append(token.lexeme.text.c_str(), token.lexeme.text.size() + 1);
                t.floatValue = token.floatValue;
            }
            else
            {
                t.text = InternText(token.lexeme.text);
                t.value = file->values.size();
                if (token.value != nullptr)
                    file->values.push_back(token.value);
            }
            before = mScanner.GetState();
        }
    }
    catch (...)
    {
        // A malformed lexeme stops the token array; reading on from there raises the error where it occurs.
    }
    mCachedTokenCount += file->tokens.size();
    file->tokens.shrink_to_fit();
    file->end = mScanner.GetHotBookmark(before);
    if (file->complete)
    {
        file->stream = std::make_shared<IMemStream>(nullptr, 0, UCS2String(pStream->Name()));
        file->end.pStream = file->stream;
    }
    return file;
}

std::uint32_t RawTokenizer::InternText(const UTF8String& text)
{
    auto i = mTextIndex.emplace(text, std::uint32_t(mTexts.size()));
    if (i.second)
        mTexts.push_back(&i.first->first);
    return i.first->second;
}

void RawTokenizer::SetStringEncoding(CharacterEncodingID encoding)
{
    mScanner.SetCharacterEncoding(encoding);
}

void pov_parser::RawTokenizer::SetNestedBlockComments(bool allow)
{
    mScanner.SetNestedBlockComments(allow);
}

//------------------------------------------------------------------------------

void RawTokenizer::ReadCachedToken(RawToken& token)
{
    const CachedFile& file = *mPosition.file;
    const CachedToken& t = file.tokens[mPosition.index++];
    if (t.category == Lexeme::kFloatLiteral)
    {
        token.lexeme.text = &file.floatTexts[t.text];
        token.floatValue = t.floatValue;
        token.value = nullptr;
    }
    else
    {
        token.lexeme.text = *mTexts[t.text];
        if (t.category == Lexeme::kStringLiteral)
            token.value = file.values[t.value];
        else
            token.value = nullptr;
    }
    token.lexeme.position.line = t.line;
    token.lexeme.position.column = t.column;
    token.lexeme.position.offset = t.offset;
    token.lexeme.category = t.category;
    token.id = t.id;
    token.expressionId = t.expressionId;
    token.isReservedWord = t.isReservedWord;
    token.isPseudoIdentifier = t.isPseudoIdentifier;
}

bool RawTokenizer::ContinueFromCache()
{
    if (mPosition.file->complete)
        return false;
    mPosition.cached = false;
    return mScanner.GoToBookmark(mPosition.file->end);
}

bool RawTokenizer::GetNextToken(RawToken& token)
{
    if (mPosition.cached)
    {
        if (mPosition.index < mPosition.file->tokens.size())
        {
            ReadCachedToken(token);
            return true;
        }
        if (!ContinueFromCache())
            return false;
    }
    return GetNextScannedToken(token);
}

bool RawTokenizer::GetNextScannedToken(RawToken& token)
{
    if (!mScanner.GetNextLexeme(token.lexeme))
        return false;

    switch (token.lexeme.category)
    {
        case Lexeme::kWord:             if (ProcessWordLexeme(token))           return true;
        case Lexeme::kFloatLiteral:     if (ProcessFloatLiteralLexeme(token))   return true;
        case Lexeme::kStringLiteral:    if (ProcessStringLiteralLexeme(token))  return true;
        case Lexeme::kOther:            if (ProcessOtherLexeme(token))          return true;
        case Lexeme::kUTF8SignatureBOM: if (ProcessSignatureLexeme(token))      return true;
        default:                        POV_PARSER_PANIC();                     return true;
    }
}

bool RawTokenizer::GetNextDirective(RawToken& token)
{
    if (mPosition.cached)
    {
        const std::vector<CachedToken>& tokens = mPosition.file->tokens;
        while ((mPosition.index < tokens.size()) && (tokens[mPosition.index].id != int(HASH_TOKEN)))
            ++mPosition.index;
        if (mPosition.index < tokens.size())
        {
            ReadCachedToken(token);
            return true;
        }
        if (!ContinueFromCache())
            return false;
    }

    if (!mScanner.GetNextDirective(token.lexeme))
        return false;

    POV_PARSER_ASSERT(token.lexeme.category == Lexeme::kOther);
    POV_PARSER_ASSERT(token.lexeme.text == "#");

    token.id = TokenId::HASH_TOKEN;
    token.expressionId = TokenId::HASH_TOKEN;
    token.value = nullptr;
    token.isReservedWord = false;
    token.isPseudoIdentifier = false;

    return true;
}

bool RawTokenizer::ProcessWordLexeme(RawToken& token)
{
    POV_PARSER_ASSERT(token.lexeme.category == Lexeme::kWord);
    POV_PARSER_ASSERT(token.lexeme.text.size() > 0);

    auto& i = mKnownWords[token.lexeme.text];
    if (i.id == int(NOT_A_TOKEN))
    {
        i.id = ++mNextIdentifierId;
        i.expressionId = IDENTIFIER_TOKEN;
    }
    token.id = i.id;
    token.expressionId = i.expressionId;
    token.value = nullptr;
    token.isReservedWord = i.isReservedWord;
    token.isPseudoIdentifier = i.isPseudoIdentifier;

    return true;
}

bool RawTokenizer::ProcessFloatLiteralLexeme(RawToken& token)
{
    POV_PARSER_ASSERT(token.lexeme.category == Lexeme::kFloatLiteral);

    token.id = int(FLOAT_TOKEN);
    token.expressionId = FLOAT_TOKEN_CATEGORY;

    if (std::is_same<DBL, double>::value)
    {
        char* end;
        token.floatValue = DBL(std::strtod(token.lexeme.text.c_str(), &end));
        if (end == token.lexeme.text.c_str())
            return false;
    }
    else if (sscanf(token.lexeme.text.c_str(), POV_DBL_FORMAT_STRING, &token.floatValue) == 0)
        return false;

    token.isReservedWord = false;
    token.isPseudoIdentifier = false;

    return true;
}

bool RawTokenizer::ProcessStringLiteralLexeme(RawToken& token)
{
    POV_PARSER_ASSERT(token.lexeme.category == Lexeme::kStringLiteral);
    POV_PARSER_ASSERT(token.lexeme.text.size() >= 2);
    POV_PARSER_ASSERT(token.lexeme.text.front() == '"');
    POV_PARSER_ASSERT(token.lexeme.text.back() == '"');

    token.id = int(STRING_LITERAL_TOKEN);
    token.expressionId = STRING_LITERAL_TOKEN;

    std::shared_ptr<StringValue> pValue(std::make_shared<StringValue>());
    std::shared_ptr<AmbiguousStringValue> pAmbiguousValue;
    UCS4 c;

    pValue->data.reserve(token.lexeme.text.size() - 2);
    auto payloadBegin = token.lexeme.text.cbegin() + 1;
    auto payloadEnd = token.lexeme.text.cend() - 1;
    auto i = payloadBegin;
    while (i != payloadEnd)
    {
        if (*i == '\\')
        {
            // For now, presume the escape sequence to be ambiguous.
            bool isAmbiguous = true;
            bool isInvalid = false;

            auto escapeSequenceBegin = i;
            POV_PARSER_ASSERT(payloadEnd - escapeSequenceBegin >= 2); // Bare `\` at end of string should have escaped the end-of-string quote char.
            auto escapeSequenceEnd = escapeSequenceBegin + 2; // Typical length of escape sequence.

            ++i;
            switch (*i)
            {
                case '\"':
                    isAmbiguous = false;
                    // FALLTHROUGH
                case '\'':
                case '\\':
                    c = UCS4(*i);
                    ++i;
                    break;

                case 'a': c = 0x0007u; ++i; break;   // "Alert"         = BEL
                case 'b': c = 0x0008u; ++i; break;   // "Backspace"     = BS
                case 't': c = 0x0009u; ++i; break;   // "Tab"           = HT
                case 'n': c = 0x000Au; ++i; break;   // "New line"      = LF
                case 'v': c = 0x000Bu; ++i; break;   // "Vertical tab"  = VT
                case 'f': c = 0x000Cu; ++i; break;   // "Form feed"     = FF
                case 'r': c = 0x000Du; ++i; break;   // "Return"        = CR

                case 'u':
                    ++i;
                    escapeSequenceEnd = payloadEnd;
                    if (!ProcessUCSEscapeDigits(c, i, escapeSequenceEnd, 4))
                        isInvalid = true;
                    /// @todo Do we want to add support for surrogate pairs?
                    break;

                case 'U':
                    ++i;
                    escapeSequenceEnd = payloadEnd;
                    if (!ProcessUCSEscapeDigits(c, i, escapeSequenceEnd, 6))
                        isInvalid = true;
                    /// @todo Do we want to add support for surrogate pairs?
                    break;

                default:
                    isInvalid = true;
                    c = UCS4(*i);
                    ++i;
                    break;
            }
            if (isAmbiguous)
            {
                if (!pAmbiguousValue)
                {
                    // String has been unambiguous -- until now.
                    pAmbiguousValue = std::make_shared<AmbiguousStringValue>(*pValue);
                    pValue = pAmbiguousValue;
                }

                /// @todo Add support for non-BMP characters (requires UTF16String instead of UCS2String).
                pAmbiguousValue->data += UCS2(c);

                for (auto iEscapeChar = escapeSequenceBegin; iEscapeChar != escapeSequenceEnd; ++iEscapeChar)
                {
                    /// @todo Add support for non-BMP characters (requires UTF16String instead of UCS2String).
                    pAmbiguousValue->fileName += UCS2(*iEscapeChar);
                }

                if (isInvalid && (pAmbiguousValue->invalidEscapeSequence == nullptr))
                    pAmbiguousValue->invalidEscapeSequence = new AmbiguousStringValue::InvalidEscapeSequenceInfo(
                        mScanner.GetInputStream(), token.lexeme.position, escapeSequenceBegin, escapeSequenceEnd);
            }
            else
            {
                pValue->Append(UCS2(c));
            }
        }
        else if (Octet(*i) <= 0x7F)
        {
            pValue->Append(UCS2(*i));
            ++i;
        }
        else
        {
            if (!pov_base::UCS::DecodeUTF8Sequence(c, i, payloadEnd))
                c = pov_base::UCS::kReplacementCharacter;

            /// @todo Add support for non-BMP characters (requires UTF16String instead of UCS2String).
            pValue->Append(UCS2(c));
        }
    }

    token.value = pValue;
    token.isReservedWord = false;
    token.isPseudoIdentifier = false;

    return true;
}

bool RawTokenizer::ProcessUCSEscapeDigits(UCS4& c, UTF8String::const_iterator& i, UTF8String::const_iterator& escapeSequenceEnd, unsigned int digits)
{
    POV_PARSER_ASSERT(digits <= 8);

    if ((escapeSequenceEnd - i) < digits)
        return false;
    escapeSequenceEnd = i + digits;

    c = 0x0000u;
    while (i != escapeSequenceEnd)
    {
        int hexDigit = HexDigitToInt(*i);
        if (hexDigit < 0)
            return false;
        c = (c << 4) + hexDigit;
        ++i;
    }

    /// @todo Do we want to add support for surrogate pairs?
    return IsUCS4ScalarValue(c);
}

bool RawTokenizer::ProcessOtherLexeme(RawToken& token)
{
    POV_PARSER_ASSERT(token.lexeme.category == Lexeme::kOther);
    POV_PARSER_ASSERT(token.lexeme.text.size() > 0);

    TokenId tokenId = NOT_A_TOKEN;

    if (token.lexeme.text.size() == 1)
    {
        switch (token.lexeme.text[0])
        {
            // 0x00 through 0x1F should have been interpreted as control characters or rejected as non-printable.
            // ' ' should have been interpreted as whitespace.
            case '!':   tokenId = EXCLAMATION_TOKEN;    break;
            // '"' should have been interpreted as start of string literal.
            case '#':   tokenId = HASH_TOKEN;           break;
            case '$':   tokenId = DOLLAR_TOKEN;         break;
            case '%':   tokenId = PERCENT_TOKEN;        break;
            case '&':   tokenId = AMPERSAND_TOKEN;      break;
            case '\'':  tokenId = SINGLE_QUOTE_TOKEN;   break;
            case '(':   tokenId = LEFT_PAREN_TOKEN;     break;
            case ')':   tokenId = RIGHT_PAREN_TOKEN;    break;
            case '*':   tokenId = STAR_TOKEN;           break;
            case '+':   tokenId = PLUS_TOKEN;           break;
            case ',':   tokenId = COMMA_TOKEN;          break;
            case '-':   tokenId = DASH_TOKEN;           break;
            case '.':   tokenId = PERIOD_TOKEN;         break;
            case '/':   tokenId = SLASH_TOKEN;          break;
            // '0' through '9' should have been interpreted as (start of or minimal) float literal.
            case ':':   tokenId = COLON_TOKEN;          break;
            case ';':   tokenId = SEMI_COLON_TOKEN;     break;
            case '<':   tokenId = LEFT_ANGLE_TOKEN;     break;
            case '=':   tokenId = EQUALS_TOKEN;         break;
            case '>':   tokenId = RIGHT_ANGLE_TOKEN;    break;
            case '?':   tokenId = QUESTION_TOKEN;       break;
            case '@':   tokenId = AT_TOKEN;             break;
            // 'A' through 'Z' should have been interpreted as (start of or minimal) word literal.
            case '[':   tokenId = LEFT_SQUARE_TOKEN;    break;
            case '\\':  tokenId = BACK_SLASH_TOKEN;     break;
            case ']':   tokenId = RIGHT_SQUARE_TOKEN;   break;
            case '^':   tokenId = HAT_TOKEN;            break;
            // '_' should have been interpreted as (start of or minimal) word literal.
            case '`':   tokenId = BACK_QUOTE_TOKEN;     break;
            // 'a' through 'z' should have been interpreted as (start of or minimal) word literal.
            case '{':   tokenId = LEFT_CURLY_TOKEN;     break;
            case '|':   tokenId = BAR_TOKEN;            break;
            case '}':   tokenId = RIGHT_CURLY_TOKEN;    break;
            case '~':   tokenId = TILDE_TOKEN;          break;
            // 0x7F should have been rejected as non-printable.
            // 0x80 through 0xFF should have been rejected as non-ASCII.
            default:    POV_PARSER_PANIC();             break;
        }
    }
    else if (token.lexeme.text == "!=")
        tokenId = REL_NE_TOKEN;
    else if (token.lexeme.text == "<=")
        tokenId = REL_LE_TOKEN;
    else if (token.lexeme.text == ">=")
        tokenId = REL_GE_TOKEN;
    else
        POV_PARSER_PANIC(); // Should not have been produced by scanner.

    token.id                    = int(tokenId);
    token.expressionId          = tokenId;
    token.value                 = nullptr;
    token.isReservedWord        = false;
    token.isPseudoIdentifier    = false;

    POV_EXPERIMENTAL_ASSERT(GetCategorizedTokenId(tokenId) == tokenId);

    return true;
}

bool RawTokenizer::ProcessSignatureLexeme(RawToken& token)
{
    POV_PARSER_ASSERT(token.lexeme.text.size() > 0);

    TokenId tokenId = NOT_A_TOKEN;

    switch (token.lexeme.category)
    {
        case Lexeme::kUTF8SignatureBOM: tokenId = UTF8_SIGNATURE_TOKEN; break;
        default:                        POV_PARSER_PANIC();             break;
    }

    token.id                    = int(tokenId);
    token.expressionId          = SIGNATURE_TOKEN_CATEGORY;
    token.value                 = nullptr;
    token.isReservedWord        = false;
    token.isPseudoIdentifier    = false;

    return true;
}

//------------------------------------------------------------------------------

bool RawTokenizer::GetRaw(unsigned char* buffer, size_t size)
{
    if (mPosition.cached)
        return false;
    return mScanner.GetRaw(buffer, size);
}

//------------------------------------------------------------------------------

pov_parser::ConstStreamPtr RawTokenizer::GetInputStream() const
{
    if (mPosition.file != nullptr)
        return mPosition.file->stream;
    return mScanner.GetInputStream();
}

pov_base::UCS2String RawTokenizer::GetInputStreamName() const
{
    if (mPosition.file != nullptr)
        return mPosition.file->stream->Name();
    return mScanner.GetInputStreamName();
}

LexemePosition RawTokenizer::CachedPosition() const
{
    const CachedFile& file = *mPosition.file;
    if (mPosition.index >= file.tokens.size())
        return file.end;
    const CachedToken& t = file.tokens[mPosition.index];
    LexemePosition position;
    position.line = t.line;
    position.column = t.column;
    position.offset = t.offset;
    return position;
}

pov_parser::RawTokenizer::HotBookmark RawTokenizer::GetHotBookmark()
{
    if (!mPosition.cached)
        return HotBookmark(mScanner.GetHotBookmark(), mPosition);
    const Scanner::HotBookmark& end = mPosition.file->end;
    return HotBookmark(Scanner::HotBookmark(mPosition.file->stream, CachedPosition(), end.characterEncoding, end.nominalEndOfLine,
                                            end.allowNestedBlockComments), mPosition);
}

pov_parser::RawTokenizer::ColdBookmark RawTokenizer::GetColdBookmark() const
{
    if (!mPosition.cached)
        return ColdBookmark(mScanner.GetColdBookmark(), mPosition);
    const Scanner::HotBookmark& end = mPosition.file->end;
    return ColdBookmark(Scanner::ColdBookmark(mPosition.file->stream->Name(), CachedPosition(), end.characterEncoding,
                                              end.nominalEndOfLine, end.allowNestedBlockComments), mPosition);
}

bool RawTokenizer::GoToBookmark(const HotBookmark& bookmark)
{
    mPosition = bookmark;
    if (bookmark.cached)
        return true;
    return mScanner.GoToBookmark(static_cast<const Scanner::HotBookmark&>(bookmark));
}

bool RawTokenizer::GoToBookmark(const ColdBookmark& bookmark)
{
    if (bookmark.file == nullptr)
        return mScanner.GoToBookmark(static_cast<const Scanner::ColdBookmark&>(bookmark));
    const unsigned int instance = (bookmark.file == mPosition.file) ? mPosition.instance : ++mLastInstance;
    mPosition = bookmark;
    mPosition.instance = instance;
    if (bookmark.cached)
        return true;
    const Scanner::ColdBookmark& b = bookmark;
    return mScanner.GoToBookmark(Scanner::HotBookmark(bookmark.file->stream, b, b.characterEncoding, b.nominalEndOfLine, b.allowNestedBlockComments));
}

}
// end of namespace pov_parser
