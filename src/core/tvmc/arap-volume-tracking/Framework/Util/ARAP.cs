//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using MathNet.Numerics.LinearAlgebra;
using System;
using System.Collections.Generic;
using System.Numerics;
using System.Threading.Tasks;
using MathNetVector = MathNet.Numerics.LinearAlgebra.Vector<float>;

namespace Framework
{
    public static class ARAP
    {
        //private static readonly object LockObject = new object();
        private static readonly Matrix<float> flip = Matrix<float>.Build.DenseOfDiagonalArray(new float[] { 1, 1, -1 });
        private static readonly Matrix<float> Identity = Matrix<float>.Build.DenseIdentity(3);

        public static Transform GetTransformUnweighted(Vector4[] pts1, Vector4[] pts2, List<int> ind)
        {
            int n = ind.Count;

            Vector4[] a = new Vector4[n];
            Vector4[] b = new Vector4[n];
            Vector4 cA = new Vector4();
            Vector4 cB = new Vector4();

            //Compute centroids
            for (int i = 0; i < n; i++)
            {
                int j = ind[i];
                a[i] = pts1[j];
                b[i] = pts2[j];

                cA += a[i];
                cB += b[i];
            }

            cA /= n;
            cB /= n;

            //Translation vector is determined from weighted centroids
            MathNetVector cAV = MathNetVector.Build.Dense(3);
            MathNetVector cBV = MathNetVector.Build.Dense(3);
            cAV[0] = cA.X;
            cAV[1] = cA.Y;
            cAV[2] = cA.Z;

            cBV[0] = cB.X;
            cBV[1] = cB.Y;
            cBV[2] = cB.Z;

            for (int i = 0; i < n; i++)
            {
                a[i] -= cA;
                b[i] -= cB;
            }

            //Create matrices
            Matrix<float> mA = Matrix<float>.Build.Dense(n, 3);
            Matrix<float> mB = Matrix<float>.Build.Dense(n, 3);

            for (int i = 0; i < n; i++)
            {
                mA[i, 0] = a[i].X;
                mA[i, 1] = a[i].Y;
                mA[i, 2] = a[i].Z;

                mB[i, 0] = b[i].X;
                mB[i, 1] = b[i].Y;
                mB[i, 2] = b[i].Z;
            }

            var h = mA.Transpose() * mB;
            var svd = h.Svd();


            var R = svd.VT.Transpose() * svd.U.Transpose();

            if (R.Determinant() < 0)
            {
                //Correct the sign of the rotation
                R = svd.VT.Transpose() * flip * svd.U.Transpose();
            }

            return new Transform(R, cAV, cBV);
        }

        public static Transform GetTransform(Vector4[] a, Vector4[] b, float[] weights)
        {
            Vector4 cA = new Vector4();
            Vector4 cB = new Vector4();
            float wSum = 0;
            int n = a.Length;

            //Compute weighted centroids
            for (int i = 0; i < n; i++)
            {
                wSum += weights[i];
                cA += weights[i] * a[i];
                cB += weights[i] * b[i];
            }

            cA /= wSum;
            cB /= wSum;

            //Translation vector is determined from weighted centroids
            MathNetVector cAV = MathNetVector.Build.Dense(3);
            MathNetVector cBV = MathNetVector.Build.Dense(3);
            cAV[0] = cA.X;
            cAV[1] = cA.Y;
            cAV[2] = cA.Z;

            cBV[0] = cB.X;
            cBV[1] = cB.Y;
            cBV[2] = cB.Z;

            //No rotation can be determined for isolated point
            if (n < 2)
            {
                return new Transform(Identity, cAV, cBV);
            }

            for (int i = 0; i < n; i++)
            {
                a[i] -= cA;
                b[i] -= cB;
            }

            //Create matrices
            Matrix<float> mA = Matrix<float>.Build.Dense(n, 3);
            Matrix<float> mB = Matrix<float>.Build.Dense(n, 3);

            for (int i = 0; i < n; i++)
            {
                mA[i, 0] = a[i].X;
                mA[i, 1] = a[i].Y;
                mA[i, 2] = a[i].Z;

                mB[i, 0] = weights[i] * b[i].X;
                mB[i, 1] = weights[i] * b[i].Y;
                mB[i, 2] = weights[i] * b[i].Z;
            }

            var h = mA.Transpose() * mB;
            var svd = h.Svd();


            var R = svd.VT.Transpose() * svd.U.Transpose();

            if (R.Determinant() < 0)
            {
                //Correct the sign of the rotation
                R = svd.VT.Transpose() * flip * svd.U.Transpose();
            }



            //var norm = (R.Transpose() * R - Matrix<float>.Build.DenseIdentity(3)).FrobeniusNorm();

            ////Console.WriteLine("Norm: {0}", norm);

            //if (norm > 1e-3)
            //{
            //    lock (LockObject)
            //    {
            //        Console.Error.WriteLine($"Failed to find rigid transform for index {pointIndex}");

            //        Console.WriteLine("Orthogonality loss: {0}", norm);

            //        Console.WriteLine("Weights: [{0}]", string.Join(",", nnW));

            //        Console.WriteLine(svd.Determinant);
            //        Console.WriteLine(svd.S);
            //        Console.WriteLine(svd.VT);
            //        Console.WriteLine(svd.U);
            //    }
            //}

            return new Transform(R, cAV, cBV);
        }

        public static Vector4[] Fit(Vector4[] source, Vector4[] target, float[] weights)
        {
            Transform A = GetTransform(source, target, weights);

            Vector4[] result = new Vector4[source.Length];
            for (int i = 0; i < source.Length; i++)
            {
                result[i] = A.Apply(source[i]);
            }
            return result;
        }

        public static Transform GetTransform(Vector4[] pts1, Vector4[] pts2, int pointIndex, SmoothWeights weights)
        {
            if (weights.nn == null)
            {
                throw new Exception("Rigid transformation cannot be established without first calling weights.cutoff(float value)");
            }

            //Get neighbors
            List<int> neighbors = weights.nn[pointIndex];
            int n = neighbors.Count;

            Vector4[] a = new Vector4[n];
            Vector4[] b = new Vector4[n];
            float[] nnW = new float[n];

            //Compute weighted centroids
            for (int i = 0; i < n; i++)
            {
                int j = neighbors[i];
                a[i] = pts1[j];
                b[i] = pts2[j];

                nnW[i] = weights.weights[pointIndex][j];
            }

            return GetTransform(a, b, nnW);
        }

        public static Vector4 Predict(Vector4[] pts1, Vector4[] pts2, int pointIndex, SmoothWeights weights)
        {

            Transform A = GetTransform(pts1, pts2, pointIndex, weights);
            return A.Apply(pts1[pointIndex]);
        }

        public static Vector4[] ARAPGradient(Vector4[] pts1, Vector4[] pts2, SmoothWeights weights)
        {
            Vector4[] gradient = new Vector4[pts1.Length];
            Parallel.For(0, gradient.Length, i =>
            //for (int i = 0; i < gradient.Length; i++)
            {
                Vector4 prediction = ARAP.Predict(pts1, pts2, i, weights);
                gradient[i] = (prediction - pts2[i]);
            });

            return gradient;
        }
    }
}
