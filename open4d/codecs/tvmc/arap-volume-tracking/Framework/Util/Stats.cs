//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using MathNet.Numerics.LinearAlgebra;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace Framework
{
    public static class Stats
    {
        public static Comparer<float> invCmp = Comparer<float>.Create((a, b) => b.CompareTo(a));

        private static int[] CreateIndicesForSort(int n)
        {
            int[] idx = new int[n];
            for (int i = 0; i < n; i++)
            {
                idx[i] = i;
            }
            return idx;
        }

        private static float TrajectoryDistanceSq(Vector4[][] pc, int p1, int p2)
        {
            float dist = 0f;

            for (int i = 0; i < pc.Length; i++)
            {
                dist += Vector4.DistanceSquared(pc[i][p1], pc[i][p2]);
            }

            return dist;
        }

        public static float[] Irregularity(Vector4[][] pc)
        {
            int pointCount = pc[0].Length;

            float[] result = new float[pointCount];

            for (int i = 0; i < pointCount; i++)
            {
                float minDist = float.MaxValue;
                for (int j = 0; j < pointCount; j++)
                {
                    if (i == j)
                        continue;

                    float dist = TrajectoryDistanceSq(pc, i, j);
                    if (dist < minDist)
                    {
                        minDist = dist;
                    }
                }

                result[i] = minDist;
            }

            return result;
        }

        public static (int[], float[]) IrregularitySorted(Vector4[][] pc)
        {
            float[] dists = Irregularity(pc);
            int[] idx = CreateIndicesForSort(dists.Length);

            Array.Sort(dists, idx, invCmp);

            return (idx, dists);
        }

        public static (double, float[]) PCACompactness(Vector4[][] pc)
        {
            Matrix<double> m = Matrix<double>.Build.Dense(pc.Length * 3, pc[0].Length);
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
            return AnalyzeCompactness(coefs);
        }

        private static (double, float[]) AnalyzeCompactness(Matrix<double> coefs)
        {
            double[] coefsums = new double[coefs.ColumnCount];
            float[] contribution = new float[coefs.RowCount];
            for (int i = 0; i < coefs.ColumnCount; i++)
                for (int j = 0; j < coefs.RowCount; j++)
                {
                    coefsums[i] += Math.Abs(coefs[j, i]);
                    contribution[j] += i * (float)Math.Abs(coefs[j, i]);
                }
            double centroid = 0;
            double weight = 0;
            for (int i = 0; i < coefsums.Length; i++)
            {
                centroid += coefsums[coefsums.Length - 1 - i] * i;
                weight += coefsums[coefsums.Length - 1 - i];
            }
            centroid /= weight;
            Console.WriteLine("PCA compactness: {0} (lower = more compact, lowest possible = 0)", centroid);
            return (centroid, contribution);
        }

        public static void OverallStats(VolumeGrid[] vg, Vector4[][] pc, string label, string outDir, int frameCount, int pointCount)
        {
            (double compactness, float[] pcacContribution) = Stats.PCACompactness(pc);
            File.WriteAllText(outDir + $"/PCAC_{label}.txt", compactness.ToString());
            IO.SavePointStats(pcacContribution, outDir + $"/PCAC_contribution_{label}.txt");

            double[] avg = new double[frameCount];
            float[] contribution = new float[pointCount];
            for (int i = 0; i < frameCount; i++)
            {
                float[] frameContributions;
                (avg[i], frameContributions) = vg[i].LloydStats(pc[i]);

                for (int j = 0; j < pointCount; j++)
                {
                    contribution[j] += frameContributions[j];
                }
            }

            PrintLloydStats(avg, label, outDir);

            float[] dists = Stats.Irregularity(pc);

            IO.SavePointStats(dists, outDir + $"/irregularity_{label}.csv");
            IO.SavePointStats(contribution, outDir + $"/DFU_contribution_{label}.csv");

            Array.Sort(dists, Stats.invCmp);

            IO.SavePointStats(dists, outDir + $"/irregularity_sorted_{label}.csv");
        }

        public static double PrintLloydStats(double[] avg, string label, string outDir)
        {
            using (var writer = new StreamWriter($"{outDir}/DFU_{label}.csv"))
            {
                for (int i = 0; i < avg.Length; i++)
                    writer.WriteLine($"{avg[i]:0.######};");
            }

            Console.WriteLine("Deviation From Uniformity (DFU) written to " + $"{outDir}/DFU_{label}.csv");

            double avgsum = 0;
            for (int i = 0; i < avg.Length; i++)
                avgsum += avg[i];

            double dfu = avgsum / avg.Length;

            Console.WriteLine($"Average DFU: {dfu}");

            File.WriteAllText($"{outDir}/DFU_{label}.txt", dfu.ToString());

            return dfu;
        }

        public static double MSE(Vector4[] pc1, Vector4[] pc2)
        {
            double result = 0;

            for (int i = 0; i < pc1.Length; i++)
            {
                result += Vector4.DistanceSquared(pc1[i], pc2[i]);
            }

            return result / pc1.Length;
        }

        public static double MSME (Transform[] tf1, Transform[] tf2, Vector4[] pc1, VolumeGrid vg)
        {
            double[] dists = new double[pc1.Length];
            double result = 0;
            List<Vector4>[] points = vg.GetPoints(pc1);

            Parallel.For(0, pc1.Length, (i) => {
                TransformDistance tfDist = new TransformDistance(points[i]);

                dists[i] = tfDist.GetTransformDistance(tf1[i].R, tf2[i].R, tf1[i].t, tf2[i].t);
            });

            for (int i = 0; i < pc1.Length; i++)
            {
                result += dists[i];
            }

            return result / pc1.Length;
        }
    }
}
