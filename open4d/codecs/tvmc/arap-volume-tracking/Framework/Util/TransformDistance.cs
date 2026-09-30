//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using MathNet.Numerics.LinearAlgebra;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

using SysNumVector4 = System.Numerics.Vector4;

namespace Framework
{
    public class TransformDistance
    {
        readonly Vector<float> qSum;
        readonly Matrix<float> qqtSum;
        readonly float qtqSum;
        readonly int vq;
        //public IEnumerable<SysNumVector4> q;

        public TransformDistance(IEnumerable<SysNumVector4> q)
        {
            // List<SysNumVector4> q = new List<SysNumVector4>();
            // q.Add(new SysNumVector4(1, 2, 3, 1));

            qtqSum = QTQSum(q);
            qSum = QSum(q);
            qqtSum = QQTSum(q);
            vq = q.Count();

            // Console.WriteLine($"QTQ {qtqSum}");
            // Console.WriteLine($"Q {qSum}");
            // Console.WriteLine($"QQT {qqtSum}");
        }

        //static private SysNumVector4[] coordFrame = new SysNumVector4[]
        //{
        //    SysNumVector4.Zero,
        //    //SysNumVector4.UnitX * -1f,
        //    //SysNumVector4.UnitX,
        //    //SysNumVector4.UnitY * -1f,
        //    //SysNumVector4.UnitY,
        //    //SysNumVector4.UnitZ * -1f,
        //    //SysNumVector4.UnitZ
        //};

        //static private SysNumVector4[] GetLocalFrame(SysNumVector4 pos, float delta)
        //{
        //    SysNumVector4[] res = new SysNumVector4[coordFrame.Length];

        //    for (int i = 0; i < coordFrame.Length; i++)
        //    {
        //        res[i] = coordFrame[i] * delta + pos;
        //    }

        //    return res;
        //}

        //public TransformDistance(SysNumVector4 point) : this(GetLocalFrame(point, 0.01f))
        //{

        //}

        public static void TestTransformDistance()
        {
            Random r = new Random();
            Stopwatch sw = new Stopwatch();

            List<SysNumVector4> qList = new List<SysNumVector4>();

            for (int i = 0; i < 1000000; i++)
                qList.Add(new SysNumVector4((float)r.NextDouble(), (float)r.NextDouble(), (float)r.NextDouble(), 1.0f));

            var tdw = new TransformDistance(qList);

            float rxa = 0.45f;

            Matrix<float> R1 = CreateRotationX(rxa);
            Matrix<float> R2 = CreateRotationX(rxa);

            Vector<float> t1 = CreateVector(3f, 0.9f, 0.7f);
            Vector<float> t2 = CreateVector(3f, 0.9f, 0.7f);

            Console.WriteLine(new Transform(R1, t1));

            sw.Start();
            var dist1 = tdw.GetTransformDistance(R1, R2, t1, t2);
            var time1 = sw.ElapsedMilliseconds;

            Console.WriteLine($"Method 1: {dist1}, {time1} ms");
        }

        public float GetTransformDistance(Matrix<float> R1, Matrix<float> R2, Vector<float> t1, Vector<float> t2)
        {
            var dif = t1 - t2;
            var greenTerm = 2 * (dif).DotProduct(R1 * qSum);
            var blueTerm = -2 * (dif).DotProduct(R2 * qSum);
            var blackTerm = vq * (dif.DotProduct(dif));
            float pinkTerm = FrobeniusProduct(2f * R1.Transpose() * R2, qqtSum);

            //var tmp = (greenTerm + blueTerm + blackTerm);
            var res = 2f * qtqSum + greenTerm + blueTerm + blackTerm - pinkTerm;

            return MathF.Abs(res / vq);
        }

        private static float QTQSum(IEnumerable<SysNumVector4> qList)
        {
            float sum = 0;

            foreach (var q in qList)
            {
                var qi = CreateVector(q);
                sum += qi * qi;
            }

            return sum;
        }

        private static Vector<float> QSum(IEnumerable<SysNumVector4> qList)
        {
            var sum = Vector<float>.Build.Dense(3, 1);

            foreach (var q in qList)
            {
                sum[0] += q.X;
                sum[1] += q.Y;
                sum[2] += q.Z;
            }

            return sum;
        }

        private static Matrix<float> QQTSum(IEnumerable<SysNumVector4> qList)
        {
            var sum = Matrix<float>.Build.Dense(3, 3);

            foreach (var q in qList)
            {
                var qi = CreateVector(q).ToColumnMatrix();
                sum += qi * qi.Transpose();
            }

            return sum;
        }

        private static float FrobeniusProduct(Matrix<float> m1, Matrix<float> m2)
        {
            float sum = 0;

            for (int i = 0; i < m1.RowCount; i++)
                for (int j = 0; j < m1.ColumnCount; j++)
                    sum += m1[i, j] * m2[i, j];

            return sum;
        }


        private static Vector<float> CreateVector(float x, float y, float z)
        {
            return Vector<float>.Build.DenseOfArray(new float[] { x, y, z });
        }

        private static Vector<float> CreateVector(SysNumVector4 v)
        {
            return Vector<float>.Build.DenseOfArray(new float[] { v.X, v.Y, v.Z });
        }

        private static Matrix<float> CreateRotationX(float rxa)
        {
            float c = MathF.Cos(rxa);
            float s = MathF.Sin(rxa);

            return Matrix<float>.Build.DenseOfArray(new float[,]
            {
                { 1, 0,  0 },
                { 0, c, -s },
                { 0, s,  c },
            });
        }

    }
}
