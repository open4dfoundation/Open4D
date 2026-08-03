//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Numerics;
using System.Threading.Tasks;

namespace Framework
{
    public abstract class SmoothWeights
    {
        public int n;
        public float[][] weights;
        public List<int>[] nn;
        
        public SmoothWeights(int n)
        {
            if (n != -1)
            {
                Init(n);
            }
        }

        protected void Init(int n)
        {
            this.n = n;

            nn = new List<int>[n];

            weights = new float[n][];
            for (int i = 0; i < n; i++)
            {
                weights[i] = new float[n];
            }
        }

        public void Cutoff(float value)
        {
            Parallel.For(0, weights.GetLength(0), i =>
            //for (int i = 0; i < weights.Length; i++)
            {
                nn[i] = new List<int>();
                for (int j = 0; j < weights.Length; j++)
                {
                    if (weights[i][j] > value)
                    {
                        nn[i].Add(j);
                    }
                }
            });
        }

        public HashSet<int> DetectIrregular()
        {
            HashSet<int> irregular = new();

            for (int i = 0; i < nn.Length; i++)
            {
                if (nn[i].Count <= 2)
                {
                    irregular.Add(i);
                }
            }

            return irregular;
        }

        public void Export(string fn)
        {
            FileStream fs = new FileStream(fn, FileMode.Create);
            BinaryWriter bw = new BinaryWriter(fs);

            bw.Write(weights.Length);

            for (int i = 0; i < weights.Length; i++)
            {
                for (int j = 0; j < weights.Length; j++)
                {
                    bw.Write(weights[i][j]);
                }
            }
            bw.Close();

            Console.WriteLine("Weights written into file: {0}", fn);
        }

        public float[] WeightStats()
        {
            float avg = 0f;
            int min = int.MaxValue;
            int max = 0;

            float[] sum = new float[n];

            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++)
                {
                    sum[i] += weights[i][j];
                }


                int count = nn[i].Count;

                avg += count;
                if (count < min)
                {
                    min = count;
                }

                if (count > max)
                {
                    max = count;
                }
            };

            //if (first)
            //{
            //    first = false;
            Console.WriteLine("Weight statistics:");
            Console.WriteLine("AVG count: {0}", avg / weights.GetLength(0));
            Console.WriteLine("Max count: {0}", max);
            Console.WriteLine("Min count: {0}", min);

            return sum;
        }

        protected void UpdateFromDistances(float[,] dist, float k0)
        {
            Parallel.For(0, weights.GetLength(0), i =>
            {
                for (int j = 0; j < weights.GetLength(0); j++)
                {
                    weights[i][j] = (float)Math.Exp(-k0 * dist[i, j]);
                }
            });
        }

        public abstract void Update(Vector4[][] pc, int frame, VolumeGrid vg = null);

        public abstract void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null);
    }

    public abstract class SmoothWeightsWithHistory : SmoothWeights
    {
        public SmoothWeightsWithHistory(int n) : base(n)  {
            if (n != -1)
            {
                Init(n);
            }
        }

        protected new void Init(int n)
        {
            base.Init(n);

            history = new float[n][];
            historyNew = new float[n][];
            for (int i = 0; i < n; i++)
            {
                history[i] = new float[n];
                historyNew[i] = new float[n];
            }
        }

        public float[][] history, historyNew;

        public void ExportHistory(string fn)
        {
            FileStream fs = new FileStream(fn, FileMode.Create);
            BinaryWriter bw = new BinaryWriter(fs);

            bw.Write(history.Length);

            for (int i = 0; i < history.Length; i++)
            {
                for (int j = 0; j < history.Length; j++)
                {
                    bw.Write(history[i][j]);
                }
            }
            bw.Close();

            Console.WriteLine("Weights written into file: {0}", fn);
        }
    }

    public class ExternalWeights : SmoothWeights
    {
        public ExternalWeights(string fn) : base(-1)
        {
            Console.WriteLine("Loading external weights from file: {0}", fn);

            FileStream fs = new FileStream(fn, FileMode.Open);
            BinaryReader br = new BinaryReader(fs);
            int n = br.ReadInt32();

            Init(n);

            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++)
                {
                    weights[i][j] = br.ReadSingle();
                }
            }
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            //do nothing, external weights are static
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            //do nothing, external weights are static
        }
    }
}
