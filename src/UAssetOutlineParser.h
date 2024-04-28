#pragma once
#include "parserintf.h"
class UAssetOutlineParser final : public OutlineParserInterface
{
  public:
    virtual void parseBinaryInput(const QCString& fileName,
      const std::vector<std::uint8_t>& fileBuf,
      const std::shared_ptr<Entry>& root) override;
    OUTLINE_PARSER_REJECT_TEXT;

    bool needsPreprocessing(const QCString& extension) const override;
    void parsePrototype(const QCString& text) override;
};

