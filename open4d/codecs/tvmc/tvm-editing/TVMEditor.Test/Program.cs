using System;
using System.IO;
using TVMEditor.Test.Experiments;
using System.Diagnostics;

namespace TVMEditor.Test
{
    class Program
    {
        static void Main(string[] args)
        {
            if (args.Length < 6)
            {
                Console.WriteLine("Usage: Program <dataset> <mode> <firstIndex> <lastIndex> [inputDir] [outputDir]");
                Console.WriteLine("<dataset>: dancer | basketball | mitch | thomas");
                Console.WriteLine("<mode>: 1 (deform mesh to the reference shape) | 2 (deform reference mesh back to each mesh in the group)");
                Console.WriteLine("<firstIndex>: Starting index of the files to process");
                Console.WriteLine("<lastIndex>: Ending index of the files to process");
                Console.WriteLine("[inputDir]: Optional, default is 'Data/basketball_player_2000'");
                Console.WriteLine("[outputDir]: Optional, default is 'output'");
                return;
            }

            string dataset = args[0].ToLower();
            string mode = args[1].ToLower();

            if (!(mode=="1" || mode=="2"))
            {
                Console.WriteLine("Error: <mode> must be 1 or 2");
                return;
            }

            // Parse firstIndex and lastIndex
            if (!int.TryParse(args[2], out int firstIndex))
            {
                Console.WriteLine("Error: <firstIndex> must be an integer.");
                return;
            }

            if (!int.TryParse(args[3], out int lastIndex))
            {
                Console.WriteLine("Error: <lastIndex> must be an integer.");
                return;
            }

            // Optional parameters for inputDir and outputDir
            string inputDir = args.Length > 4 ? args[4] : "Data/basketball_player_2000";
            string outputDir = args.Length > 5 ? args[5] : "output";

            if (!Directory.Exists(outputDir))
                Directory.CreateDirectory(outputDir);

            Stopwatch stopwatch = Stopwatch.StartNew();

            switch (dataset)
            {
                case "dancer":
                    Dancer.Run(inputDir, outputDir, firstIndex, lastIndex, mode);
                    break;
                case "basketball":
                    Basketball.Run(inputDir, outputDir, firstIndex, lastIndex, mode);
                    break;
                case "mitch":
                    Mitch.Run(inputDir, outputDir, firstIndex, lastIndex, mode);
                    break;
                case "thomas":
                    Thomas.Run(inputDir, outputDir, firstIndex, lastIndex, mode);
                    break;
                default:
                    Console.WriteLine($"Unknown dataset: {dataset}");
                    break;
            }

            stopwatch.Stop();
            Console.WriteLine($"Elapsed time: {stopwatch.Elapsed.TotalSeconds:F2} seconds");
        }
    }
}
