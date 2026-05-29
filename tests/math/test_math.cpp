#include "next/math/math.h"
#include <gtest/gtest.h>
#include <cmath>

namespace Next {
namespace testing {

using ::testing::Test;

class MathTest : public Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    const float EPSILON = 0.0001f;
};

// ============ Vec3 Tests ============

// Test Vec3 default constructor
TEST_F(MathTest, Vec3DefaultConstructor) {
    Vec3 v;

    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

// Test Vec3 parameterized constructor
TEST_F(MathTest, Vec3Constructor) {
    Vec3 v(1.0f, 2.0f, 3.0f);

    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

// Test Vec3 addition
TEST_F(MathTest, Vec3Addition) {
    Vec3 v1(1.0f, 2.0f, 3.0f);
    Vec3 v2(4.0f, 5.0f, 6.0f);

    Vec3 result = v1 + v2;

    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 7.0f);
    EXPECT_FLOAT_EQ(result.z, 9.0f);
}

// Test Vec3 subtraction
TEST_F(MathTest, Vec3Subtraction) {
    Vec3 v1(5.0f, 7.0f, 9.0f);
    Vec3 v2(4.0f, 5.0f, 6.0f);

    Vec3 result = v1 - v2;

    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

// Test Vec3 scalar multiplication
TEST_F(MathTest, Vec3ScalarMultiplication) {
    Vec3 v(1.0f, 2.0f, 3.0f);

    Vec3 result = v * 2.5f;

    EXPECT_FLOAT_EQ(result.x, 2.5f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 7.5f);
}

// Test Vec3 dot product
TEST_F(MathTest, Vec3DotProduct) {
    Vec3 v1(1.0f, 2.0f, 3.0f);
    Vec3 v2(4.0f, 5.0f, 6.0f);

    float result = v1.Dot(v2);

    EXPECT_FLOAT_EQ(result, 32.0f);  // 1*4 + 2*5 + 3*6 = 32
}

// Test Vec3 dot product with perpendicular vectors
TEST_F(MathTest, Vec3DotProductPerpendicular) {
    Vec3 v1(1.0f, 0.0f, 0.0f);
    Vec3 v2(0.0f, 1.0f, 0.0f);

    float result = v1.Dot(v2);

    EXPECT_NEAR(result, 0.0f, EPSILON);
}

// Test Vec3 cross product
TEST_F(MathTest, Vec3CrossProduct) {
    Vec3 v1(1.0f, 0.0f, 0.0f);
    Vec3 v2(0.0f, 1.0f, 0.0f);

    Vec3 result = v1.Cross(v2);

    EXPECT_NEAR(result.x, 0.0f, EPSILON);
    EXPECT_NEAR(result.y, 0.0f, EPSILON);
    EXPECT_NEAR(result.z, 1.0f, EPSILON);
}

// Test Vec3 cross product anti-commutativity
TEST_F(MathTest, Vec3CrossProductAntiCommutative) {
    Vec3 v1(1.0f, 2.0f, 3.0f);
    Vec3 v2(4.0f, 5.0f, 6.0f);

    Vec3 result1 = v1.Cross(v2);
    Vec3 result2 = v2.Cross(v1);

    EXPECT_NEAR(result1.x, -result2.x, EPSILON);
    EXPECT_NEAR(result1.y, -result2.y, EPSILON);
    EXPECT_NEAR(result1.z, -result2.z, EPSILON);
}

// Test Vec3 length
TEST_F(MathTest, Vec3Length) {
    Vec3 v(3.0f, 4.0f, 0.0f);

    float length = v.Length();

    EXPECT_NEAR(length, 5.0f, EPSILON);
}

// Test Vec3 normalize
TEST_F(MathTest, Vec3Normalize) {
    Vec3 v(3.0f, 4.0f, 0.0f);

    Vec3 normalized = v.Normalize();

    EXPECT_NEAR(normalized.Length(), 1.0f, EPSILON);
    EXPECT_NEAR(normalized.x, 0.6f, EPSILON);
    EXPECT_NEAR(normalized.y, 0.8f, EPSILON);
}

// Test Vec3 normalize zero vector
TEST_F(MathTest, Vec3NormalizeZero) {
    Vec3 v(0.0f, 0.0f, 0.0f);

    Vec3 normalized = v.Normalize();

    EXPECT_NEAR(normalized.x, 0.0f, EPSILON);
    EXPECT_NEAR(normalized.y, 0.0f, EPSILON);
    EXPECT_NEAR(normalized.z, 0.0f, EPSILON);
}

// ============ Mat4 Tests ============

// Test Mat4 default constructor (identity)
TEST_F(MathTest, Mat4DefaultConstructor) {
    Mat4 m;

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            EXPECT_NEAR(m(i, j), expected, EPSILON);
        }
    }
}

// Test Mat4 identity static method
TEST_F(MathTest, Mat4Identity) {
    Mat4 m = Mat4::Identity();

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            EXPECT_NEAR(m(i, j), expected, EPSILON);
        }
    }
}

// Test Mat4 element access
TEST_F(MathTest, Mat4ElementAccess) {
    Mat4 m;

    m(0, 0) = 5.0f;
    m(1, 2) = 3.0f;
    m(3, 3) = 7.0f;

    EXPECT_FLOAT_EQ(m(0, 0), 5.0f);
    EXPECT_FLOAT_EQ(m(1, 2), 3.0f);
    EXPECT_FLOAT_EQ(m(3, 3), 7.0f);
}

// Test Mat4 multiplication with identity
TEST_F(MathTest, Mat4MultiplicationIdentity) {
    Mat4 m1;
    Mat4 m2 = Mat4::Identity();

    Mat4 result = m1 * m2;

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(result.m[i], m1.m[i], EPSILON);
    }
}

// Test Mat4 rotation X
TEST_F(MathTest, Mat4RotationX) {
    Mat4 rot = Mat4::RotateX(3.14159f / 2.0f);  // 90 degrees

    // Rotate (0, 1, 0) around X axis should give (0, 0, 1)
    Vec3 v(0.0f, 1.0f, 0.0f);
    Vec3 result(
        rot(0, 0) * v.x + rot(0, 1) * v.y + rot(0, 2) * v.z,
        rot(1, 0) * v.x + rot(1, 1) * v.y + rot(1, 2) * v.z,
        rot(2, 0) * v.x + rot(2, 1) * v.y + rot(2, 2) * v.z
    );

    EXPECT_NEAR(result.x, 0.0f, EPSILON);
    EXPECT_NEAR(result.y, 0.0f, 0.1f);  // cos(90°) ≈ 0
    EXPECT_NEAR(result.z, 1.0f, 0.1f);  // sin(90°) ≈ 1
}

// Test Mat4 rotation Y
TEST_F(MathTest, Mat4RotationY) {
    Mat4 rot = Mat4::RotateY(3.14159f / 2.0f);  // 90 degrees

    // Rotate (1, 0, 0) around Y axis should give (0, 0, -1)
    Vec3 v(1.0f, 0.0f, 0.0f);
    Vec3 result(
        rot(0, 0) * v.x + rot(0, 1) * v.y + rot(0, 2) * v.z,
        rot(1, 0) * v.x + rot(1, 1) * v.y + rot(1, 2) * v.z,
        rot(2, 0) * v.x + rot(2, 1) * v.y + rot(2, 2) * v.z
    );

    EXPECT_NEAR(result.x, 0.0f, 0.1f);
    EXPECT_NEAR(result.y, 0.0f, EPSILON);
    EXPECT_NEAR(result.z, -1.0f, 0.1f);
}

// Test Mat4 rotation Z
TEST_F(MathTest, Mat4RotationZ) {
    Mat4 rot = Mat4::RotateZ(3.14159f / 2.0f);  // 90 degrees

    // Rotate (1, 0, 0) around Z axis should give (0, 1, 0)
    Vec3 v(1.0f, 0.0f, 0.0f);
    Vec3 result(
        rot(0, 0) * v.x + rot(0, 1) * v.y + rot(0, 2) * v.z,
        rot(1, 0) * v.x + rot(1, 1) * v.y + rot(1, 2) * v.z,
        rot(2, 0) * v.x + rot(2, 1) * v.y + rot(2, 2) * v.z
    );

    EXPECT_NEAR(result.x, 0.0f, 0.1f);
    EXPECT_NEAR(result.y, 1.0f, 0.1f);
    EXPECT_NEAR(result.z, 0.0f, EPSILON);
}

// Test Mat4 translation
TEST_F(MathTest, Mat4Translation) {
    Mat4 t = Mat4::Translation(5.0f, 10.0f, 15.0f);

    EXPECT_NEAR(t(0, 3), 5.0f, EPSILON);
    EXPECT_NEAR(t(1, 3), 10.0f, EPSILON);
    EXPECT_NEAR(t(2, 3), 15.0f, EPSILON);

    // Diagonal should be 1.0
    EXPECT_NEAR(t(0, 0), 1.0f, EPSILON);
    EXPECT_NEAR(t(1, 1), 1.0f, EPSILON);
    EXPECT_NEAR(t(2, 2), 1.0f, EPSILON);
    EXPECT_NEAR(t(3, 3), 1.0f, EPSILON);
}

// Test Mat4 scale
TEST_F(MathTest, Mat4Scale) {
    Mat4 s = Mat4::Scale(2.0f, 3.0f, 4.0f);

    EXPECT_NEAR(s(0, 0), 2.0f, EPSILON);
    EXPECT_NEAR(s(1, 1), 3.0f, EPSILON);
    EXPECT_NEAR(s(2, 2), 4.0f, EPSILON);
    EXPECT_NEAR(s(3, 3), 1.0f, EPSILON);
}

// Test Mat4 perspective projection
TEST_F(MathTest, Mat4Perspective) {
    float fov = 1.047f;  // 60 degrees in radians
    float aspect = 16.0f / 9.0f;
    float nearZ = 0.1f;
    float farZ = 100.0f;

    Mat4 proj = Mat4::Perspective(fov, aspect, nearZ, farZ);

    // Check some basic properties
    EXPECT_GT(proj(0, 0), 0.0f);
    EXPECT_GT(proj(1, 1), 0.0f);
    EXPECT_GT(proj(2, 2), 0.0f);
    EXPECT_NEAR(proj(2, 3), 1.0f, EPSILON);
    EXPECT_NEAR(proj(3, 3), 0.0f, EPSILON);
}

// Test Mat4 look at
TEST_F(MathTest, Mat4LookAt) {
    Vec3 eye(0.0f, 0.0f, 5.0f);
    Vec3 target(0.0f, 0.0f, 0.0f);
    Vec3 up(0.0f, 1.0f, 0.0f);

    Mat4 view = Mat4::LookAt(eye, target, up);

    // Looking from (0,0,5) towards origin with up (0,1,0)
    // Should create a view matrix with specific properties
    EXPECT_NEAR(view(3, 3), 1.0f, EPSILON);
}

// Test Mat4 transpose
TEST_F(MathTest, Mat4Transpose) {
    Mat4 m;
    m(0, 1) = 1.0f;
    m(0, 2) = 2.0f;
    m(1, 0) = 3.0f;
    m(2, 0) = 4.0f;

    Mat4 transposed = m.Transpose();

    EXPECT_NEAR(transposed(1, 0), 1.0f, EPSILON);
    EXPECT_NEAR(transposed(2, 0), 2.0f, EPSILON);
    EXPECT_NEAR(transposed(0, 1), 3.0f, EPSILON);
    EXPECT_NEAR(transposed(0, 2), 4.0f, EPSILON);
}

// Test Mat4 transpose of transpose is original
TEST_F(MathTest, Mat4DoubleTranspose) {
    Mat4 m;
    m(0, 1) = 1.0f;
    m(1, 2) = 2.0f;
    m(2, 3) = 3.0f;

    Mat4 doubleTransposed = m.Transpose().Transpose();

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_NEAR(doubleTransposed(i, j), m(i, j), EPSILON);
        }
    }
}

// Test Mat4 combined transformations
TEST_F(MathTest, Mat4CombinedTransformations) {
    Mat4 t = Mat4::Translation(1.0f, 2.0f, 3.0f);
    Mat4 r = Mat4::RotateY(0.5f);
    Mat4 s = Mat4::Scale(2.0f, 2.0f, 2.0f);

    // Combined: Scale * Rotate * Translate
    Mat4 combined = s * r * t;

    // Should not crash and produce valid matrix
    EXPECT_TRUE(std::isfinite(combined(0, 0)));
    EXPECT_TRUE(std::isfinite(combined(1, 1)));
    EXPECT_TRUE(std::isfinite(combined(2, 2)));
}

// Test Vec3 with transform
TEST_F(MathTest, Vec3WithTransform) {
    Vec3 v(1.0f, 0.0f, 0.0f);
    Mat4 t = Mat4::Translation(5.0f, 10.0f, 15.0f);

    // Apply translation
    Vec3 result(
        t(0, 0) * v.x + t(0, 1) * v.y + t(0, 2) * v.z + t(0, 3),
        t(1, 0) * v.x + t(1, 1) * v.y + t(1, 2) * v.z + t(1, 3),
        t(2, 0) * v.x + t(2, 1) * v.y + t(2, 2) * v.z + t(2, 3)
    );

    EXPECT_NEAR(result.x, 6.0f, EPSILON);
    EXPECT_NEAR(result.y, 10.0f, EPSILON);
    EXPECT_NEAR(result.z, 15.0f, EPSILON);
}

} // namespace testing
} // namespace Next

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
