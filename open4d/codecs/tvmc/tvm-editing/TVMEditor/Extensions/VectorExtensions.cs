using MathNet.Numerics.LinearAlgebra.Single;
using System;
using System.Numerics;

namespace TVMEditor.Extensions
{
    public static class VectorExtensions
    {
        public static float GetDimension(this Vector3 vector, int dimension)
        {
            switch (dimension)
            {
                case 0:
                    return vector.X;
                case 1:
                    return vector.Y;
                case 2:
                    return vector.Z;
            }

            throw new Exception("Invalid dimension for Vector3");
        }

        public static void SetDimension(this Vector3 vector, int dimension, float value)
        {
            switch (dimension)
            {
                case 0:
                    vector.X = value;
                    break;
                case 1:
                    vector.Y = value;
                    break;
                case 2:
                    vector.Z = value;
                    break;
                default:
                    throw new Exception("Invalid dimension for Vector3");
            }
        }

        public static Vector3 ToVector3(this DenseVector vector)
        {
            return new Vector3(vector[0], vector[1], vector[2]);
        }

        public static Matrix4x4 MultiplyByScalar(this Matrix4x4 m, float x)
        {
            return new Matrix4x4
            {
                M11 = x * m.M11,
                M12 = x * m.M12,
                M13 = x * m.M13,
                M14 = x * m.M14,
                M21 = x * m.M21,
                M22 = x * m.M22,
                M23 = x * m.M23,
                M24 = x * m.M24,
                M31 = x * m.M31,
                M32 = x * m.M32,
                M33 = x * m.M33,
                M34 = x * m.M34,
                M41 = x * m.M41,
                M42 = x * m.M42,
                M43 = x * m.M43,
                M44 = x * m.M44
            };
        }

        public static Matrix4x4 AddMatrix(this Matrix4x4 m1, Matrix4x4 m2)
        {
            return new Matrix4x4
            {
                M11 = m1.M11 + m2.M11,
                M12 = m1.M12 + m2.M12,
                M13 = m1.M13 + m2.M13,
                M14 = m1.M14 + m2.M14,
                M21 = m1.M21 + m2.M21,
                M22 = m1.M22 + m2.M22,
                M23 = m1.M23 + m2.M23,
                M24 = m1.M24 + m2.M24,
                M31 = m1.M31 + m2.M31,
                M32 = m1.M32 + m2.M32,
                M33 = m1.M33 + m2.M33,
                M34 = m1.M34 + m2.M34,
                M41 = m1.M41 + m2.M41,
                M42 = m1.M42 + m2.M42,
                M43 = m1.M43 + m2.M43,
                M44 = m1.M44 + m2.M44
            };
        }

        public static Vector4[] ToVector4(this Vector3[] vertices)
        {
            var mesh = new Vector4[vertices.Length];

            for (int i = 0; i < vertices.Length; i++)
            {
                var v = vertices[i];
                mesh[i] = new Vector4(v.X, v.Y, v.Z, 1);
            }

            return mesh;
        }

        public static Matrix4x4 Zero4x4Matrix()
        {
            return new Matrix4x4
            {
                M11 = 0,
                M12 = 0,
                M13 = 0,
                M14 = 0,
                M21 = 0,
                M22 = 0,
                M23 = 0,
                M24 = 0,
                M31 = 0,
                M32 = 0,
                M33 = 0,
                M34 = 0,
                M41 = 0,
                M42 = 0,
                M43 = 0,
                M44 = 0
            };
        }

        public static Vector4 GetColumn(this Matrix4x4 m, int col)
        {
            if (col == 0)
            {
                return new Vector4(m.M11, m.M21, m.M31, m.M41);
            }
            else if (col == 1)
            {
                return new Vector4(m.M12, m.M22, m.M32, m.M42);
            }
            else if (col == 2)
            {
                return new Vector4(m.M13, m.M23, m.M33, m.M43);
            }
            else if (col == 3)
            {
                return new Vector4(m.M14, m.M24, m.M34, m.M44);
            }

            throw new IndexOutOfRangeException();
        }

        public static Vector4 GetRow(this Matrix4x4 m, int col)
        {
            if (col == 0)
            {
                return new Vector4(m.M11, m.M12, m.M13, m.M14);
            }
            else if (col == 1)
            {
                return new Vector4(m.M21, m.M22, m.M23, m.M24);
            }
            else if (col == 2)
            {
                return new Vector4(m.M31, m.M32, m.M33, m.M34);
            }
            else if (col == 3)
            {
                return new Vector4(m.M41, m.M42, m.M43, m.M44);
            }

            throw new IndexOutOfRangeException();
        }
    }
}
