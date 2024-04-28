#pragma once
#include "parserintf.h"
class UAssetCodeParser final : public CodeParserInterface
{
  public:
    void parseBinaryCode(
      OutputCodeList& codeOutList,
      const QCString& scopeName,
      const std::vector<std::uint8_t>& input,
      SrcLangExt lang, bool isExampleBlock,
      const QCString& exampleName,
      const FileDef* fileDef,
      int startLine,
      int endLine,
      bool inlineFragment,
      const MemberDef* memberDef,
      bool showLineNumbers,
      const Definition* searchCtx,
      bool collectXRefs) override;
    CODE_PARSER_REJECT_TEXT;

    void resetCodeParserState() override;
};

