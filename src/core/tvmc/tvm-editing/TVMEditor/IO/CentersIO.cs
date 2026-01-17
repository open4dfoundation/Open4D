using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Numerics;

namespace TVMEditor.IO
{
    public static class CentersIO
    {
        public static Vector3[][] LoadCentersFiles(string[] centersFiles)
        {
            var centers = new Vector3[centersFiles.Length][];

            for (var i = 0; i < centersFiles.Length; i++)
            {
                centers[i] = LoadCentersFile(centersFiles[i]);
            }

            return centers;
        }

        public static Vector3[][] LoadCentersFiles(string directoryPath)
        {
            var files = new DirectoryInfo(directoryPath).GetFiles().Where(f => f.FullName.EndsWith(".bin") || f.FullName.EndsWith(".xyz"))
                .OrderBy(f => f.FullName).Select(f => f.FullName).ToArray();
            return LoadCentersFiles(files);
        }

        public static Vector3[] LoadCentersFile(string centersFile)
        {
            if (centersFile.EndsWith(".bin"))
            {
                return LoadBin(centersFile);
            }
            else if (centersFile.EndsWith(".xyz"))
            {
                return LoadXYZ(centersFile);
            }

            return null;
        }

        public static Vector3[] LoadXYZ(string file)
        {
            List<Vector3> r = new List<Vector3>();
            using (StreamReader reader = new StreamReader(new FileStream(file, FileMode.Open)))
            {
                while (!reader.EndOfStream)
                {
                    string line = reader.ReadLine();
                    string[] split = line.Split(' ');
                    float x = float.Parse(split[0]);
                    float y = float.Parse(split[1]);
                    float z = float.Parse(split[2]);
                    var vec = new Vector3(x, y, z);
                    r.Add(vec);
                }
            }
            return r.ToArray();
        }

        public static Vector3[] LoadBin(string file)
        {
            BinaryReader br = new BinaryReader(new FileStream(file, FileMode.Open));
            int c = br.ReadInt32();
            Vector3[] r = new Vector3[c];
            for (int i = 0; i < c; i++)
            {
                float x = br.ReadSingle();
                float y = br.ReadSingle();
                float z = br.ReadSingle();
                r[i] = new Vector3(x, y, z);
            }
            br.Close();
            return r;
        }

        public static void WriteCentersToBin(Vector3[][] centers, string directoryPath)
        {
            if (!Directory.Exists(directoryPath))
                Directory.CreateDirectory(directoryPath);

            for (var i = 0; i < centers.Length; i++)
            {
                using (BinaryWriter bw = new BinaryWriter(new FileStream($"{directoryPath}\\{i:000000}.bin", FileMode.Create)))
                {
                    bw.Write(centers[i].Length);

                    for (var j = 0; j < centers[i].Length; j++)
                    {
                        bw.Write(centers[i][j].X);
                        bw.Write(centers[i][j].Y);
                        bw.Write(centers[i][j].Z);
                    }
                }
            }
        }
    }
}
