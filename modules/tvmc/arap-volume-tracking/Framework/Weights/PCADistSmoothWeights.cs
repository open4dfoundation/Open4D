//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using MathNet.Numerics.LinearAlgebra;
using System.Numerics;
using System.Threading.Tasks;

namespace Framework
{
    public class PCADistSmoothWeights : SmoothWeights
    {
        private readonly float k0;

        public PCADistSmoothWeights(Vector4[][] pc, float k0) : base(pc[0].Length)
        {
            this.k0 = k0;
            UpdateFull(pc);
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            //do nothing for now
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            int n = pc[0].Length;

            Matrix<double> m = Matrix<double>.Build.Dense(pc.Length * 3, n);
            for (int i = 0; i < m.RowCount / 3; i++)
            {
                for (int j = 0; j < m.ColumnCount; j++)
                {
                    m[3 * i, j] = pc[i][j].X;
                    m[3 * i + 1, j] = pc[i][j].Y;
                    m[3 * i + 2, j] = pc[i][j].Z;
                }
            }

            // compute mean
            Matrix<double> mean = Matrix<double>.Build.Dense(pc.Length * 3, 1);
            for (int i = 0; i < m.RowCount; i++)
            {
                for (int j = 0; j < m.ColumnCount; j++)
                {
                    mean[i, 0] += m[i, j];
                }
                mean[i, 0] /= m.ColumnCount;
            }

            // subtract mean
            for (int i = 0; i < m.ColumnCount; i++)
            {
                for (int j = 0; j < m.RowCount; j++)
                    m[j, i] -= mean[j, 0];
            }

            // autocorrelation
            var mt = m.Transpose();
            var ac = m * mt;

            var evd = ac.Evd();
            var coefs = mt * evd.EigenVectors;

            Vector4[] c = new Vector4[n];

            for (int i = 0; i < n; i++)
            {
                c[i].X = (float)coefs[i, coefs.ColumnCount - 1];
                c[i].Y = (float)coefs[i, coefs.ColumnCount - 2];
                c[i].Z = (float)coefs[i, coefs.ColumnCount - 3];
                c[i].W = (float)coefs[i, coefs.ColumnCount - 4];
            }

            float[,] distances = new float[n, n];

            for (int frame = 0; frame < pc.Length; frame++)
            {
                Parallel.For(0, n, i =>
                {
                    Vector4 pos = c[i];
                    for (int j = i + 1; j < n; j++)
                    {
                        float dist = (pos - c[j]).LengthSquared();
                        distances[i, j] = dist;
                        distances[j, i] = dist;
                    }
                });
            }

            UpdateFromDistances(distances, k0);
        }
    }
}
