//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace Framework
{
    public static class IO
    {
        private readonly static NumberFormatInfo nfi = new NumberFormatInfo() {
            NumberDecimalSeparator = ".",
            NumberGroupSeparator = ","
        };

        public static Vector4[] LoadPC(string v)
        {
            using BinaryReader br = new BinaryReader(new FileStream(v, FileMode.Open));
            int c = br.ReadInt32();
            Vector4[] r = new Vector4[c];
            for (int i = 0; i < c; i++)
            {
                float x = br.ReadSingle();
                float y = br.ReadSingle();
                float z = br.ReadSingle();
                r[i] = new Vector4(x, y, z, 0);
            }
            br.Close();
            return r;
        }

        public static Vector4[] LoadPCXYZ(string v)
        {
            List<Vector4> points = new List<Vector4>();

            StreamReader sr;
            try
            {
                sr = new StreamReader(v);
            }
            catch (FileNotFoundException e)
            {
                throw new Exception(e.Message);
            }

            string line = sr.ReadLine();

            while (line != null)
            {
                line = line.Trim();
                points.Add(ParseLine(line));


                line = sr.ReadLine();
            }

            return points.ToArray();
        }

        private static Vector4 ParseLine(string line)
        {
            string[] entries = line.Split(' ');

            if (entries.Length != 3)
            {
                throw new Exception("Invalid data format");
            }

            Vector4 position = new Vector4
            {
                X = float.Parse(entries[0], nfi),
                Y = float.Parse(entries[1], nfi),
                Z = float.Parse(entries[2], nfi)
            };

            return position;
        }

        public static void ExportInterStats(string fn, float[][] stats)
        {
            FileStream fs = new FileStream(fn, FileMode.Create);
            BinaryWriter bw = new BinaryWriter(fs);

            bw.Write(stats.Length);

            for (int i = 0; i < stats.Length; i++)
            {
                for (int j = 0; j < stats.Length; j++)
                {
                    bw.Write(stats[i][j]);
                }
            }
            bw.Close();
        }

        public static void ExportInterStats(string fn, float[,] stats)
        {
            FileStream fs = new FileStream(fn, FileMode.Create);
            BinaryWriter bw = new BinaryWriter(fs);

            int n = stats.GetLength(0);

            bw.Write(n);

            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++)
                {
                    bw.Write(stats[i,j]);
                }
            }
            bw.Close();
        }

        public static Transform ParseTransform(string line)
        {
            string[] entries = line.Split(' ');

            if (entries.Length != 12)
            {
                throw new Exception("Invalid data format");
            }

            float[,] R = new float[,]
            {
                { float.Parse(entries[0]), float.Parse(entries[1]), float.Parse(entries[2])},
                { float.Parse(entries[3]), float.Parse(entries[4]), float.Parse(entries[5])},
                { float.Parse(entries[6]), float.Parse(entries[7]), float.Parse(entries[8])},
            };

            float[] t = new float[] { float.Parse(entries[9]), float.Parse(entries[10]), float.Parse(entries[11]) };

            return new Transform(R, t);
        }

        public static void ExportPCBin(string v, Vector4[] data)
        {
            using BinaryWriter bw = new BinaryWriter(new FileStream(v, FileMode.Create));
            bw.Write(data.Length);
            for (int i = 0; i < data.Length; i++)
            {
                bw.Write(data[i].X);
                bw.Write(data[i].Y);
                bw.Write(data[i].Z);
            }
            bw.Close();
        }

        public static void ExportPCXYZ(string v, Vector4[] data)
        {
            using var bw = new StreamWriter(new FileStream(v, FileMode.Create));
            for (int i = 0; i < data.Length; i++)
            {
                bw.WriteLine($"{data[i].X} {data[i].Y} {data[i].Z}");
            }
            bw.Close();
        }

        public static void ExportTransforms(string v, Transform[] data)
        {
            using var bw = new StreamWriter(new FileStream(v, FileMode.Create));
            for (int i = 0; i < data.Length; i++)
            {
                bw.WriteLine(data[i].ToString());
            }
            bw.Close();
        }

        public static Transform[] LoadTransforms(string v)
        {
            List<Transform> transforms = new List<Transform>();

            StreamReader sr;
            try
            {
                sr = new StreamReader(v);
            }
            catch (FileNotFoundException e)
            {
                throw new Exception(e.Message);
            }

            string line = sr.ReadLine();

            while (line != null)
            {
                line = line.Trim();
                transforms.Add(ParseTransform(line));


                line = sr.ReadLine();
            }

            return transforms.ToArray();
        }

        public static void SavePointStats(float[] stats, string fn)
        {
            StreamWriter sw = new StreamWriter(fn);
            for (int i = 0; i < stats.Length; i++)
            {
                sw.WriteLine(stats[i]);
            }
            sw.Close();
        }

        public static void PrintEnergy(string energyDir, int i, double lloydEnergy, double inertiaEnergy, double smoothEnergy)
        {
            //var energyDir = $"{config.outDir}/energy";
            if (!Directory.Exists(energyDir))
                Directory.CreateDirectory(energyDir);

            using StreamWriter sw = new StreamWriter($"{energyDir}/{i}_energy_log.csv", true);
            sw.WriteLine($"{lloydEnergy}\t{inertiaEnergy}\t{smoothEnergy}");
        }
    }
}
