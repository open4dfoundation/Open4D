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

namespace Framework.Tracking
{
    public class ForwardTracking
    {
        //SmoothWeights weights;
        private readonly bool presmooth = true;
        private readonly int presmoothIterations = 10;
        private readonly float gradientThreshold;
        private readonly float applyLloyd = 1f;
        private readonly float applyARAP = 1f;
        private readonly string energyDir;

        public ForwardTracking(bool presmooth, int presmoothIterations, float gradientThreshold, float applyLloyd, float applyARAP, string energyDir)
        {
            this.presmooth = presmooth;
            this.presmoothIterations = presmoothIterations;
            this.gradientThreshold = gradientThreshold;
            this.applyLloyd = applyLloyd;
            this.applyARAP = applyARAP;
            this.energyDir = energyDir;
        }

        public Vector4[] TrackFrame(Vector4[][] pc, int i, SmoothWeights weights, VolumeGrid vg)
        {
            if (i != 0)
            {
                pc[i] = Profiler.Profile("Extrapolating", () => Extrapolate(pc, i));
                Profiler.Profile("Presmoothing", () => Presmooth(pc, weights, i));
                Profiler.Profile("Optimization", () => OptimizeFrame(pc, weights, vg, i));
            }

            //vg.VerifyInside(pc[i], "Extrapolated");
            return pc[i];
        }

        private static Vector4[] Extrapolate(Vector4[][] pc, int i)
        {
            if (i == 1)
                return (Vector4[])pc[0].Clone();
            Vector4[] result = new Vector4[pc[0].Length];
            for (int j = 0; j < result.Length; j++)
            {
                result[j] = pc[i - 1][j] + (pc[i - 1][j] - pc[i - 2][j]);
            }
            return result;
        }

        private static void ApplyVectors(Vector4[] pc, Vector4[] vect)
        {
            for (int i = 0; i < pc.Length; i++)
                pc[i] += vect[i];
        }

        private void Presmooth(Vector4[][] pc, SmoothWeights weights, int i)
        {
            if (presmooth)
            {
                for (int j = 0; j < presmoothIterations; j++)
                {
                    ApplyVectors(pc[i], ARAP.ARAPGradient(pc[i - 1], pc[i], weights));
                }
            }
        }

        private void OptimizeFrame(Vector4[][] pc, SmoothWeights weights, VolumeGrid vg, int i)
        {
            double thresholdSquared = gradientThreshold * gradientThreshold;
            double maxDist;
            double energy;

            int maxit = 1000;
            int it = 0;

            bool stop = false;

            while (!stop && it < maxit)
            {
                Vector4[] smoothGradient = ARAP.ARAPGradient(pc[i - 1], pc[i], weights);
                Vector4[] lloydGradient = vg.LloydGradient(pc[i], 1.0f);

                Vector4 A;
                Vector4 B;
                energy = 0;

                double lloydTotal = 0;
                double smoothTotal = 0;
                maxDist = double.MinValue;

                for (int j = 0; j < pc[i].Length; j++)
                {
                    var oldPoint = pc[i][j];
                    A = pc[i][j] + lloydGradient[j];
                    B = pc[i][j] + smoothGradient[j];
                    //vg.VerifyInside(A, "Lloyd");
                    //vg.VerifyInside(B, "ARAP");

                    pc[i][j] = (applyLloyd * A + applyARAP * B) / (applyLloyd + applyARAP);
                    //vg.VerifyInside(pc[i][j], "Combined");

                    var lloydEnergy = (A - pc[i][j]).LengthSquared();
                    var smoothEnergy = (B - pc[i][j]).LengthSquared();

                    energy += lloydEnergy + smoothEnergy;
                    maxDist = Math.Max(maxDist, (pc[i][j] - oldPoint).LengthSquared());

                    lloydTotal += lloydEnergy;
                    smoothTotal += smoothEnergy;

                    if (double.IsNaN(lloydTotal) || double.IsNaN(smoothTotal))
                    {
                        Console.WriteLine("NaN energy at {0}!!!", j);
                        Console.WriteLine("Old point: {0}", oldPoint);
                        Console.WriteLine("New point: {0}", pc[i][j]);
                        Console.WriteLine("Lloyd prediction: {0}", A);
                        Console.WriteLine("Smooth prediction: {0}", B);
                        throw new Exception($"NaN energy at {j}!!!");
                    }
                }

                IO.PrintEnergy(energyDir, i, lloydTotal, 0, smoothTotal);

                if (maxDist < thresholdSquared)
                    stop = true;

                it++;
            }

            //if (config.saveGradients)
            //    GradientStats.exportGrad(config, i, 0,
            //        new Vector4[][] { smoothGradHistory, uniGradHistory, inertiaGradHistory },
            //        new string[] { "smooth", "uni", "inertia" });
        }
    }
}
