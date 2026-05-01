#include "../ecgProcessing.hpp"
#include <gtest/gtest.h>

class FilterTest : public ::testing::Test
{
protected:
    IIRFilter f;
    ECGBuffers bufs;

    void SetUp() override
    {
        initFilter(f);
        initBuffers(bufs);
    }
};

TEST_F(FilterTest, BufferReadyAferFullFill)
{
    for (size_t i = 0; i < BUFFER_SIZE; i++)
    {
        pushSample(bufs, 2048, 1.0f);
    }
    EXPECT_TRUE(bufs.bufferReady);
    EXPECT_EQ(bufs.sampleIdx, 0);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}