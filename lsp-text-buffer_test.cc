#include "lsp-text-buffer.h"

#include <gtest/gtest.h>
#include <absl/strings/str_cat.h>

TEST(TextBufferTest, RecreateEmptyFile) {
  EditTextBuffer buffer("");
  EXPECT_EQ(buffer.lines(), 0);
  EXPECT_EQ(buffer.document_length(), 0);
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_TRUE(s.empty());
  });
}

TEST(TextBufferTest, RecreateFileWithAndWithoutNewlineAtEOF) {
  static constexpr absl::string_view kBaseFile =
    "Hello World\n"
    "\n"
    "Foo";

  for (const absl::string_view append : { "", "\n"}) {
    const std::string &content = absl::StrCat(kBaseFile, append);
    EditTextBuffer buffer(content);
    EXPECT_EQ(buffer.lines(), 3);

    buffer.ProcessContent([&](absl::string_view s) {
      EXPECT_EQ(std::string(s), content);
    });
  }
}

TEST(TextBufferTest, RecreateCRLFFiles) {
  EditTextBuffer buffer("Foo\r\nBar\r\n");
  EXPECT_EQ(buffer.lines(), 2);
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ(std::string(s), "Foo\r\nBar\r\n");
  });
}
