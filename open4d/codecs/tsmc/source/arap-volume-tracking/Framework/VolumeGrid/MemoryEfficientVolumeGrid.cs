//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Numerics;
using System.Threading.Tasks;
//using System.Windows.Forms;

namespace Framework
{
    /// <summary>
    /// Volume grid represented by a single array of bytes
    /// where each bit corresponds to occupancy of a single cell
    /// </summary>
    public class MemoryEfficientVolumeGrid : VolumeGrid, IDisposable
    {
        byte[] data;

        /// <summary>
        /// Backend for the cell-to-nearest-point scans (Lloyd, stats, affinity sums).
        /// Only Backend.Cuda changes them; the other backends run the managed code.
        /// </summary>
        public Backend Accel { get; set; } = Backend.Cpu;

        ArapCuda.NativeGrid nativeGrid;

        ArapCuda.NativeGrid Native => nativeGrid ??= ArapCuda.CreateGrid(data, XRes, YRes, ZRes, min, a);

        public void Dispose()
        {
            nativeGrid?.Dispose();
            nativeGrid = null;
        }

        public override bool GetValue(int x, int y, int z)
        {
            int index = ((x * YRes) + y) * ZRes + z;
            int byteIndex = index / 8;
            int bitIndex = index % 8;
            return (data[byteIndex] & (1 << bitIndex)) != 0;
        }

        public bool GetValue(int byteIndex, int bitIndex)
        {
            return (data[byteIndex] & (1 << bitIndex)) != 0;
        }

        public override void Save(string fn)
        {
            bool current = false;
            int count = 0;
            using FileStream fs = new FileStream(fn, FileMode.Create);
            using BinaryWriter bw = new BinaryWriter(fs);
            bw.Write(XRes);
            bw.Write(YRes);
            bw.Write(ZRes);
            bw.Write(min.X);
            bw.Write(min.Y);
            bw.Write(min.Z);
            bw.Write(a);

            int numBits = XRes * YRes * ZRes;
            int byteIndex = 0;
            byte byteVal = data[0];
            for (int i = 0; i < numBits; i++)
            {
                byteVal = data[byteIndex];
                int bit = i % 8;

                bool val = (byteVal & (1 << bit)) != 0;

                if (val != current)
                {
                    bw.Write(current);
                    bw.Write(count);
                    current = val;
                    count = 0;
                }
                count++;

                if (bit == 7)
                {
                    byteIndex++;
                }
            }

            bw.Write(current);
            bw.Write(count);

            bw.Close();
            fs.Close();
        }

        private void Read(string fn)
        {
            FileStream fs = new FileStream(fn, FileMode.Open);
            BinaryReader br = new BinaryReader(fs);
            XRes = br.ReadInt32();
            YRes = br.ReadInt32();
            ZRes = br.ReadInt32();
            min.X = br.ReadSingle();
            min.Y = br.ReadSingle();
            min.Z = br.ReadSingle();
            a = br.ReadSingle();

            int numBits = XRes * YRes * ZRes;
            int numBytes = (int)Math.Ceiling(numBits / 8.0);
            this.data = new byte[numBytes];

            byte currentValue = br.ReadByte();
            uint count = br.ReadUInt32();
            uint counter = 0;

            for (int x = 0; x < XRes; x++)
            {
                int xI = x * YRes * ZRes;
                for (int y = 0; y < YRes; y++)
                {
                    int yI = y * ZRes;

                    int z = 0;
                    while (z < ZRes)
                    {
                        if (counter == count)
                        {
                            currentValue = br.ReadByte();
                            count = br.ReadUInt32();
                            counter = 0;
                        }

                        if (currentValue != 0)
                        {
                            int index = xI + yI + z;
                            int byteIndex = index / 8;
                            int bitIndex = index % 8;
                            data[byteIndex] += (byte)(1 << bitIndex);
                        }

                        counter++;
                        z++;
                    }
                }
            }
            br.Close();
        }

        private void FillFromMD(byte[][][] md)
        {
            int numBits = XRes * YRes * ZRes;
            int numBytes = (int)Math.Ceiling(numBits / 8.0);
            this.data = new byte[numBytes];

            for (int x = 0; x < XRes; x++)
            {
                int xI = x * YRes * ZRes;
                for (int y = 0; y < YRes; y++)
                {
                    int yI = y * ZRes;
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (md[x][y][z] != 0)
                        {
                            int index = xI + yI + z;
                            int byteIndex = index / 8;
                            int bitIndex = index % 8;
                            data[byteIndex] += (byte)(1 << bitIndex);
                        }
                    }
                }
            }
        }

        public MemoryEfficientVolumeGrid(Vector4 min, float a, byte[][][] data, int XRes, int YRes, int ZRes)
        {
            this.min = min;
            this.a = a;
            
            //this.maxValue = maxValue;
            this.XRes = XRes;
            this.YRes = YRes;
            this.ZRes = ZRes;
            FillFromMD(data);
        }

        public MemoryEfficientVolumeGrid(string fn)
        {
            Read(fn);
        }

        public MemoryEfficientVolumeGrid(TriangleMesh mesh, int maxRes) : this(mesh, maxRes, Backend.Cpu)
        {
        }

        public MemoryEfficientVolumeGrid(TriangleMesh mesh, int maxRes, Backend backend)
        {
            this.Accel = backend;
            var bb = mesh.BoundingBox;
            this.min = bb.MinPoint;
            float maxSize = (bb.MaxPoint - bb.MinPoint).X;
            maxSize = Math.Max(maxSize, (bb.MaxPoint - bb.MinPoint).Y);
            maxSize = Math.Max(maxSize, (bb.MaxPoint - bb.MinPoint).Z);
            this.a = maxSize / maxRes;
            //this.maxValue = 255;
            XRes = (int)Math.Ceiling((bb.MaxPoint.X - bb.MinPoint.X) / a);
            YRes = (int)Math.Ceiling((bb.MaxPoint.Y - bb.MinPoint.Y) / a);
            ZRes = (int)Math.Ceiling((bb.MaxPoint.Z - bb.MinPoint.Z) / a);

            int numBits = XRes * YRes * ZRes;
            int numBytes = (int)Math.Ceiling(numBits / 8.0);
            this.data = new byte[numBytes];

            if (backend != Backend.Cpu)
            {
                ArapCuda.Voxelize(mesh, min, a, XRes, YRes, ZRes, data, backend);
                return;
            }

            var tenth = XRes / 10;

            for (int x = 0; x < XRes; x++)
            {
                //DateTime start = DateTime.Now;
                int xI = x * YRes * ZRes;
                //dataOld[x] = new byte[YRes][];
                Parallel.For(0, YRes, y =>
                {
                    int yI = y * ZRes;
                    //dataOld[x][y] = new byte[ZRes];
                    Vector4 q = Int2xyz(x, y, 0);
                    var depthArray = mesh.InsideTest3(q);
                    float zf = min.Z + a / 2;
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (TriangleMesh.InsideTest2(zf, depthArray))
                        {
                            int index = xI + yI + z;
                            int byteIndex = index / 8;
                            int bitIndex = index % 8;
                            data[byteIndex] += (byte)(1 << bitIndex);
                        }

                        zf += a;
                    }
                });
                if ((x + 1) % tenth == 0) Console.Write($"{(x + 1) / tenth}0% ");
            }
            Console.WriteLine();
        }

        //static object lockObj = new();

        public override Vector4[] LloydGradient(Vector4[] pts, float overRelax)
        {
            if (Accel != Backend.Cuda)
                return LloydGradientCpu(pts, overRelax);

            var m = ArapCuda.Assign(Native, pts, false);

            Vector4[] gradient = new Vector4[pts.Length];
            for (int i = 0; i < pts.Length; i++)
            {
                int c = m.Counts[i];
                if (c > 0)
                {
                    var centroid = new Vector4((float)(m.Sums[3 * i] / c), (float)(m.Sums[3 * i + 1] / c), (float)(m.Sums[3 * i + 2] / c), min.W);
                    gradient[i] = (centroid - pts[i]) * overRelax;
                }
                else
                {
                    gradient[i] = NearestIn(pts[i]) - pts[i];
                }
            }
            return gradient;
        }

        Vector4[] LloydGradientCpu(Vector4[] pts, float overRelax)
        {
            Vector4[][] Gcentroids = new Vector4[XRes][];
            int[][] Gcounts = new int[XRes][];
            float[][] GweightSum = new float[XRes][];
            KDTree Gtree = new KDTree(pts);
            
            Parallel.For(0, XRes, x =>
            {
                Gcentroids[x] = new Vector4[pts.Length];
                Gcounts[x] = new int[pts.Length];
                GweightSum[x] = new float[pts.Length];

                int xI = x * YRes * ZRes;
                //Console.Write(".");
                //Vector4[] centroids = new Vector4[pts.Length];
                //int[] counts = new int[pts.Length];
                //float[] weightSum = new float[pts.Length];
                KDTree tree = new KDTree(Gtree);
                //for (int x = 0;x<XRes;x++)
                int nearest = 0;
                for (int y = 0; y < YRes; y++)
                {
                    int yI = y * ZRes;
                    for (int z = 0; z < ZRes; z++)
                    {
                        int index = xI + yI + z;
                        int byteIndex = index / 8;
                        int bitIndex = index % 8;

                        if (GetValue(byteIndex, bitIndex))
                        {
                            var p = Int2xyz(x, y, z);
                            //float weight = dataOld[x][y][z] / (float)maxValue;
                            float weight = 1f;
                            nearest = tree.FindNearestWithGuess(p, nearest);
                            Gcentroids[x][nearest] += weight * p;
                            Gcounts[x][nearest] += 1;
                            GweightSum[x][nearest] += weight;
                        }
                    }
                }                
            });

            Vector4[] centroids = new Vector4[pts.Length];
            int[] counts = new int[pts.Length];
            float[] weightSum = new float[pts.Length];

            for (int x = 0; x < XRes; x++)
            {
                for (int i = 0; i < centroids.Length; i++)
                {
                    centroids[i] += Gcentroids[x][i];
                    counts[i] += Gcounts[x][i];
                    weightSum[i] += GweightSum[x][i];
                }
            }



            double avg = 0;
            // reuse GCentroids for result. Not nice, but efficient
            for (int i = 0; i < pts.Length; i++)
            {
                avg += counts[i];
                if (counts[i] > 0)
                {
                    var vector = centroids[i] * (1.0f / weightSum[i]) - pts[i];
                    centroids[i] = vector * overRelax;
                }
                else
                {
                    //var inOrOut = probe(Gcentroids[i]);
                    //Console.ForegroundColor = ConsoleColor.Red;
                    //Console.WriteLine("Orphaned centroid {0}!", inOrOut ? "in" : "out");
                    //Console.ResetColor();
                    centroids[i] = NearestIn(pts[i]) - pts[i];
                }
            }
            avg /= (double)pts.Length;
            double stdev = 0;
            for (int i = 0; i < pts.Length; i++)
            {
                double dif = counts[i] - avg;
                stdev += dif * dif;
            }
            //Console.WriteLine("Stdev after Lloyd: {0}, stdev/avg: {1}", Math.Sqrt(stdev / pts.Length), Math.Sqrt(stdev / pts.Length) / avg);
            return centroids;
        }

        public override (double, float[]) LloydStats(Vector4[] pts)
        {
            if (Accel != Backend.Cuda)
                return LloydStatsCpu(pts);

            return StatsFromCounts(ArapCuda.Assign(Native, pts, false).Counts);
        }

        (double, float[]) LloydStatsCpu(Vector4[] pts)
        {
            int[][] Gcounts = new int[XRes][];
            KDTree Gtree = new KDTree(pts);

            Parallel.For(0, XRes, x =>
            {
                int xI = x * YRes * ZRes;
                Gcounts[x] = new int[pts.Length];
                KDTree tree = new KDTree(Gtree);

                int nearest = 0;
                for (int y = 0; y < YRes; y++)
                {
                    int yI = y * ZRes;
                    for (int z = 0; z < ZRes; z++)
                    {
                        int index = xI + yI + z;
                        int byteIndex = index / 8;
                        int bitIndex = index % 8;

                        if (GetValue(byteIndex, bitIndex))
                        {
                            var p = Int2xyz(x, y, z);
                            nearest = tree.FindNearestWithGuess(p, nearest);
                            Gcounts[x][nearest] += 1;
                        }
                    }
                }
                //lock (this)
                //{
                //    for (int i = 0; i < pts.Length; i++)
                //    {
                //        Gcounts[i] += counts[i];
                //    }
                //}
            });

            int[] counts = new int[pts.Length];
            for (int x = 0; x < XRes; x++)
            {
                for (int i = 0; i < pts.Length; i++)
                    counts[i] += Gcounts[x][i];
            }

            return StatsFromCounts(counts);
        }

        static (double, float[]) StatsFromCounts(int[] counts)
        {
            float[] contribution = new float[counts.Length];
            double avg = 0;
            for (int i = 0; i < counts.Length; i++)
                avg += counts[i];


            avg /= (double)counts.Length;

            double stdev = 0;
            for (int i = 0; i < counts.Length; i++)
            {
                double dif = counts[i] - avg;
                stdev += dif * dif;
                contribution[i] = (float)(dif * dif);
            }

            var res = Math.Sqrt(stdev / counts.Length) / avg;
            //Console.WriteLine("Stdev after Lloyd: stdev/avg: {0}", res);

            return (res, contribution);
        }

        public override TransformDistance[] GetTransformDistances(Vector4[] pts)
        {
            if (Accel != Backend.Cuda)
                return base.GetTransformDistances(pts);

            var m = ArapCuda.Assign(Native, pts, true);

            TransformDistance[] dst = new TransformDistance[pts.Length];
            for (int i = 0; i < pts.Length; i++)
            {
                // GetPoints also puts the point itself into its own cell set
                Vector4 p = pts[i];
                double sx = m.Sums[3 * i] + p.X, sy = m.Sums[3 * i + 1] + p.Y, sz = m.Sums[3 * i + 2] + p.Z;
                double xx = m.Second[6 * i] + (double)p.X * p.X, xy = m.Second[6 * i + 1] + (double)p.X * p.Y;
                double xz = m.Second[6 * i + 2] + (double)p.X * p.Z, yy = m.Second[6 * i + 3] + (double)p.Y * p.Y;
                double yz = m.Second[6 * i + 4] + (double)p.Y * p.Z, zz = m.Second[6 * i + 5] + (double)p.Z * p.Z;

                // TransformDistance.QSum starts from Vector.Build.Dense(3, 1), i.e. (1, 1, 1)
                var qSum = MathNet.Numerics.LinearAlgebra.Vector<float>.Build.DenseOfArray(new float[] { (float)(sx + 1), (float)(sy + 1), (float)(sz + 1) });
                var qqt = MathNet.Numerics.LinearAlgebra.Matrix<float>.Build.DenseOfArray(new float[,]
                {
                    { (float)xx, (float)xy, (float)xz },
                    { (float)xy, (float)yy, (float)yz },
                    { (float)xz, (float)yz, (float)zz },
                });
                dst[i] = new TransformDistance(m.Counts[i] + 1, (float)(xx + yy + zz), qSum, qqt);
            }
            return dst;
        }

        public override List<Vector4>[] GetPoints(Vector4[] pts)
        {
            KDTree tree = new KDTree(pts);
            int nearest = 0;

            List<Vector4>[] points = new List<Vector4>[pts.Length];

            for (int i = 0; i < points.Length; i++)
                points[i] = new List<Vector4>();

            for (int x = 0; x < XRes; x++)
            {
                int xI = x * YRes * ZRes;
                for (int y = 0; y < YRes; y++)
                {
                    int yI = y * ZRes;
                    for (int z = 0; z < ZRes; z++)
                    {
                        int index = xI + yI + z;
                        int byteIndex = index / 8;
                        int bitIndex = index % 8;

                        if (GetValue(byteIndex, bitIndex))
                        {
                            var p = Int2xyz(x, y, z);
                            nearest = tree.FindNearestWithGuess(p, nearest);
                            points[nearest].Add(p);
                        }
                    }
                }
            }

            // for (int i = 0; i < points.Length; i++)
            //     points[i].TrimExcess();

            //Vector4[][] pointsArrays = new Vector4[points.Length][];

            for (int i = 0; i < points.Length; i++)
            {
                points[i].Add(pts[i]);
            }


            return points;
        }

        Vector4 NearestIn(Vector4 q)
        {
            float smDist = float.MaxValue;
            Vector4 result = new Vector4(float.NaN, float.NaN, float.NaN, float.NaN);

            for (int x = 0; x < XRes; x++)
            {
                int xI = x * YRes * ZRes;
                for (int y = 0; y < YRes; y++)
                {
                    int yI = y * ZRes;
                    for (int z = 0; z < ZRes; z++)
                    {
                        int index = xI + yI + z;
                        int byteIndex = index / 8;
                        int bitIndex = index % 8;

                        if (GetValue(byteIndex, bitIndex))
                        {
                            var distsq = (q - Int2xyz(x, y, z)).LengthSquared();
                            if (distsq < smDist)
                            {
                                smDist = distsq;
                                result = Int2xyz(x, y, z);
                            }
                        }
                    }
                }
            }

            //Console.WriteLine("Nearest point found at {0}", Math.Sqrt(smDist));
            return result;
        }
    }
}
