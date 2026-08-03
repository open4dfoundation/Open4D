//
// Copyright (c) 2022,2023 Jan Dvořák, Zuzana Káčereková, Petr Vaněček, Lukáš Hruda, Libor Váša
// Licensed under the MIT License
//

using Framework;
using System;
using System.ComponentModel;
/*using DameCL;
using MeshBrowser.SlimDXRenderer;*/
using System.IO;
using System.Xml.Serialization;

namespace Client
{
    public class Config
    {
        [Description("Processing mode")]
        public Mode mode { get; set; } = Mode.process;

        #region IO Settings

        [Description("Input file name prefix (without 3 trailing numbers)")]
        public string fileNamePrefix { get; set; } = "ball";


        [Description("Input directory")]
        public string inDir { get; set; } = "ball";

        public string gtDir { get; set; } = "";


        [Description("Output directory")]
        public string outDir { get; set; } = "ball_res";

        internal string inPath { get; set; }
        internal string outPath { get; set; }
        internal string outImprPath { get; set; }
        internal string outFiltPath { get; set; }

        internal string outStatsDir { get; set; }

        [Description("File name starting index (3 trailing numbers)")]
        public int firstIndex { get; set; } = 0;

        [Description("File name last index")]
        public int lastIndex { get; set; } = 545;

        #endregion
        [XmlIgnore]
        public bool reverse { get; set; } = false;

        //public int lastIndex = 545;
        //public int lastIndex = 250;
        //public int lastIndex = 5;

        [XmlIgnore]
        public bool presmooth { get; set; } = true;
        [XmlIgnore]
        public int presmoothIterations { get; set; } = 10;
        [Description("Resolution of the volume grid")]
        public int volumeGridResolution { get; set; } = 512;
        [Description("Number of tracked points")]
        public int pointCount { get; set; } = 1000;
        [Description("Threshold for gradient element size for stopping optimization")]
        public float gradientThreshold { get; set; } = 0.00001f;

        [Description("Controls falloff for distance based affinity")]
        public float smoothSigma { get; set; } = 0.1861f;
        [Description("Controls falloff transformation difference based affinity")]
        public float smoothSigma2 { get; set; } = 0.01f;

        [Description("Controls IIR filter")]
        public float falloffStrength { get; set; } = 0.01f;

        [XmlIgnore]
        public float arapCutoff { get; set; } = 0.001f;
        [XmlIgnore]
        internal float gaussK0 { get; set; }
        [XmlIgnore]
        internal float gaussKMax { get; set; }
        //internal float gaussKResultSmooth;
        [XmlIgnore]
        public bool exportWeights { get; set; }
        [XmlIgnore]
        public bool exportWeightsEveryFrame { get; set; }
        [XmlIgnore]
        public string weightFile { get; set; } = null;

        [Description("Weight for smoothness term")]
        public float applySmooth { get; set; } = 1f;
        [Description("Weight for uniformness term")]
        public float applyLloyd { get; set; } = 1f;

        public bool firstFrameFixed { get; set; } = false;

        public int filterCount { get; set; } = 10;

        public int numberOfImprovements { get; set; } = 1;

        public int maxIt { get; set; } = 30;

        public void writeConfigXML(string filename)
        {
            using (var stream = new FileStream(filename, FileMode.Create))
            {
                var xml = new XmlSerializer(typeof(Config));
                xml.Serialize(stream, this);
            }
        }

        public static Config loadConfigXML(string filename)
        {
            using (var stream = new FileStream(filename, FileMode.Open))
            {
                var xml = new XmlSerializer(typeof(Config));
                Config c = (Config)xml.Deserialize(stream);

                c.inPath = c.inDir + "/" + c.fileNamePrefix;
                c.outPath = c.outDir + "/" + c.fileNamePrefix;
                c.outImprPath = c.outDir + "/impr/" + c.fileNamePrefix;
                c.outFiltPath = c.outDir + "/filt/" + c.fileNamePrefix;
                c.outStatsDir = c.outDir + "/stats";

                // a bit ugly that i'm using the sigma field but i guess i can make a toggle between the old and new methods or
                // just rename this if we decide to only use the new version
                c.gaussK0 = (float)(Math.Log(0.5) / -(c.smoothSigma * c.smoothSigma));
                // smooth2 is rMax
                c.gaussKMax = (float)(Math.Log(0.5) / -(c.smoothSigma2 * c.smoothSigma2));
                //Console.WriteLine("New sigma: {0}", c.gaussKMax);
                //Console.WriteLine("Old sigma: {0}", (float)(Math.Log(0.01) / -(c.smoothSigma2 * c.smoothSigma2)));
                

                //float rho = (float)Math.Sqrt((Math.Log(0.5) / Math.Log(0.01)) * (c.smoothSigma2 * c.smoothSigma2));
                //Console.WriteLine(rho);
                //Environment.Exit(0);
                //Console.WriteLine(c.gaussKMax);
                //Console.WriteLine((float)(Math.Log(0.5) / -(rho * rho)));

                //c.gaussKMax = (float)(Math.Log(0.01) / -(c.smoothSigma2 * c.smoothSigma2));

                //c.gaussKResultSmooth = (float)(Math.Log(0.01) / -(c.resultSmoothSigma * c.resultSmoothSigma));

                return c;
            }
        }

        internal int frameCount()
        {
            return lastIndex - firstIndex + 1;
        }

        internal int GetFileIndex(int i)
        {
            if (reverse)
            {
                return (lastIndex - firstIndex) - i;
            }
            else
            {
                return firstIndex + i;
            }
        }

        internal string volumeDataFileName(int i)
        {
            return String.Format("{0}vg_{1}_{2:000}.bin", inPath, volumeGridResolution, GetFileIndex(i));
        }

        internal string pointCloudFileName(int i)
        {
            return String.Format("{0}pc_{1}_{2:000}.bin", inPath, pointCount, GetFileIndex(i));
        }

        internal string resultFileName(int i)
        {
            return String.Format("{0}res_{1}_{2:000}.bin", outPath, pointCount, GetFileIndex(i));
        }
        internal string resultFileNameXYZ(int i)
        {
            return String.Format("{0}res_{1}_{2:000}.xyz", outPath, pointCount, GetFileIndex(i));
        }

        internal string resultTransformFilename(int i)
        {
            return string.Format("{0}transform_{1}_{2:000}.txt", outPath, pointCount, GetFileIndex(i));
        }

        internal string improvedResultFilenameXYZ(int i)
        {
            return string.Format("{0}impr_{1}_{2:000}.xyz", outImprPath, pointCount, GetFileIndex(i));
        }

        internal string filteredResultFilenameXYZ(int i)
        {
            return string.Format("{0}filt_{1}_{2:000}.xyz", outFiltPath, pointCount, GetFileIndex(i));
        }

        internal string improvementTransformFilename(int i)
        {
            return string.Format("{0}transform_{1}_{2:000}.txt", outImprPath, pointCount, GetFileIndex(i));
        }

        internal string inputFileName(int i)
        {
            return String.Format("{0}{1:000}.obj", inPath, GetFileIndex(i));
        }
    }
}

