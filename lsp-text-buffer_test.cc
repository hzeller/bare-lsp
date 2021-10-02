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
    EXPECT_EQ("Foo\r\nBar\r\n", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplyFullContent) {
  EditTextBuffer buffer("Foo\nBar\n");
  buffer.ApplyChange({ .range = {}, .has_range = false, .text = "NewFile" });
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("NewFile", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLineInsert) {
  EditTextBuffer buffer("Hello World");
  buffer.ApplyChange({
      .range = {
        .start = { 0, 6 },
        .end = { 0, 6 },
      },
      .has_range = true,
      .text = "brave ",
    });
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("Hello brave World", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLineReplace) {
  EditTextBuffer buffer("Hello World\n");
  buffer.ApplyChange({
      .range = {
        .start = { 0, 6 },
        .end = { 0, 11},
      },
      .has_range = true,
      .text = "Planet",
    });
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("Hello Planet\n", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLineReplaceNotFirstLine) {
  EditTextBuffer buffer("Hello World\nFoo\n");
  buffer.ApplyChange({
      .range = {
        .start = { 1, 0 },
        .end = { 1, 3},
      },
      .has_range = true,
      .text = "Bar",
    });
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("Hello World\nBar\n", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLineReplaceCorrectOverlongEnd) {
  const TextDocumentContentChangeEvent change = {
    .range = {
      .start = { 0, 6 },
      .end = { 0, 42 },  // Too long end shall be trimmed
    },
    .has_range = true,
    .text = "Planet",
  };

  {
    EditTextBuffer buffer("Hello World\n");
    buffer.ApplyChange(change);
    buffer.ProcessContent([&](absl::string_view s) {
      EXPECT_EQ("Hello Planet\n", std::string(s));
    });
  }

  {
    EditTextBuffer buffer("Hello World");
    buffer.ApplyChange(change);
    buffer.ProcessContent([&](absl::string_view s) {
      EXPECT_EQ("Hello Planet", std::string(s));
    });
  }
}
