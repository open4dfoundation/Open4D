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
    public class GlobalOptimizer
    {
        private const float initLambda = 0.1f;
        private const float lambdaMult = 0.5f;
        private readonly int maxIt = 30;
        

        private Vector4[][] pc;
        private Vector4[][] candidate;
        private readonly Vector4[][] grad;
        private readonly VolumeGrid[] vg;
        private readonly int frameCount;
        private int pointCount;
        private readonly string outStatDir;
        
        private readonly int start;
        private readonly bool firstFrameFixed;
        
        private readonly float applyLloyd;
        private readonly float applyARAP;
        //private readonly float alpha, beta;

        public GlobalOptimizer(Vector4[][] pc, VolumeGrid[] vg, int frameCount, int pointCount, int maxIt, bool firstFrameFixed, float applyLloyd, float applyARAP, string outStatDir)
        {
            this.pc = pc;
            this.vg = vg;
            this.frameCount = frameCount;
            this.pointCount = pointCount;
            start = (firstFrameFixed) ? 1 : 0;
            this.firstFrameFixed = firstFrameFixed;
            this.grad = new Vector4[frameCount][];
            this.candidate = new Vector4[frameCount][];

            this.applyLloyd = applyLloyd;
            this.applyARAP = applyARAP;

            //alpha = applyLloyd / (applyLloyd + applyARAP);
            //beta = applyARAP / (applyLloyd + applyARAP);

            this.maxIt = maxIt;

            this.outStatDir = outStatDir;
        }

        private static Vector4[] Mix(Vector4[] a, float multA, Vector4[] b, float multB)
        {
            Vector4[] result = new Vector4[a.Length];

            for (int i = 0; i < a.Length; i++)
            {
                result[i] = multA * a[i] + multB * b[i];
            }

            return result;
        }

        private static Vector4[] Mix(Vector4[] a, float multA, Vector4[] b, float multB, Vector4[] c, float multC)
        {
            Vector4[] result = new Vector4[a.Length];

            for (int i = 0; i < a.Length; i++)
            {
                result[i] = multA * a[i] + multB * b[i] + multC * c[i];
            }

            return result;
        }

        private static (float, float, float) Energy(Vector4[] a, float multA, Vector4[] b1, Vector4[] b2, float multB)
        {
            float energy = 0f;
            float aEnergy = 0f;
            float bEnergy = 0f;

            for (int i = 0; i < a.Length; i++)
            {
                float mA = multA * a[i].LengthSquared();
                float mB = multB * (b1[i].LengthSquared() + b2[i].LengthSquared());

                aEnergy += mA;
                bEnergy += mB;
                energy += mA + mB;
            }

            return (energy, aEnergy, bEnergy);
        }

        public int Filter(int filterCount, SmoothWeights weights)
        {
            //Obtain a set of indices to be deleted
            HashSet<int> irregular = new();

            (int[] idx, float[] dists) = Stats.IrregularitySorted(pc);

            int index = 0;

            //Altered detection which permits prior selection of different set of irregular centers
            while (irregular.Count != filterCount)
            {
                if (!irregular.Contains(idx[index]))
                {
                    Console.WriteLine($"{idx[index]} - {dists[index]}");
                    irregular.Add(idx[index]);
                }
                

                index++;
            }

            float[] detected = new float[pointCount];
            for (int i = 0; i < pointCount; i++)
            {
                if (irregular.Contains(i))
                {
                    detected[i] = 1;
                }
            }

            //IO.SavePointStats(detected, "detected_irregular.csv");

            Filter filter = new Filter(irregular, pc[0].Length);
            for (int i = 0; i < frameCount; i++)
            {
                pc[i] = filter.FilterPointCloud(pc[i]);
            }

            filter.FilterWeights(weights);

            this.pointCount = filter.Reduced;

            return filter.Reduced;
        }

        private (Vector4[], Vector4[], Vector4[]) GetGradient(Vector4[][] input, int f, SmoothWeights weights)
        {
            Vector4[] lloydGradient = vg[f].LloydGradient(input[f], 1.0f);
            Vector4[] fwARAP = (f == 0) ? new Vector4[input[f].Length] : ARAP.ARAPGradient(input[f - 1], input[f], weights);
            Vector4[] bwARAP = (f == frameCount - 1) ? new Vector4[input[f].Length] : ARAP.ARAPGradient(input[f + 1], input[f], weights);

            return (lloydGradient, fwARAP, bwARAP);
        }

        private float EvalGradEnergy(SmoothWeights weights, Vector4[][] input)
        {
            float energy = 0f;
            for (int f = start; f < frameCount; f++)
            {
                (Vector4[] lloydGradient, Vector4[] fwGradient, Vector4[] bwGradient) = GetGradient(input, f, weights);

                grad[f] = Mix(lloydGradient, applyLloyd, fwGradient, applyARAP, bwGradient, applyARAP);

                (float e, _, _) = Energy(lloydGradient, applyLloyd, fwGradient, bwGradient, applyARAP);
                energy += e;
            }

            return energy;
        }

        private float EvalEnergy(SmoothWeights weights, Vector4[][] input)
        {
            float energy = 0f;
            float lloydEnergy = 0f;
            float arapEnergy = 0f;
            for (int f = start; f < frameCount; f++)
            {
                (Vector4[] lloydGradient, Vector4[] fwGradient, Vector4[] bwGradient) = GetGradient(input, f, weights);
                (float e, float lE, float aE) = Energy(lloydGradient, applyLloyd, fwGradient, bwGradient, applyARAP);
                energy += e;
                lloydEnergy += lE;
                arapEnergy += aE;
            }

            Console.WriteLine($"Lloyd: {lloydEnergy}, ARAP: {arapEnergy}");

            return energy;
        }

        public Vector4[][] Optimize(SmoothWeights weights)
        {
            if (firstFrameFixed)
            {
                grad[0] = new Vector4[pointCount];
            }

            bool improved = true;
            for (int it = 0; it < maxIt; it++)
            {
                if (!improved)
                {
                    break;
                }

                Profiler.Profile($"Iteration no. {it + 1}", (Action)(() => {
                    //Calculate gradient and original energy
                    float origEnergy = Profiler.Profile("Gradient evaluation", () => EvalGradEnergy(weights, pc));

                    improved = false;

                    //Try to improve the position

                    float lambda = initLambda;
                    for (int attempt = 0; attempt < 20; attempt++)
                    {
                        //Create candidate positions as orig + lambda * grad
                        Parallel.For(0, frameCount, (f) =>
                        {
                            candidate[f] = Mix(pc[f], 1f, grad[f], lambda);
                        });

                        float currentEnergy = Profiler.Profile("Global energy of candidate", () => EvalEnergy(weights, candidate));

                        Console.WriteLine("Lambda = {0:E4}, orig: {1}, new: {2}", lambda, origEnergy, currentEnergy);

                        //Check energy
                        //If better then accept, otherwise repeat
                        if (currentEnergy < origEnergy)
                        {
                            Console.WriteLine("Energy improved!");
                            pc = candidate;
                            candidate = new Vector4[frameCount][];
                            improved = true;
                            break;
                        }

                        lambda *= lambdaMult;
                    }
                }));

                Profiler.Profile("StatsOrig", () => {
                    Stats.OverallStats(vg, pc, $"it_{it}_{pointCount}", outStatDir, frameCount, pointCount);
                });
            }

            return pc;
        }
    }
}
