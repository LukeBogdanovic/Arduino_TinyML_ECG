#include "../ecgProcessing.hpp"
#include <gtest/gtest.h>

class FilterTest : public ::testing::Test
{
protected:
    IIRFilter f;
    ECGBuffers bufs;
    std::vector<float> coeffs = {4.0f, 0.20254323f, 0.0f, -0.40508647f, 0.0f, 0.20254323f, 1.0f, -2.36106054f, 1.93885424f, -0.77592369f, 0.19833429f};

    void SetUp() override
    {
        initFilter(f);
        initBuffers(bufs);
    }
};

/**
 * UpdateFilterCoeffs Testing
 *
 */
TEST_F(FilterTest, updateFilterCoeffsAcceptsValidCoeffs)
{
    EXPECT_TRUE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

TEST_F(FilterTest, updateFilterCoeffsLengthCheck)
{
    coeffs.clear();
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

TEST_F(FilterTest, updateFilterCoeffsOrderCheckLessThan)
{
    coeffs.at(0) = 0;
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

TEST_F(FilterTest, updateFilterCoeffsOrderCheckGreaterThan)
{
    coeffs.at(0) = 21;
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

TEST_F(FilterTest, updateFilterCoeffsUnexpectedFilterLength)
{
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size() - 1)); // Simulating
}

/**
 * PushSample Testing
 *
 */
TEST_F(FilterTest, BufferReadyAferFull)
{
    for (size_t i = 0; i < BUFFER_SIZE; i++)
    {
        pushSample(bufs, 2048, 1.0f);
    }
    EXPECT_TRUE(bufs.bufferReady);
    EXPECT_EQ(bufs.sampleIdx, 0);
}

/**
 * ClampMagnitude Testing
 */
TEST_F(FilterTest, clampMagnitudeToMin)
{
    float val = clampMagnitude(-256.0f);
    EXPECT_FLOAT_EQ(val, 0.0f);
}

TEST_F(FilterTest, clampMagnitudeToMax)
{
    float val = clampMagnitude(65600.0f);
    EXPECT_FLOAT_EQ(val, 65535.0f);
}

TEST_F(FilterTest, clampMagnitudeNormalInput)
{
    float val = clampMagnitude(23765.0f);
    EXPECT_TRUE((val >= 0.0f) && (val <= 65535.0f));
}

/**
 * Entrypoint function
 *
 */
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}