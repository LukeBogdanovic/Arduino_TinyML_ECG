#include "../ecgProcessing.hpp"
#include <gtest/gtest.h>
#include <math.h>

/**
 * Testing base class.
 * Used to keep constant state for testing all cases.
 */
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

// UpdateFilterCoeffs Testing
/**
 * Test for valid input parameters case
 */
TEST_F(FilterTest, updateFilterCoeffsAcceptsValidCoeffs)
{
    EXPECT_TRUE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

/**
 * Test for checking if length guard works if length of coefficients is less than 1
 */
TEST_F(FilterTest, updateFilterCoeffsLengthCheck)
{
    coeffs.clear();
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

/**
 * Test for checking if the order guard works when order is less than 1 (Minimum filter order allowed)
 */
TEST_F(FilterTest, updateFilterCoeffsOrderCheckLessThan)
{
    coeffs.at(0) = 0;
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

/**
 * Test for checking if the order guard works when order is greater than 20 (Max filter order allowed)
 */
TEST_F(FilterTest, updateFilterCoeffsOrderCheckGreaterThan)
{
    coeffs.at(0) = 21;
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

/**
 * Test for checking that expected length guard prevents wrong provided length from being used
 */
TEST_F(FilterTest, updateFilterCoeffsUnexpectedFilterLength)
{
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size() - 1));
}

/**
 * Test for checking if finite value guard prevents Not A Number (NAN) values
 */
TEST_F(FilterTest, updateFilterCoeffsCheckFinite)
{
    coeffs.at(1) = NAN;
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

/**
 * Test for checking if finite value guard prevents Infinite values
 */
TEST_F(FilterTest, updateFilterCoeffsCheckFinite)
{
    coeffs.at(1) = INFINITY;
    EXPECT_FALSE(updateFilterCoeffs(f, coeffs.data(), coeffs.size()));
}

// PushSample Testing
/**
 * Test for checking that buffer displays ready and resets sample index after filling the buffer
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

// ClampMagnitude Testing
/**
 * Test for checking values get clamped to minimum value when less than 0
 */
TEST_F(FilterTest, clampMagnitudeToMinWhenLess)
{
    float val = clampMagnitude(-256.0f);
    EXPECT_FLOAT_EQ(val, 0.0f);
}

/**
 * Test for checking values get clamped to to minimum value when equal to 0 (min value)
 */
TEST_F(FilterTest, clampMagnitudeToMin)
{
    float val = clampMagnitude(0.0f);
    EXPECT_FLOAT_EQ(val, 0.0f);
}

/**
 * Test for checking values get clamped to max value when equal to 65535 (max value)
 */
TEST_F(FilterTest, clampMagnitudeToMax)
{
    float val = clampMagnitude(65535.0f);
    EXPECT_FLOAT_EQ(val, 65535.0f);
}

/**
 * Test for checking values get clamped to max when value greater than the max value
 */
TEST_F(FilterTest, clampMagnitudeToMaxWhenGreater)
{
    float val = clampMagnitude(65600.0f);
    EXPECT_FLOAT_EQ(val, 65535.0f);
}

/**
 * Test for checking values remain the same when entered value is between the min and max values
 */
TEST_F(FilterTest, clampMagnitudeNormalInput)
{
    float val = clampMagnitude(23765.0f);
    EXPECT_TRUE((val >= 0.0f) && (val <= 65535.0f));
}

/**
 * Test for checking large values get clamped to the max value
 */
TEST_F(FilterTest, clampMagnitudeToMaxLargeValue)
{
    float val = clampMagnitude(1e10);
    EXPECT_FLOAT_EQ(val, 65535.0f);
}

// Entrypoint function
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}