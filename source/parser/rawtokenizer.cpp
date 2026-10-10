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
#include "parser/symboltable.h"

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
    throw InvalidEscapeSequenceException(fileName, position, text);
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
    symbolHash(-1),
    isReservedWord(false),
    isPseudoIdentifier(false)
{}

//******************************************************************************

static constexpr std::size_t kMaxCachedTokensPerFile = 1 << 18;
static constexpr std::size_t kMaxCachedTokens = 1 << 20;
static constexpr std::size_t kMaxCachedBytesPerFile = 16 << 20;
static constexpr std::size_t kMaxCachedBytes = 64 << 20;

RawTokenizer::RawTokenizer() :
    mNextIdentifierId(TOKEN_COUNT+1),
    mLastInstance(0),
    mUseCount(0)
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

static POV_OFF_T StreamSize(IStream& stream)
{
    if (!stream.seekg(0, IOBase::seek_end))
        return -1;
    const POV_OFF_T size = stream.tellg();
    return stream.seekg(0) ? size : -1;
}

void RawTokenizer::SetInputStream(StreamPtr pStream, const UCS2String& path)
{
    if (mBudget == nullptr)
    {
        SetInputStream(pStream);
        return;
    }
    Filesystem::FileStamp stamp = Filesystem::GetFileStamp(path);
    if (stamp.size < 0)
        stamp.size = StreamSize(*pStream);
    auto cached = mCachedFiles.find(stamp.name);
    if ((cached != mCachedFiles.end()) &&
        ((stamp.size < 0) || (cached->second->size != stamp.size) || (cached->second->time != stamp.time)))
    {
        mCachedFiles.erase(cached);
        cached = mCachedFiles.end();
    }
    if (cached == mCachedFiles.end())
    {
        if (!MakeRoom())
        {
            SetInputStream(pStream);
            return;
        }
        cached = mCachedFiles.emplace(stamp.name, LexFile(pStream, stamp)).first;
    }
    cached->second->lastUse = ++mUseCount;
    mPosition.file = cached->second;
    mPosition.index = 0;
    mPosition.instance = ++mLastInstance;
    mPosition.cached = true;
}

void RawTokenizer::ForgetFile(const UCS2String& canonicalPath)
{
    mCachedFiles.erase(canonicalPath);
}

bool RawTokenizer::MakeRoom()
{
    while ((mBudget->tokens >= kMaxCachedTokens) || (mBudget->bytes >= kMaxCachedBytes))
    {
        auto oldest = mCachedFiles.end();
        for (auto i = mCachedFiles.begin(); i != mCachedFiles.end(); ++i)
            if ((i->second.use_count() == 1) &&
                ((oldest == mCachedFiles.end()) || (i->second->lastUse < oldest->second->lastUse)))
                oldest = i;
        if (oldest == mCachedFiles.end())
            return false;
        mCachedFiles.erase(oldest);
    }
    return true;
}

CachedFilePtr RawTokenizer::LexFile(StreamPtr pStream, const Filesystem::FileStamp& stamp)
{
    CachedFilePtr file = std::make_shared<CachedFile>();
    file->stream = pStream;
    file->size = stamp.size;
    file->time = stamp.time;
    file->lastUse = 0;
    file->bytes = 0;
    file->complete = false;
    mScanner.SetInputStream(pStream);
    const std::size_t maxTokens = std::min(kMaxCachedTokensPerFile, kMaxCachedTokens - mBudget->tokens);
    const std::size_t maxBytes = std::min(kMaxCachedBytesPerFile, kMaxCachedBytes - mBudget->bytes);
    std::unordered_map<UTF8String, std::uint32_t> textIndex;
    Scanner::State before = mScanner.GetState();
    RawToken token;
    try
    {
        while (file->tokens.size() < maxTokens)
        {
            token.floatValue = 0.0;
            token.symbolHash = -1;
            if (!mScanner.GetNextLexeme(token.lexeme))
            {
                file->complete = true;
                break;
            }
            if ((token.lexeme.category == Lexeme::kFloatLiteral) ? !ProcessFloatLiteralLexeme(token)
                                                                 : !ProcessLexeme(token))
                break;
            const LexemePosition& start = token.lexeme.position;
            const std::size_t length = token.lexeme.text.size();
            if ((start.line > UINT32_MAX) || (start.column > UINT32_MAX))
                break;
            if (file->tokens.empty() && (token.expressionId == SIGNATURE_TOKEN_CATEGORY))
                mScanner.SetCharacterEncoding(CharacterEncodingID::kUTF8);
            CachedToken t;
            t.offset = start.offset;
            t.line = std::uint32_t(start.line);
            t.column = std::uint32_t(start.column);
            t.id = token.id;
            t.expressionId = token.expressionId;
            t.category = token.lexeme.category;
            t.isReservedWord = token.isReservedWord;
            t.isPseudoIdentifier = token.isPseudoIdentifier;
            const std::uint32_t textCount = std::uint32_t(file->texts.size());
            t.text = textCount;
            if (t.category != Lexeme::kFloatLiteral)
            {
                const auto known = textIndex.find(token.lexeme.text);
                if (known != textIndex.end())
                    t.text = known->second;
            }
            const Scanner::State after = mScanner.GetState();
            const bool irregularEnd = (after.position.line != start.line) ||
                                      (after.position.column != start.column + POV_LONG(length)) ||
                                      (after.position.offset != start.offset + POV_OFF_T(length));
            std::size_t cost = sizeof(CachedToken) + (irregularEnd ? 4 * sizeof(LexemePosition) : 0);
            if (t.text == textCount)
                cost += sizeof(UTF8String) + ((length < 16) ? 0 : length + 1);
            if (t.category == Lexeme::kStringLiteral)
                cost += sizeof(AmbiguousStringValue) + 4 * length;
            if (file->bytes + cost > maxBytes)
                break;
            if (t.text == textCount)
            {
                if (t.category != Lexeme::kFloatLiteral)
                    textIndex.emplace(token.lexeme.text, textCount);
                file->texts.push_back(token.lexeme.text);
            }
            if (t.category == Lexeme::kFloatLiteral)
                t.floatValue = token.floatValue;
            else if (t.category == Lexeme::kStringLiteral)
            {
                t.value = file->values.size();
                file->values.push_back(token.value);
            }
            else
                t.symbolHash = token.symbolHash;
            if (irregularEnd)
                file->ends.emplace(file->tokens.size(), after.position);
            file->tokens.push_back(t);
            file->bytes += cost;
            before = after;
        }
    }
    catch (const TokenizerException&)
    {
        // Reading on from here raises the error where it occurs.
    }
    mBudget->tokens += file->tokens.size();
    mBudget->bytes += file->bytes;
    file->budget = mBudget;
    file->tokens.shrink_to_fit();
    file->texts.shrink_to_fit();
    file->end = mScanner.GetHotBookmark(before);
    if (file->complete)
    {
        file->stream = std::make_shared<IMemStream>(nullptr, 0, UCS2String(pStream->Name()));
        file->end.pStream = file->stream;
    }
    return file;
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
    token.lexeme.text = file.texts[t.text];
    token.floatValue = (t.category == Lexeme::kFloatLiteral) ? t.floatValue : 0.0;
    token.symbolHash = (t.category == Lexeme::kWord) ? t.symbolHash : -1;
    if (t.category == Lexeme::kStringLiteral)
        token.value = file.values[t.value];
    else
        token.value = nullptr;
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
    token.floatValue = 0.0;
    token.symbolHash = -1;
    if (!mScanner.GetNextLexeme(token.lexeme))
        return false;
    return ProcessLexeme(token);
}

bool RawTokenizer::ProcessLexeme(RawToken& token)
{
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

    token.floatValue = 0.0;
    token.symbolHash = -1;
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
    if (i.symbolHash < 0)
        i.symbolHash = SymbolTable::get_hash_value(token.lexeme.text.c_str());
    token.id = i.id;
    token.symbolHash = i.symbolHash;
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
        if (end != token.lexeme.text.c_str() + token.lexeme.text.size())
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
                        mScanner.GetInputStreamName(), token.lexeme.position, escapeSequenceBegin,
                        escapeSequenceEnd);
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
    if (mPosition.index == 0)
        return LexemePosition();
    if (mPosition.index >= file.tokens.size())
        return file.end;
    const auto irregular = file.ends.find(mPosition.index - 1);
    if (irregular != file.ends.end())
        return irregular->second;
    const CachedToken& t = file.tokens[mPosition.index - 1];
    const std::size_t length = file.texts[t.text].size();
    LexemePosition position;
    position.line = t.line;
    position.column = t.column + POV_LONG(length);
    position.offset = t.offset + POV_OFF_T(length);
    return position;
}

pov_parser::RawTokenizer::HotBookmark RawTokenizer::GetHotBookmark()
{
    if (!mPosition.cached)
        return HotBookmark(mScanner.GetHotBookmark(), mPosition);
    const Scanner::HotBookmark& end = mPosition.file->end;
    const Scanner::HotBookmark b(mPosition.file->stream, CachedPosition(), end.characterEncoding,
                                 end.nominalEndOfLine, end.allowNestedBlockComments);
    return HotBookmark(b, mPosition);
}

pov_parser::RawTokenizer::ColdBookmark RawTokenizer::GetColdBookmark() const
{
    if (!mPosition.cached)
        return ColdBookmark(mScanner.GetColdBookmark(), mPosition);
    const Scanner::HotBookmark& end = mPosition.file->end;
    const Scanner::ColdBookmark b(mPosition.file->stream->Name(), CachedPosition(),
                                  end.characterEncoding, end.nominalEndOfLine,
                                  end.allowNestedBlockComments);
    return ColdBookmark(b, mPosition);
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
    if (!bookmark.cached)
        return mScanner.GoToBookmark(static_cast<const Scanner::ColdBookmark&>(bookmark));
    const bool sameFile = (bookmark.file == mPosition.file);
    const unsigned int instance = sameFile ? mPosition.instance : ++mLastInstance;
    mPosition = bookmark;
    mPosition.instance = instance;
    return true;
}

}
// end of namespace pov_parser
