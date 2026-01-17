using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Numerics;
using TVMEditor.Structures;

namespace TVMEditor.IO
{
    public static class TransformationsIO
    {
        public static DualQuaternion[] LoadTransformations(string filePath)
        {
            var transformations = new List<DualQuaternion>();

            using (TextReader tr = new StreamReader(filePath))
            {
                string line;
                while ((line = tr.ReadLine()) != null)
                {
                    if (string.IsNullOrEmpty(line))
                        continue;

                    var split = line.Split(';');
                    transformations.Add(new DualQuaternion 
                    { 
                        Real = new Quaternion(
                            float.Parse(split[0], CultureInfo.InvariantCulture),
                            float.Parse(split[1], CultureInfo.InvariantCulture),
                            float.Parse(split[2], CultureInfo.InvariantCulture),
                            float.Parse(split[3], CultureInfo.InvariantCulture)
                            ),
                        Dual = new Quaternion(
                            float.Parse(split[4], CultureInfo.InvariantCulture),
                            float.Parse(split[5], CultureInfo.InvariantCulture),
                            float.Parse(split[6], CultureInfo.InvariantCulture),
                            float.Parse(split[7], CultureInfo.InvariantCulture)
                            )
                    });
                }
            }

            return transformations.ToArray();
        }

        public static (int[], DualQuaternion[]) LoadIndexedTransformations(string indicesFilePath, string transformationsFilePath)
        {
            var indices = new List<int>();

            using (TextReader tr = new StreamReader(indicesFilePath))
            {
                string line;
                while ((line = tr.ReadLine()) != null)
                {
                    if (string.IsNullOrEmpty(line))
                        continue;

                    indices.Add(int.Parse(line));
                }
            }

            return (indices.ToArray(), LoadTransformations(transformationsFilePath));
        }

        public static void SaveTransformations(string filePath, DualQuaternion[] transformations)
        {
            using (TextWriter tw = new StreamWriter(filePath))
            {
                foreach (var transformation in transformations)
                {
                    tw.WriteLine(TransformationToString(transformation));
                }
            }
        }

        public static void SaveIndices(string filePath, int[] indices)
        {
            using (TextWriter tw = new StreamWriter(filePath))
            {
                foreach (var index in indices)
                {
                    tw.WriteLine(index);
                }
            }
        }

        private static string TransformationToString(DualQuaternion t)
        {
            return $"{t.Real.X.ToString(CultureInfo.InvariantCulture)};{t.Real.Y.ToString(CultureInfo.InvariantCulture)};{t.Real.Z.ToString(CultureInfo.InvariantCulture)};{t.Real.W.ToString(CultureInfo.InvariantCulture)};{t.Dual.X.ToString(CultureInfo.InvariantCulture)};{t.Dual.Y.ToString(CultureInfo.InvariantCulture)};{t.Dual.Z.ToString(CultureInfo.InvariantCulture)};{t.Dual.W.ToString(CultureInfo.InvariantCulture)}";
        }
    }
}
