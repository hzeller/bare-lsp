#include "message-stream-splitter.h"

#include <absl/status/status.h>
#include <absl/strings/string_view.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>

using ::testing::HasSubstr;

TEST(MessageStreamSplitterTest, NotRegisteredMessageProcessor) {
  MessageStreamSplitter s(4096);
  auto status = s.PullFrom([](char *, int) { return 0; });
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
}

// A stream simulator that is pre-filled with data and allows to
// simulate partial reads.
class DataStreamSimulator {
 public:
  DataStreamSimulator(absl::string_view content, int max_chunk = -1)
      : content_(content), max_chunk_(max_chunk) {}

  int read(char *buf, int size) {
    if (max_chunk_ > 0 && size > max_chunk_) size = max_chunk_;
    size = std::min(size, (int)content_.length() - read_pos_);
    memcpy(buf, content_.data() + read_pos_, size);
    read_pos_ += size;
    return size;
  }

 private:
  const std::string content_;
  const int max_chunk_;
  int read_pos_ = 0;
};

TEST(MessageStreamSplitterTest, CompleteReadValidMessage) {
  static constexpr absl::string_view kHeader = "Content-Length: 3\r\n\r\n";
  static constexpr absl::string_view kBody = "foo";

  DataStreamSimulator stream(absl::StrCat(kHeader, kBody));
  MessageStreamSplitter s(4096);
  int processor_call_count = 0;
  s.SetMessageProcessor([&](absl::string_view header, absl::string_view body) {
    ++processor_call_count;
    EXPECT_EQ(std::string(header), kHeader);
    EXPECT_EQ(std::string(body), kBody);
  });

  auto status =
      s.PullFrom([&](char *buf, int size) { return stream.read(buf, size); });
  EXPECT_TRUE(status.ok());
  EXPECT_EQ(processor_call_count, 1);

  // Calling more read will report EOF as we have finished our data.
  // This is reported as kUnavailable, the expected status code in this case.
  status = s.PullFrom([&](char *buf, int size) {  //
    return stream.read(buf, size);
  });
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);

  EXPECT_EQ(processor_call_count, 1);  // No additional calls recorded here.
}

TEST(MessageStreamSplitterTest, StreamDoesNotContainCompleteData) {
  static constexpr absl::string_view kHeader = "Content-Length: 3\r\n\r\n";
  static constexpr absl::string_view kBody = "fo";  // <- too short

  DataStreamSimulator stream(absl::StrCat(kHeader, kBody));
  MessageStreamSplitter s(4096);
  int processor_call_count = 0;
  s.SetMessageProcessor(
      [&](absl::string_view, absl::string_view) { ++processor_call_count; });

  absl::Status status = absl::OkStatus();
  while (status.ok()) {
    status =
        s.PullFrom([&](char *buf, int size) { return stream.read(buf, size); });
  }

  // We reached EOF, but we still have data pending. Reported as data loss.
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kDataLoss);

  EXPECT_EQ(processor_call_count, 0);
}

TEST(MessageStreamSplitterTest, CompleteReadMultipleMessages) {
  static constexpr absl::string_view kHeader = "Content-Length: 3\r\n\r\n";
  static constexpr absl::string_view kBody[2] = {"foo", "bar"};

  DataStreamSimulator stream(
      absl::StrCat(kHeader, kBody[0], kHeader, kBody[1]));
  MessageStreamSplitter s(4096);
  int processor_call_count = 0;
  // We expect one call per complete header/body pair.
  s.SetMessageProcessor([&](absl::string_view header, absl::string_view body) {
    EXPECT_EQ(std::string(header), kHeader);
    EXPECT_EQ(std::string(body), kBody[processor_call_count]);
    ++processor_call_count;
  });
  auto status = s.PullFrom([&](char *buf, int size) {
    return stream.read(buf, size);  // The complete chunk is read in one go.
  });
  EXPECT_EQ(processor_call_count, 2);
}

// Simulate short reads. Each read call only trickles out a few bytes.
TEST(MessageStreamSplitterTest, CompleteReadMultipleMessagesShortRead) {
  static constexpr absl::string_view kHeader = "Content-Length: 3\r\n\r\n";
  static constexpr absl::string_view kBody[2] = {"foo", "bar"};
  static constexpr int kTrickleReadSize = 2;

  DataStreamSimulator stream(absl::StrCat(kHeader, kBody[0], kHeader, kBody[1]),
                             kTrickleReadSize);
  MessageStreamSplitter s(4096);
  int processor_call_count = 0;
  s.SetMessageProcessor([&](absl::string_view header, absl::string_view body) {
    EXPECT_EQ(std::string(header), kHeader);
    EXPECT_EQ(std::string(body), kBody[processor_call_count]);
    ++processor_call_count;
  });

  int read_call_count = 0;
  absl::Status status = absl::OkStatus();
  while (status.ok()) {
    ++read_call_count;
    status =
        s.PullFrom([&](char *buf, int size) { return stream.read(buf, size); });
  }

  // Read until we reached EOF, indicated as kUnavailable
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);  // EOF
  EXPECT_GT(read_call_count, 10);  // this requires a few read calls.
  EXPECT_EQ(processor_call_count, 2);
}

TEST(MessageStreamSplitterTest, NotAvailableContentHeaderReadError) {
  static constexpr absl::string_view kHeader = "not-content-length: 3\r\n\r\n";
  static constexpr absl::string_view kBody = "foo";

  DataStreamSimulator stream(absl::StrCat(kHeader, kBody));
  MessageStreamSplitter s(4096);
  int processor_call_count = 0;
  s.SetMessageProcessor([&](absl::string_view header, absl::string_view body) {
    ++processor_call_count;
  });
  auto status =
      s.PullFrom([&](char *buf, int size) { return stream.read(buf, size); });
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), HasSubstr("header"));
  EXPECT_EQ(processor_call_count, 0);
}

TEST(MessageStreamSplitterTest, GarbledSizeInContentHeader) {
  static constexpr absl::string_view kHeader = "Content-Length: xyz\r\n\r\n";
  static constexpr absl::string_view kBody = "foo";

  DataStreamSimulator stream(absl::StrCat(kHeader, kBody));
  MessageStreamSplitter s(4096);
  int processor_call_count = 0;
  s.SetMessageProcessor([&](absl::string_view header, absl::string_view body) {
    ++processor_call_count;
  });
  auto status =
      s.PullFrom([&](char *buf, int size) { return stream.read(buf, size); });
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), HasSubstr("header"));
  EXPECT_EQ(processor_call_count, 0);
}
