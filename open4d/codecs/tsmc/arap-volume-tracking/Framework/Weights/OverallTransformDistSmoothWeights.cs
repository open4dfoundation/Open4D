//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace Framework
{
    public class OverallTransformDistSmoothWeights : SmoothWeights
    {
        private readonly float kMax;
        private readonly SmoothWeights initAffinity;
        private bool initialized = false;

        public OverallTransformDistSmoothWeights(Vector4[][] pc, float k0, float kMax, float cutoff, SmoothWeights initAffinity = null) : base(pc[0].Length)
        {
            this.kMax = kMax;

            if (initAffinity == null)
            {
                this.initAffinity = new MaxDistSmoothWeights(pc, k0);
            }
            else
            {
                this.initAffinity = initAffinity;
            }
            
            this.initAffinity.UpdateFull(pc);
            this.initAffinity.Cutoff(cutoff);
        }

        private static float[,] GetDistances(Vector4[][] pc, SmoothWeights affinity, VolumeGrid[] vg = null)
        {
            //affinity.Export("initAff.bin");

            int n = pc[0].Length;

            float[,] max = new float[n, n];

            //Directory.CreateDirectory("tf_out");

            for (int frame = 1; frame < pc.Length; frame++)
            {
                (float[,] dist, Transform[] tfs) = Distances.TransformDistance(pc[frame - 1], pc[frame], affinity, vg?[frame]);
                //IO.ExportTransforms($"tf_out/tf_{frame}.txt", tfs);

                Parallel.For(0, n, i =>
                {
                    for (int j = 0; j < n; j++)
                    {
                        if (dist[i,j] > max[i, j])
                        {                            
                            max[i, j] = dist[i,j];
                        }
                    }
                });
            }

            //IO.ExportInterStats("max_tf_dist.bin", max);

            return max;
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            //throw new NotImplementedException();
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            SmoothWeights aff = initialized ? this : initAffinity;
            initialized = true;

            UpdateFromDistances(GetDistances(pc, aff, vg), kMax);
        }
    }
}
