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
    public class MemoryEfficientVolumeGrid : VolumeGrid
    {
        byte[] data;

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

        public MemoryEfficientVolumeGrid(TriangleMesh mesh, int maxRes)
        {
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
            int[][] Gcounts = new int[XRes][];
            float[] contribution = new float[pts.Length];
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

            double avg = 0;
            for (int i = 0; i < pts.Length; i++)
                avg += counts[i];


            avg /= (double)pts.Length;

            double stdev = 0;
            for (int i = 0; i < pts.Length; i++)
            {
                double dif = counts[i] - avg;
                stdev += dif * dif;
                contribution[i] = (float)(dif * dif);
            }

            var res = Math.Sqrt(stdev / pts.Length) / avg;
            //Console.WriteLine("Stdev after Lloyd: stdev/avg: {0}", res);

            return (res, contribution);
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
