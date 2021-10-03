#include "lsp-text-buffer.h"

#include <absl/strings/str_cat.h>
#include <gtest/gtest.h>

TEST(TextBufferTest, RecreateEmptyFile) {
  EditTextBuffer buffer("");
  EXPECT_EQ(buffer.lines(), 0);
  EXPECT_EQ(buffer.document_length(), 0);
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_TRUE(s.empty());
  });
}

TEST(TextBufferTest, RecreateFileWithAndWithoutNewlineAtEOF) {
  static constexpr absl::string_view kBaseFile =
      "Hello World\n"
      "\n"
      "Foo";

  for (const absl::string_view append : {"", "\n"}) {
    const std::string &content = absl::StrCat(kBaseFile, append);
    EditTextBuffer buffer(content);
    EXPECT_EQ(buffer.lines(), 3);

    buffer.ProcessContent([&](absl::string_view s) {  //
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
  const TextDocumentContentChangeEvent change = {
      .range = {},
      .has_range = false,
      .text = "NewFile",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_EQ("NewFile", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLine_Insert) {
  EditTextBuffer buffer("Hello World");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 6},
              .end = {0, 6},
          },
      .has_range = true,
      .text = "brave ",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  EXPECT_EQ(buffer.document_length(), 17);
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("Hello brave World", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLine_InsertFromEmptyFile) {
  EditTextBuffer buffer("");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 0},
              .end = {0, 0},
          },
      .has_range = true,
      .text = "New File!",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_EQ("New File!", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLine_Replace) {
  EditTextBuffer buffer("Hello World\n");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 6},
              .end = {0, 11},
          },
      .has_range = true,
      .text = "Planet",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("Hello Planet\n", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLine_ReplaceNotFirstLine) {
  // Make sure we properly access the right line.
  EditTextBuffer buffer("Hello World\nFoo\n");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {1, 0},
              .end = {1, 3},
          },
      .has_range = true,
      .text = "Bar",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("Hello World\nBar\n", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLine_Erase) {
  EditTextBuffer buffer("Hello World\n");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 5},
              .end = {0, 11},
          },
      .has_range = true,
      .text = "",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  EXPECT_EQ(buffer.document_length(), 6);
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_EQ("Hello\n", std::string(s));
  });
}

TEST(TextBufferTest, ChangeApplySingleLine_ReplaceCorrectOverlongEnd) {
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 6}, .end = {0, 42},  // Too long end shall be trimmed
          },
      .has_range = true,
      .text = "Planet",
  };

  {
    EditTextBuffer buffer("Hello World\n");
    EXPECT_TRUE(buffer.ApplyChange(change));
    buffer.ProcessContent([&](absl::string_view s) {
      EXPECT_EQ("Hello Planet\n", std::string(s));
    });
  }

  {
    EditTextBuffer buffer("Hello World");
    EXPECT_TRUE(buffer.ApplyChange(change));
    buffer.ProcessContent([&](absl::string_view s) {
      EXPECT_EQ("Hello Planet", std::string(s));
    });
  }
}

TEST(TextBufferTest, ChangeApplyMultiLine_EraseBetweenLines) {
  EditTextBuffer buffer("Hello\nWorld\n");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 2},  // From here to end of line
              .end = {1, 0},
          },
      .has_range = true,
      .text = "y ",
  };
  EXPECT_TRUE(buffer.ApplyChange(change));
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_EQ("Hey World\n", std::string(s));
  });
  EXPECT_EQ(buffer.document_length(), 10);  // won't work yet.
}

TEST(TextBufferTest, ChangeApplyMultiLine_InsertMoreLines) {
  EditTextBuffer buffer("Hello\nbrave World\n");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 2},  // From here to end of line
              .end = {1, 5},
          },
      .has_range = true,
      .text = "y!\nThis will be a new line\nand more in this",
  };
  EXPECT_EQ(buffer.lines(), 2);
  EXPECT_TRUE(buffer.ApplyChange(change));
  EXPECT_EQ(buffer.lines(), 3);
  static constexpr absl::string_view kExpected =
      "Hey!\nThis will be a new line\nand more in this World\n";
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_EQ(kExpected, std::string(s));
  });
  EXPECT_EQ(buffer.document_length(), kExpected.length());
}

TEST(TextBufferTest, ChangeApplyMultiLine_InsertFromStart) {
  EditTextBuffer buffer("");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {0, 0},
              .end = {0, 0},
          },
      .has_range = true,
      .text = "This is now\na multiline\nfile\n",
  };
  EXPECT_EQ(buffer.lines(), 0);
  EXPECT_TRUE(buffer.ApplyChange(change));
  EXPECT_EQ(buffer.lines(), 3);
  buffer.ProcessContent([&](absl::string_view s) {
    EXPECT_EQ("This is now\na multiline\nfile\n", std::string(s));
  });
  EXPECT_EQ(buffer.document_length(), change.text.length());
}

TEST(TextBufferTest, ChangeApplyMultiLine_RemoveLines) {
  EditTextBuffer buffer("Foo\nBar\nBaz\nQuux");
  const TextDocumentContentChangeEvent change = {
      .range =
          {
              .start = {1, 0},
              .end = {3, 0},
          },
      .has_range = true,
      .text = "",
  };
  EXPECT_EQ(buffer.lines(), 4);
  EXPECT_TRUE(buffer.ApplyChange(change));
  EXPECT_EQ(buffer.lines(), 2);
  buffer.ProcessContent([&](absl::string_view s) {  //
    EXPECT_EQ("Foo\nQuux", std::string(s));
  });
  EXPECT_EQ(buffer.document_length(), 8);
}
