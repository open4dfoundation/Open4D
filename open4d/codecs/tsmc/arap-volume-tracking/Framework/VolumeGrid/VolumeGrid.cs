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
    public abstract class VolumeGrid
    {
        public Vector4 min;
        public float a;
        public int XRes, YRes, ZRes;

        public abstract bool GetValue(int x, int y, int z);
        public abstract void Save(string fn);
        public abstract Vector4[] LloydGradient(Vector4[] pts, float overRelax);
        public abstract (double, float[]) LloydStats(Vector4[] pts);
        public abstract List<Vector4>[] GetPoints(Vector4[] pts);

        public bool Probe(Vector4 p)
        {
            var v = (p - min) / a;
            int i = (int)Math.Floor(v.X);
            int j = (int)Math.Floor(v.Y);
            int k = (int)Math.Floor(v.Z);
            if ((i < 0) || (j < 0) || (k < 0))
                return false;
            if ((i >= XRes) || (j >= YRes) || (k >= ZRes))
                return false;
            return GetValue(i, j, k);
        }

        public void VerifyInside(Vector4[] points, string type)
        {
            for (int i = 0; i < points.Length; i++)
            {
                var inOut = Probe(points[i]);

                if (!inOut)
                {
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine($"Point outside!! Type: {type}");
                    Console.ResetColor();

                }
            }
        }

        public void VerifyInside(Vector4 point, string type)
        {
            var inOut = Probe(point);

            if (!inOut)
            {
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine($"Point outside!! Type: {type}");
                Console.ResetColor();
            }
        }

        public Vector4[] RandomSample(int count)
        {
            Vector4[] result = new Vector4[count];
            Random rnd = new Random(0);
            HashSet<Triangle> samples = new HashSet<Triangle>();
            for (int i = 0; i < count; i++)
            {
                Triangle t = new Triangle();
                do
                {
                    t.V1 = rnd.Next(XRes);
                    t.V2 = rnd.Next(YRes);
                    t.V3 = rnd.Next(ZRes);
                } while (!GetValue(t.V1, t.V2, t.V3) || samples.Contains(t));
                samples.Add(t);
                result[i] = Int2xyz(t.V1, t.V2, t.V3);
            }

            return result;
        }

        public void Perturb(Vector4[] pts)
        {
            Random rnd = new Random(0);
            for (int i = 0; i < pts.Length; i++)
            {
                Vector4 perturbation = new Vector4((float)rnd.NextDouble() - 0.5f, (float)rnd.NextDouble() - 0.5f, (float)rnd.NextDouble() - 0.5f, 0);
                perturbation *= a * 0.01f;
                pts[i] += perturbation;
            }
        }

        public Vector4 Int2xyz(int x, int y, int z)
        {
            Vector4 r = min;
            r.X += x * a + a / 2;
            r.Y += y * a + a / 2;
            r.Z += z * a + a / 2;
            return r;
        }
    }

    /// <summary>
    /// Volume grid represented by a multi-dimensional byte array
    /// where each value represents a single cell
    /// (Might be inefficient if we want to store values for all the frames)
    /// </summary>
    public class MDArrayVolumeGrid : VolumeGrid
    {
        byte[][][] data;
        readonly byte maxValue = 255;

        public override bool GetValue(int x, int y, int z)
        {
            return data[x][y][z] != 0;
        }


        public override void Save(string fn)
        {
            byte current = 0;
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

            for (int x = 0; x < XRes; x++)
            {
                for (int y = 0; y < YRes; y++)
                {
                    for (int z = 0; z < ZRes; z++)
                    {
                        var val = data[x][y][z];
                        if (val != current)
                        {
                            bw.Write(current);
                            bw.Write(count);
                            current = val;
                            count = 0;
                        }
                        count++;
                    }
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

            byte currentValue = br.ReadByte();
            uint count = br.ReadUInt32();
            uint counter = 0;


            data = new byte[XRes][][];
            for (int x = 0; x < XRes; x++)
            {
                data[x] = new byte[YRes][];
                for (int y = 0; y < YRes; y++)
                {
                    data[x][y] = new byte[ZRes];
                    int z = 0;
                    while (z < ZRes)
                    {
                        if (counter == count)
                        {
                            currentValue = br.ReadByte();
                            count = br.ReadUInt32();
                            counter = 0;
                        }
                        data[x][y][z] = currentValue;
                        counter++;
                        z++;
                    }
                }
            }
            br.Close();
        }

        //public MDArrayVolumeGrid(string fn, bool old)
        //{
        //    FileStream fs = new FileStream(fn, FileMode.Open);
        //    BinaryReader br = new BinaryReader(fs);
        //    XRes = br.ReadInt32();
        //    YRes = br.ReadInt32();
        //    ZRes = br.ReadInt32();
        //    min.X = br.ReadSingle();
        //    min.Y = br.ReadSingle();
        //    min.Z = br.ReadSingle();
        //    a = br.ReadSingle();
        //    this.maxValue = 1;
        //    data = new byte[XRes][][];
        //    for (int x = 0; x < XRes; x++)
        //    {
        //        data[x] = new byte[YRes][];
        //        for (int y = 0; y < YRes; y++)
        //        {
        //            data[x][y] = new byte[ZRes];
        //            for (int z = 0; z < ZRes; z++)
        //                data[x][y][z] = br.ReadByte();
        //        }
        //    }
        //    br.Close();
        //}

        public MDArrayVolumeGrid(Vector4 min, float a, byte[][][] data, int XRes, int YRes, int ZRes)
        {
            this.min = min;
            this.a = a;
            this.data = data;
            //this.maxValue = maxValue;
            this.XRes = XRes;
            this.YRes = YRes;
            this.ZRes = ZRes;
        }

        public MDArrayVolumeGrid(string fn)
        {
            Read(fn);
        }

        public MDArrayVolumeGrid(TriangleMesh mesh, int maxRes)
        {
            var bb = mesh.BoundingBox;
            this.min = bb.MinPoint;
            float maxSize = (bb.MaxPoint - bb.MinPoint).X;
            maxSize = Math.Max(maxSize, (bb.MaxPoint - bb.MinPoint).Y);
            maxSize = Math.Max(maxSize, (bb.MaxPoint - bb.MinPoint).Z);
            this.a = maxSize / maxRes;
            this.maxValue = 255;
            XRes = (int)Math.Ceiling((bb.MaxPoint.X - bb.MinPoint.X) / a);
            YRes = (int)Math.Ceiling((bb.MaxPoint.Y - bb.MinPoint.Y) / a);
            ZRes = (int)Math.Ceiling((bb.MaxPoint.Z - bb.MinPoint.Z) / a);
            data = new byte[XRes][][];

            var tenth = XRes / 10;

            for (int x = 0; x < XRes; x++)
            {
                DateTime start = DateTime.Now;
                data[x] = new byte[YRes][];
                Parallel.For(0, YRes, y =>
                {
                    data[x][y] = new byte[ZRes];
                    Vector4 q = Int2xyz(x, y, 0);
                    var depthArray = mesh.InsideTest3(q);
                    float zf = min.Z + a / 2;
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (TriangleMesh.InsideTest2(zf, depthArray))
                            data[x][y][z] = 255;
                        zf += a;
                    }
                });
                if ((x + 1) % tenth == 0) Console.Write($"{(x + 1) / tenth}0% ");
            }
            Console.WriteLine();
        }

        public override Vector4[] LloydGradient(Vector4[] pts, float overRelax)
        {
            Vector4[] Gcentroids = new Vector4[pts.Length];
            int[] Gcounts = new int[pts.Length];
            float[] GweightSum = new float[pts.Length];
            KDTree Gtree = new KDTree(pts);
            Parallel.For(0, XRes, x =>
            {
                //Console.Write(".");
                Vector4[] centroids = new Vector4[pts.Length];
                int[] counts = new int[pts.Length];
                float[] weightSum = new float[pts.Length];
                KDTree tree = new KDTree(Gtree);
                //for (int x = 0;x<XRes;x++)
                int nearest = 0;
                for (int y = 0; y < YRes; y++)
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (data[x][y][z] != 0)
                        {
                            var p = Int2xyz(x, y, z);
                            float weight = data[x][y][z] / (float)maxValue;
                            nearest = tree.FindNearestWithGuess(p, nearest);
                            centroids[nearest] += weight * p;
                            counts[nearest] += 1;
                            weightSum[nearest] += weight;
                        }
                    }
                lock (this)
                {
                    for (int i = 0; i < centroids.Length; i++)
                    {
                        Gcentroids[i] += centroids[i];
                        Gcounts[i] += counts[i];
                        GweightSum[i] += weightSum[i];
                    }
                }
            });
            double avg = 0;
            // reuse GCentroids for result. Not nice, but efficient
            for (int i = 0; i < pts.Length; i++)
            {
                avg += Gcounts[i];
                if (Gcounts[i] > 0)
                {
                    var vector = Gcentroids[i] * (1.0f / GweightSum[i]) - pts[i];
                    Gcentroids[i] = vector * overRelax;
                }
                else
                {
                    var inOrOut = Probe(Gcentroids[i]);
                    Console.ForegroundColor = ConsoleColor.Red;
                    Console.WriteLine("Orphaned centroid {0}!", inOrOut ? "in" : "out");
                    Console.ResetColor();
                    Gcentroids[i] = NearestIn(pts[i]) - pts[i];
                }
            }
            avg /= (double)pts.Length;
            double stdev = 0;
            for (int i = 0; i < pts.Length; i++)
            {
                double dif = Gcounts[i] - avg;
                stdev += dif * dif;
            }
            //Console.WriteLine("Stdev after Lloyd: {0}, stdev/avg: {1}", Math.Sqrt(stdev / pts.Length), Math.Sqrt(stdev / pts.Length) / avg);
            return Gcentroids;
        }

        public override (double, float[]) LloydStats(Vector4[] pts)
        {
            int[][] Gcounts = new int[XRes][];
            float[] contribution = new float[pts.Length];
            KDTree Gtree = new KDTree(pts);

            Parallel.For(0, XRes, x =>
            {
                Gcounts[x] = new int[pts.Length];
                KDTree tree = new KDTree(Gtree);

                int nearest = 0;
                for (int y = 0; y < YRes; y++)
                {
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (data[x][y][z] != 0)
                        {
                            var p = Int2xyz(x, y, z);
                            nearest = tree.FindNearestWithGuess(p, nearest);
                            Gcounts[x][nearest] += 1;
                        }
                    }
                }
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
                for (int y = 0; y < YRes; y++)
                {
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (data[x][y][z] != 0)
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
                for (int y = 0; y < YRes; y++)
                    for (int z = 0; z < ZRes; z++)
                    {
                        if (data[x][y][z] != 0)
                        {
                            var distsq = (q - Int2xyz(x, y, z)).LengthSquared();
                            if (distsq < smDist)
                            {
                                smDist = distsq;
                                result = Int2xyz(x, y, z);
                            }
                        }
                    }
            Console.WriteLine("Nearest point found at {0}", Math.Sqrt(smDist));
            return result;
        }
    }
}
