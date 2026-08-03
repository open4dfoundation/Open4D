//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;

namespace Framework
{
    public class MaxTransformDistSmoothWeights : SmoothWeights
    {
        private readonly float kSp;
        private readonly float kTf;
        private readonly bool useSp;
        private readonly bool useTf;
        private readonly float[,] maxSpDist;
        private readonly float[,] maxTfDist;

        public MaxTransformDistSmoothWeights(Vector4[][] pc, float kSp, float kTf, bool useSp = true, bool useTf = true) : base(pc[0].Length)
        {
            this.kSp = kSp;
            this.kTf = kTf;
            this.useSp = useSp;
            this.useTf = useTf;

            float[,] spDist = Distances.SpatialDistance(pc[0]);
            maxSpDist = (useSp) ? spDist: new float[n,n];
            maxTfDist = new float[n,n];

            UpdateFromDistances(spDist, kSp);
        }

        private void UpdateMax(float[,] max, float[,] dist)
        {
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < n; j++)
                {
                    if (dist[i, j] > max[i, j])
                    {
                        max[i, j] = dist[i, j];
                    }
                }
            }
        }

        private void UpdateMaxSym(float[,] max, float[,] dist)
        {
            for (int i = 0; i < n; i++)
            {
                for (int j = i; j < n; j++)
                {
                    if (dist[i, j] > max[i, j])
                    {
                        max[i, j] = dist[i, j];
                        max[j, i] = dist[i, j];
                    }
                }
            }
        }

        public override void Update(Vector4[][] pc, int frame, VolumeGrid vg = null)
        {
            if (useSp)
            {
                float[,] spDist = Distances.SpatialDistance(pc[frame]);
                UpdateMaxSym(maxSpDist, spDist);
            }

            if (useTf)
            {
                (float[,] tfDist, _) = Distances.TransformDistance(pc[frame - 1], pc[frame], this, vg);
                UpdateMax(maxTfDist, tfDist);
            }

            Update();
        }

        private void Update()
        {
            Parallel.For(0, weights.GetLength(0), i =>
            {
                for (int j = 0; j < weights.GetLength(0); j++)
                {
                    weights[i][j] = MathF.Exp(-kSp * maxSpDist[i, j]) * MathF.Exp(-kTf * maxTfDist[i, j]);
                }
            });
        }

        public override void UpdateFull(Vector4[][] pc, VolumeGrid[] vg = null)
        {
            throw new NotImplementedException();
        }
    }
}
