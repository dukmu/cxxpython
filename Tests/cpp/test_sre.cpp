#include "../../Modules/_sre/engine.h"
#include "../../Modules/_sre/sre_constants.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace {
TEST(SreEngine, ValidatorRejectsForgedMarksAndTruncatedControlFlow)
{
    const uint32_t valid[]{SRE_OP_MARK, 0, SRE_OP_LITERAL, 'a', SRE_OP_MARK, 1, SRE_OP_SUCCESS};
    EXPECT_EQ(cxx_sre_validate(valid, std::size(valid), 1), 1);
    EXPECT_EQ(cxx_sre_validate(valid, std::size(valid), 0), 0);
    const uint32_t forged[]{SRE_OP_MARK, 2, SRE_OP_SUCCESS};
    EXPECT_EQ(cxx_sre_validate(forged, std::size(forged), 1), 0);
    const uint32_t truncated[]{SRE_OP_LITERAL, SRE_OP_SUCCESS};
    EXPECT_EQ(cxx_sre_validate(truncated, std::size(truncated), 0), 0);
    const uint32_t jump[]{SRE_OP_JUMP, UINT32_MAX, SRE_OP_SUCCESS};
    EXPECT_EQ(cxx_sre_validate(jump, std::size(jump), 0), 0);
}

TEST(SreEngine, CapturesAndEmptySearchHavePreciseOffsets)
{
    const uint32_t code[]{SRE_OP_MARK, 0, SRE_OP_LITERAL, 'a', SRE_OP_MARK, 1, SRE_OP_SUCCESS};
    const uint32_t text[]{'x', 'a'};
    std::array<ptrdiff_t, 4> marks;
    int last = -1;
    ASSERT_EQ(cxx_sre_run(code, std::size(code), text, std::size(text), 1, 0, 2, 1, 0, 0,
                          marks.data(), &last, nullptr, nullptr),
              1);
    EXPECT_EQ(marks, (std::array<ptrdiff_t, 4>{1, 2, 1, 2}));
    EXPECT_EQ(last, 1);
    const uint32_t empty[]{SRE_OP_SUCCESS};
    ASSERT_EQ(
        cxx_sre_run(empty, 1, text, 2, 0, 0, 2, 1, 0, 1, marks.data(), &last, nullptr, nullptr), 1);
    EXPECT_EQ(marks[0], 1);
    EXPECT_EQ(marks[1], 1);
    EXPECT_EQ(
        cxx_sre_run(empty, 1, nullptr, 0, 0, 0, 0, 0, 1, 0, marks.data(), &last, nullptr, nullptr),
        1);
}

TEST(SreEngine, LongPrefixAndRepeatLoopsHonorInterruption)
{
    const uint32_t prefix[]{SRE_OP_INFO,
                            8,
                            SRE_INFO_PREFIX | SRE_INFO_LITERAL,
                            1,
                            1,
                            1,
                            1,
                            'z',
                            0,
                            SRE_OP_LITERAL,
                            'z',
                            SRE_OP_SUCCESS};
    const uint32_t repeat[]{SRE_OP_INFO,
                            4,
                            0,
                            0,
                            UINT32_MAX,
                            SRE_OP_REPEAT_ONE,
                            6,
                            0,
                            UINT32_MAX,
                            SRE_OP_LITERAL,
                            'a',
                            SRE_OP_SUCCESS,
                            SRE_OP_SUCCESS};
    std::vector<uint32_t> text(20000, 'a');
    std::array<ptrdiff_t, 2> marks;
    auto interrupt = [](void *value) {
        ++*static_cast<int *>(value);
        return 1;
    };
    for (auto code : {std::span<const uint32_t>(prefix), std::span<const uint32_t>(repeat)}) {
        ASSERT_EQ(cxx_sre_validate(code.data(), code.size(), 0), 1);
        int calls = 0, last = -1;
        EXPECT_EQ(cxx_sre_run(code.data(), code.size(), text.data(), text.size(), 0, 0, text.size(),
                              code[5] != SRE_OP_REPEAT_ONE, 0, 0, marks.data(), &last, interrupt,
                              &calls),
                  -10);
        EXPECT_GT(calls, 0);
    }
}
} // namespace
