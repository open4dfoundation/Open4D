//
// Copyright (c) 2022,2023 Jan Dvoøák, Zuzana Káèereková, Petr Vanìèek, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace Framework.Tracking
{
    public class CanonicalNeighborhood
    {
        public Vector4[] canonical;
        private float[] weights;
        private int internalIndex;
        private List<int> neighbors;
        private int n;
        public double err;

        public CanonicalNeighborhood(Vector4[][] pc, SmoothWeights smoothWeights, int index)
        {
            neighbors = smoothWeights.nn[index];
            n = neighbors.Count;

            weights = new float[n];
            canonical = new Vector4[n];

            for (int i = 0; i < n; i++)
            {
                int neighbor = neighbors[i];
                if (neighbor == index)
                {
                    internalIndex = i;
                }

                weights[i] = smoothWeights.weights[index][neighbor];
                canonical[i] = pc[0][neighbor];
            }

            Optimise(pc);
        }

        private static void Add(Vector4[] a, Vector4[] b)
        {
            for (int i = 0; i < a.Length; i++)
            {
                a[i] += b[i];
            }
        }

        private static void Scale(Vector4[] a, float scale)
        {
            for (int i = 0; i < a.Length; i++)
            {
                a[i] *= scale;
            }
        }

        private void Optimise(Vector4[][] pc)
        {
            //create local neighborhoods
            Vector4[][] local = new Vector4[pc.Length][];
            for (int frame = 0; frame < pc.Length; frame++)
            {
                local[frame] = new Vector4[n];

                for (int j = 0; j < n; j++)
                {
                    int neighbor = neighbors[j];
                    local[frame][j] = pc[frame][neighbor];
                }
            }

            //TODO set the number of iterations
            for (int it = 0; it < 5; it++)
            {
                err = 0.0;
                Vector4[] newCan = new Vector4[n];
                for (int frame = 0; frame < pc.Length; frame++)
                {
                    Vector4[] fitted = ARAP.Fit(local[frame], canonical, weights);
                    err += (1.0 / pc.Length) * Stats.MSE(fitted, canonical);
                    Add(newCan, fitted);
                }
                Scale(newCan, 1f / pc.Length);
                canonical = newCan;
            }
        }

        public Vector4 Predict(Vector4[] pc)
        {
            Vector4[] local = new Vector4[n];
            for (int j = 0; j < n; j++)
            {
                int neighbor = neighbors[j];
                local[j] = pc[neighbor];
            }

            Transform A = ARAP.GetTransform(canonical, local, weights);
            return A.Apply(canonical[internalIndex]);
        }

        public Vector4[] Fit(Vector4[] pc)
        {
            Vector4[] local = new Vector4[n];
            for (int j = 0; j < n; j++)
            {
                int neighbor = neighbors[j];
                local[j] = pc[neighbor];
            }

            return ARAP.Fit(canonical, local, weights);
        }

        public static Vector4[] CanonicalGradient(Vector4[] pts, CanonicalNeighborhood[] canonical)
        {
            Vector4[] gradient = new Vector4[pts.Length];
            _ = Parallel.For(0, gradient.Length, i =>
            //for (int i = 0; i < gradient.Length; i++)
            {
                Vector4 prediction = canonical[i].Predict(pts);
                gradient[i] = (prediction - pts[i]);
            });

            return gradient;
        }
    }
}
