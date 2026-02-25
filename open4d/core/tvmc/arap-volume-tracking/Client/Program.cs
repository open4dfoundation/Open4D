//
// Copyright (c) 2022,2023 Jan Dvoøák, Zuzana Káèereková, Petr Vanìèek, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using Client.GLCompute;
using Framework;
using Framework.Tracking;
using MathNet.Numerics.LinearAlgebra;
using System;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Numerics;
using System.Reflection;
using System.Threading.Tasks;
using System.Xml.Serialization;

namespace Client
{
    public partial class Program
    {
        static Config config;

        static readonly NumberFormatInfo nfi_underscore = new NumberFormatInfo()
        {
            NumberDecimalSeparator = "_"
        };

        [STAThread]
        static void Main(string[] args)
        {
            Console.ForegroundColor = ConsoleColor.Gray;

            System.Threading.Thread.CurrentThread.CurrentCulture = new System.Globalization.CultureInfo("en-US");

            if ((config = ReadConfig(args)) == null)
            {
                Console.WriteLine("No config file found.");
                Console.WriteLine("Launch as:");
                Console.WriteLine("\tclient <pathToConfigFile>.");
                return;
            }

            PrintCurrentConfigSettings(config);
            InitOutputDirectory();

            Profiler.AddMeasure(new ProfilerTimeIntervalProvider());
            Profiler.AddMeasure(new ProfilerTotalTimeProvider() { FilterIn = new[] { "Frame" } });

            Profiler.AddOutput(new ProfilerConsoleOutput());
            Profiler.AddOutput(new ProfilerCSVOutput(typeof(ProfilerTimeIntervalProvider), "Frame", config.outDir) { FilterIn = new[] { "Frame", "Preprocess", "Tracking", "LloydCPUSingle" } });
            Profiler.AddOutput(new ProfilerXmlOutput(config.outDir));
            Profiler.Init();

            if (config.mode.Has(Mode.gtcmp))
            {
                GTComparison(config.gtDir);
            }
            else if (config.mode.Has(Mode.improvement))
            {
                Improvement();
            }
            else if (config.mode.Has(Mode.tracking))
            {
                Tracking();
            }
            else
            {
                PreprocessOnly();
            }

            Profiler.Finish();
        }

        private static void Improvement()
        {
            Vector4[][] pc = new Vector4[config.frameCount()][];
            VolumeGrid[] vg = new VolumeGrid[config.frameCount()];
            SmoothWeights extW = null;

            if (config.weightFile != null)
                extW = new ExternalWeights(config.weightFile);

            Profiler.Profile("Load", () =>
            {
                for (int i = 0; i < config.frameCount(); i++)
                {
                    vg[i] = new MemoryEfficientVolumeGrid(config.volumeDataFileName(i));
                    pc[i] = IO.LoadPCXYZ(config.resultFileNameXYZ(i));
                }
            });

            Profiler.Profile("StatsOrig", () => {
                Stats.OverallStats(vg, pc, $"orig_{config.pointCount}", config.outStatsDir, config.frameCount(), config.pointCount);
            });

            GlobalOptimizer go = new GlobalOptimizer(pc, vg,
                config.frameCount(), config.pointCount, config.maxIt, config.firstFrameFixed, config.applyLloyd, config.applySmooth, config.outStatsDir);

            for (int impr = 0; impr < config.numberOfImprovements; impr++)
            {
                SmoothWeights weights;
                if (config.weightFile == null)
                {
                    weights = new OverallTransformDistSmoothWeights(pc, config.gaussK0, config.gaussKMax, config.arapCutoff);
                    weights.UpdateFull(pc, vg);
                    weights.Cutoff(config.arapCutoff);
                    if (config.exportWeights)
                    {
                        weights.Export($"weights_{config.pointCount}_{config.smoothSigma2.ToString(nfi_underscore)}.bin");
                    }
                }
                else
                {
                    weights = extW;
                    weights.Cutoff(config.arapCutoff);
                }

                config.pointCount = Profiler.Profile("Filtering", () =>
                    go.Filter(config.filterCount, weights)
                );

                if (impr == 0)
                {
                    Profiler.Profile("StatsFiltered", () => {
                        Stats.OverallStats(vg, pc, $"filtered_{config.pointCount}", config.outStatsDir, config.frameCount(), config.pointCount);
                    });

                    for (int i = 0; i < config.frameCount(); i++)
                    {
                        IO.ExportPCXYZ(config.filteredResultFilenameXYZ(i), pc[i]);
                    }
                }

                pc = Profiler.Profile("Optimization", () =>
                    go.Optimize(weights)
                );

                for (int i = 0; i < config.frameCount(); i++)
                {
                    IO.ExportPCXYZ(config.improvedResultFilenameXYZ(i), pc[i]);
                    if (i > 0)
                    {
                        Transform[] transforms = new Transform[config.pointCount];
                        Parallel.For(0, config.pointCount, j =>
                        {
                            transforms[j] = ARAP.GetTransform(pc[i - 1], pc[i], j, weights);
                        });

                        IO.ExportTransforms(config.improvementTransformFilename(i), transforms);
                    }
                }

                Profiler.Profile("Stats", () =>
                    Stats.OverallStats(vg, pc, $"impr_{config.pointCount}", config.outStatsDir, config.frameCount(), config.pointCount)
                );
            }
        }

        private static void Tracking()
        {
            Vector4[][] pc = new Vector4[config.frameCount()][];
            double[] avg = new double[config.frameCount()];

            SmoothWeights weights = null;
            VolumeGrid vg = null;

            ForwardTracking fw = new ForwardTracking(
                config.presmooth, config.presmoothIterations,
                config.gradientThreshold,
                config.applyLloyd, config.applySmooth,
                $"{config.outDir}/energy"
            );

            int lastFrame = config.frameCount() - 1;

            for (int i = 0; i < config.frameCount(); i++)
            {
                using var profiler = new Profiler("Frame", $"{config.inputFileName(i)}");

                Profiler.Profile("Preprocess", config.mode.Any(Mode.preprocess) ? "calculated" : "loaded from files", () => Preprocess(ref pc, out vg, i));


                Profiler.Profile("Tracking", () => {
                    if (i == 0)
                    {
                        weights = DetermineWeights(pc);
                    }
                    else
                    {
                        weights.Cutoff(config.arapCutoff);

                        fw.TrackFrame(pc, i, weights, vg);

                        Transform[] transforms = new Transform[config.pointCount];
                        Parallel.For(0, config.pointCount, j =>
                        {
                            transforms[j] = ARAP.GetTransform(pc[i - 1], pc[i], j, weights);
                        });

                        IO.ExportTransforms(config.resultTransformFilename(i), transforms);

                        if (i < lastFrame)
                        {
                            Profiler.Profile("Affinity update", () => weights.Update(pc, i, vg));
                        }
                    }
                    IO.ExportPCXYZ(config.resultFileNameXYZ(i), pc[i]);

                });

                if (config.mode.Has(Mode.stats)) (avg[i], _) = vg.LloydStats(pc[i]);
            }

            double dfu = Stats.PrintLloydStats(avg, $"res_{config.pointCount}", config.outStatsDir);

            if (config.exportWeights)
            {
                weights?.Export("weights_fw_tracking.bin");
            }

            (double compactness, float[] contribution) = Stats.PCACompactness(pc);
            File.WriteAllText(config.outStatsDir + $"/PCAC_res_{config.pointCount}.txt", compactness.ToString());
            IO.SavePointStats(contribution, config.outStatsDir + $"/PCAC_contribution_{config.pointCount}.txt");

            using (StreamWriter sw = new StreamWriter($"{config.outStatsDir}/param.csv", true))
            {
                sw.WriteLine($"{config.smoothSigma};{config.smoothSigma2};{compactness};{dfu}");
            }

            if (config.mode.Has(Mode.preprocessVG_GPU))
                GLCompute.ComputeWindow.Instance().Stop();
        }

        private static void PreprocessOnly()
        {
            Vector4[][] pc = new Vector4[config.frameCount()][];

            VolumeGrid vg = null;

            for (int i = 0; i < config.frameCount(); i++)
            {
                using var profiler = new Profiler("Frame", $"{config.inputFileName(i)}");

                Profiler.Profile("Preprocess", config.mode.Any(Mode.preprocess) ? "calculated" : "loaded from files", () => Preprocess(ref pc, out vg, i));
            }

            if (config.mode.Has(Mode.preprocessVG_GPU))
                GLCompute.ComputeWindow.Instance().Stop();
        }

        private static void GTComparison(string gt_dir)
        {
            Vector4[] pc = null, gt = null;
            Transform[] pcTf = null, gtTf = null;
            VolumeGrid vg = null;
            double mse = 0;
            double mste = 0;
            double[] avg = new double[config.frameCount() - 1];

            int f = config.frameCount() - 1;

            for (int i = 1; i < config.frameCount(); i++)
            {
                Profiler.Profile("Frame", $"{i + 1}", () => {
                    Profiler.Profile("Loading", () => {
                        vg = new MemoryEfficientVolumeGrid(config.volumeDataFileName(i)/*, (byte)(config.multilevelVolumeGrid ? 255 : 1)*/);

                        pc = IO.LoadPCXYZ(config.mode.Has(Mode.improvement) ? config.improvedResultFilenameXYZ(i) : config.resultFileNameXYZ(i));
                        pcTf = IO.LoadTransforms(config.mode.Has(Mode.improvement) ? config.improvementTransformFilename(i) : config.resultTransformFilename(i));

                        gt = IO.LoadPCXYZ($"{gt_dir}/interaction_gt_{config.pointCount}_{i:D3}.xyz");
                        gtTf = IO.LoadTransforms($"{gt_dir}/interaction_gt_transform_{config.pointCount}_{i:D3}.txt");
                    });


                    mse += Profiler.Profile("MSE", () => Stats.MSE(gt, pc));
                    mste += Profiler.Profile("MSME", () => Stats.MSME(gtTf, pcTf, gt, vg));

                    (avg[i - 1], _) = Profiler.Profile("DFUF", () => vg.LloydStats(gt));
                });
            }

            mse /= f;
            mste /= f;

            string label = config.mode.Has(Mode.improvement) ? "impr" : "res";

            File.WriteAllText(config.outStatsDir + $"/MSE_{label}.txt", mse.ToString());
            File.WriteAllText(config.outStatsDir + $"/MSTE_{label}.txt", mste.ToString());

            double dfu = Stats.PrintLloydStats(avg, $"gt_{config.pointCount}", config.outStatsDir);

            Console.WriteLine($"MSE: {mse}");
            Console.WriteLine($"MSTE: {mste}");
            Console.WriteLine($"GT DFU: {dfu}");
        }


        private static SmoothWeights DetermineWeights(Vector4[][] pc)
        {
            if (config.weightFile != null)
            {
                return new ExternalWeights(config.weightFile);
            }
            else if (config.mode.Has(Mode.IIR))
            {
                return new TransformDistSmoothWeights(pc, config.gaussK0, config.gaussKMax, config.falloffStrength);
            }
            else
            {
                return new MaxTransformDistSmoothWeights(pc, config.gaussK0, config.gaussKMax, true, true);
            }
        }

        private static void InitOutputDirectory()
        {
            Directory.CreateDirectory(config.outDir);
            Directory.CreateDirectory(config.outStatsDir);

            if (config.mode.Any(Mode.tracking))
            {
                config.writeConfigXML($"{config.outDir}/config.xml");
            }

            if (config.mode.Has(Mode.improvement))
            {
                Directory.CreateDirectory(config.outDir + "/impr");
                Directory.CreateDirectory(config.outDir + "/filt");
            }
        }

        private static void Preprocess(ref Vector4[][] pc, out VolumeGrid vg, int i)
        {
            vg = null;
            var mode = config.mode;

            if (mode.Has(Mode.preprocess))
            {
                GLCompute.Voxelization voxelization = null;
                uint[] activeCells = null;
                int activeCellsVBO = 0;

                if (mode.Has(Mode.preprocessVG_GPU))
                    (vg, voxelization, activeCells, activeCellsVBO) = PreprocessMeshVgGpu(config, i);
                else
                    vg = PreprocessMeshVg(config, i);
                if (i == 0)
                {
                    if (mode.Has(Mode.preprocessPC_GPU))
                        pc[i] = PreprocessMeshPC_GPU(config, i, vg, voxelization, activeCells, activeCellsVBO);
                    else
                        pc[i] = PreprocessMeshPC(config, i, vg);
                }
            }
            else if (mode.Any(Mode.process))
            {
                vg = new MemoryEfficientVolumeGrid(config.volumeDataFileName(i)/*, (byte)(config.multilevelVolumeGrid ? 255 : 1)*/);
                if (i == 0)
                {
                    if (mode.Has(Mode.preprocessPC))
                        pc[i] = PreprocessMeshPC(config, i, vg);
                    else
                        pc[i] = IO.LoadPC(config.pointCloudFileName(i));
                }
            }
            else
            {
                pc[i] = IO.LoadPC(config.resultFileName(i));
            }
        }

        private static (VolumeGrid, Voxelization, uint[], int) PreprocessMeshVgGpu(Config config, int i)
        {
            Task<VolumeGrid> saveVG = null;
            Voxelization voxelization = null;
            uint[] activeCells = null;
            int activeCellsVBO = 0;

            Profiler.Profile("Volume grid construction", "GPU", () => {
                ObjLoader loader = new ObjLoader();
                TriangleMesh mesh = loader.Execute(config.inputFileName(i));
                voxelization = Voxelization.Instance(config);
                voxelization.Run(in mesh, out activeCells, out activeCellsVBO);
                var v = voxelization;
                saveVG = Task.Run(() => v.SaveAsVG(config.volumeDataFileName(i), true));
            });

            saveVG.Wait();

            return (saveVG.Result, voxelization, activeCells, activeCellsVBO);
        }


        /// <summary>
        /// 
        /// </summary>
        /// <param name="config"></param>
        /// <param name="i"></param>
        /// <param name="vg"></param>
        /// <param name="voxelization"></param>
        /// <param name="activeCells"></param>
        /// <param name="activeCellsVBO"></param>
        /// <returns></returns>
        private static Vector4[] PreprocessMeshPC_GPU(Config config, int i, VolumeGrid vg, GLCompute.Voxelization voxelization, uint[] activeCells, int activeCellsVBO)
        {
            var pts = Profiler.Profile("LloydGPUComplete", () =>
            {
                var volumeSampling = GLCompute.VolumeSampling.Instance(config);
                volumeSampling.Run(in activeCells, out var centers, out var centersVBO);

                var lloyd = GLCompute.Lloyd.Instance(config);
                lloyd.Run(activeCells, centers, activeCellsVBO, centersVBO, out var lloydCenters);

                return voxelization.IjklToVector4(lloydCenters);

            });

            IO.ExportPCBin(config.pointCloudFileName(i), pts);

#if DEBUG
            using var pointFile = new StreamWriter(config.pointCloudFileName(i) + ".xyz");
            for (int p = 0; p < pts.Length; p++)
            {
                pointFile.WriteLine($"{pts[p].X} {pts[p].Y} {pts[p].Z}");
            }
#endif
            return pts;

        }

        private static VolumeGrid PreprocessMeshVg(Config config, int i)
        {
            return Profiler.Profile("Building volume grid", () => {
                ObjLoader loader = new ObjLoader();
                TriangleMesh mesh = loader.Execute(config.inputFileName(i));
                VolumeGrid vg = new MemoryEfficientVolumeGrid(mesh, config.volumeGridResolution);
                vg.Save(config.volumeDataFileName(i));
                return vg;
            });
        }

        private static Vector4[] PreprocessMeshPC(Config config, int i, VolumeGrid vg)
        {
            return Profiler.Profile("Lloyd", () => {
                var pts = vg.RandomSample(config.pointCount);
                vg.Perturb(pts);
                for (int j = 0; j < 100; j++)
                {
                    var gradient = vg.LloydGradient(pts, 1.8f);
                    ApplyVectors(pts, gradient);
                    if ((j + 1) % 10 == 0)
                        Console.Write($"{j + 1}% ");
                }
                Console.WriteLine();
                IO.ExportPCBin(config.pointCloudFileName(i), pts);
                return pts;
            });

        }

        private static void ApplyVectors(Vector4[] pc, Vector4[] vect)
        {
            for (int i = 0; i < pc.Length; i++)
                pc[i] += vect[i];
        }


        static void PrintCurrentConfigSettings(Config currentConfig)
        {
            Console.WriteLine("Current configuration: ");
            var properties = typeof(Config).GetProperties(BindingFlags.Public | BindingFlags.Instance);
            foreach (var property in properties)
            {
                if (property.GetValue(currentConfig) == null) continue;
                if (property.PropertyType == typeof(bool) && (bool)property.GetValue(currentConfig) == false) continue;

                var browsable = (XmlIgnoreAttribute)(property.GetCustomAttribute(typeof(XmlIgnoreAttribute)));
                if (browsable is not null) continue;

                var descriptionAttr = (DescriptionAttribute)property.GetCustomAttribute(typeof(DescriptionAttribute));
                string description = descriptionAttr?.Description;
                string value = property.GetValue(currentConfig).ToString();
                Console.Write($"{property.Name} \t= ");
                var fcolor = Console.ForegroundColor;
                Console.ForegroundColor = ConsoleColor.White;
                Console.Write($"{value}");
                Console.ForegroundColor = ConsoleColor.DarkGray;
                if (description != null) Console.Write($"\t// {description}");
                Console.ForegroundColor = fcolor;
                Console.WriteLine();
            }
            Console.WriteLine("--------------------------------------------");
        }

        private static Config ReadConfig(string[] args)
        {
            string fn = "config.xml";

            if (args.Length > 0)
                fn = args[0];

            if (!File.Exists(fn))
                return null;
            return Config.loadConfigXML(fn);
        }
    }
}
