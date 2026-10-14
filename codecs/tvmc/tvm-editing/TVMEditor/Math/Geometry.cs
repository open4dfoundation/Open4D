using MathNet.Numerics.LinearAlgebra;
using System.Numerics;

namespace TVMEditor.Math
{
    public class Geometry
    {
        public static Quaternion QuaternionFromMatrix(Matrix4x4 m)
        {
            // Adapted from: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/index.htm
            Quaternion q = new Quaternion();
            q.W = (float)System.Math.Sqrt(System.Math.Max(0, 1 + m.M11 + m.M22 + m.M33)) / 2;
            q.X = (float)System.Math.Sqrt(System.Math.Max(0, 1 + m.M11 - m.M22 - m.M33)) / 2;
            q.Y = (float)System.Math.Sqrt(System.Math.Max(0, 1 - m.M11 + m.M22 - m.M33)) / 2;
            q.Z = (float)System.Math.Sqrt(System.Math.Max(0, 1 - m.M11 - m.M22 + m.M33)) / 2;
            q.X *= System.Math.Sign(q.X * (m.M32 - m.M23));
            q.Y *= System.Math.Sign(q.Y * (m.M13 - m.M31));
            q.Z *= System.Math.Sign(q.Z * (m.M21 - m.M12));
            return q;
        }

        public static Quaternion QuaternionFromMatrix(Matrix<float> m)
        {
            // Adapted from: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/index.htm
            Quaternion q = new Quaternion();
            q.W = (float)System.Math.Sqrt(System.Math.Max(0, 1 + m[0, 0] + m[1, 1] + m[2, 2])) / 2;
            q.X = (float)System.Math.Sqrt(System.Math.Max(0, 1 + m[0, 0] - m[1, 1] - m[2, 2])) / 2;
            q.Y = (float)System.Math.Sqrt(System.Math.Max(0, 1 - m[0, 0] + m[1, 1] - m[2, 2])) / 2;
            q.Z = (float)System.Math.Sqrt(System.Math.Max(0, 1 - m[0, 0] - m[1, 1] + m[2, 2])) / 2;
            q.X *= System.Math.Sign(q.X * (m[2, 1] - m[1, 2]));
            q.Y *= System.Math.Sign(q.Y * (m[0, 2] - m[2, 0]));
            q.Z *= System.Math.Sign(q.Z * (m[1, 0] - m[0, 1]));
            return q;
        }
    }

}
