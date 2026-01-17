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
    public static class Distances
    {
        public static float[,] SpatialDistance(Vector4[] pc)
        {
            int n = pc.Length;
            float[,] result = new float[n, n];

            Parallel.For(0, n, i => {
                Vector4 pos = pc[i];

                for (int j = i + 1; j < n; j++)
                {
                    float dist = Vector4.DistanceSquared(pos, pc[j]);
                    result[i, j] = dist;
                    result[j, i] = dist;
                }
            });

            return result;
        }

        static private readonly Vector4[] coordFrame = new Vector4[]
        {
            Vector4.Zero,
            Vector4.UnitX * -1f,
            Vector4.UnitX,
            Vector4.UnitY * -1f,
            Vector4.UnitY,
            Vector4.UnitZ * -1f,
            Vector4.UnitZ
        };

        static private List<Vector4> GetLocalFrame(Vector4 pos, float delta)
        {
            List<Vector4> res = new();

            for (int i = 0; i < coordFrame.Length; i++)
            {
                res.Add(coordFrame[i] * delta + pos);
            }

            return res;
        }

        private static List<Vector4>[] GetPointsLocal(Vector4[] pc)
        {
            List<Vector4>[] points = new List<Vector4>[pc.Length];

            for (int i = 0; i < pc.Length; i++)
            {
                points[i] = GetLocalFrame(pc[i], 0.01f);
            }

            return points;
        }

        public static (float[,], Transform[]) TransformDistance(Vector4[] prev, Vector4[] current, SmoothWeights affinity, VolumeGrid vg = null)
        {
            int n = current.Length;
            float[,] result = new float[n, n];
            List<Vector4>[] points = (vg == null) ? GetPointsLocal(current) : vg.GetPoints(current);
            Transform[] transforms = new Transform[n];
            TransformDistance[] dst = new TransformDistance[n];

            Parallel.For(0, n, i =>
            {
                transforms[i] = ARAP.GetTransform(prev, current, i, affinity);
                dst[i] = new TransformDistance(points[i]);
            });

            Parallel.For(0, n, i =>
            {
                TransformDistance currentDst = dst[i];
                Transform A = transforms[i];

                for (int j = 0; j < n; j++)
                {

                    Transform B = transforms[j];
                    result[i,j] = currentDst.GetTransformDistance(A.R, B.R, A.t, B.t);
                }
            });

            return (result, transforms);
        }
    }
}
