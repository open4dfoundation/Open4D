using System;
using System.Numerics;

namespace TVMEditor.Structures
{
    public struct DualQuaternion
    {
        public Quaternion Real;
        public Quaternion Dual;

        public DualQuaternion(Quaternion real, Quaternion dual)
        {
            Real = real;
            Dual = dual;
        }

        public DualQuaternion Copy()
        {
            return new DualQuaternion(
                new Quaternion(Real.X, Real.Y, Real.Z, Real.W),
                new Quaternion(Dual.X, Dual.Y, Dual.Z, Dual.W)
                );
        }

        public DualQuaternion Conjugate()
        {
            return Conjugate(this);
        }

        public float Norm()
        {
            return (float) System.Math.Sqrt(Quaternion.Dot(this.Real, this.Real));
        }

        public DualQuaternion Normalize()
        {
            return this * (1 / Norm());
        }

        public Vector3 TranslationVector
        {
            get
            {
                var Conjugate = new Quaternion(-Real.X, -Real.Y, -Real.Z, Real.W);
                var TQ = Dual * Conjugate;
                return new Vector3(
                    TQ.X + TQ.X,
                    TQ.Y + TQ.Y,
                    TQ.Z + TQ.Z
                );
            }
        }

        public Vector3 Transform(Vector3 p)
        { 
            return TranslationVector + Vector3.Transform(p, Real);
        }

        public void AddTranslation(Vector3 p)
        {
            var trn = Translation(p);
            var prod = this * trn;
            Real = prod.Real;
            Dual = prod.Dual;
        }

        public void AddRotation(Quaternion r)
        {
            var rot = Rotation(r);
            var prod = this * rot;
            Real = prod.Real;
            Dual = prod.Dual;
        }

        public Vector3 GetTranslation()
        {
            return new Vector3(Dual.X * 2, Dual.Y * 2, Dual.Z * 2);
        }

        public string SerializeCSV()
        {
            return $"{Real.X};{Real.Y};{Real.Z};{Real.W};{Dual.X};{Dual.Y};{Dual.Z};{Dual.W}";
        }

        public static DualQuaternion Rotation(Quaternion rotation)
        {
            return new DualQuaternion(rotation, new Quaternion(0, 0, 0, 0));
        }

        public static DualQuaternion Translation(Vector3 p)
        {
            var r = new Quaternion(0, 0, 0, 1);
            var d = new Quaternion(p.X / 2, p.Y / 2, p.Z / 2, 0);
            return new DualQuaternion(r, d);
        }

        public static float[] ThreeCompMult2(Quaternion a, Quaternion b)
        {
            return new float[]
            {
            a.W * b.X - a.X * b.W + a.Y * b.Z - a.Z * b.Y,
            a.W * b.Y - a.X * b.Z - a.Y * b.W + a.Z * b.X,
            a.W * b.Z - a.X * b.Y - a.Y * b.X - a.Z * b.W
            };
        }

        public static float[] ThreeCompMult3(Quaternion a, Quaternion b)
        {
            var m2 = ThreeCompMult2(a, b);
            return new float[]
            {
            -m2[0], -m2[1], -m2[2]
            };
        }

        public static Vector3 DualMult(DualQuaternion a, DualQuaternion b)
        {
            var d1 = ThreeCompMult2(a.Real, b.Dual);
            var d2 = ThreeCompMult3(a.Dual, b.Real);
            return new Vector3(d1[0] + d2[0], d1[1] + d2[1], d1[2] + d2[2]);
        }

        public static DualQuaternion Add(DualQuaternion q1, DualQuaternion q2)
        {
            return new DualQuaternion(Add(q1.Real, q2.Real), Add(q1.Dual, q2.Dual));
        }

        public static Quaternion Add(Quaternion q1, Quaternion q2)
        {
            return new Quaternion(q1.X + q2.X, q1.Y + q2.Y, q1.Z + q2.Z, q1.W + q2.W);
        }

        public static DualQuaternion Sub(DualQuaternion q1, DualQuaternion q2)
        {
            return new DualQuaternion(Sub(q1.Real, q2.Real), Sub(q1.Dual, q2.Dual));
        }

        public static Quaternion Sub(Quaternion q1, Quaternion q2)
        {
            return new Quaternion(q1.X - q2.X, q1.Y - q2.Y, q1.Z - q2.Z, q1.W - q2.W);
        }

        public static DualQuaternion Multiply(DualQuaternion q1, DualQuaternion q2)
        {
            return new DualQuaternion(q1.Real * q2.Real, Add(q1.Real * q2.Dual, q1.Dual * q2.Real));
        }

        public static DualQuaternion Multiply(DualQuaternion q, float s)
        {
            return new DualQuaternion(Multiply(q.Real, s), Multiply(q.Dual, s));
        }

        public static Quaternion Multiply(Quaternion q, float s)
        {
            return new Quaternion(q.X * s, q.Y * s, q.Z * s, q.W * s);
        }

        public static DualQuaternion Conjugate(DualQuaternion q)
        {
            return new DualQuaternion(Conjugate(q.Real), Conjugate(q.Dual));
        }

        public static Quaternion Conjugate(Quaternion q)
        {
            return new Quaternion(-q.X, -q.Y, -q.Z, q.W);
        }

        public static DualQuaternion Identity()
        {
            return new DualQuaternion(new Quaternion(0, 0, 0, 1), new Quaternion(0, 0, 0, 0));
        }

        public static DualQuaternion Zero()
        {
            return new DualQuaternion(new Quaternion(0, 0, 0, 0), new Quaternion(0, 0, 0, 0));
        }

        public static DualQuaternion operator +(DualQuaternion q1, DualQuaternion q2)
        {
            return Add(q1, q2);
        }

        public static DualQuaternion operator *(DualQuaternion q, float s)
        {
            return Multiply(q, s);
        }

        public static DualQuaternion operator *(float s, DualQuaternion q)
        {
            return Multiply(q, s);
        }

        public static DualQuaternion operator *(DualQuaternion q1, DualQuaternion q2)
        {
            return Multiply(q1, q2);
        }

        public static DualQuaternion operator -(DualQuaternion q1, DualQuaternion q2)
        {
            return Sub(q1, q2);
        }

        public static DualQuaternion operator /(DualQuaternion q, float s)
        {
            return Multiply(q, 1f / s);
        }

        public Matrix4x4 ToMatrix()
        {
            return new Matrix4x4
            {
                // Rotation
                M11 = Real.W * Real.W + Real.X * Real.X - Real.Y * Real.Y - Real.Z * Real.Z,
                M12 = 2 * Real.X * Real.Y - 2 * Real.W * Real.Z,
                M13 = 2 * Real.X * Real.Z + 2 * Real.W * Real.Y,
                M21 = 2 * Real.X * Real.Y + 2 * Real.W * Real.Z,
                M22 = Real.W * Real.W - Real.X * Real.X + Real.Y * Real.Y - Real.Z * Real.Z,
                M23 = 2 * Real.Y * Real.Z - 2 * Real.W * Real.X,
                M31 = 2 * Real.X * Real.Z - 2 * Real.W * Real.Y,
                M32 = 2 * Real.Y * Real.Z + 2 * Real.W * Real.X,
                M33 = Real.W * Real.W - Real.X * Real.X - Real.Y * Real.Y + Real.Z * Real.Z,
                // Translation
                M14 = Dual.X,
                M24 = Dual.Y,
                M34 = Dual.Y,
                M44 = Dual.W
            };
        }
    }
}
